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
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

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

static GVariant *
read_file_into_variant (const gchar *filename)
{
	GVariant *variant;
	GError *error = NULL;
	gchar *contents;
	gsize length;

	g_file_get_contents (filename, &contents, &length, &error);
	if (error != NULL) {
		realm_handle_error (error, _("Couldn't read credential cache"));
		return NULL;
	}

	variant = g_variant_new_from_data (G_VARIANT_TYPE ("ay"),
	                                   contents, length,
	                                   TRUE, g_free, contents);

	return g_variant_ref_sink (variant);
}

static GVariant *
kinit_to_kerberos_cache (const gchar *name,
                         const gchar *realm)
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
		handle_krb5_error (code, NULL, _("Couldn't initialize kerberos"));
		goto cleanup;
	}

	code = krb5_parse_name (context, name, &principal);
	if (code != 0) {
		handle_krb5_error (code, context, _("Couldn't parse user name: %s"), name);
		goto cleanup;
	}

	/* Use our realm as the default */
	if (strchr (name, '@') == NULL) {
		code = krb5_set_principal_realm (context, principal, realm);
		g_return_val_if_fail (code == 0, NULL);
	}

	code = krb5_get_init_creds_opt_alloc (context, &options);
	if (code != 0) {
		handle_krb5_error (code, context, _("Couldn't setup kerberos options"));
		goto cleanup;
	}

	filename = g_build_filename (g_get_user_runtime_dir (), "realmd-krb5-cache.XXXXXX", NULL);
	temp_fd = g_mkstemp_full (filename, O_RDWR, S_IRUSR | S_IWUSR);
	if (temp_fd == -1) {
		realm_handle_error (NULL, _("Couldn't create credential cache file: %s"), g_strerror (errno));
		goto cleanup;
	}
	close (temp_fd);

	code = krb5_cc_resolve (context, filename, &ccache);
	if (code != 0) {
		handle_krb5_error (code, context, _("Couldn't resolve credential cache"));
		goto cleanup;
	}

	code = krb5_get_init_creds_opt_set_out_ccache (context, options, ccache);
	if (code != 0) {
		handle_krb5_error (code, context, _("Couldn't setup credential cache"));
		goto cleanup;
	}

	code = krb5_get_init_creds_password (context, &my_creds, principal, NULL,
	                                     krb5_prompter_posix, 0, 0, NULL, options);
	if (code != 0) {
		handle_krb5_error (code, context, _("Couldn't authenticate as %s"), name);
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

static const gchar *
find_appropriate_cred_type (RealmDbusKerberosMembership *membership,
                            gboolean join,
                            const gchar **owner)
{
	GVariant *supported;
	GVariantIter iter;
	const gchar *cred_owner;
	const gchar *cred_type;

	if (join)
		supported = realm_dbus_kerberos_membership_get_supported_join_credentials (membership);
	else
		supported = realm_dbus_kerberos_membership_get_supported_leave_credentials (membership);

	g_variant_iter_init (&iter, supported);
	while (g_variant_iter_loop (&iter, "(&s&s)", &cred_type, &cred_owner)) {
		if (g_str_equal (cred_type, "ccache") || g_str_equal (cred_type, "password")) {
			*owner = g_intern_string (cred_owner);
			return g_intern_string (cred_type);
		}
	}

	return NULL;
}

static RealmDbusKerberosMembership *
find_supported_membership_in_realms (const gchar **realms)
{
	RealmDbusKerberosMembership *membership;
	RealmDbusRealm *realm;
	GDBusProxy *proxy;
	GError *error = NULL;
	gint i;

	for (i = 0; realms[i] != NULL; i++) {
		realm = realm_path_to_realm (realms[i]);
		if (!realm)
			continue;

		if (!realm_supports_interface (realm, REALM_DBUS_KERBEROS_INTERFACE) ||
		    !realm_supports_interface (realm, REALM_DBUS_KERBEROS_MEMBERSHIP_INTERFACE)) {
			g_object_unref (realm);
			continue;
		}

		proxy = G_DBUS_PROXY (realm);
		membership = realm_dbus_kerberos_membership_proxy_new_sync (g_dbus_proxy_get_connection (proxy),
		                                                            G_DBUS_PROXY_FLAGS_NONE,
		                                                            g_dbus_proxy_get_name (proxy),
		                                                            g_dbus_proxy_get_object_path (proxy),
		                                                            NULL, &error);

		g_object_unref (realm);

		if (error != NULL) {
			g_warning ("Couldn't get kerberos proxy: %s", error->message);
			g_error_free (error);
			continue;
		}

		return membership;
	}

	return NULL;
}

static RealmDbusKerberosMembership *
find_configured_membership_in_realms (const gchar **realms,
                                      const gchar *name)
{
	RealmDbusKerberosMembership *membership;
	RealmDbusRealm *realm;
	GDBusProxy *proxy;
	GError *error = NULL;
	gint i;

	for (i = 0; realms[i] != NULL; i++) {
		realm = realm_path_to_realm (realms[i]);
		if (!realm)
			continue;

		if (!realm_supports_interface (realm, REALM_DBUS_KERBEROS_INTERFACE) ||
		    g_strcmp0 (realm_dbus_realm_get_configured (realm),
		               REALM_DBUS_KERBEROS_MEMBERSHIP_INTERFACE) != 0) {
			g_object_unref (realm);
			continue;
		}

		if (name != NULL &&
		    g_ascii_strcasecmp (realm_dbus_realm_get_name (realm), name) != 0) {
			g_object_unref (realm);
			continue;
		}

		proxy = G_DBUS_PROXY (realm);
		membership = realm_dbus_kerberos_membership_proxy_new_sync (g_dbus_proxy_get_connection (proxy),
		                                                            G_DBUS_PROXY_FLAGS_NONE,
		                                                            g_dbus_proxy_get_name (proxy),
		                                                            g_dbus_proxy_get_object_path (proxy),
		                                                            NULL, &error);

		g_object_unref (realm);

		if (error != NULL) {
			g_warning ("Couldn't get kerberos proxy: %s", error->message);
			g_error_free (error);
			continue;
		}

		return membership;
	}

	return NULL;
}

static RealmDbusKerberos *
cast_to_kerberos (gpointer proxy)
{
	RealmDbusKerberos *kerberos;
	GError *error = NULL;

	kerberos = realm_dbus_kerberos_proxy_new_sync (g_dbus_proxy_get_connection (proxy),
	                                               G_DBUS_PROXY_FLAGS_NONE,
	                                               g_dbus_proxy_get_name (proxy),
	                                               g_dbus_proxy_get_object_path (proxy),
	                                               NULL, &error);

	if (error != NULL) {
		g_warning ("Couldn't get kerberos proxy: %s", error->message);
		g_error_free (error);
	}

	return kerberos;
}

static GVariant *
build_ccache_or_password_creds (RealmDbusKerberosMembership *membership,
                                const gchar *user_name,
                                gboolean join)
{
	RealmDbusKerberos *kerberos;
	GVariant *contents;
	const gchar *cred_type;
	const gchar *cred_owner;
	GVariant *creds = NULL;
	const gchar *realm_name;
	gchar *password;
	gchar *prompt;

	cred_type = find_appropriate_cred_type (membership, join, &cred_owner);
	if (cred_type == NULL) {
		realm_handle_error (NULL, _("Realm has no supported way to authenticate"));
		return NULL;
	}

	if (user_name == NULL)
		user_name = realm_dbus_kerberos_membership_get_suggested_administrator (membership);
	if (user_name == NULL)
		user_name = g_get_user_name ();

	/* Do a kinit for the given realm */
	if (g_str_equal (cred_type, "ccache")) {
		kerberos = cast_to_kerberos (membership);
		realm_name = realm_dbus_kerberos_get_realm_name (kerberos);
		contents = kinit_to_kerberos_cache (user_name, realm_name);
		g_object_unref (kerberos);

	} else if (g_str_equal (cred_type, "password")) {
		prompt = g_strdup_printf (_("Password for %s: "), user_name);
		password = getpass (prompt);
		g_free (prompt);

		if (password == NULL) {
			realm_print_error (_("Couldn't prompt for password: %s"), g_strerror (errno));
			contents = NULL;
		} else {
			contents = g_variant_new ("(ss)", user_name, password);
			memset (password, 0, strlen (password));
		}

	} else {
		g_assert_not_reached ();
	}

	if (contents) {
		creds = g_variant_new ("(ss@v)", cred_type, cred_owner,
		                       g_variant_new_variant (contents));
		g_variant_ref_sink (creds);
	}

	return creds;
}

static int
realm_join_or_leave (RealmDbusKerberosMembership *membership,
                     const gchar *user_name,
                     const gchar *computer_ou,
                     gboolean join)
{
	GError *error = NULL;
	GVariant *options;
	GVariant *creds;
	SyncClosure sync;

	creds = build_ccache_or_password_creds (membership, user_name, join);

	sync.result = NULL;
	sync.loop = g_main_loop_new (NULL, FALSE);

	options = realm_build_options ("computer-ou", computer_ou, NULL);
	g_variant_ref_sink (options);

	/* Start actual operation */
	g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (membership), G_MAXINT);
	if (join)
		realm_dbus_kerberos_membership_call_join (membership, creds, options,
		                                          NULL, on_complete_get_result, &sync);
	else
		realm_dbus_kerberos_membership_call_leave (membership, creds, options,
		                                           NULL, on_complete_get_result, &sync);

	g_variant_unref (options);
	g_variant_unref (creds);

	/* This mainloop is quit by on_complete_get_result */
	g_main_loop_run (sync.loop);

	if (join)
		realm_dbus_kerberos_membership_call_join_finish (membership, sync.result, &error);
	else
		realm_dbus_kerberos_membership_call_leave_finish (membership, sync.result, &error);

	g_object_unref (sync.result);
	g_main_loop_unref (sync.loop);

	if (error != NULL) {
		realm_handle_error (error, join ? _("Couldn't join realm") : _("Couldn't leave realm"));
		return 1;
	}

	return 0;
}

static int
perform_join (GDBusConnection *connection,
              const gchar *string,
              const gchar *user_name,
              const gchar *computer_ou)
{
	RealmDbusKerberosMembership *membership;
	RealmDbusProvider *provider;
	GVariant *options;
	GError *error = NULL;
	gchar **realms;
	gint relevance;
	gint ret;

	provider = realm_dbus_provider_proxy_new_sync (connection, G_DBUS_PROXY_FLAGS_NONE,
	                                               REALM_DBUS_BUS_NAME,
	                                               REALM_DBUS_SERVICE_PATH,
	                                               NULL, &error);
	if (error != NULL) {
		realm_handle_error (error, _("Couldn't connect to realm service"));
		return 1;
	}

	options = realm_build_options (NULL, NULL);
	g_variant_ref_sink (options);

	g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (provider), G_MAXINT);
	realm_dbus_provider_call_discover_sync (provider, string, options,
	                                        &relevance, &realms, NULL, &error);

	g_object_unref (provider);
	g_variant_unref (options);

	if (error != NULL) {
		realm_handle_error (error, _("Couldn't connect to realm service"));
		return 1;
	}

	membership = find_supported_membership_in_realms ((const gchar **)realms);
	g_strfreev (realms);

	if (membership == NULL) {
		realm_handle_error (NULL, _("No such realm found: %s"), string);
		return 1;
	}

	ret = realm_join_or_leave (membership, user_name, computer_ou, TRUE);

	g_object_unref (membership);

	return ret;
}

static int
perform_leave (GDBusConnection *connection,
               const gchar *string,
               const gchar *user_name)
{
	RealmDbusKerberosMembership *membership;
	gchar **paths;
	gint ret;

	paths = realm_lookup_paths ();
	if (paths == NULL)
		return 1;

	membership = find_configured_membership_in_realms ((const gchar **)paths, string);
	g_strfreev (paths);

	if (membership == NULL) {
		if (string == NULL)
			realm_handle_error (NULL, "Couldn't find a configured realm");
		else
			realm_handle_error (NULL, "Couldn't find the configured realm: %s", string);
		return 1;
	}

	ret = realm_join_or_leave (membership, user_name, NULL, FALSE);
	g_object_unref (membership);

	return ret;
}

int
realm_join (int argc,
            char *argv[])
{
	GOptionContext *context;
	GDBusConnection *connection;
	gchar *arg_user = NULL;
	gboolean arg_verbose = FALSE;
	GError *error = NULL;
	const gchar *realm_name;
	gchar *arg_computer_ou = NULL;
	gint ret = 0;

	GOptionEntry option_entries[] = {
		{ "user", 'U', 0, G_OPTION_ARG_STRING, &arg_user, N_("User name to use for enrollment"), NULL },
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &arg_verbose, N_("Verbose output"), NULL },
		{ "computer-ou", 0, 0, G_OPTION_ARG_STRING, &arg_computer_ou, N_("Computer OU DN to join"), NULL },
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
		connection = realm_get_connection (arg_verbose);
		if (connection) {
			realm_name = argc < 2 ? "" : argv[1];
			ret = perform_join (connection, realm_name, arg_user,
			                    arg_computer_ou);
			g_object_unref (connection);
		} else {
			ret = 1;
		}
	}

	g_free (arg_user);
	g_free (arg_computer_ou);
	g_option_context_free (context);
	return ret;
}

int
realm_leave (int argc,
            char *argv[])
{
	GDBusConnection *connection;
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
		connection = realm_get_connection (arg_verbose);
		if (connection) {
			realm_name = argc < 2 ? NULL : argv[1];
			ret = perform_leave (connection, realm_name, arg_user);
			g_object_unref (connection);
		} else {
			ret = 1;
		}
	}

	g_free (arg_user);
	g_option_context_free (context);
	return ret;
}
