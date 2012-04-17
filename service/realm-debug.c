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

static RealmDebugFlags current_flags = 0;

static GDebugKey keys[] = {
	{ "process", REALM_DEBUG_PROCESS },
	{ "diagnostics", REALM_DEBUG_DIAGNOSTICS },
	{ 0, }
};

static void
debug_set_flags (RealmDebugFlags new_flags)
{
	current_flags |= new_flags;
}

void
realm_debug_init (void)
{
	static gsize initialized_flags = 0;

	if (g_once_init_enter (&initialized_flags)) {
		realm_debug_set_flags (g_getenv ("REALM_DEBUG"));
		g_once_init_leave (&initialized_flags, 1);
	}
}

void
realm_debug_set_flags (const gchar *flags_string)
{
	guint nkeys;

	for (nkeys = 0; keys[nkeys].value; nkeys++);

	if (flags_string)
		debug_set_flags (g_parse_debug_string (flags_string, keys, nkeys));
}

gboolean
realm_debug_flag_is_set (RealmDebugFlags flag)
{
	return (flag & current_flags) != 0;
}

void
realm_debug_message (RealmDebugFlags flag,
                     const gchar *format,
                     ...)
{
	gchar *message;
	va_list args;

	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);

	if (flag & current_flags)
		g_log (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%s", message);

	g_free (message);
}

#else /* !WITH_DEBUG */

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
