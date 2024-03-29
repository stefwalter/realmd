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

#include "realm-command.h"
#include "realm-daemon.h"
#include "realm-diagnostics.h"
#include "realm-errors.h"
#include "realm-samba-config.h"
#include "realm-samba-winbind.h"
#include "realm-settings.h"
#include "realm-service.h"

#include <glib/gstdio.h>

#include <errno.h>

static void
on_nss_complete (GObject *source,
                 GAsyncResult *result,
                 gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Enabling winbind in nsswitch.conf and pam failed");
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	g_simple_async_result_complete (res);

	g_object_unref (res);
}

static void
on_enable_do_nss (GObject *source,
                  GAsyncResult *result,
                  gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GDBusMethodInvocation *invocation = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;

	realm_service_enable_and_restart_finish (result, &error);
	if (error == NULL) {
		realm_command_run_known_async ("winbind-enable-logins", NULL, invocation,
		                               NULL, on_nss_complete, g_object_ref (res));

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

void
realm_samba_winbind_configure_async (RealmIniConfig *config,
                                     GDBusMethodInvocation *invocation,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
	GSimpleAsyncResult *res;
	GError *error = NULL;

	g_return_if_fail (config != NULL);
	g_return_if_fail (invocation != NULL || G_IS_DBUS_METHOD_INVOCATION (invocation));

	res = g_simple_async_result_new (NULL, callback, user_data,
	                                 realm_samba_winbind_configure_async);
	if (invocation != NULL)
		g_simple_async_result_set_op_res_gpointer (res, g_object_ref (invocation),
		                                           g_object_unref);

	/* TODO: need to use autorid mapping */

	realm_ini_config_change (config, REALM_SAMBA_CONFIG_GLOBAL, &error,
	                         "idmap uid", "10000-20000",
	                         "idmap gid", "10000-20000",
	                         "winbind enum users", "no",
	                         "winbind enum groups", "no",
	                         "template shell", realm_settings_string ("user", "shell"),
	                         "winbind offline logon", "yes",
	                         "winbind refresh tickets", "yes",
	                         NULL);

	if (error == NULL) {
		realm_service_enable_and_restart ("winbind", invocation,
		                                  on_enable_do_nss, g_object_ref (res));
	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete_in_idle (res);
	}

	g_object_unref (res);
}

gboolean
realm_samba_winbind_configure_finish (GAsyncResult *result,
                                      GError **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      realm_samba_winbind_configure_async), FALSE);
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;
	return TRUE;
}

static void
on_disable_complete (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;

	realm_service_disable_and_stop_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	g_simple_async_result_complete (res);

	g_object_unref (res);
}

static void
on_nss_do_disable (GObject *source,
                   GAsyncResult *result,
                   gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GDBusMethodInvocation *invocation = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Disabling winbind in /etc/nsswitch.conf failed");
	if (error == NULL) {
		realm_service_disable_and_stop ("winbind", invocation,
		                                on_disable_complete, g_object_ref (res));
	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

void
realm_samba_winbind_deconfigure_async (RealmIniConfig *config,
                                       GDBusMethodInvocation *invocation,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
	GSimpleAsyncResult *res;

	g_return_if_fail (config != NULL);
	g_return_if_fail (invocation != NULL || G_IS_DBUS_METHOD_INVOCATION (invocation));

	res = g_simple_async_result_new (NULL, callback, user_data,
	                                 realm_samba_winbind_deconfigure_async);
	if (invocation != NULL)
		g_simple_async_result_set_op_res_gpointer (res, g_object_ref (invocation),
		                                           g_object_unref);

	realm_command_run_known_async ("winbind-disable-logins", NULL, invocation,
	                               NULL, on_nss_do_disable, g_object_ref (res));

	g_object_unref (res);
}

gboolean
realm_samba_winbind_deconfigure_finish (GAsyncResult *result,
                                        GError **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      realm_samba_winbind_deconfigure_async), FALSE);
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;
	return TRUE;

}
