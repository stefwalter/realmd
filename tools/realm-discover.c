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
print_kerberos_info (RealmClient *client,
                     RealmDbusKerberos *kerberos)
{
	g_print ("  type: kerberos\n");
	g_print ("  realm-name: %s\n", realm_dbus_kerberos_get_realm_name (kerberos));
	g_print ("  domain-name: %s\n", realm_dbus_kerberos_get_domain_name (kerberos));
}

static void
print_realm_info (RealmClient *client,
                  RealmDbusRealm *realm)
{
	RealmDbusKerberos *kerberos;
	const gchar *configured;
	GVariant *details;
	const gchar *name;
	const gchar *value;
	gboolean is_configured;
	gchar *string;
	const gchar *policy;
	GVariantIter iter;

	g_return_if_fail (REALM_DBUS_IS_REALM (realm));
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

	kerberos = realm_client_to_kerberos (client, realm);
	if (kerberos) {
		print_kerberos_info (client, kerberos);
		g_object_unref (kerberos);
	} else {
		g_print ("  type: unknown\n");
	}

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
}

static int
perform_discover (RealmClient *client,
                  const gchar *string,
                  const gchar *server_software,
                  const gchar *client_software)
{
	gboolean found = FALSE;
	GError *error = NULL;
	GList *realms;
	GList *l;

	realms = realm_client_discover (client, string, client_software, server_software,
	                                REALM_DBUS_REALM_INTERFACE, &error);

	if (error != NULL) {
		realm_handle_error (error, _("Couldn't discover realms"));
		return 1;
	}

	for (l = realms; l != NULL; l = g_list_next (l)) {
		print_realm_info (client, l->data);
		found = TRUE;
	}

	g_list_free_full (realms, g_object_unref);

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
	RealmClient *client;
	GOptionContext *context;
	gboolean arg_verbose = FALSE;
	gchar *arg_client_software = NULL;
	gchar *arg_server_software = NULL;
	GError *error = NULL;
	gint result = 0;
	gint ret;
	gint i;

	GOptionEntry option_entries[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &arg_verbose, N_("Verbose output"), NULL },
		{ "client-software", 0, 0, G_OPTION_ARG_STRING, &arg_client_software, N_("Use specific client software"), NULL },
		{ "server-software", 0, 0, G_OPTION_ARG_STRING, &arg_server_software, N_("Use specific server software"), NULL },
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

	client = realm_client_new (arg_verbose);
	if (!client) {
		ret = 1;

	/* The default realm? */
	} else if (argc == 1) {
		ret = perform_discover (client, NULL, arg_server_software,
		                        arg_client_software);
		g_object_unref (client);

	/* Specific realms */
	} else {
		for (i = 1; i < argc; i++) {
			ret = perform_discover (client, argv[i],
			                        arg_server_software, arg_client_software);
			if (ret != 0)
				result = ret;
		}
		g_object_unref (client);
	}

	g_free (arg_server_software);
	g_free (arg_client_software);
	g_option_context_free (context);
	return result;
}

static int
perform_list (RealmClient *client,
              gboolean verbose)
{
	RealmDbusProvider *provider;
	const gchar *const *realms;
	gboolean printed = FALSE;
	RealmDbusRealm *realm;
	gint i;

	provider = realm_client_get_provider (client);
	realms = realm_dbus_provider_get_realms (provider);

	for (i = 0; realms && realms[i] != NULL; i++) {
		realm = realm_client_get_realm (client, realms[i]);
		print_realm_info (client, realm);
		printed = TRUE;
		g_object_unref (realm);
	}

	if (verbose && !printed)
		g_printerr ("No known realms\n");

	return 0;
}

int
realm_list (int argc,
            char *argv[])
{
	RealmClient *client;
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
		client = realm_client_new (arg_verbose);
		if (client) {
			ret = perform_list (client, arg_verbose);
			g_object_unref (client);
		} else {
			ret = 1;
		}
	}

	g_option_context_free (context);
	return ret;
}
