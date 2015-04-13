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

#include "service/realm-packages.h"

#include "service/realm-daemon.h"
#include "service/realm-diagnostics.h"
#include "service/realm-invocation.h"
#include "service/realm-options.h"
#include "service/realm-settings.h"

#include <stdio.h>

static GMainLoop *loop;

static void
on_ready_get_result (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GAsyncResult **place = (GAsyncResult **)user_data;
	*place = g_object_ref (result);
	g_main_loop_quit (loop);
}

static gint
test_install (const gchar **package_sets)
{
	GDBusConnection *connection;
	GAsyncResult *result = NULL;
	GError *error = NULL;

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (connection == NULL) {
		g_message ("Couldn't open DBus connection: %s", error->message);
		g_clear_error (&error);
		return 1;
	}

	realm_packages_install_async (package_sets, NULL, connection, on_ready_get_result, &result);
	g_object_unref (connection);

	g_main_loop_run (loop);

	realm_packages_install_finish (result, &error);
	g_object_unref (result);

	if (error != NULL) {
		g_message ("Couldn't install packages: %s", error->message);
		g_clear_error (&error);
		return 1;
	}

	return 0;
}

int
main(int argc,
     char *argv[])
{
	const gchar *package_sets[] = { "sssd", "samba", "adcli", NULL };

#if !GLIB_CHECK_VERSION(2, 36, 0)
	g_type_init ();
#endif

	realm_settings_init ();

	loop = g_main_loop_new (NULL, FALSE);
	test_install (package_sets);
	g_main_loop_unref (loop);

	return 0;
}

/* Dummy functions */

GCancellable *
realm_invocation_get_cancellable (GDBusMethodInvocation *invocation)
{
	return g_cancellable_new ();
}

const gchar *
realm_invocation_get_operation (GDBusMethodInvocation *invocation)
{
	return NULL;
}

gboolean
realm_daemon_is_install_mode (void)
{
	return FALSE;
}

void
realm_diagnostics_info (GDBusMethodInvocation *invocation,
                        const gchar *format,
                        ...)
{
	va_list va;

	va_start (va, format);
	vfprintf (stderr, format, va);
	fputc ('\n', stderr);
	va_end (va);
}

void
realm_diagnostics_error (GDBusMethodInvocation *invocation,
                         GError *unused,
                         const gchar *format,
                         ...)
{
	va_list va;

	va_start (va, format);
	vfprintf (stderr, format, va);
	fputc ('\n', stderr);
	va_end (va);
}

gboolean
realm_options_automatic_install (void)
{
	return TRUE;
}
