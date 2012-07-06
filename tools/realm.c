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
		if (argv[i][0] != '-') {
			command = argv[i];
			argc--;
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
