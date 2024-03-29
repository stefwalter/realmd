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

#include <sys/types.h>
#include <sys/stat.h>
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
call_leave (RealmDbusKerberosMembership *membership,
            GVariant *credentials,
            GVariant *options,
            GError **error)
{
	SyncClosure sync;
	gboolean ret;

	sync.result = NULL;
	sync.loop = g_main_loop_new (NULL, FALSE);

	/* Start actual operation */
	realm_dbus_kerberos_membership_call_leave (membership, credentials, options,
	                                           NULL, on_complete_get_result, &sync);

	/* This mainloop is quit by on_complete_get_result */
	g_main_loop_run (sync.loop);

	ret = realm_dbus_kerberos_membership_call_leave_finish (membership, sync.result, error);

	g_object_unref (sync.result);
	g_main_loop_unref (sync.loop);

	return ret ? 0 : 1;
}

static RealmDbusKerberosMembership *
locate_configured_matching_kerberos_realm (RealmClient *client,
                                           const gchar *realm_name)
{
	RealmDbusKerberosMembership *membership = NULL;
	RealmDbusProvider *provider;
	const gchar *const *paths;
	RealmDbusRealm *realm;
	const gchar *configured;
	const gchar *name;
	gboolean matched;
	gint i;

	provider = realm_client_get_provider (client);
	paths = realm_dbus_provider_get_realms (provider);

	for (i = 0; paths && paths[i]; i++) {
		matched = FALSE;

		realm = realm_client_get_realm (client, paths[i]);
		membership = realm_client_to_kerberos_membership (client, realm);
		configured = realm_dbus_realm_get_configured (realm);

		if (membership != NULL && configured != NULL && !g_str_equal (configured, "")) {
			if (realm_name == NULL) {
				matched = TRUE;
			} else {
				name = realm_dbus_realm_get_name (realm);
				matched = (g_ascii_strcasecmp (name, realm_name) == 0);
			}
		}

		g_object_unref (realm);

		if (matched)
			break;

		g_clear_object (&membership);
	}

	return membership;
}

static int
perform_user_leave (RealmClient *client,
                    RealmDbusKerberosMembership *membership,
                    const gchar *user_name)
{
	GError *error = NULL;
	GVariant *supported;
	GVariant *credentials;
	GVariant *options;
	gint ret;

	supported = realm_dbus_kerberos_membership_get_supported_leave_credentials (membership);
	credentials = realm_client_build_principal_creds (client, membership, supported, user_name, &error);
	if (!credentials) {
		realm_handle_error (error, NULL);
		return 1;
	}

	options = realm_build_options(NULL, NULL);
	ret = call_leave (membership, credentials, options, &error);

	if (error != NULL)
		realm_handle_error (error, _("Couldn't leave realm"));

	return ret;
}

static int
perform_leave (RealmClient *client,
               const gchar *realm_name,
               const gchar *user_name)
{
	RealmDbusKerberosMembership *membership;
	gint ret;

	membership = locate_configured_matching_kerberos_realm (client, realm_name);
	if (membership == NULL) {
		if (realm_name == NULL)
			realm_handle_error (NULL, "Couldn't find a configured realm");
		else
			realm_handle_error (NULL, "Couldn't find the configured realm: %s", realm_name);
		return 1;
	}

	ret = perform_user_leave (client, membership, user_name);
	g_object_unref (membership);

	return ret;
}

int
realm_leave (int argc,
            char *argv[])
{
	RealmClient *client;
	GOptionContext *context;
	gchar *arg_user = NULL;
	gboolean arg_verbose = FALSE;
	GError *error = NULL;
	const gchar *realm_name;
	gint ret = 0;

	GOptionEntry option_entries[] = {
		{ "user", 'U', 0, G_OPTION_ARG_STRING, &arg_user, N_("User name to use for enrollment"), NULL },
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

	} else {
		client = realm_client_new (arg_verbose);
		if (client) {
			realm_name = argc < 2 ? NULL : argv[1];
			ret = perform_leave (client, realm_name, arg_user);
			g_object_unref (client);
		} else {
			ret = 1;
		}
	}

	g_free (arg_user);
	g_option_context_free (context);
	return ret;
}
