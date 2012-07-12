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

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>

static void
print_realm_info (GVariant *realm_info)
{
	RealmDbusKerberos *realm = NULL;
	const gchar *bus_name;
	const gchar *object_path;
	const gchar *interface_name;
	GError *error = NULL;
	GVariantIter iter;
	GVariant *details;
	const gchar *name;
	const gchar *value;
	gboolean enrolled;
	gchar *string;

	g_variant_get (realm_info, "(&s&o&s)", &bus_name, &object_path, &interface_name);

	if (!g_str_equal (interface_name, REALM_DBUS_KERBEROS_REALM_INTERFACE))
		return;

	realm = realm_dbus_kerberos_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
	                                                    G_DBUS_PROXY_FLAGS_NONE,
	                                                    bus_name, object_path,
	                                                    NULL, &error);

	if (error != NULL) {
		g_warning ("couldn't use realm service: %s", error->message);
		g_error_free (error);
		return;
	}

	enrolled = realm_dbus_kerberos_get_enrolled (realm);
	g_print ("%s\n", realm_dbus_kerberos_get_name (realm));
	g_print ("  domain: %s\n", realm_dbus_kerberos_get_domain (realm));
	g_print ("  enrolled: %s\n", enrolled ? "yes" : "no");

	details = realm_dbus_kerberos_get_details (realm);
	if (details) {
		g_variant_iter_init (&iter, details);
		while (g_variant_iter_loop (&iter, "{&s&s}", &name, &value))
			g_print ("  %s: %s\n", name, value);
	}

	if (enrolled) {
		g_print ("  login-format: %s\n", realm_dbus_kerberos_get_login_format (realm));
		string = g_strjoinv (", ", (gchar **)realm_dbus_kerberos_get_permitted_logins (realm));
		g_print ("  permitted-logins: %s\n", string);
		g_free (string);
	}

	g_object_unref (realm);
}

static void
on_diagnostics_signal (GDBusConnection *connection,
                       const gchar *sender_name,
                       const gchar *object_path,
                       const gchar *interface_name,
                       const gchar *signal_name,
                       GVariant *parameters,
                       gpointer user_data)
{
	const gchar *data;
	const gchar *unused;
	g_variant_get (parameters, "(&s&s)", &data, &unused);
	g_printerr ("%s", data);
}

static void
connect_to_diagnostics (GDBusProxy *proxy)
{
	GDBusConnection *connection;
	const gchar *bus_name;
	const gchar *object_path;

	connection = g_dbus_proxy_get_connection (proxy);
	bus_name = g_dbus_proxy_get_name (proxy);
	object_path = g_dbus_proxy_get_object_path (proxy);

	g_dbus_connection_signal_subscribe (connection, bus_name,
	                                    REALM_DBUS_DIAGNOSTICS_INTERFACE,
	                                    REALM_DBUS_DIAGNOSTICS_SIGNAL,
	                                    object_path, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
	                                    on_diagnostics_signal, NULL, NULL);
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
perform_discover (const gchar *string,
                  gboolean verbose)
{
	RealmDbusProvider *provider;
	GVariant *realm_info;
	gboolean found = FALSE;
	GError *error = NULL;
	GVariantIter iter;
	SyncClosure sync;
	GVariant *realms;
	gint relevance;

	provider = realm_dbus_provider_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
	                                                       G_DBUS_PROXY_FLAGS_NONE,
	                                                       "org.freedesktop.realmd",
	                                                       "/org/freedesktop/realmd",
	                                                       NULL, &error);
	if (error != NULL) {
		realm_handle_error (error, "couldn't connect to realm service");
		return 2;
	}

	/* Setup diagnostics */
	if (verbose)
		connect_to_diagnostics (G_DBUS_PROXY (provider));

	sync.result = NULL;
	sync.loop = g_main_loop_new (NULL, FALSE);

	g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (provider), G_MAXINT);
	realm_dbus_provider_call_discover (provider, string ? string : "",
	                                   "unused-operation-id",
	                                   NULL, on_complete_get_result, &sync);

	/* This mainloop is quit by on_complete_get_result */
	g_main_loop_run (sync.loop);

	realm_dbus_provider_call_discover_finish (provider, &relevance, &realms,
	                                          sync.result, &error);

	g_object_unref (sync.result);
	g_main_loop_unref (sync.loop);

	if (error != NULL) {
		realm_handle_error (error, "couldn't discover realm");
		return 2;
	}

	g_variant_iter_init (&iter, realms);
	while ((realm_info = g_variant_iter_next_value (&iter)) != NULL) {
		print_realm_info (realm_info);
		g_variant_unref (realm_info);
		found = TRUE;
	}

	g_variant_unref (realms);

	if (!found) {
		if (string == NULL)
			realm_handle_error (NULL, "no default domain discovered");
		else
			realm_handle_error (NULL, "no such realm found: %s", string);
		return 1;
	}

	return 0;
}

int
realm_discover (int argc,
                char *argv[])
{
	GOptionContext *context;
	gboolean arg_verbose = FALSE;
	GError *error = NULL;
	gint result = 0;
	gint ret;
	gint i;

	GOptionEntry option_entries[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &arg_verbose, "Verbose output", NULL },
		{ NULL, }
	};

	g_type_init ();

	context = g_option_context_new ("realm-or-domain");
	g_option_context_add_main_entries (context, option_entries, NULL);

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("%s: %s\n", g_get_prgname (), error->message);
		g_error_free (error);
		ret = 2;
	}

	/* The default realm? */
	if (argc == 1) {
		ret = perform_discover (NULL, arg_verbose);

	/* Specific realms */
	} else {
		for (i = 1; i < argc; i++) {
			ret = perform_discover (argv[i], arg_verbose);
			if (ret != 0)
				result = ret;
		}
	}

	g_option_context_free (context);
	return result;
}

static int
perform_list (gboolean verbose)
{
	RealmDbusProvider *provider;
	GVariant *realms;
	GVariant *realm_info;
	GError *error = NULL;
	GVariantIter iter;
	gboolean printed = FALSE;

	provider = realm_dbus_provider_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
	                                                       G_DBUS_PROXY_FLAGS_NONE,
	                                                       "org.freedesktop.realmd",
	                                                       "/org/freedesktop/realmd",
	                                                       NULL, &error);
	if (error != NULL) {
		realm_handle_error (error, "couldn't connect to realm service");
		return 1;
	}

	realms = realm_dbus_provider_get_realms (provider);
	g_variant_iter_init (&iter, realms);
	while (g_variant_iter_loop (&iter, "@(sos)", &realm_info)) {
		print_realm_info (realm_info);
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
	GOptionContext *context;
	gboolean arg_verbose = FALSE;
	GError *error = NULL;
	gint ret = 0;

	GOptionEntry option_entries[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &arg_verbose, "Verbose output", NULL },
		{ NULL, }
	};

	context = g_option_context_new ("realm");
	g_option_context_add_main_entries (context, option_entries, NULL);

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("%s: %s\n", g_get_prgname (), error->message);
		g_error_free (error);
		ret = 2;

	} else if (argc == 0) {
		g_printerr ("%s: no arguments necessary\n", g_get_prgname ());
		ret = 2;

	} else {
		ret = perform_list (arg_verbose);
	}

	g_option_context_free (context);
	return ret;
}
