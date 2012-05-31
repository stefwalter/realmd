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
handle_error (GError *error,
              const gchar *format,
              ...)
{
	GString *message;
	va_list va;

	message = g_string_new ("");
	g_string_append_printf (message, "%s: ", g_get_prgname ());

	va_start (va, format);
	g_string_append_vprintf (message, format, va);
	va_end (va);

	if (error) {
		g_string_append (message, ": ");
		g_string_append (message, error->message);
		g_error_free (error);
	}

	g_printerr ("%s\n", message->str);
	g_string_free (message, TRUE);
}

static void
handle_krb5_error (krb5_error_code code,
                   krb5_context context,
                   const gchar *format,
                   ...)
{
	GString *message;
	va_list va;

	message = g_string_new ("");
	g_string_append_printf (message, "%s: ", g_get_prgname ());

	va_start (va, format);
	g_string_append_vprintf (message, format, va);
	va_end (va);

	if (code != 0) {
		g_string_append (message, ": ");
		g_string_append (message, krb5_get_error_message (context, code));
	}

	g_printerr ("%s\n", message->str);
	g_string_free (message, TRUE);
}

static RealmDbusKerberosRealm *
realm_info_to_realm_proxy (GVariant *realm_info)
{
	RealmDbusKerberosRealm *realm = NULL;
	const gchar *bus_name;
	const gchar *object_path;
	const gchar *interface_name;
	GError *error = NULL;

	g_variant_get (realm_info, "(&s&o&s)", &bus_name, &object_path, &interface_name);

	if (g_str_equal (interface_name, REALM_DBUS_KERBEROS_REALM_INTERFACE)) {
		realm = realm_dbus_kerberos_realm_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
		                                                          G_DBUS_PROXY_FLAGS_NONE,
		                                                          bus_name, object_path,
		                                                          NULL, &error);
	}

	if (error != NULL)
		handle_error (error, "couldn't use realm service");
	else if (realm == NULL)
		handle_error (NULL, "unsupported realm type: %s", interface_name);

	return realm;
}

static RealmDbusKerberosRealm *
realms_to_realm_proxy (GVariant *realms)
{
	RealmDbusKerberosRealm *realm = NULL;
	GVariant *realm_info;
	GVariantIter iter;

	g_variant_iter_init (&iter, realms);
	while ((realm_info = g_variant_iter_next_value (&iter)) != NULL) {
		realm = realm_info_to_realm_proxy (realm_info);
		g_variant_unref (realm_info);

		if (realm != NULL)
			break;
	}

	return realm;
}

static RealmDbusKerberosRealm *
discover_realm_for_string (const gchar *string)
{
	RealmDbusKerberosRealm *realm;
	RealmDbusProvider *provider;
	GError *error = NULL;
	GVariant *realms;
	gint relevance;

	provider = realm_dbus_provider_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
	                                                       G_DBUS_PROXY_FLAGS_NONE,
	                                                       REALM_DBUS_ALL_PROVIDER_NAME,
	                                                       REALM_DBUS_ALL_PROVIDER_PATH,
	                                                       NULL, &error);
	if (error != NULL) {
		handle_error (error, "couldn't connect to realm service");
		return NULL;
	}

	g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (provider), G_MAXINT);
	realm_dbus_provider_call_discover_sync (provider, string, &relevance,
	                                        &realms, NULL, &error);

	g_object_unref (provider);

	if (error != NULL) {
		handle_error (error, "couldn't connect to realm service");
		return NULL;
	}

	realm = realms_to_realm_proxy (realms);
	g_variant_unref (realms);

	if (realm == NULL)
		handle_error (NULL, "no such realm found: %s", string);

	return realm;
}

static GVariant *
read_file_into_variant (const gchar *filename)
{
	GVariant *variant;
	GError *error = NULL;
	gchar *contents;
	gsize length;

	g_file_get_contents (filename, &contents, &length, &error);
	if (error != NULL) {
		handle_error (error, "couldn't read credential cache");
		return NULL;
	}

	variant = g_variant_new_from_data (G_VARIANT_TYPE ("ay"),
	                                   contents, length,
	                                   TRUE, g_free, contents);

	return g_variant_ref_sink (variant);
}

static GVariant *
kinit_to_kerberos_cache (const gchar *name)
{
	krb5_get_init_creds_opt *options = NULL;
	krb5_context context = NULL;
	krb5_principal principal = NULL;
	krb5_error_code code;
	int temp_fd;
	gchar *filename = NULL;
	krb5_ccache ccache = NULL;
	krb5_creds my_creds;
	GVariant *result = NULL;

	code = krb5_init_context (&context);
	if (code != 0) {
		handle_krb5_error (code, NULL, "couldn't initialize kerberos");
		goto cleanup;
	}

	code = krb5_parse_name (context, name, &principal);
	if (code != 0) {
		handle_krb5_error (code, context, "couldn't parse user name");
		goto cleanup;
	}

	code = krb5_get_init_creds_opt_alloc (context, &options);
	if (code != 0) {
		handle_krb5_error (code, context, "couldn't setup options");
		goto cleanup;
	}

	filename = g_build_filename (g_get_user_runtime_dir (), "realmd-krb5-cache.XXXXXX", NULL);
	temp_fd = g_mkstemp_full (filename, O_RDWR, S_IRUSR | S_IWUSR);
	if (temp_fd == -1) {
		handle_error (NULL, "couldn't create credential cache file: %s", g_strerror (errno));
		goto cleanup;
	}
	close (temp_fd);

	code = krb5_cc_resolve (context, filename, &ccache);
	if (code != 0) {
		handle_krb5_error (code, context, "couldn't resolve credential cache");
		goto cleanup;
	}

	code = krb5_get_init_creds_opt_set_out_ccache (context, options, ccache);
	if (code != 0) {
		handle_krb5_error (code, context, "couldn't setup credential cache");
		goto cleanup;
	}

	code = krb5_get_init_creds_password (context, &my_creds, principal, NULL,
	                                     krb5_prompter_posix, 0, 0, NULL, options);
	if (code != 0) {
		handle_krb5_error (code, context, "couldn't authenticate as %s", name);
		goto cleanup;
	}

	krb5_cc_close (context, ccache);
	ccache = NULL;

	result = read_file_into_variant (filename);
	krb5_free_cred_contents (context, &my_creds);

cleanup:
	if (filename) {
		g_unlink (filename);
		g_free (filename);
	}

	if (options)
		krb5_get_init_creds_opt_free (context, options);
	if (principal)
		krb5_free_principal (context, principal);
	if (ccache)
		krb5_cc_close (context, ccache);
	if (context)
		krb5_free_context (context);
	return result;
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
	g_variant_get (parameters, "(&s)", &data);
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
realm_join_or_leave (const gchar *string,
                     const gchar *user_name,
                     gboolean verbose,
                     gboolean join)
{
	RealmDbusKerberosRealm *realm;
	GVariant *kerberos_cache;
	const gchar *realm_name;
	GError *error = NULL;
	GVariant *options;
	SyncClosure sync;
	gchar *principal;

	if (user_name == NULL)
		user_name = g_get_user_name ();

	/* Discover the realm */
	realm = discover_realm_for_string (string);
	if (realm == NULL)
		return 1;

	/* Do a kinit for the given realm */
	realm_name = realm_dbus_kerberos_realm_get_name (realm);
	principal = g_strdup_printf ("%s@%s", user_name, realm_name);
	kerberos_cache = kinit_to_kerberos_cache (principal);
	g_free (principal);
	if (kerberos_cache == NULL) {
		g_object_unref (realm);
		return 1;
	}

	sync.result = NULL;
	sync.loop = g_main_loop_new (NULL, FALSE);

	/* Setup diagnostics */
	if (verbose)
		connect_to_diagnostics (G_DBUS_PROXY (realm));

	options = g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0);
	g_variant_ref_sink (options);

	/* Start actual operation */
	g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (realm), G_MAXINT);
	if (join)
		realm_dbus_kerberos_realm_call_enroll_with_credential_cache (realm, kerberos_cache, options,
		                                                             NULL, on_complete_get_result,
		                                                             &sync);
	else
		realm_dbus_kerberos_realm_call_unenroll_with_credential_cache (realm, kerberos_cache, options,
		                                                               NULL, on_complete_get_result,
		                                                               &sync);

	g_variant_unref (options);
	g_variant_unref (kerberos_cache);

	/* This mainloop is quit by on_complete_get_result */
	g_main_loop_run (sync.loop);

	if (join)
		realm_dbus_kerberos_realm_call_enroll_with_credential_cache_finish (realm,
		                                                                    sync.result,
		                                                                    &error);
	else
		realm_dbus_kerberos_realm_call_unenroll_with_credential_cache_finish (realm,
		                                                                      sync.result,
		                                                                      &error);

	g_object_unref (sync.result);
	g_main_loop_unref (sync.loop);
	g_object_unref (realm);

	if (error != NULL) {
		handle_error (error, join ? "couldn't join realm" : "couldn't leave realm");
		return 1;
	}

	return 0;
}

static int
realm_list (gboolean verbose)
{
	RealmDbusProvider *provider;
	RealmDbusKerberosRealm *realm;
	GVariant *realms;
	GVariant *realm_info;
	GError *error = NULL;
	GVariantIter iter;
	gboolean printed = FALSE;

	provider = realm_dbus_provider_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
	                                                       G_DBUS_PROXY_FLAGS_NONE,
	                                                       REALM_DBUS_ALL_PROVIDER_NAME,
	                                                       REALM_DBUS_ALL_PROVIDER_PATH,
	                                                       NULL, &error);
	if (error != NULL) {
		handle_error (error, "couldn't connect to realm service");
		return 1;
	}

	realms = realm_dbus_provider_get_realms (provider);
	g_variant_iter_init (&iter, realms);
	while (g_variant_iter_loop (&iter, "@(sos)", &realm_info)) {
		realm = realm_info_to_realm_proxy (realm_info);
		if (realm != NULL) {
			g_print ("%s: %s\n",
			         realm_dbus_kerberos_realm_get_name (realm),
			         realm_dbus_kerberos_realm_get_enrolled (realm) ? "enrolled" : "not enrolled");
			g_object_unref (realm);
		}
		printed = TRUE;
	}

	if (verbose && !printed)
		g_printerr ("No known realms\n");

	g_object_unref (provider);
	return 0;
}

int
main (int argc,
      char *argv[])
{
	GOptionContext *context;
	gchar *arg_user = NULL;
	gboolean arg_join = FALSE;
	gboolean arg_leave = FALSE;
	gboolean arg_verbose = FALSE;
	GError *error = NULL;
	gint ret = 0;

	GOptionEntry option_entries[] = {
		{ "join", 'j', 0, G_OPTION_ARG_NONE, &arg_join, "Join a realm", NULL },
		{ "leave", 'l', 0, G_OPTION_ARG_NONE, &arg_leave, "Leave the realm", NULL },
		{ "user", 'U', 0, G_OPTION_ARG_STRING, &arg_user, "User name to use for enrollment", NULL },
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &arg_verbose, "Verbose output", NULL },
		{ NULL, }
	};

	g_type_init ();

	context = g_option_context_new ("realm");
	g_option_context_add_main_entries (context, option_entries, NULL);

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("%s: %s\n", g_get_prgname (), error->message);
		g_error_free (error);
		ret = 2;

	} else if (arg_join) {
		if (argc != 2) {
			g_printerr ("%s: specify one realm to leave\n", g_get_prgname ());
			ret = 2;
		} else {
			ret = realm_join_or_leave (argv[1], arg_user, arg_verbose, TRUE);
		}

	} else if (arg_leave) {
		if (argc != 2) {
			g_printerr ("%s: specify one realm to leave\n", g_get_prgname ());
			ret = 2;
		} else {
			ret = realm_join_or_leave (argv[1], arg_user, arg_verbose, FALSE);
		}

	} else if (argc == 1) {
		ret = realm_list (arg_verbose);

	} else {
		g_printerr ("%s: invalid options\n", g_get_prgname ());
		ret = 2;
	}

	g_free (arg_user);
	g_option_context_free (context);
	return ret;
}
