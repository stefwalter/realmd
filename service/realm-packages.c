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
#include "realm-daemon.h"
#include "realm-errors.h"
#include "realm-invocation.h"
#include "realm-options.h"
#include "realm-packages.h"
#include "realm-settings.h"

#include <glib/gi18n.h>

#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>

typedef struct {
	PkTask *task;
	GHashTable *check;
	GDBusMethodInvocation *invocation;
	gboolean automatic;
} InstallClosure;

static void
install_closure_free (gpointer data)
{
	InstallClosure *install = data;
	g_object_ref (install->task);
	g_clear_object (&install->invocation);
	if (install->check)
		g_hash_table_destroy (install->check);
	g_free (install);
}

static void
on_install_progress (PkProgress *progress,
                     PkProgressType type,
                     gpointer user_data)
{
	gchar *string;
	guint unumber;
	gint number;

	if (type == PK_PROGRESS_TYPE_STATUS) {
#ifdef TODO
		PkStatusEnum status;
		g_object_get (progress, "status", &status, NULL);
		switch (status) {
		case PK_STATUS_WAIT:
			realm_status (install->invocation, "Waiting for package system");
			break;
		case PK_STATUS_ENUM_WAITING_FOR_AUTH:
			pk_status_enum_to_localised_text ();
		};
#endif
	}

	switch (type) {
	case PK_PROGRESS_TYPE_PACKAGE_ID:
		g_object_get (progress, "package-id", &string, NULL);
		g_debug ("package-id: %s", string);
		g_free (string);
		break;
	case PK_PROGRESS_TYPE_TRANSACTION_ID:
		g_object_get (progress, "transaction-id", &string, NULL);
		g_debug ("transaction-id: %s", string);
		g_free (string);
		break;
	case PK_PROGRESS_TYPE_PERCENTAGE:
		g_object_get (progress, "percentage", &number, NULL);
		g_debug ("percentage: %d", number);
		break;
	case PK_PROGRESS_TYPE_STATUS:
		g_object_get (progress, "status", &unumber, NULL);
		g_debug ("status: %s", pk_status_enum_to_string (unumber));
		break;
	case PK_PROGRESS_TYPE_ELAPSED_TIME:
		g_object_get (progress, "elapsed-time", &unumber, NULL);
		g_debug ("elapsed-time: %u", unumber);
		break;
	case PK_PROGRESS_TYPE_REMAINING_TIME:
		g_object_get (progress, "remaining-time", &unumber, NULL);
		g_debug ("remaining-time: %u", unumber);
		break;
	case PK_PROGRESS_TYPE_SPEED:
		g_object_get (progress, "speed", &unumber, NULL);
		g_debug ("speed: %u", unumber);
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

static gchar *
package_names_to_list (GHashTable *packages)
{
	GString *string;
	GHashTableIter iter;
	const gchar *name;

	string = g_string_new ("");
	g_hash_table_iter_init (&iter, packages);
	while (g_hash_table_iter_next (&iter, (void **)&name, NULL)) {
		if (string->len)
			g_string_append (string, ", ");
		g_string_append (string, name);
	}

	return g_string_free (string, FALSE);
}

static gchar **
extract_results (InstallClosure *install,
                 PkResults *results,
                 GHashTable *names,
                 GError **error)
{
	GPtrArray *packages;
	PkPackage *package;
	GPtrArray *ids;
	const gchar *name;
	gchar *missing;
	guint i;

#if !PK_CHECK_VERSION(0, 8, 13)
	GPtrArray *messages;

	messages = pk_results_get_message_array (results);
	for (i = 0; i < messages->len; i++) {
		realm_diagnostics_info (install->invocation, "%s",
		                        pk_message_get_details (messages->pdata[i]));
	}
	g_ptr_array_free (messages, TRUE);
#endif

	packages = pk_results_get_package_array (results);
	ids = g_ptr_array_new_with_free_func (g_free);

	for (i = 0; i < packages->len; i++) {
		package = PK_PACKAGE (packages->pdata[i]);
		name = pk_package_get_name (package);
		g_hash_table_remove (install->check, name);
		if (pk_package_get_info (package) != PK_INFO_ENUM_INSTALLED) {
			g_ptr_array_add (ids, g_strdup (pk_package_get_id (package)));
			g_hash_table_add (names, g_strdup (name));
		}
	}

	g_ptr_array_free (packages, TRUE);

	if (g_hash_table_size (install->check) == 0) {
		g_ptr_array_add (ids, NULL);
		return (gchar **)g_ptr_array_free (ids, FALSE);

	/* If not all packages were found, then this is an error */
	} else {
		missing = package_names_to_list (install->check);
		g_set_error (error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             _("The following packages are not available for installation: %s"), missing);
		g_free (missing);
		g_ptr_array_free (ids, TRUE);
		return NULL;
	}
}

static void
on_install_installed (GObject *source,
                      GAsyncResult *result,
                      gpointer user_data)
{
	GTask *task = G_TASK (user_data);
	InstallClosure *install = g_task_get_task_data (task);
	GError *error = NULL;
	PkResults *results;

	results = pk_task_generic_finish (install->task, result, &error);
	if (error == NULL) {
		g_object_unref (results);
		g_task_return_boolean (task, TRUE);
	} else {
		g_task_return_error (task, error);
	}

	g_object_unref (task);
}

static void
on_install_resolved (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GTask *task = G_TASK (user_data);
	InstallClosure *install = g_task_get_task_data (task);
	gchar **package_ids = NULL;
	GHashTable *names;
	GCancellable *cancellable;
	GError *error = NULL;
	PkResults *results;
	gchar *remote;
	gchar *missing;

	names = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	results = pk_task_generic_finish (install->task, result, &error);
	if (error == NULL) {
		package_ids = extract_results (install, results, names, &error);
		g_object_unref (results);
	}

	if (error == NULL) {
		missing = package_names_to_list (names);
		if (package_ids == NULL || *package_ids == NULL) {
			g_task_return_boolean (task, TRUE);

		} else if (!install->automatic) {
			g_set_error (&error, REALM_ERROR, REALM_ERROR_FAILED,
			             _("Necessary packages are not installed: %s"), missing);

		} else {
			/* String should match that in realm-client.c */
			realm_diagnostics_info (install->invocation, "%s: %s",
			                        _("Installing necessary packages"), missing);
			cancellable = realm_invocation_get_cancellable (install->invocation);
			pk_task_install_packages_async (install->task, package_ids, cancellable,
			                                on_install_progress, install,
			                                on_install_installed, g_object_ref (task));
		}

		g_free (missing);
	}

	if (error != NULL) {
		/*
		 * This is after our first interaction with package-kit. If it's
		 * not installed then we'll get a standard DBus error that it
		 * couldn't activate the service.
		 *
		 * So translate that into something useful for the caller. If
		 * PackageKit is not installed, then it is assumed that the
		 * distro or administrator wants to take full control over the
		 * installation of packages.
		 */
		if (error->domain == PK_CONTROL_ERROR) {
			remote = g_dbus_error_get_remote_error (error);
			if (remote && g_str_equal (remote, "org.freedesktop.DBus.Error.ServiceUnknown")) {
				g_dbus_error_strip_remote_error (error);
				realm_diagnostics_error (install->invocation, error, "PackageKit not available");
				g_clear_error (&error);
				missing = package_names_to_list (install->check);
				g_set_error (&error, REALM_ERROR, REALM_ERROR_FAILED,
				             _("Necessary packages are not installed: %s"), missing);
				g_free (missing);
			}
			g_free (remote);
		}

		g_task_return_error (task, error);
	}

	g_hash_table_unref (names);
	g_strfreev (package_ids);
	g_object_unref (task);
}

static void
lookup_required_files_and_packages (const gchar **package_sets,
                                    gchar ***result_packages,
                                    gchar ***result_files,
                                    gboolean *result_unconditional)
{
	GHashTable *settings;
	GHashTableIter iter;
	GPtrArray *packages;
	GPtrArray *files;
	gboolean unconditional;
	gchar *section;
	gchar *package;
	gchar *file;
	gint i;

	unconditional = FALSE;
	packages = g_ptr_array_new_with_free_func (g_free);
	files = g_ptr_array_new_with_free_func (g_free);

	for (i = 0; package_sets[i] != NULL; i++) {
		section = g_strdup_printf ("%s-packages", package_sets[i]);
		settings = realm_settings_section (section);
		if (settings == NULL) {
			g_critical ("No section found in settings: %s", section);
			return;
		}
		g_free (section);

		g_hash_table_iter_init (&iter, settings);
		while (g_hash_table_iter_next (&iter, (gpointer *)&package, (gpointer *)&file)) {
			file = g_strstrip (g_strdup (file));
			if (g_str_equal (file, "")) {
				g_free (file);
				unconditional = TRUE;
			} else {
				g_ptr_array_add (files, file);
			}
			package = g_strstrip (g_strdup (package));
			if (g_str_equal (package, ""))
				g_free (package);
			else
				g_ptr_array_add (packages, package);
		}
	}

	if (result_packages) {
		g_ptr_array_add (packages, NULL);
		*result_packages = (gchar **)g_ptr_array_free (packages, FALSE);
	} else {
		g_ptr_array_free (files, TRUE);
	}

	if (result_files) {
		g_ptr_array_add (files, NULL);
		*result_files = (gchar **)g_ptr_array_free (files, FALSE);
	} else {
		g_ptr_array_free (files, TRUE);
	}

	if (result_unconditional)
		*result_unconditional = unconditional;
}

gchar **
realm_packages_expand_sets (const gchar **package_sets)
{
	gchar **packages = NULL;

	g_return_val_if_fail (package_sets != NULL, NULL);

	lookup_required_files_and_packages (package_sets, &packages, NULL, NULL);
	return packages;
}

void
realm_packages_install_async (const gchar **package_sets,
                              GDBusMethodInvocation *invocation,
                              GVariant *options,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	GTask *task;
	InstallClosure *install;
	gboolean unconditional;
	gchar **required_files;
	gchar **packages;
	gchar *string;
	gboolean have;
	gint i;

	g_return_if_fail (package_sets != NULL);
	g_return_if_fail (invocation == NULL || G_IS_DBUS_METHOD_INVOCATION (invocation));

	lookup_required_files_and_packages (package_sets, &packages, &required_files, &unconditional);

	task = g_task_new (NULL, NULL, callback, user_data);
	install = g_new0 (InstallClosure, 1);
	install->task = pk_task_new ();
	install->automatic = realm_options_automatic_install (options);
	pk_task_set_interactive (install->task, FALSE);
	pk_client_set_background (PK_CLIENT (install->task), FALSE);
	install->invocation = invocation ? g_object_ref (invocation) : NULL;
	g_task_set_task_data (task, install, install_closure_free);

	if (realm_daemon_is_install_mode ()) {
		have = TRUE;
		realm_diagnostics_info (invocation, "Assuming packages are installed");

	} else if (unconditional) {
		have = FALSE;
		realm_diagnostics_info (invocation, "Unconditionally checking packages");

	} else {
		have = realm_packages_check_paths ((const gchar **)required_files, invocation);
		if (required_files[0] != NULL) {
			string = g_strjoinv (", ", required_files);
			realm_diagnostics_info (invocation, "Required files: %s", string);
			g_free (string);
		}
	}

	g_strfreev (required_files);

	if (have) {
		g_task_return_boolean (task, TRUE);

	} else {
		realm_diagnostics_info (invocation, "Resolving required packages");

		/*
		 * In an unexpected move, pk_task_resolve_async() does not fail or provide
		 * any feedback when a requested package does not exist.
		 *
		 * So we make a note of the ones we requested here, to compare against
		 * what we get back.
		 */
		install->check = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		for (i = 0; packages[i] != NULL; i++)
			g_hash_table_add (install->check, g_strdup (packages[i]));

		pk_task_resolve_async (install->task,
		                       pk_filter_bitfield_from_string ("arch"),
		                       packages, NULL,
		                       on_install_progress, install,
		                       on_install_resolved, g_object_ref (task));
	}

	g_strfreev (packages);
	g_object_unref (task);
}

gboolean
realm_packages_install_finish (GAsyncResult *result,
                               GError **error)
{
	if (g_task_propagate_boolean (G_TASK (result), error))
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
