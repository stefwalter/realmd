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

#include "realm-samba-config.h"
#include "realm-command.h"
#include "realm-constants.h"
#include "realm-daemon.h"
#include "realm-diagnostics.h"
#include "realm-errors.h"

#include <gio/gio.h>
#include <glib/gstdio.h>

#include <errno.h>
#include <stdarg.h>

typedef struct {
	gchar *section;
	GHashTable *values;
	GDBusMethodInvocation *invocation;
} SetClosure;

static void
set_closure_free (gpointer data)
{
	SetClosure *set = data;
	g_free (set->section);
	g_hash_table_destroy (set->values);
	g_clear_object (&set->invocation);
	g_slice_free (SetClosure, set);
}

static void
run_net_conf_setparm (const gchar *section,
                      const gchar *name,
                      const gchar *value,
                      GDBusMethodInvocation *invocation,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
	/* Use our custom smb.conf */
	const gchar *args[] = {
		REALM_NET_PATH, "-s", SERVICE_DIR "/ad-provider-smb.conf",
		"conf", "setparm", section, name, value,
		NULL,
	};

	realm_command_runv_async ((gchar **)args, NULL, invocation, NULL, callback, user_data);
}

static void
begin_one_value (GSimpleAsyncResult *res);

static void
on_one_value (GObject *source,
              GAsyncResult *result,
              gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Configuring samba kerberos settings failed");
	if (error == NULL) {
		begin_one_value (res);

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
begin_one_value (GSimpleAsyncResult *res)
{
	SetClosure *set = g_simple_async_result_get_op_res_gpointer (res);
	GHashTableIter iter;
	gchar *name;
	gchar *value;

	g_hash_table_iter_init (&iter, set->values);
	if (!g_hash_table_iter_next (&iter, (gpointer *)&name, (gpointer *)&value)) {
		g_simple_async_result_complete (res);
		return;
	}

	realm_diagnostics_info (set->invocation,
	                        "Setting smbconf: [%s] %s = %s\n",
	                        set->section, name, value);
	run_net_conf_setparm (set->section, name, value, set->invocation,
	                      on_one_value, g_object_ref (res));
	g_hash_table_remove (set->values, name);
}

void
realm_samba_config_set_async (const gchar *section,
                              GDBusMethodInvocation *invocation,
                              GAsyncReadyCallback callback,
                              gpointer user_data,
                              ...)
{
	GSimpleAsyncResult *res;
	SetClosure *set;
	const gchar *name;
	const gchar *value;
	va_list va;

	g_return_if_fail (section != NULL);

	res = g_simple_async_result_new (NULL, callback, user_data,
	                                 realm_samba_config_set_async);
	set = g_slice_new (SetClosure);
	set->invocation = invocation ? g_object_ref (invocation) : NULL;
	set->section = g_strdup (section);
	set->values = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	g_simple_async_result_set_op_res_gpointer (res, set, set_closure_free);

	va_start (va, user_data);
	for (;;) {
		name = va_arg (va, const gchar *);
		if (name == NULL)
			break;
		value = va_arg (va, const gchar *);
		g_return_if_fail (value != NULL);

		g_hash_table_insert (set->values, g_strdup (name), g_strdup (value));
	}
	va_end (va);

	if (g_hash_table_size (set->values) == 0)
		g_simple_async_result_complete_in_idle (res);
	else
		begin_one_value (res);

	g_object_unref (res);
}

gboolean
realm_samba_config_set_finish (GAsyncResult *result,
                               GError **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      realm_samba_config_set_async), FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;
	return TRUE;
}
