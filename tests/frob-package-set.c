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

#include <stdlib.h>

#include "service/realm-packages.h"
#include "service/realm-settings.h"

static void
on_packages (GObject *source,
             GAsyncResult *result,
             gpointer user_data)
{
	GMainLoop *loop = user_data;
	GError *error = NULL;

	realm_packages_install_finish (result, &error);
	g_assert_no_error (error);

	g_main_loop_quit (loop);
}

int
main(int argc,
     char *argv[])
{
	GMainLoop *loop = NULL;
	const gchar *package_sets[] = { "winbind" , NULL};

	g_type_init ();

	loop = g_main_loop_new (NULL, FALSE);

	realm_settings_init ();
	realm_packages_install_async (package_sets, NULL, on_packages, loop);

	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	return 0;
}
