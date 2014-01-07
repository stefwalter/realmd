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
                       gboolean skip_in_install_mode,
                       GDBusMethodInvocation *invocation,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	GTask *task;

	/* If install mode, don't do certain service stuff */
	if (skip_in_install_mode && realm_daemon_is_install_mode ()) {
		g_debug ("skipping %s command in install mode", command);
		task = g_task_new (NULL, NULL, callback, user_data);
		g_task_set_source_tag (task, begin_service_command);
		g_task_return_boolean (task, TRUE);
		g_object_unref (task);
	} else {
		realm_command_run_known_async (command, NULL, invocation, callback, user_data);
	}
}

static gboolean
finish_service_command (GAsyncResult *result,
                        GError **error)
{
	if (g_task_is_valid (result, NULL) &&
	    g_task_get_source_tag (G_TASK (result)) == begin_service_command) {
		return g_task_propagate_boolean (G_TASK (result), error);
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
	begin_service_command (command, FALSE, invocation, callback, user_data);
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
	begin_service_command (command, FALSE, invocation, callback, user_data);
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
	begin_service_command (command, TRUE, invocation, callback, user_data);
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
	begin_service_command (command, TRUE, invocation, callback, user_data);
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
	g_free (call);
}

static void
on_enable_restarted (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GTask *task = G_TASK (user_data);
	GError *error = NULL;

	realm_service_restart_finish (result, &error);
	if (error != NULL)
		g_task_return_error (task, error);
	else
		g_task_return_boolean (task, TRUE);
	g_object_unref (task);
}


static void
on_enable_enabled (GObject *source,
                   GAsyncResult *result,
                   gpointer user_data)
{
	GTask *task = G_TASK (user_data);
	CallClosure *call = g_task_get_task_data (task);
	GError *error = NULL;

	realm_service_enable_finish (result, &error);
	if (error == NULL) {
		realm_service_restart (call->service_name, call->invocation,
		                       on_enable_restarted, g_object_ref (task));
	} else {
		g_task_return_error (task, error);
	}

	g_object_unref (task);
}

void
realm_service_enable_and_restart (const gchar *service_name,
                                  GDBusMethodInvocation *invocation,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	GTask *task;
	CallClosure *call;

	task = g_task_new (NULL, NULL, callback, user_data);
	call = g_new0 (CallClosure, 1);
	call->service_name = g_strdup (service_name);
	call->invocation = invocation ? g_object_ref (invocation) : invocation;
	g_task_set_task_data (task, call, call_closure_free);

	realm_service_enable (call->service_name, call->invocation,
	                      on_enable_enabled, g_object_ref (task));

	g_object_unref (task);
}

gboolean
realm_service_enable_and_restart_finish (GAsyncResult *result,
                                         GError **error)
{
	g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
on_disable_stopped (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GTask *task = G_TASK (user_data);
	GError *error = NULL;

	realm_service_stop_finish (result, &error);
	if (error != NULL)
		g_task_return_error (task, error);
	else
		g_task_return_boolean (task, TRUE);
	g_object_unref (task);
}


static void
on_disable_disabled (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GTask *task = G_TASK (user_data);
	CallClosure *call = g_task_get_task_data (task);
	GError *error = NULL;

	realm_service_disable_finish (result, &error);
	if (error == NULL) {
		realm_service_stop (call->service_name, call->invocation,
		                    on_disable_stopped, g_object_ref (task));
	} else {
		g_task_return_error (task, error);
	}

	g_object_unref (task);
}

void
realm_service_disable_and_stop (const gchar *service_name,
                                GDBusMethodInvocation *invocation,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	GTask *task;
	CallClosure *call;

	task = g_task_new (NULL, NULL, callback, user_data);
	call = g_new0 (CallClosure, 1);
	call->service_name = g_strdup (service_name);
	call->invocation = invocation ? g_object_ref (invocation) : invocation;
	g_task_set_task_data (task, call, call_closure_free);

	realm_service_disable (call->service_name, call->invocation,
	                       on_disable_disabled, g_object_ref (task));

	g_object_unref (task);
}

gboolean
realm_service_disable_and_stop_finish (GAsyncResult *result,
                                       GError **error)
{
	g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean (G_TASK (result), error);
}
