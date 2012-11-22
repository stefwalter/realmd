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

#include "valgrind/valgrind.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include <krb5/krb5.h>

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
	{ "permit", realm_permit, "realm permit [-ax] [-R realm] user ...", N_("Permit user logins") },
	{ "deny", realm_deny, "realm deny --all [-R realm]", N_("Deny user logins") },
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

	option = g_variant_new ("{sv}", "operation", g_variant_new_string (realm_operation_id));
	g_ptr_array_add (opts, option);

	options = g_variant_new_array (G_VARIANT_TYPE ("{sv}"), (GVariant * const*)opts->pdata, opts->len);
	g_ptr_array_free (opts, TRUE);

	return options;
}

gboolean
realm_is_configured (RealmDbusRealm *realm)
{
	const gchar *configured;

	g_return_val_if_fail (REALM_DBUS_IS_REALM (realm), FALSE);

	configured = realm_dbus_realm_get_configured (realm);
	return (configured && !g_str_equal (configured, ""));
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

	/* Behave well under valgrind */
	if (RUNNING_ON_VALGRIND) {
		if (!g_getenv ("G_SLICE"))
			g_setenv ("G_SLICE", "always-malloc", TRUE);
	}

	setlocale (LC_ALL, "");

#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif

#if !GLIB_CHECK_VERSION(2, 36, 0)
	g_type_init ();
#endif

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
