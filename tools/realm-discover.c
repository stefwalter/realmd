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

#include <krb5/krb5.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

static void
print_kerberos_info (const gchar *realm_path)
{
	RealmDbusKerberos *kerberos;
	GError *error = NULL;

	kerberos = realm_dbus_kerberos_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
	                                                       G_DBUS_PROXY_FLAGS_NONE,
	                                                       REALM_DBUS_BUS_NAME, realm_path,
	                                                       NULL, &error);

	if (error != NULL) {
		g_warning ("Couldn't use realm service: %s", error->message);
		g_error_free (error);
		return;
	}

	g_print ("  type: kerberos\n");
	g_print ("  realm-name: %s\n", realm_dbus_kerberos_get_realm_name (kerberos));
	g_print ("  domain-name: %s\n", realm_dbus_kerberos_get_domain_name (kerberos));

	g_object_unref (kerberos);
}

static void
print_realm_info (const gchar *realm_path)
{
	RealmDbusRealm *realm;
	const gchar *configured;
	GVariant *details;
	const gchar *name;
	const gchar *value;
	gboolean is_configured;
	gchar *string;
	const gchar *policy;
	GVariantIter iter;

	realm = realm_path_to_realm (realm_path);
	if (realm == NULL)
		return;

	g_print ("%s\n", realm_dbus_realm_get_name (realm));

	is_configured = TRUE;
	configured = realm_dbus_realm_get_configured (realm);
	if (configured == NULL || g_str_equal (configured, "")) {
		configured = "no";
		is_configured = FALSE;

	} else if (g_str_equal (configured, REALM_DBUS_KERBEROS_MEMBERSHIP_INTERFACE)) {
		configured = "kerberos-member";
	}

	g_print ("  configured: %s\n", configured);

	details = realm_dbus_realm_get_details (realm);
	if (details) {
		g_variant_iter_init (&iter, details);
		while (g_variant_iter_loop (&iter, "(&s&s)", &name, &value))
			g_print ("  %s: %s\n", name, value);
	}

	if (realm_supports_interface (realm, REALM_DBUS_KERBEROS_INTERFACE))
		print_kerberos_info (realm_path);
	else
		g_print ("  type: unknown\n");

	if (is_configured) {
		string = g_strjoinv (", ", (gchar **)realm_dbus_realm_get_login_formats (realm));
		g_print ("  login-formats: %s\n", string);
		g_free (string);
		policy = realm_dbus_realm_get_login_policy (realm);
		g_print ("  login-policy: %s\n", policy);
		if (strstr (policy, REALM_DBUS_LOGIN_POLICY_PERMITTED)) {
			string = g_strjoinv (", ", (gchar **)realm_dbus_realm_get_permitted_logins (realm));
			g_print ("  permitted-logins: %s\n", string);
			g_free (string);
		}
	}

	g_object_unref (realm);
}

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
perform_discover (GDBusConnection *connection,
                  const gchar *string)
{
	RealmDbusProvider *provider;
	gboolean found = FALSE;
	GError *error = NULL;
	SyncClosure sync;
	gchar **realms;
	gint relevance;
	GVariant *options;
	gint i;

	provider = realm_dbus_provider_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
	                                                       G_DBUS_PROXY_FLAGS_NONE,
	                                                       REALM_DBUS_BUS_NAME,
	                                                       REALM_DBUS_SERVICE_PATH,
	                                                       NULL, &error);
	if (error != NULL) {
		realm_handle_error (error, _("Couldn't connect to realmd service"));
		return 2;
	}

	sync.result = NULL;
	sync.loop = g_main_loop_new (NULL, FALSE);

	options = g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0);
	g_variant_ref_sink (options);

	g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (provider), G_MAXINT);
	realm_dbus_provider_call_discover (provider, string ? string : "",
	                                   options, NULL, on_complete_get_result, &sync);

	g_variant_unref (options);

	/* This mainloop is quit by on_complete_get_result */
	g_main_loop_run (sync.loop);

	realm_dbus_provider_call_discover_finish (provider, &relevance, &realms,
	                                          sync.result, &error);

	g_object_unref (sync.result);
	g_main_loop_unref (sync.loop);

	if (error != NULL) {
		realm_handle_error (error, _("Couldn't discover realm"));
		return 2;
	}

	for (i = 0; realms[i] != NULL; i++) {
		print_realm_info (realms[i]);
		found = TRUE;
	}

	g_strfreev (realms);

	if (!found) {
		if (string == NULL)
			realm_handle_error (NULL, _("No default realm discovered"));
		else
			realm_handle_error (NULL, _("No such realm found: %s"), string);
		return 1;
	}

	return 0;
}

int
realm_discover (int argc,
                char *argv[])
{
	GDBusConnection *connection;
	GOptionContext *context;
	gboolean arg_verbose = FALSE;
	GError *error = NULL;
	gint result = 0;
	gint ret;
	gint i;

	GOptionEntry option_entries[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &arg_verbose, N_("Verbose output"), NULL },
		{ NULL, }
	};

	g_type_init ();

	context = g_option_context_new ("realm-or-domain");
	g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);
	g_option_context_add_main_entries (context, option_entries, NULL);

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("%s: %s\n", g_get_prgname (), error->message);
		g_error_free (error);
		ret = 2;
	}

	connection = realm_get_connection (arg_verbose);
	if (!connection) {
		ret = 1;

	/* The default realm? */
	} else if (argc == 1) {
		ret = perform_discover (connection, NULL);
		g_object_unref (connection);

	/* Specific realms */
	} else {
		for (i = 1; i < argc; i++) {
			ret = perform_discover (connection, argv[i]);
			if (ret != 0)
				result = ret;
		}
		g_object_unref (connection);
	}

	g_option_context_free (context);
	return result;
}

static int
perform_list (GDBusConnection *connection,
              gboolean verbose)
{
	RealmDbusProvider *provider;
	const gchar *const *realms;
	GError *error = NULL;
	gboolean printed = FALSE;
	gint i;

	provider = realm_dbus_provider_proxy_new_sync (connection, G_DBUS_PROXY_FLAGS_NONE,
	                                               REALM_DBUS_BUS_NAME,
	                                               REALM_DBUS_SERVICE_PATH,
	                                               NULL, &error);
	if (error != NULL) {
		realm_handle_error (error, _("Couldn't connect to realmd service"));
		return 1;
	}

	realms = realm_dbus_provider_get_realms (provider);
	for (i = 0; realms[i] != NULL; i++) {
		print_realm_info (realms[i]);
		printed = TRUE;
	}

	if (verbose && !printed)
		g_printerr ("No known realms\n");

	g_object_unref (provider);
	return 0;
}

int
realm_list (int argc,
            char *argv[])
{
	GDBusConnection *connection;
	GOptionContext *context;
	gboolean arg_verbose = FALSE;
	GError *error = NULL;
	gint ret = 0;

	GOptionEntry option_entries[] = {
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

	} else if (argc == 0) {
		g_printerr ("%s: no arguments necessary\n", g_get_prgname ());
		ret = 2;

	} else {
		connection = realm_get_connection (arg_verbose);
		if (connection) {
			ret = perform_list (connection, arg_verbose);
			g_object_unref (connection);
		} else {
			ret = 1;
		}
	}

	g_option_context_free (context);
	return ret;
}
