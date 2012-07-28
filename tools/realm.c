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
#include <glib-object.h>

struct {
	const char *name;
	int (* function) (int argc, char *argv[]);
	const char *usage;
	const char *description;
} realm_commands[] = {
	{ "discover", realm_discover, "realm discover -v [realm-name]", "Discover available realm" },
	{ "join", realm_join, "realm join -v [-U user] realm-name", "Enroll this machine in a realm" },
	{ "leave", realm_leave, "realm leave -v [-U user] [realm-name]", "Unenroll this machine from a realm" },
	{ "list", realm_list, "realm list", "List known realms" },
	{ "permit", realm_permit, "realm permit [-a] [-R realm] user ...", "Permit user logins" },
	{ "deny", realm_deny, "realm deny [-a] [-R realm] user ...", "Deny user logins" },
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

RealmDbusKerberos *
realm_info_to_realm_proxy (GVariant *realm_info)
{
	RealmDbusKerberos *realm = NULL;
	const gchar *object_path;
	const gchar *interface_name;
	GError *error = NULL;

	g_variant_get (realm_info, "(&o&s)", &object_path, &interface_name);

	if (g_str_equal (interface_name, REALM_DBUS_KERBEROS_REALM_INTERFACE)) {
		realm = realm_dbus_kerberos_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
		                                                    G_DBUS_PROXY_FLAGS_NONE,
		                                                    REALM_DBUS_BUS_NAME, object_path,
		                                                    NULL, &error);
	}

	if (error != NULL)
		realm_handle_error (error, "couldn't use realm service");
	else if (realm == NULL)
		realm_handle_error (NULL, "unsupported realm type: %s", interface_name);

	return realm;
}

static RealmDbusKerberos *
find_enrolled_in_realms (GVariant *realms,
                         const gchar *realm_name)
{
	RealmDbusKerberos *result = NULL;
	RealmDbusKerberos *realm;
	GVariant *realm_info;
	GVariantIter iter;
	const gchar *name;

	g_variant_iter_init (&iter, realms);
	while ((realm_info = g_variant_iter_next_value (&iter)) != NULL) {
		realm = realm_info_to_realm_proxy (realm_info);
		g_variant_unref (realm_info);
		if (realm == NULL)
			continue;

		if (realm_dbus_kerberos_get_enrolled (realm)) {

			/* Searching for any enrolled realm */
			if (realm_name == NULL) {
				if (result == NULL) {
					result = realm;
					realm = NULL;
				} else {
					realm_handle_error (NULL, "more than one enrolled realm, please specify the realm name");
					g_object_unref (realm);
					g_object_unref (result);
					return NULL;
				}

			/* Searching for a specific enrolled realm */
			} else {
				name = realm_dbus_kerberos_get_name (realm);
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
			realm_handle_error (NULL, "no enrolled realms found");
		return result;
	}

	realm_handle_error (NULL, "enrolled realm not found: %s", realm_name);
	return NULL;
}


RealmDbusKerberos *
realm_name_to_enrolled (GDBusConnection *connection,
                        const gchar *realm_name)
{
	RealmDbusKerberos *realm;
	RealmDbusProvider *provider;
	GError *error = NULL;
	GVariant *realms;

	if (realm_name != NULL && g_str_equal (realm_name, ""))
		realm_name = NULL;

	provider = realm_dbus_provider_proxy_new_sync (connection,
	                                               G_DBUS_PROXY_FLAGS_NONE,
	                                               REALM_DBUS_BUS_NAME,
	                                               REALM_DBUS_SERVICE_PATH,
	                                               NULL, &error);
	if (error != NULL) {
		realm_handle_error (error, "couldn't connect to realm service");
		return NULL;
	}

	/* Find the right realm, but only enrolled */
	realms = realm_dbus_provider_get_realms (provider);
	g_return_val_if_fail (realms != NULL, NULL);

	realm = find_enrolled_in_realms (realms, realm_name);
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
		realm_handle_error (error, "couldn't connect to system bus");
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
