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
#include "realm-client.h"
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
call_join (RealmDbusKerberosMembership *membership,
           GVariant *credentials,
           GVariant *options,
           GError **error)
{
	SyncClosure sync;
	gboolean ret;

	sync.result = NULL;
	sync.loop = g_main_loop_new (NULL, FALSE);

	/* Start actual operation */
	realm_dbus_kerberos_membership_call_join (membership, credentials, options,
	                                          NULL, on_complete_get_result, &sync);

	/* This mainloop is quit by on_complete_get_result */
	g_main_loop_run (sync.loop);

	ret = realm_dbus_kerberos_membership_call_join_finish (membership, sync.result, error);

	g_object_unref (sync.result);
	g_main_loop_unref (sync.loop);

	return ret ? 0 : 1;
}

static int
perform_otp_join (RealmClient *client,
                  RealmDbusKerberosMembership *membership,
                  const gchar *one_time_password,
                  GVariant *options)
{
	GVariant *supported;
	GVariant *credentials;
	GError *error = NULL;
	int ret;

	supported = realm_dbus_kerberos_membership_get_supported_join_credentials (membership);
	credentials = realm_client_build_otp_creds (client, supported, one_time_password, &error);
	if (credentials == NULL) {
		realm_handle_error (error, NULL);
		return 1;
	}

	ret = call_join (membership, credentials, options, &error);

	if (error != NULL)
		realm_handle_error (error, _("Couldn't join realm"));

	return ret;
}

static int
perform_automatic_join (RealmClient *client,
                        RealmDbusKerberosMembership *membership,
                        GVariant *options,
                        gboolean *try_other)
{
	GVariant *supported;
	GVariant *credentials;
	GError *error = NULL;
	gchar *remote;
	int ret;

	supported = realm_dbus_kerberos_membership_get_supported_join_credentials (membership);
	credentials = realm_client_build_automatic_creds (client, supported, &error);
	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)) {
		*try_other = TRUE;
		return 1;
	} else if (credentials == NULL) {
		realm_handle_error (error, NULL);
		return 1;
	}

	ret = call_join (membership, credentials, options, &error);

	if (error != NULL) {
		remote = g_dbus_error_get_remote_error (error);
		if (g_strcmp0 (remote, REALM_DBUS_ERROR_AUTH_FAILED) == 0) {
			*try_other = TRUE;
			g_error_free (error);
		} else {
			*try_other = FALSE;
			realm_handle_error (error, _("Couldn't join realm"));
		}
		g_free (remote);
	}

	return ret;
}

static int
perform_user_join (RealmClient *client,
                   RealmDbusKerberosMembership *membership,
                   const gchar *user_name,
                   GVariant *options)
{
	GVariant *supported;
	GVariant *credentials;
	GError *error = NULL;
	int ret;

	supported = realm_dbus_kerberos_membership_get_supported_join_credentials (membership);

	credentials = realm_client_build_principal_creds (client, membership, supported,
	                                                  user_name, &error);
	if (credentials == NULL) {
		realm_handle_error (error, NULL);
		return 1;
	}

	ret = call_join (membership, credentials, options, &error);

	if (error != NULL)
		realm_handle_error (error, _("Couldn't join realm"));

	return ret;
}

static int
perform_join (RealmClient *client,
              const gchar *string,
              const gchar *user_name,
              const gchar *computer_ou,
              const gchar *client_software,
              const gchar *server_software,
              const gchar *membership_software,
              const gchar *one_time_password)
{
	RealmDbusKerberosMembership *membership;
	gboolean try_other = FALSE;
	GError *error = NULL;
	GVariant *options;
	GList *realms;
	gint ret;

	realms = realm_client_discover (client, string, client_software, server_software,
	                                REALM_DBUS_KERBEROS_MEMBERSHIP_INTERFACE, &error);

	if (error != NULL) {
		realm_handle_error(error, _("Couldn't discover realm"));
		return 1;
	} else if (realms == NULL) {
		realm_handle_error(NULL, _("No such realm found"));
		return 1;
	}

	membership = realms->data;
	options = realm_build_options (REALM_DBUS_OPTION_COMPUTER_OU, computer_ou,
	                               REALM_DBUS_OPTION_MEMBERSHIP_SOFTWARE, membership_software,
	                               NULL);
	g_variant_ref_sink (options);

	if (one_time_password) {
		ret = perform_otp_join (client, membership, one_time_password, options);

	} else if (user_name) {
		ret = perform_user_join (client, membership, user_name, options);

	} else {
		ret = perform_automatic_join (client, membership, options, &try_other);
		if (try_other)
			ret = perform_user_join (client, membership, user_name, options);
	}

	g_variant_unref (options);
	g_list_free_full (realms, g_object_unref);
	return ret;
}

int
realm_join (int argc,
            char *argv[])
{
	GOptionContext *context;
	RealmClient *client;
	gchar *arg_user = NULL;
	gboolean arg_verbose = FALSE;
	GError *error = NULL;
	const gchar *realm_name;
	gchar *arg_computer_ou = NULL;
	gchar *arg_client_software = NULL;
	gchar *arg_server_software = NULL;
	gchar *arg_membership_software = NULL;
	gchar *arg_one_time_password = NULL;
	gint ret = 0;

	GOptionEntry option_entries[] = {
		{ "user", 'U', 0, G_OPTION_ARG_STRING, &arg_user,
		  N_("User name to use for enrollment"), NULL },
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &arg_verbose,
		  N_("Verbose output"), NULL },
		{ "computer-ou", 0, 0, G_OPTION_ARG_STRING, &arg_computer_ou,
		  N_("Computer OU DN to join"), NULL },
		{ "client-software", 0, 0, G_OPTION_ARG_STRING, &arg_client_software,
		  N_("Use specific client software"), NULL },
		{ "server-software", 0, 0, G_OPTION_ARG_STRING, &arg_server_software,
		  N_("Use specific server software"), NULL },
		{ "membership-software", 0, 0, G_OPTION_ARG_STRING, &arg_membership_software,
		  N_("Use specific membership software"), NULL },
		{ "one-time-password", 0, 0, G_OPTION_ARG_STRING, &arg_one_time_password,
		  N_("Join using a preset one time password"), NULL },
		{ NULL, }
	};

	context = g_option_context_new ("realm");
	g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);
	g_option_context_add_main_entries (context, option_entries, NULL);

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("%s: %s\n", g_get_prgname (), error->message);
		g_error_free (error);
		ret = 2;

	} else if (argc > 2) {
		g_printerr ("%s: %s\n", _("Specify one realm to join"), g_get_prgname ());
		ret = 2;

	} else {
		client = realm_client_new (arg_verbose);
		if (client) {
			realm_name = argc < 2 ? "" : argv[1];
			ret = perform_join (client, realm_name, arg_user,
			                    arg_computer_ou, arg_client_software,
			                    arg_server_software, arg_membership_software,
			                    arg_one_time_password);
			g_object_unref (client);
		} else {
			ret = 1;
		}
	}

	g_free (arg_user);
	g_free (arg_computer_ou);
	g_free (arg_client_software);
	g_free (arg_server_software);
	g_option_context_free (context);
	return ret;
}
