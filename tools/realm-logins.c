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

#include "realm.h"
#include "realm-dbus-constants.h"
#include "realm-dbus-generated.h"

#include <glib.h>

#include <string.h>

typedef struct {
	GAsyncResult *result;
	GMainLoop *loop;
} SyncClosure;

static void
on_complete_get_result (GObject *source,
                        GAsyncResult *result,
                        gpointer user_data)
{
	SyncClosure *sync = user_data;
	sync->result = g_object_ref (result);
	g_main_loop_quit (sync->loop);
}

static int
perform_permit_or_deny_logins (GDBusConnection *connection,
                               const gchar *realm_name,
                               const gchar **logins,
                               gint n_logins,
                               gboolean permit)
{
	RealmDbusKerberos *realm;
	SyncClosure sync;
	gchar **add_or_remove;
	GError *error = NULL;
	const gchar *empty[] = { NULL };

	realm = realm_name_to_enrolled (connection, realm_name);
	if (realm == NULL)
		return 1;

	/* Make it null terminated */
	add_or_remove = g_new0 (gchar *, n_logins + 1);
	memcpy (add_or_remove, logins, sizeof (gchar *) * n_logins);

	sync.result = NULL;
	sync.loop = g_main_loop_new (NULL, FALSE);

	/* Start actual operation */
	g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (realm), G_MAXINT);

	realm_dbus_kerberos_call_change_login_policy (realm, REALM_DBUS_LOGIN_POLICY_PERMITTED,
	                                              permit ? (const gchar * const*)add_or_remove : empty,
	                                              permit ? empty : (const gchar * const*)add_or_remove,
	                                              realm_operation_id,
	                                              NULL, on_complete_get_result, &sync);

	/* This mainloop is quit by on_complete_get_result */
	g_main_loop_run (sync.loop);

	realm_dbus_kerberos_call_change_login_policy_finish (realm, sync.result, &error);

	g_object_unref (sync.result);
	g_main_loop_unref (sync.loop);

	if (error != NULL) {
		realm_handle_error (error, "couldn't %s logins",
		                    permit ? "permit" : "deny");
		return 1;
	}

	return 0;
}

static int
perform_permit_or_deny_all (GDBusConnection *connection,
                            const gchar *realm_name,
                            gboolean permit)
{
	RealmDbusKerberos *realm;
	const gchar *policy;
	const gchar *logins[] = { NULL };
	GError *error = NULL;

	realm = realm_name_to_enrolled (connection, realm_name);
	if (realm == NULL)
		return 1;

	policy = permit ? REALM_DBUS_LOGIN_POLICY_ANY : REALM_DBUS_LOGIN_POLICY_DENY;
	realm_dbus_kerberos_call_change_login_policy_sync (realm, policy,
	                                                   (const gchar * const *)logins,
	                                                   (const gchar * const *)logins,
	                                                   realm_operation_id,
	                                                   NULL, &error);

	if (error != NULL) {
		realm_handle_error (error, "couldn't %s all logins",
		                    permit ? "permit" : "deny");
		return 1;
	}

	return 0;
}

static int
realm_permit_or_deny (gboolean permit,
                      int argc,
                      char *argv[])
{
	GDBusConnection *connection;
	GOptionContext *context;
	gboolean arg_all = FALSE;
	gboolean arg_verbose = FALSE;
	gchar *realm_name = NULL;
	GError *error = NULL;
	gint ret = 0;

	GOptionEntry option_entries[] = {
		{ "all", 'a', 0, G_OPTION_ARG_NONE, &arg_all,
		  permit ? "Permit any domain user login" : "Deny any domain user login", NULL },
		{ "realm", 'R', 0, G_OPTION_ARG_STRING, &realm_name, "Realm to permit/deny logins for", NULL },
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &arg_verbose, "Verbose output", NULL },
		{ NULL, }
	};

	context = g_option_context_new ("realm");
	g_option_context_add_main_entries (context, option_entries, NULL);

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("%s: %s\n", g_get_prgname (), error->message);
		g_error_free (error);
		ret = 2;
	}

	if (arg_all) {
		if (argc != 1) {
			g_printerr ("%s: no users should be specified with -a or --all\n", g_get_prgname ());
			ret = 2;
		} else {
			connection = realm_get_connection (arg_verbose);
			if (connection) {
				ret = perform_permit_or_deny_all (connection, realm_name, permit);
				g_object_unref (connection);
			} else {
				ret = 1;
			}
		}
	} else if (argc < 2) {
		g_printerr ("%s: specify users to %s\n", g_get_prgname (), permit ? "permit" : "deny");
		ret = 2;

	} else {
		connection = realm_get_connection (arg_verbose);
		if (connection) {
			ret = perform_permit_or_deny_logins (connection, realm_name,
			                                     (const gchar **)(argv + 1),
			                                     argc - 1, permit);
			g_object_unref (connection);
		} else {
			ret = 1;
		}
	}

	g_free (realm_name);
	g_option_context_free (context);
	return ret;
}

int
realm_permit (int argc,
              char *argv[])
{
	return realm_permit_or_deny (TRUE, argc, argv);
}

int
realm_deny (int argc,
            char *argv[])
{
	return realm_permit_or_deny (FALSE, argc, argv);
}
