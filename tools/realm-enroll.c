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

/* Only one operation at a time per process, so fine to do this */
static const gchar *operation_id = "realm-enroll";

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

static RealmDbusKerberos *
realms_to_realm_proxy (GVariant *realms,
                       const gchar *enrolled)
{
	RealmDbusKerberos *realm = NULL;
	GVariant *realm_info;
	GVariantIter iter;
	const gchar *name;

	g_variant_iter_init (&iter, realms);
	while ((realm_info = g_variant_iter_next_value (&iter)) != NULL) {
		realm = realm_info_to_realm_proxy (realm_info);
		g_variant_unref (realm_info);

		if (realm != NULL && enrolled &&
		    !realm_dbus_kerberos_get_enrolled (realm)) {
			name = realm_dbus_kerberos_get_name (realm);
			if (name && g_ascii_strcasecmp (enrolled, name) == 0) {
				g_object_unref (realm);
				realm = NULL;
			}
		}

		if (realm != NULL)
			break;
	}

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
		realm_handle_error (error, "couldn't read credential cache");
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
		realm_handle_error (NULL, "couldn't create credential cache file: %s", g_strerror (errno));
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
	const gchar *operation_id;
	const gchar *data;

	g_variant_get (parameters, "(&s&s)", &data, &operation_id);
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
realm_join_or_leave (RealmDbusKerberos *realm,
                     const gchar *user_name,
                     gboolean verbose,
                     gboolean join)
{
	GVariant *kerberos_cache;
	const gchar *realm_name;
	GError *error = NULL;
	GVariant *options;
	SyncClosure sync;
	gchar *principal;

	if (user_name == NULL)
		user_name = realm_dbus_kerberos_get_suggested_administrator (realm);
	if (user_name == NULL)
		user_name = g_get_user_name ();

	/* Do a kinit for the given realm */
	realm_name = realm_dbus_kerberos_get_name (realm);
	principal = g_strdup_printf ("%s@%s", user_name, realm_name);
	kerberos_cache = kinit_to_kerberos_cache (principal);
	g_free (principal);
	if (kerberos_cache == NULL)
		return 1;

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
		realm_dbus_kerberos_call_enroll_with_credential_cache (realm, kerberos_cache, options,
		                                                       operation_id, NULL,
		                                                       on_complete_get_result, &sync);
	else
		realm_dbus_kerberos_call_unenroll_with_credential_cache (realm, kerberos_cache, options,
		                                                         operation_id, NULL,
		                                                         on_complete_get_result, &sync);

	g_variant_unref (options);
	g_variant_unref (kerberos_cache);

	/* This mainloop is quit by on_complete_get_result */
	g_main_loop_run (sync.loop);

	if (join)
		realm_dbus_kerberos_call_enroll_with_credential_cache_finish (realm,
		                                                              sync.result,
		                                                              &error);
	else
		realm_dbus_kerberos_call_unenroll_with_credential_cache_finish (realm,
		                                                                sync.result,
		                                                                &error);

	g_object_unref (sync.result);
	g_main_loop_unref (sync.loop);

	if (error != NULL) {
		realm_handle_error (error, join ? "couldn't join realm" : "couldn't leave realm");
		return 1;
	}

	return 0;
}

static int
perform_join (const gchar *string,
              const gchar *user_name,
              gboolean verbose)
{
	RealmDbusKerberos *realm;
	RealmDbusProvider *provider;
	GError *error = NULL;
	GVariant *realms;
	gint relevance;
	gint ret;

	provider = realm_dbus_provider_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
	                                                       G_DBUS_PROXY_FLAGS_NONE,
	                                                       "org.freedesktop.realmd",
	                                                       "/org/freedesktop/realmd",
	                                                       NULL, &error);
	if (error != NULL) {
		realm_handle_error (error, "couldn't connect to realm service");
		return 1;
	}

	g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (provider), G_MAXINT);
	realm_dbus_provider_call_discover_sync (provider, string, operation_id,
	                                        &relevance, &realms, NULL, &error);

	g_object_unref (provider);

	if (error != NULL) {
		realm_handle_error (error, "couldn't connect to realm service");
		return 1;
	}

	realm = realms_to_realm_proxy (realms, FALSE);
	g_variant_unref (realms);

	if (realm == NULL) {
		realm_handle_error (NULL, "no such realm found: %s", string);
		return 1;
	}

	ret = realm_join_or_leave (realm, user_name, verbose, TRUE);
	g_object_unref (realm);

	return ret;
}

static int
perform_leave (const gchar *string,
               const gchar *user_name,
               gboolean verbose)
{
	RealmDbusKerberos *realm;
	gint ret;

	/* Find the right realm, but only enrolled */
	realm = realm_name_to_enrolled (string);

	/* Message already printed */
	if (realm == NULL)
		return 1;

	ret = realm_join_or_leave (realm, user_name, verbose, FALSE);
	g_object_unref (realm);

	return ret;
}

int
realm_join (int argc,
            char *argv[])
{
	GOptionContext *context;
	gchar *arg_user = NULL;
	gboolean arg_verbose = FALSE;
	GError *error = NULL;
	const gchar *realm_name;
	gint ret = 0;

	GOptionEntry option_entries[] = {
		{ "user", 'U', 0, G_OPTION_ARG_STRING, &arg_user, "User name to use for enrollment", NULL },
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &arg_verbose, "Verbose output", NULL },
		{ NULL, }
	};

	context = g_option_context_new ("realm");
	g_option_context_add_main_entries (context, option_entries, NULL);

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("%s: %s\n", g_get_prgname (), error->message);
		g_error_free (error);
		ret = 2;

	} else if (argc > 2) {
		g_printerr ("%s: specify one realm to join\n", g_get_prgname ());
		ret = 2;

	} else {
		realm_name = argc < 2 ? "" : argv[1];
		ret = perform_join (realm_name, arg_user, arg_verbose);
	}

	g_free (arg_user);
	g_option_context_free (context);
	return ret;
}

int
realm_leave (int argc,
            char *argv[])
{
	GOptionContext *context;
	gchar *arg_user = NULL;
	gboolean arg_verbose = FALSE;
	GError *error = NULL;
	const gchar *realm_name;
	gint ret = 0;

	GOptionEntry option_entries[] = {
		{ "user", 'U', 0, G_OPTION_ARG_STRING, &arg_user, "User name to use for enrollment", NULL },
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &arg_verbose, "Verbose output", NULL },
		{ NULL, }
	};

	context = g_option_context_new ("realm");
	g_option_context_add_main_entries (context, option_entries, NULL);

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("%s: %s\n", g_get_prgname (), error->message);
		g_error_free (error);
		ret = 2;

	} else if (argc < 2) {
		g_printerr ("%s: specify one realm to join\n", g_get_prgname ());
		ret = 2;

	} else {
		realm_name = argc < 2 ? NULL : argv[1];
		ret = perform_leave (realm_name, arg_user, arg_verbose);
	}

	g_free (arg_user);
	g_option_context_free (context);
	return ret;
}
