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

#include "realm-ad-sssd.h"
#include "realm-command.h"
#include "realm-daemon.h"
#include "realm-diagnostics.h"
#include "realm-errors.h"

#include <glib/gstdio.h>

#include <errno.h>

static void
on_restart_sssd_done (GObject *source,
                      GAsyncResult *result,
                      gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL, "Couldn't restart sssd daemon");
	if (error != NULL)
		g_simple_async_result_take_error (res, error);

	g_simple_async_result_complete (res);
	g_object_unref (res);
}

static void
on_script_restart_sssd (GObject *object,
                        GAsyncResult *result,
                        gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GDBusMethodInvocation *invocation = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL, "Couldn't enable sssd daemon");
	if (error == NULL) {
		realm_command_run_async (NULL, invocation, NULL, on_restart_sssd_done, g_object_ref (res),
		                      "sssd-restart", NULL); /* actual command is expanded */
	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
on_enable_sssd_restart (GObject *object,
                        GAsyncResult *result,
                        gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GDBusMethodInvocation *invocation = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL, "Couldn't enable sssd daemon");
	if (error == NULL)
		realm_command_run_async (NULL, invocation, NULL, on_enable_sssd_restart, g_object_ref (res),
		                      "sssd-enable", NULL); /* actual command is expanded */

	g_object_unref (res);
}

static void
on_script_enable_sssd (GObject *object,
                       GAsyncResult *result,
                       gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GDBusMethodInvocation *invocation = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (error == NULL && status != 0) {
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Configure script for sssd.conf returned a failure code");
	}

	if (error == NULL) {
		realm_command_run_async (NULL, invocation, NULL, on_enable_sssd_restart, g_object_ref (res),
		                      "enable-sssd", NULL); /* actual command is expanded */
	}

	g_object_unref (res);
}

void
realm_ad_sssd_configure_async (RealmAdSssdAction action,
                               const gchar *realm,
                               GDBusMethodInvocation *invocation,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	GSimpleAsyncResult *res;
	GAsyncReadyCallback next;
	const gchar *arg;
	const gchar *sssd_conf;

	switch (action) {
	case REALM_AD_SSSD_ADD_REALM:
		arg = "add";
		next = on_script_enable_sssd;
		break;
	case REALM_AD_SSSD_REMOVE_REALM:
		arg = "remove";
		next = on_script_restart_sssd;
		break;
	default:
		g_return_if_reached ();
		break;
	}

	res = g_simple_async_result_new (NULL, callback, user_data,
	                                 realm_ad_sssd_configure_async);

#ifdef TODO
	sssd_conf = realm_daemon_resolve_file ("sssd.conf");
#else
	sssd_conf = "/etc/sssd/sssd.conf";
#endif

	realm_command_run_async (NULL, invocation, NULL, next, g_object_ref (res),
	                      "python", SERVICE_DIR "/ad-provider-sssd", "-c", sssd_conf, arg, realm, NULL);

	g_object_unref (res);
}

gboolean
realm_ad_sssd_configure_finish (GAsyncResult *result,
                                GError **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      realm_ad_sssd_configure_async), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}
