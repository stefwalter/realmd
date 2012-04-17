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

#include "realm-diagnostics.h"
#include "realm-packages.h"

#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>

typedef struct {
	PkTask *task;
	gchar **packages;
	GDBusMethodInvocation *invocation;
} InstallClosure;

static void
install_closure_free (gpointer data)
{
	InstallClosure *install = data;
	g_object_ref (install->task);
	g_strfreev (install->packages);
	g_clear_object (&install->invocation);
	g_slice_free (InstallClosure, install);
}

static void
on_install_progress (PkProgress *progress,
                     PkProgressType type,
                     gpointer user_data)
{
	InstallClosure *install = user_data;
	gchar *string;
	guint unumber;
	gint number;

	if (type == PK_PROGRESS_TYPE_STATUS) {
#ifdef TODO
		PkStatusEnum status;
		g_object_get (progress, "status", &status, NULL);
		switch (status) {
		case PK_STATUS_WAIT:
			realm_status (install->invocation, _("Waiting for package system"));
			break;
		case PK_STATUS_ENUM_WAITING_FOR_AUTH:
			pk_status_enum_to_localised_text ();
		};
#endif
	}

	switch (type) {
	case PK_PROGRESS_TYPE_PACKAGE_ID:
		g_object_get (progress, "package-id", &string, NULL);
		realm_diagnostics_info (install->invocation, "package-id: %s\n", string);
		g_free (string);
		break;
	case PK_PROGRESS_TYPE_TRANSACTION_ID:
		g_object_get (progress, "transaction-id", &string, NULL);
		realm_diagnostics_info (install->invocation, "transaction-id: %s\n", string);
		g_free (string);
		break;
	case PK_PROGRESS_TYPE_PERCENTAGE:
		g_object_get (progress, "percentage", &number, NULL);
		realm_diagnostics_info (install->invocation, "percentage: %d\n", number);
		break;
	case PK_PROGRESS_TYPE_SUBPERCENTAGE:
		g_object_get (progress, "subpercentage", &number, NULL);
		realm_diagnostics_info (install->invocation, "subpercentage: %d\n", number);
		break;
	case PK_PROGRESS_TYPE_STATUS:
		g_object_get (progress, "status", &unumber, NULL);
		realm_diagnostics_info (install->invocation, "status: %s\n",
		                     pk_status_enum_to_string (unumber));
		break;
	case PK_PROGRESS_TYPE_ELAPSED_TIME:
		g_object_get (progress, "elapsed-time", &unumber, NULL);
		realm_diagnostics_info (install->invocation, "elapsed-time: %u\n", unumber);
		break;
	case PK_PROGRESS_TYPE_REMAINING_TIME:
		g_object_get (progress, "remaining-time", &unumber, NULL);
		realm_diagnostics_info (install->invocation, "remaining-time: %u\n", unumber);
		break;
	case PK_PROGRESS_TYPE_SPEED:
		g_object_get (progress, "speed", &unumber, NULL);
		realm_diagnostics_info (install->invocation, "speed: %u\n", unumber);
		break;
	case PK_PROGRESS_TYPE_INVALID:
	case PK_PROGRESS_TYPE_ALLOW_CANCEL:
	case PK_PROGRESS_TYPE_CALLER_ACTIVE:
	case PK_PROGRESS_TYPE_ROLE:
	case PK_PROGRESS_TYPE_UID:
	case PK_PROGRESS_TYPE_PACKAGE:
	case PK_PROGRESS_TYPE_ITEM_PROGRESS:
	default:
		break;
	}
}

static gchar **
extract_uninstalled_package_ids (PkResults *results)
{
	GPtrArray *packages;
	PkPackage *package;
	GPtrArray *ids;
	guint i;

	packages = pk_results_get_package_array (results);
	ids = g_ptr_array_new_with_free_func (g_free);

	for (i = 0; i < packages->len; i++) {
		package = PK_PACKAGE (packages->pdata[i]);
		if (pk_package_get_info (package) != PK_INFO_ENUM_INSTALLED)
			g_ptr_array_add (ids, g_strdup (pk_package_get_id (package)));
	}

	g_ptr_array_free (packages, TRUE);
	return (gchar **)g_ptr_array_free (ids, FALSE);
}

static void
on_install_installed (GObject *source,
                      GAsyncResult *result,
                      gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	InstallClosure *install = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	PkResults *results;

	results = pk_task_generic_finish (install->task, result, &error);
	if (error == NULL)
		g_simple_async_result_take_error (res, error);
	else
		g_object_unref (results);

	g_simple_async_result_complete (res);
	g_object_unref (res);
}

static void
on_install_resolved (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	InstallClosure *install = g_simple_async_result_get_op_res_gpointer (res);
	gchar **package_ids;
	GError *error = NULL;
	PkResults *results;

	results = pk_task_generic_finish (install->task, result, &error);
	if (error == NULL) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);

	} else {
		package_ids = extract_uninstalled_package_ids (results);
		pk_task_install_packages_async (install->task, package_ids, NULL,
		                                on_install_progress, install,
		                                on_install_installed, g_object_ref (res));
		g_strfreev (package_ids);
		g_object_unref (results);
	}

	g_object_unref (res);
}

static void
on_install_refresh (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	InstallClosure *install = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	PkResults *results;
	PkBitfield filter;

	results = pk_task_generic_finish (install->task, result, &error);
	if (error == NULL) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);

	} else {
		filter = pk_filter_bitfield_from_string ("arch");
		pk_task_resolve_async (install->task, filter, install->packages, NULL,
		                       on_install_progress, install, on_install_resolved, g_object_ref (res));
		g_object_unref (results);
	}

	g_object_unref (res);
}

void
realm_packages_install_async (const gchar **required_files,
                              const gchar **package_names,
                              GDBusMethodInvocation *invocation,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	GSimpleAsyncResult *res;
	InstallClosure *install;

	g_return_if_fail (package_names != NULL);
	g_return_if_fail (invocation == NULL || G_IS_DBUS_METHOD_INVOCATION (invocation));

	res = g_simple_async_result_new (NULL, callback, user_data, realm_packages_install_async);
	install = g_slice_new (InstallClosure);
	install->packages = g_strdupv ((gchar **)package_names);
	install->task = pk_task_new ();
	pk_task_set_interactive (install->task, FALSE);
	install->invocation = invocation ? g_object_ref (invocation) : NULL;
	g_simple_async_result_set_op_res_gpointer (res, install, install_closure_free);

	if (required_files) {
		if (realm_packages_check_paths (required_files, invocation)) {
			g_simple_async_result_complete_in_idle (res);
			g_object_unref (res);
			return;
		}
	}

	realm_diagnostics_info (invocation, "Refreshing package cache");

	pk_task_refresh_cache_async (install->task, FALSE, NULL, on_install_progress, install,
	                             on_install_refresh, g_object_ref (res));

	g_object_unref (res);
}

gboolean
realm_packages_install_finish (GAsyncResult *result,
                               GError **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      realm_packages_install_async), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

gboolean
realm_packages_check_paths (const gchar **paths,
                            GDBusMethodInvocation *invocation)
{
	gint i;

	g_return_val_if_fail (paths != NULL, FALSE);
	g_return_val_if_fail (invocation == NULL || G_IS_DBUS_METHOD_INVOCATION (invocation), FALSE);

	for (i = 0; paths[i] != NULL; i++) {
		if (!g_file_test (paths[i], G_FILE_TEST_EXISTS)) {
			realm_diagnostics_info (invocation, "Couldn't find file: %s", paths[i]);
			return FALSE;
		}
	}

	return TRUE;
}

