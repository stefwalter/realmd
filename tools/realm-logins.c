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
#include <glib/gi18n.h>

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

static RealmDbusRealm *
locate_configured_matching_realm (RealmClient *client,
                                  const gchar *realm_name)
{
	RealmDbusProvider *provider;
	const gchar *const *paths;
	RealmDbusRealm *realm = NULL;
	const gchar *configured;
	gboolean matched;
	gint i;

	provider = realm_client_get_provider (client);
	paths = realm_dbus_provider_get_realms (provider);

	for (i = 0; paths && paths[i]; i++) {
		matched = FALSE;

		realm = realm_client_get_realm (client, paths[i]);
		if (realm != NULL) {
			configured = realm_dbus_realm_get_configured (realm);
			matched = (realm_name == NULL ||
			           g_strcmp0 (realm_dbus_realm_get_name (realm), realm_name) == 0) &&
			          (configured && !g_str_equal (configured, ""));
		}

		if (matched)
			break;

		g_object_unref (realm);
		realm = NULL;
	}

	if (realm == NULL) {
		if (!realm_name)
			realm_handle_error (NULL, "Couldn't find a configured realm");
		else
			realm_handle_error (NULL, "Couldn't find a matching realm");
		return NULL;
	}

	return realm;
}

static int
perform_permit_or_deny_logins (RealmClient *client,
                               const gchar *realm_name,
                               const gchar **logins,
                               gint n_logins,
                               gboolean permit)
{
	RealmDbusRealm *realm;
	SyncClosure sync;
	gchar **add_or_remove;
	GError *error = NULL;
	const gchar *empty[] = { NULL };
	GVariant *options;

	realm = locate_configured_matching_realm (client, realm_name);
	if (realm == NULL)
		return 1;

	/* Make it null terminated */
	add_or_remove = g_new0 (gchar *, n_logins + 1);
	memcpy (add_or_remove, logins, sizeof (gchar *) * n_logins);

	sync.result = NULL;
	sync.loop = g_main_loop_new (NULL, FALSE);

	options = realm_build_options (NULL, NULL);
	g_variant_ref_sink (options);

	realm_dbus_realm_call_change_login_policy (realm, REALM_DBUS_LOGIN_POLICY_PERMITTED,
	                                           permit ? (const gchar * const*)add_or_remove : empty,
	                                           permit ? empty : (const gchar * const*)add_or_remove,
	                                           options, NULL, on_complete_get_result, &sync);

	g_variant_unref (options);

	/* This mainloop is quit by on_complete_get_result */
	g_main_loop_run (sync.loop);

	realm_dbus_realm_call_change_login_policy_finish (realm, sync.result, &error);

	g_object_unref (sync.result);
	g_main_loop_unref (sync.loop);
	g_object_unref (realm);

	if (error != NULL) {
		realm_handle_error (error,
		                    permit ? _("Couldn't permit logins") : _("Couldn't deny logins"));
		return 1;
	}

	return 0;
}

static int
perform_permit_or_deny_all (RealmClient *client,
                            const gchar *realm_name,
                            gboolean permit)
{
	RealmDbusRealm *realm;
	const gchar *policy;
	const gchar *logins[] = { NULL };
	GError *error = NULL;
	GVariant *options;

	realm = locate_configured_matching_realm (client, realm_name);
	if (realm == NULL)
		return 1;

	options = realm_build_options (NULL, NULL);
	g_variant_ref_sink (options);

	policy = permit ? REALM_DBUS_LOGIN_POLICY_ANY : REALM_DBUS_LOGIN_POLICY_DENY;
	realm_dbus_realm_call_change_login_policy_sync (realm, policy,
	                                                (const gchar * const *)logins,
	                                                (const gchar * const *)logins,
	                                                options, NULL, &error);

	g_variant_unref (options);
	g_object_unref (realm);

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
	RealmClient *client;
	GOptionContext *context;
	gboolean arg_all = FALSE;
	gboolean arg_verbose = FALSE;
	gchar *realm_name = NULL;
	GError *error = NULL;
	gint ret = 0;

	GOptionEntry option_entries[] = {
		{ "all", 'a', 0, G_OPTION_ARG_NONE, &arg_all,
		  permit ? N_("Permit any domain user login") : N_("Deny any domain user login"), NULL },
		{ "realm", 'R', 0, G_OPTION_ARG_STRING, &realm_name, N_("Realm to permit/deny logins for"), NULL },
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &arg_verbose, N_("Verbose output"), NULL },
		{ NULL, }
	};

	context = g_option_context_new ("realm");
	g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);
	g_option_context_add_main_entries (context, option_entries, NULL);

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("%s: %s\n", g_get_prgname (), error->message);
		g_error_free (error);
		ret = 2;
	}

	if (arg_all) {
		if (argc != 1) {
			g_printerr ("%s: %s\n", _("No users should be specified with -a or --all"), g_get_prgname ());
			ret = 2;
		} else {
			client = realm_client_new (arg_verbose);
			if (client) {
				ret = perform_permit_or_deny_all (client, realm_name, permit);
				g_object_unref (client);
			} else {
				ret = 1;
			}
		}
	} else if (argc < 2) {
		g_printerr ("%s: %s\n", g_get_prgname (),
		            permit ? _("Specify users to permit") : _("Specify users to deny"));
		ret = 2;

	} else {
		client = realm_client_new (arg_verbose);
		if (client) {
			ret = perform_permit_or_deny_logins (client, realm_name,
			                                     (const gchar **)(argv + 1),
			                                     argc - 1, permit);
			g_object_unref (client);
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
