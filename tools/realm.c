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
	{ "join", realm_join, "realm join -v [-U user] realm-name", "Enroll this machine in a realm" },
	{ "leave", realm_leave, "realm leave -v [-U user] [realm-name]", "Unenroll this machine from a realm" },
	{ "discover", realm_discover, "realm discover -v [realm-name]", "Discover available realm" },
	{ "list", realm_list, "realm list", "List known realms" },
};

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
	const gchar *bus_name;
	const gchar *object_path;
	const gchar *interface_name;
	GError *error = NULL;

	g_variant_get (realm_info, "(&s&o&s)", &bus_name, &object_path, &interface_name);

	if (g_str_equal (interface_name, REALM_DBUS_KERBEROS_REALM_INTERFACE)) {
		realm = realm_dbus_kerberos_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
		                                                    G_DBUS_PROXY_FLAGS_NONE,
		                                                    bus_name, object_path,
		                                                    NULL, &error);
	}

	if (error != NULL)
		realm_handle_error (error, "couldn't use realm service");
	else if (realm == NULL)
		realm_handle_error (NULL, "unsupported realm type: %s", interface_name);

	return realm;
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
