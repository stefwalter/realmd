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

#include <packagekit-glib2/packagekit.h>

static GMainLoop *loop;

static void
on_progress_callback (PkProgress *progress,
                      PkProgressType type,
                      gpointer user_data)
{
	PkPackage *package;
	gboolean boolean;
	gchar *string;
	guint unumber;
	gint number;

	switch (type) {
	case PK_PROGRESS_TYPE_PACKAGE_ID:
		g_object_get (progress, "package-id", &string, NULL);
		g_printerr ("progress: package-id: %s\n", string);
		g_free (string);
		break;
	case PK_PROGRESS_TYPE_TRANSACTION_ID:
		g_object_get (progress, "transaction-id", &string, NULL);
		g_printerr ("progress: percentage: %s\n", string);
		g_free (string);
		break;
	case PK_PROGRESS_TYPE_PERCENTAGE:
		g_object_get (progress, "percentage", &number, NULL);
		g_printerr ("progress: percentage: %d\n", number);
		break;
	case PK_PROGRESS_TYPE_SUBPERCENTAGE:
		g_object_get (progress, "subpercentage", &number, NULL);
		g_printerr ("progress: subpercentage: %d\n", number);
		break;
	case PK_PROGRESS_TYPE_ALLOW_CANCEL:
		g_object_get (progress, "allow-cancel", &boolean, NULL);
		g_printerr ("progress: allow-cancel: %s\n", boolean ? "TRUE" : "FALSE");
		break;
	case PK_PROGRESS_TYPE_STATUS:
		g_object_get (progress, "status", &unumber, NULL);
		g_printerr ("progress: status: %u %s\n", unumber, pk_status_enum_to_string (unumber));
		break;
	case PK_PROGRESS_TYPE_ROLE:
		g_object_get (progress, "role", &unumber, NULL);
		g_printerr ("progress: role: %u %s\n", unumber, pk_role_enum_to_string (unumber));
		break;
	case PK_PROGRESS_TYPE_CALLER_ACTIVE:
		g_object_get (progress, "caller-active", &boolean, NULL);
		g_printerr ("progress: caller-active: %s\n", boolean ? "TRUE" : "FALSE");
		break;
	case PK_PROGRESS_TYPE_ELAPSED_TIME:
		g_object_get (progress, "elapsed-time", &unumber, NULL);
		g_printerr ("progress: elapsed-time: %u\n", unumber);
		break;
	case PK_PROGRESS_TYPE_REMAINING_TIME:
		g_object_get (progress, "remaining-time", &unumber, NULL);
		g_printerr ("progress: remaining-time: %u\n", unumber);
		break;
	case PK_PROGRESS_TYPE_SPEED:
		g_object_get (progress, "speed", &unumber, NULL);
		g_printerr ("progress: speed: %u\n", unumber);
		break;
	case PK_PROGRESS_TYPE_UID:
		g_object_get (progress, "uid", &unumber, NULL);
		g_printerr ("progress: uid: %u\n", unumber);
		break;
	case PK_PROGRESS_TYPE_PACKAGE:
		g_object_get (progress, "package", &package, NULL);
		g_printerr ("progress: package: %p\n", package);
		g_object_unref (package);
		break;
	case PK_PROGRESS_TYPE_ITEM_PROGRESS:
		g_object_get (progress, "item-progress-id", &string, "item-progress-value", &unumber, NULL);
		g_printerr ("progress: package: %s %u\n", string, unumber);
		g_object_unref (package);
		break;
	case PK_PROGRESS_TYPE_INVALID:
	default:
		g_warn_if_reached ();
		break;
	}
}

static void
on_ready_get_result (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GAsyncResult **place = (GAsyncResult **)user_data;
	*place = g_object_ref (result);
	g_main_loop_quit (loop);
}

static void
test_resolve (void)
{
	GAsyncResult *result = NULL;
	PkTask *task;
	GError *error = NULL;
	gchar *packages[] = { "sssd", "samba-client", "samba-common", "freeipa-client" };
	PkBitfield filter;
	PkResults *results;
	PkPackage *package;
	GPtrArray *array;
	GPtrArray *ids;
	const gchar *id;
	gint i;

	task = pk_task_new ();
	pk_task_set_interactive (task, FALSE);

	filter = pk_filter_bitfield_from_string ("arch");

	pk_task_refresh_cache_async (task, FALSE, NULL,
	                             on_progress_callback, NULL,
	                             on_ready_get_result, &result);
	g_main_loop_run (loop);
	results = pk_task_generic_finish (task, result, &error);
	g_object_unref (result);

	if (error != NULL) {
		g_printerr ("%s\n", error->message);
		exit (1);
	}

	g_object_unref (results);

	g_printerr ("REFRESHED\n");

	pk_task_resolve_async (task, filter, packages, NULL,
	                       on_progress_callback, NULL,
	                       on_ready_get_result, &result);
	g_main_loop_run (loop);
	results = pk_task_generic_finish (task, result, &error);
	g_object_unref (result);

	if (error != NULL) {
		g_printerr ("%s\n", error->message);;
		exit (1);
	}

	ids = g_ptr_array_new_with_free_func (g_free);
	array = pk_results_get_package_array (results);
	for (i = 0; i < array->len; i++) {
		package = PK_PACKAGE (array->pdata[i]);
		if (pk_package_get_info (package) != PK_INFO_ENUM_INSTALLED) {
			id = pk_package_get_id (package);
			g_print ("%s\n", id);
			g_ptr_array_add (ids, g_strdup (id));
		}
	}

	g_ptr_array_free (array, TRUE);
	g_object_unref (results);

	g_printerr ("RESOLVED\n");

	if (ids->len > 0) {
		g_ptr_array_add (ids, NULL);
		pk_task_install_packages_async (task, (gchar **)ids->pdata,
		                                NULL, on_progress_callback, NULL,
		                                on_ready_get_result, &result);
		g_ptr_array_free (ids, TRUE);
		g_main_loop_run (loop);
		results = pk_task_generic_finish (task, result, &error);
		g_object_unref (result);

		if (error != NULL) {
			g_printerr ("%s\n", error->message);;
			exit (1);
		}

		g_object_unref (results);
	}

	g_object_unref (task);

}

int
main(int argc,
     char *argv[])
{
	g_type_init ();

	loop = g_main_loop_new (NULL, FALSE);
	test_resolve ();
	g_main_loop_unref (loop);

	return 0;
}
