/* realmd -- Realm configuration service
 *
 * Copyright 2012 Red Hat Inc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 *
 * Author: Stef Walter <stefw@gnome.org>
 */

#include "config.h"

#define DEBUG_FLAG REALM_DEBUG_DIAGNOSTICS
#include "realm-debug.h"
#include "realm-dbus-constants.h"
#include "realm-diagnostics.h"

#include <string.h>
#include <syslog.h>

static GDBusConnection *the_connection = NULL;
static GString *line_buffer = NULL;

void
realm_diagnostics_initialize (GDBusConnection *connection)
{
	g_return_if_fail (G_IS_DBUS_CONNECTION (connection));

	if (the_connection != NULL)
		g_object_remove_weak_pointer (G_OBJECT (the_connection),
		                              (gpointer *)&the_connection);

	the_connection = connection;
	g_object_add_weak_pointer (G_OBJECT (the_connection), (gpointer *)&the_connection);
}

static void
log_syslog_and_debug (int log_level,
                      gchar *string,
                      gsize length)
{
	gchar *at = string;
	gchar *ptr;

	/* Print all stderr lines as messages */
	while ((ptr = memchr (at, '\n', length)) != NULL) {
		*ptr = '\0';
		if (line_buffer && line_buffer->len > 0) {
			syslog (log_level, "%s%s", line_buffer->str, at);
			if (realm_debugging)
				g_log (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%s%s", line_buffer->str, at);
			g_string_set_size (line_buffer, 0);
		} else {
			syslog (log_level, "%s", at);
			if (realm_debugging)
				g_log (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%s", at);
		}

		*ptr = '\n';
		ptr++;
		length -= (ptr - at);
		at = ptr;
	}

	if (length != 0) {
		if (line_buffer == NULL)
			line_buffer = g_string_new_len (at, length);
		else
			g_string_append_len (line_buffer, at, length);
	}
}

static void
log_take_diagnostic (GDBusMethodInvocation *invocation,
                     int log_level,
                     gchar *string)
{
	static gboolean syslog_initialized = FALSE;
	GError *error = NULL;

	if (!syslog_initialized) {
		openlog ("realmd", 0, LOG_AUTH);
		syslog_initialized = TRUE;
	}

	log_syslog_and_debug (log_level, string, strlen (string));

	if (!the_connection || !invocation) {
		g_free (string);
		return;
	}

	g_dbus_connection_emit_signal (the_connection, g_dbus_method_invocation_get_sender (invocation),
	                               g_dbus_method_invocation_get_object_path (invocation),
	                               REALM_DBUS_PROVIDER_INTERFACE, REALM_DBUS_DIAGNOSTICS_SIGNAL,
	                               g_variant_new ("(s)", string), &error);

	if (error != NULL)
		g_warning ("couldn't emit the Diagnostics signal: %s", error->message);

	g_free (string);
}

void
realm_diagnostics_info (GDBusMethodInvocation *invocation,
                        const gchar *format,
                        ...)
{
	GString *message;
	va_list va;

	g_return_if_fail (invocation == NULL || G_IS_DBUS_METHOD_INVOCATION (invocation));
	g_return_if_fail (format != NULL);

	va_start (va, format);
	message = g_string_new (" * ");
	g_string_append_vprintf (message, format, va);
	va_end (va);

	if (!g_str_has_suffix (message->str, "\n"))
		g_string_append_c (message, '\n');

	log_take_diagnostic (invocation, LOG_INFO, g_string_free (message, FALSE));
}

void
realm_diagnostics_error (GDBusMethodInvocation *invocation,
                         GError *error,
                         const gchar *format,
                         ...)
{
	GString *message;
	va_list va;

	g_return_if_fail (invocation == NULL || G_IS_DBUS_METHOD_INVOCATION (invocation));
	g_return_if_fail (format != NULL);

	va_start (va, format);
	message = g_string_new (" ! ");
	g_string_append_vprintf (message, format, va);
	va_end (va);

	if (error != NULL) {
		g_string_append (message, ": ");
		g_string_append (message, error->message);
	}

	g_string_append_c (message, '\n');

	log_take_diagnostic (invocation, LOG_INFO, g_string_free (message, FALSE));
}

void
realm_diagnostics_info_data (GDBusMethodInvocation *invocation,
                             const gchar *data,
                             gssize n_data)
{
	gchar *info;
	gsize length;

	g_return_if_fail (invocation == NULL || G_IS_DBUS_METHOD_INVOCATION (invocation));
	g_return_if_fail (data != NULL);

	if (n_data == -1)
		n_data = strlen (data);

	if (g_utf8_validate (data, n_data, NULL)) {
		info = g_strndup (data, n_data);

	} else {
		info = g_convert_with_fallback (data, n_data, "utf-8", "ascii",
		                                "\xef\xbf\xbd", NULL, &length, NULL);
	}

	log_take_diagnostic (invocation, LOG_INFO, info);
}
