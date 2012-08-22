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
#include <glib-object.h>

#include <locale.h>

struct {
	const char *name;
	int (* function) (int argc, char *argv[]);
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

	va_start (va, format);
	g_string_append_vprintf (message, format, va);
	va_end (va);

	if (error) {
		g_dbus_error_strip_remote_error (error);
		g_string_append (message, ": ");
		g_string_append (message, error->message);
		g_error_free (error);
	}

	g_printerr ("%s\n", message->str);
	g_string_free (message, TRUE);
}

gboolean
realm_supports_interface (RealmDbusRealm *realm,
                          const gchar *interface)
{
	const gchar *const *supported;
	gint i;

	supported = realm_dbus_realm_get_supported_interfaces (realm);
	g_return_val_if_fail (supported != NULL, FALSE);

	for (i = 0; supported[i] != NULL; i++) {
		if (g_str_equal (supported[i], interface))
			return TRUE;
	}

	return FALSE;
}

gchar **
realm_lookup_paths (void)
{
	RealmDbusProvider *provider;
	GError *error = NULL;
	gchar **realms;

	provider = realm_dbus_provider_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
	                                                       G_DBUS_PROXY_FLAGS_NONE,
	                                                       REALM_DBUS_BUS_NAME,
	                                                       REALM_DBUS_SERVICE_PATH,
	                                                       NULL, &error);
	if (error != NULL) {
		realm_handle_error (error, _("Couldn't connect to realm service"));
		return NULL;
	}

	/* Find the right realm, but only enrolled */
	realms = g_strdupv ((gchar **)realm_dbus_provider_get_realms (provider));
	g_object_unref (provider);

	return realms;
}

RealmDbusRealm *
realm_path_to_realm (const gchar *object_path)
{
	RealmDbusRealm *realm = NULL;
	GError *error = NULL;

	realm = realm_dbus_realm_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
	                                                 G_DBUS_PROXY_FLAGS_NONE,
	                                                 REALM_DBUS_BUS_NAME, object_path,
	                                                 NULL, &error);

	if (error != NULL)
		realm_handle_error (error, _("Couldn't use realm service"));

	return realm;
}

RealmDbusRealm *
realm_paths_to_configured_realm (const gchar * const* realm_paths,
                                 const gchar *realm_name)
{
	RealmDbusRealm *result = NULL;
	const gchar *configured;
	RealmDbusRealm *realm;
	const gchar *name;
	gint i;

	for (i = 0; realm_paths[i] != NULL; i++) {
		realm = realm_path_to_realm (realm_paths[i]);
		if (realm == NULL)
			continue;

		configured = realm_dbus_realm_get_configured (realm);
		if (g_strcmp0 (configured, "") != 0) {

			/* Searching for any enrolled realm */
			if (realm_name == NULL) {
				if (result == NULL) {
					result = realm;
					realm = NULL;
				} else {
					realm_handle_error (NULL, N_("More than one enrolled realm, please specify the realm name"));
					g_object_unref (realm);
					g_object_unref (result);
					return NULL;
				}

			/* Searching for a specific enrolled realm */
			} else {
				name = realm_dbus_realm_get_name (realm);
				if (name != NULL && g_ascii_strcasecmp (name, realm_name) == 0) {
					return realm;
				}
			}
		}

		if (realm != NULL)
			g_object_unref (realm);
	}

	if (realm_name == NULL) {
		if (result == NULL)
			realm_handle_error (NULL, _("No enrolled realms found"));
		return result;
	}

	realm_handle_error (NULL, _("Enrolled realm not found: %s"), realm_name);
	return NULL;
}


RealmDbusRealm *
realm_name_to_configured (GDBusConnection *connection,
                          const gchar *realm_name)
{
	RealmDbusRealm *realm;
	RealmDbusProvider *provider;
	GError *error = NULL;
	const gchar * const* realms;

	if (realm_name != NULL && g_str_equal (realm_name, ""))
		realm_name = NULL;

	provider = realm_dbus_provider_proxy_new_sync (connection,
	                                               G_DBUS_PROXY_FLAGS_NONE,
	                                               REALM_DBUS_BUS_NAME,
	                                               REALM_DBUS_SERVICE_PATH,
	                                               NULL, &error);
	if (error != NULL) {
		realm_handle_error (error, _("Couldn't connect to realm service"));
		return NULL;
	}

	/* Find the right realm, but only enrolled */
	realms = realm_dbus_provider_get_realms (provider);
	g_return_val_if_fail (realms != NULL, NULL);

	realm = realm_paths_to_configured_realm (realms, realm_name);
	g_object_unref (provider);

	return realm;
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

	options = g_variant_new_array (G_VARIANT_TYPE ("{sv}"), (GVariant * const*)opts->pdata, opts->len);
	g_ptr_array_free (opts, TRUE);

	return options;
}

GDBusConnection *
realm_get_connection (gboolean verbose)
{
	GDBusConnection *connection;
	GError *error = NULL;

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (error == NULL) {
		if (verbose) {
			g_dbus_connection_signal_subscribe (connection, REALM_DBUS_BUS_NAME,
			                                    REALM_DBUS_SERVICE_INTERFACE,
			                                    REALM_DBUS_DIAGNOSTICS_SIGNAL,
			                                    REALM_DBUS_SERVICE_PATH,
			                                    NULL, G_DBUS_SIGNAL_FLAGS_NONE,
			                                    on_diagnostics_signal, NULL, NULL);
		}

	} else {
		realm_handle_error (error, _("Couldn't connect to system bus"));
	}

	return connection;
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

int
main (int argc,
      char *argv[])
{
	const gchar *command = NULL;
	gint i;

	setlocale (LC_ALL, "");

#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif

	g_type_init ();

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

	for (i = 0; i < G_N_ELEMENTS (realm_commands); i++) {
		if (g_str_equal (realm_commands[i].name, command))
			return (realm_commands[i].function) (argc, argv);
	}

	return usage(2);
}
