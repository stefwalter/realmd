/* identity-config - Identity configuration service
 *
 * Copyright 2012 Red Hat Inc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 */

#include "config.h"

#include "ic-ad-provider.h"
#include "ic-debug.h"
#include "ic-diagnostics.h"

#include <glib.h>

static GOptionEntry option_entries[] = {
	{ NULL }
};

int
main (int argc,
      char *argv[])
{
	GOptionContext *context;
	GError *error = NULL;
	GMainLoop *loop;
	g_type_init ();

	context = g_option_context_new ("identity-config");
	g_option_context_add_main_entries (context, option_entries, NULL);
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("%s", error->message);
		g_option_context_free (context);
		g_error_free (error);
		return 2;
	}

	ic_debug_init ();

	loop = g_main_loop_new (NULL, FALSE);
	// ic_ad_provider_start ();

	g_main_loop_run (loop);

	// ic_ad_provider_stop ();
	g_main_loop_unref (loop);
	g_option_context_free (context);

	return 0;
}
