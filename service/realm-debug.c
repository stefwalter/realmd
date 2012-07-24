/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basrealm-offset: 2; -*- */
/*
 * Copyright (C) 2007 Collabora Ltd.
 * Copyright (C) 2007 Nokia Corporation
 * Copyright (C) 2012 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"

#include "realm-debug.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#ifdef WITH_DEBUG

static gsize initialized_flags = 0;
static RealmDebugFlags current_flags = 0;

static GDebugKey keys[] = {
	{ "process", REALM_DEBUG_PROCESS },
	{ "diagnostics", REALM_DEBUG_DIAGNOSTICS },
	{ "daemon", REALM_DEBUG_SERVICE },
	{ "packages", REALM_DEBUG_PACKAGES },
	{ "provider", REALM_DEBUG_PROVIDER },
	{ "leave-temp-files", REALM_DEBUG_LEAVE_TEMP_FILES },
	{ 0, }
};

static void
debug_set_flags (RealmDebugFlags new_flags)
{
	current_flags |= new_flags;
}

void
realm_debug_set_flags (const gchar *flags_string)
{
	guint nkeys;

	for (nkeys = 0; keys[nkeys].value; nkeys++);

	if (flags_string)
		debug_set_flags (g_parse_debug_string (flags_string, keys, nkeys));
}

static void
on_realm_log_debug (const gchar *log_domain,
                    GLogLevelFlags log_level,
                    const gchar *message,
                    gpointer user_data)
{
	GString *gstring;
	const gchar *progname;
	int ret;

	gstring = g_string_new (NULL);

	progname = g_get_prgname ();
	g_string_append_printf (gstring, "(%s:%lu): %s%sDEBUG: %s\n",
	                        progname ? progname : "process", (gulong)getpid (),
	                        log_domain ? log_domain : "", log_domain ? "-" : "",
	                        message ? message : "(NULL) message");

	ret = write (1, gstring->str, gstring->len);

	/* Yes this is dumb, but gets around compiler warning */
	if (ret < 0)
		g_warning ("couldn't write debug output");

	g_string_free (gstring, TRUE);
}

void
realm_debug_init (void)
{
	const gchar *messages_env;
	const gchar *debug_env;

	if (g_once_init_enter (&initialized_flags)) {
		messages_env = g_getenv ("G_MESSAGES_DEBUG");
		debug_env = g_getenv ("REALM_DEBUG");
#ifdef REALM_DEBUG
		if (debug_env == NULL)
			debug_env = G_STRINGIFY (REALM_DEBUG);
#endif

		/*
		 * If the caller is selectively asking for certain debug
		 * messages with the REALM_DEBUG environment variable, then
		 * we install our own output handler and only print those
		 * messages. This happens irrespective of G_MESSAGES_DEBUG
		 */
		if (messages_env == NULL && debug_env != NULL)
			g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
			                   on_realm_log_debug, NULL);

		/*
		 * If the caller is using G_MESSAGES_DEBUG then we enable
		 * all our debug messages, and let Glib filter which ones
		 * to display.
		 */
		if (messages_env != NULL && debug_env == NULL)
			debug_env = "all";

		realm_debug_set_flags (debug_env);
		g_once_init_leave (&initialized_flags, 1);
	}
}

gboolean
realm_debug_flag_is_set (RealmDebugFlags flag)
{
	if G_UNLIKELY (!initialized_flags)
		realm_debug_init ();
	return (flag & current_flags) != 0;
}

void
realm_debug_message (RealmDebugFlags flag,
                     const gchar *format,
                     ...)
{
	gchar *message;
	va_list args;

	if G_UNLIKELY (!initialized_flags)
		realm_debug_init ();

	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);

	if (flag & current_flags)
		g_log (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%s", message);

	g_free (message);
}


#else /* !WITH_DEBUG */

void
realm_debug_init (void)
{

}

gboolean
realm_debug_flag_is_set (RealmDebugFlags flag)
{
	return FALSE;
}

void
realm_debug_message (RealmDebugFlags flag,
                     const gchar *format,
                     ...)
{
}

void
realm_debug_set_flags (const gchar *flags_string)
{
}

#endif /* !WITH_DEBUG */
