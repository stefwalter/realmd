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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include <krb5/krb5.h>

#include <errno.h>
#include <fcntl.h>
#include <locale.h>

static gchar *arg_install = NULL;
gboolean realm_verbose = FALSE;

struct {
	const char *name;
	int (* function) (RealmClient *client, int argc, char *argv[]);
	const char *usage;
	const char *description;
} realm_commands[] = {
	{ "discover", realm_discover, "realm discover -v [realm-name]", N_("Discover available realm") },
	{ "join", realm_join, "realm join -v [-U user] realm-name", N_("Enroll this machine in a realm") },
	{ "leave", realm_leave, "realm leave -v [-U user] [realm-name]", N_("Unenroll this machine from a realm") },
	{ "list", realm_list, "realm list", N_("List known realms") },
	{ "permit", realm_permit, "realm permit [-a] [-R realm] user ...", N_("Permit user logins") },
	{ "deny", realm_deny, "realm deny [-a] [-R realm] user ...", N_("Deny user logins") },
};

void
realm_print_error (const gchar *format,
                   ...)
{
	GString *message;
	va_list va;

	va_start (va, format);

	message = g_string_new ("");
	g_string_append_printf (message, "%s: ", g_get_prgname ());

	va_start (va, format);
	g_string_append_vprintf (message, format, va);
	va_end (va);

	g_printerr ("%s\n", message->str);
	g_string_free (message, TRUE);
}

void
realm_handle_error (GError *error,
                    const gchar *format,
                    ...)
{
	GString *message;
	va_list va;

	message = g_string_new ("");
	g_string_append_printf (message, "%s: ", g_get_prgname ());

	if (format) {
		va_start (va, format);
		g_string_append_vprintf (message, format, va);
		va_end (va);
	}

	if (error) {
		g_dbus_error_strip_remote_error (error);
		if (format)
			g_string_append (message, ": ");
		g_string_append (message, error->message);
		g_error_free (error);
	}

	g_printerr ("%s\n", message->str);
	g_string_free (message, TRUE);
}

GVariant *
realm_build_options (const gchar *first,
                     ...)
{
	const gchar *value;
	GPtrArray *opts;
	GVariant *options;
	GVariant *option;
	va_list va;

	va_start (va, first);

	opts = g_ptr_array_new ();
	while (first != NULL) {
		value = va_arg (va, const gchar *);
		if (value != NULL) {
			option = g_variant_new ("{sv}", first, g_variant_new_string (value));
			g_ptr_array_add (opts, option);
		}

		first = va_arg (va, const gchar *);
	}

	va_end (va);

	if (arg_install) {
		option = g_variant_new ("{sv}", "assume-packages", g_variant_new_boolean (TRUE));
		g_ptr_array_add (opts, option);
	}

	options = g_variant_new_array (G_VARIANT_TYPE ("{sv}"), (GVariant * const*)opts->pdata, opts->len);
	g_ptr_array_free (opts, TRUE);

	return options;
}

static void
propagate_krb5_error (GError **dest,
                      krb5_context context,
                      krb5_error_code code,
                      const gchar *format,
                      ...)
{
	GString *message;
	va_list va;

	message = g_string_new ("");

	if (format) {
		va_start (va, format);
		g_string_append_vprintf (message, format, va);
		va_end (va);
	}

	if (code != 0) {
		if (format)
			g_string_append (message, ": ");
		g_string_append (message, krb5_get_error_message (context, code));
	}

	g_set_error_literal (dest, g_quark_from_static_string ("krb5"),
	                     code, message->str);
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

GVariant *
realm_kinit_to_kerberos_cache (const gchar *name,
                               const gchar *realm,
                               const gchar *password,
                               GError **error)
{
	krb5_get_init_creds_opt *options = NULL;
	krb5_context context = NULL;
	krb5_principal principal = NULL;
	krb5_error_code code;
	int temp_fd;
	gchar *full_name = NULL;
	gchar *filename = NULL;
	krb5_ccache ccache = NULL;
	krb5_creds my_creds;
	GVariant *result = NULL;

	code = krb5_init_context (&context);
	if (code != 0) {
		propagate_krb5_error (error, NULL, code, _("Couldn't initialize kerberos"));
		goto cleanup;
	}

	if (strchr (name, '@') == NULL)
		name = full_name = g_strdup_printf ("%s@%s", name, realm);

	code = krb5_parse_name (context, name, &principal);
	if (code != 0) {
		propagate_krb5_error (error, context, code, _("Couldn't parse user name: %s"), name);
		goto cleanup;
	}

	code = krb5_get_init_creds_opt_alloc (context, &options);
	if (code != 0) {
		propagate_krb5_error (error, context, code, _("Couldn't setup kerberos options"));
		goto cleanup;
	}

	filename = g_build_filename (g_get_user_runtime_dir (), "realmd-krb5-cache.XXXXXX", NULL);
	temp_fd = g_mkstemp_full (filename, O_RDWR, S_IRUSR | S_IWUSR);
	if (temp_fd == -1) {
		int errn = errno;
		g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errn),
		             _("Couldn't create credential cache file: %s"), g_strerror (errn));
		goto cleanup;
	}
	close (temp_fd);

	code = krb5_cc_resolve (context, filename, &ccache);
	if (code != 0) {
		propagate_krb5_error (error, context, code, _("Couldn't resolve credential cache"));
		goto cleanup;
	}

	code = krb5_get_init_creds_opt_set_out_ccache (context, options, ccache);
	if (code != 0) {
		propagate_krb5_error (error, context, code, _("Couldn't setup credential cache"));
		goto cleanup;
	}

	code = krb5_get_init_creds_password (context, &my_creds, principal, (char *)password,
	                                     password ? NULL : krb5_prompter_posix,
	                                     0, 0, NULL, options);
	if (code == KRB5KDC_ERR_PREAUTH_FAILED) {
		propagate_krb5_error (error, context, code, _("Invalid password for %s"), name);
		goto cleanup;
	} else if (code != 0) {
		propagate_krb5_error (error, context, code, _("Couldn't authenticate as %s"), name);
		goto cleanup;
	}

	krb5_cc_close (context, ccache);
	ccache = NULL;

	result = read_file_into_variant (filename);
	krb5_free_cred_contents (context, &my_creds);

cleanup:
	g_free (full_name);

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

static int
usage (int code)
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS (realm_commands); i++) {
		if (i > 0)
			g_printerr ("\n");
		g_printerr (" %s\n", realm_commands[i].usage);
		g_printerr ("   %s\n", realm_commands[i].description);
	}

	return code;
}

GOptionEntry realm_global_options[] = {
	{ "install", 'i', 0, G_OPTION_ARG_STRING, &arg_install, N_("Install mode to a specific prefix"), NULL },
	{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &realm_verbose, N_("Verbose output"), NULL },
	{ NULL, }
};

int
main (int argc,
      char *argv[])
{
	const gchar *command = NULL;
	GOptionContext *context;
	RealmClient *client;
	GError *error = NULL;
	gint ret;
	gint i;

	setlocale (LC_ALL, "");

#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif

	g_type_init ();

	/* Parse the global options, don't display help or failure here */
	context = g_option_context_new ("realm");
	g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);
	g_option_context_add_main_entries (context, realm_global_options, NULL);
	g_option_context_set_help_enabled (context, FALSE);
	g_option_context_set_ignore_unknown_options (context, TRUE);

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_warning ("Unexpected error: %s", error->message);
		g_error_free (error);
	}

	g_option_context_free (context);

	/* Find/remove the first non-flag argument: the command */
	for (i = 1; i < argc; i++) {
		if (command == NULL) {
			if (argv[i][0] != '-') {
				command = argv[i];
				argc--;
			}
		}
		if (command != NULL)
			argv[i] = argv[i + 1];
	}

	if (command == NULL)
		return usage (2);

	ret = 2;
	for (i = 0; i < G_N_ELEMENTS (realm_commands); i++) {
		if (g_str_equal (realm_commands[i].name, command)) {
			client = realm_client_new (realm_verbose, arg_install);
			if (!client) {
				ret = 1;
				break;
			}

			ret = (realm_commands[i].function) (client, argc, argv);
			g_object_unref (client);

			break;
		}
	}

	if (ret == 2)
		usage(2);

	g_free (arg_install);
	return ret;

}
