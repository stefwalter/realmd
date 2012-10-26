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
#include "realm-service.h"
#include "realm-settings.h"

#include <glib/gi18n.h>

static void
begin_service_command (const gchar *command,
                       GDBusMethodInvocation *invocation,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	GSimpleAsyncResult *async;

	/* If install mode, never do any service stuff */
	if (realm_daemon_is_install_mode ()) {
		g_debug ("skipping %s command in install mode", command);
		async = g_simple_async_result_new (NULL, callback, user_data,
		                                   begin_service_command);
		g_simple_async_result_complete_in_idle (async);
		g_object_unref (async);
		return;
	}

	realm_command_run_known_async (command, NULL, invocation, NULL, callback, user_data);
}

static gboolean
finish_service_command (GAsyncResult *result,
                        GError **error)
{
	if (g_simple_async_result_is_valid (result, NULL, begin_service_command)) {
		if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
			return FALSE;
		return TRUE;
	}

	return realm_command_run_finish (result, NULL, error) != -1;
}

void
realm_service_enable (const gchar *service_name,
                      GDBusMethodInvocation *invocation,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
	gchar *command;

	command = g_strdup_printf ("%s-enable-service", service_name);
	begin_service_command (command, invocation, callback, user_data);
	g_free (command);
}

gboolean
realm_service_enable_finish (GAsyncResult *result,
                             GError **error)
{
	return finish_service_command (result, error);
}

void
realm_service_disable (const gchar *service_name,
                       GDBusMethodInvocation *invocation,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	gchar *command;

	command = g_strdup_printf ("%s-disable-service", service_name);
	begin_service_command (command, invocation, callback, user_data);
	g_free (command);
}

gboolean
realm_service_disable_finish (GAsyncResult *result,
                              GError **error)
{
	return finish_service_command (result, error);
}

void
realm_service_restart (const gchar *service_name,
                       GDBusMethodInvocation *invocation,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	gchar *command;

	command = g_strdup_printf ("%s-restart-service", service_name);
	begin_service_command (command, invocation, callback, user_data);
	g_free (command);
}

gboolean
realm_service_restart_finish (GAsyncResult *result,
                              GError **error)
{
	return finish_service_command (result, error);
}

void
realm_service_stop (const gchar *service_name,
                    GDBusMethodInvocation *invocation,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
	gchar *command;

	command = g_strdup_printf ("%s-stop-service", service_name);
	begin_service_command (command, invocation, callback, user_data);
	g_free (command);
}

gboolean
realm_service_stop_finish (GAsyncResult *result,
                           GError **error)
{
	return finish_service_command (result, error);
}

typedef struct {
	gchar *service_name;
	GDBusMethodInvocation *invocation;
} CallClosure;

static void
call_closure_free (gpointer data)
{
	CallClosure *call = data;
	g_free (call->service_name);
	g_clear_object (&call->invocation);
	g_slice_free (CallClosure, call);
}

static void
on_enable_restarted (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;

	realm_service_restart_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (async, error);
	g_simple_async_result_complete (async);

	g_object_unref (async);
}


static void
on_enable_enabled (GObject *source,
                   GAsyncResult *result,
                   gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	CallClosure *call = g_simple_async_result_get_op_res_gpointer (async);
	GError *error = NULL;

	realm_service_enable_finish (result, &error);
	if (error == NULL) {
		realm_service_restart (call->service_name, call->invocation,
		                       on_enable_restarted, g_object_ref (async));
	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (async);
}

void
realm_service_enable_and_restart (const gchar *service_name,
                                  GDBusMethodInvocation *invocation,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	GSimpleAsyncResult *async;
	CallClosure *call;

	async = g_simple_async_result_new (NULL, callback, user_data,
	                                   realm_service_enable_and_restart);
	call = g_slice_new0 (CallClosure);
	call->service_name = g_strdup (service_name);
	call->invocation = invocation ? g_object_ref (invocation) : invocation;
	g_simple_async_result_set_op_res_gpointer (async, call, call_closure_free);

	realm_service_enable (call->service_name, call->invocation,
	                      on_enable_enabled, g_object_ref (async));

	g_object_unref (async);
}

gboolean
realm_service_enable_and_restart_finish (GAsyncResult *result,
                                         GError **error)
{
	GSimpleAsyncResult *async;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      realm_service_enable_and_restart), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	async = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (async, error))
		return FALSE;

	return TRUE;
}

static void
on_disable_stopped (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;

	realm_service_stop_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (async, error);
	g_simple_async_result_complete (async);

	g_object_unref (async);
}


static void
on_disable_disabled (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	CallClosure *call = g_simple_async_result_get_op_res_gpointer (async);
	GError *error = NULL;

	realm_service_disable_finish (result, &error);
	if (error == NULL) {
		realm_service_stop (call->service_name, call->invocation,
		                    on_disable_stopped, g_object_ref (async));
	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (async);
}

void
realm_service_disable_and_stop (const gchar *service_name,
                                GDBusMethodInvocation *invocation,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	GSimpleAsyncResult *async;
	CallClosure *call;

	async = g_simple_async_result_new (NULL, callback, user_data,
	                                   realm_service_disable_and_stop);
	call = g_slice_new0 (CallClosure);
	call->service_name = g_strdup (service_name);
	call->invocation = invocation ? g_object_ref (invocation) : invocation;
	g_simple_async_result_set_op_res_gpointer (async, call, call_closure_free);

	realm_service_disable (call->service_name, call->invocation,
	                       on_disable_disabled, g_object_ref (async));

	g_object_unref (async);
}

gboolean
realm_service_disable_and_stop_finish (GAsyncResult *result,
                                       GError **error)
{
	GSimpleAsyncResult *async;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      realm_service_disable_and_stop), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	async = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (async, error))
		return FALSE;

	return TRUE;
}
