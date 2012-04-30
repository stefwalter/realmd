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

#include "realm-dbus-systemd.h"
#include "realm-diagnostics.h"
#include "realm-service.h"

typedef struct _ServiceClosure ServiceClosure;

typedef void (* PerformFunc) (RealmDbusSystemdManager *systemd,
                              GSimpleAsyncResult *res,
                              ServiceClosure *service);

struct _ServiceClosure {
	GDBusMethodInvocation *invocation;
	gchar *unit_name;
	PerformFunc perform;
};

static void
service_closure_free (gpointer data)
{
	ServiceClosure *service = data;
	if (service->invocation)
		g_object_unref (service->invocation);
	g_free (service->unit_name);
	g_slice_free (ServiceClosure, service);
}

static void
on_systemd_start (GObject *object,
                  GAsyncResult *result,
                  gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	RealmDbusSystemdManager *systemd = REALM_DBUS_SYSTEMD_MANAGER (object);
	GError *error = NULL;

	realm_dbus_systemd_manager_call_start_unit_finish (systemd, NULL,
	                                                   result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	g_simple_async_result_complete (res);

	g_object_unref (res);
}

static void
on_systemd_enable (GObject *object,
                   GAsyncResult *result,
                   gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	RealmDbusSystemdManager *systemd = REALM_DBUS_SYSTEMD_MANAGER (object);
	ServiceClosure *service = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;

	realm_dbus_systemd_manager_call_enable_unit_files_finish (systemd, NULL, NULL,
	                                                          result, &error);
	if (error == NULL) {
		realm_diagnostics_info (service->invocation, "Starting service via systemd: %s",
		                        service->unit_name);
		realm_dbus_systemd_manager_call_start_unit (systemd, service->unit_name,
		                                            "fail", NULL, on_systemd_start,
		                                            g_object_ref (res));
	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
perform_enable_and_restart (RealmDbusSystemdManager *systemd,
                            GSimpleAsyncResult *res,
                            ServiceClosure *service)
{
	const gchar *unit_files[] = {
		service->unit_name,
		NULL,
	};

	realm_diagnostics_info (service->invocation, "Enabling service via systemd: %s",
	                        service->unit_name);
	realm_dbus_systemd_manager_call_enable_unit_files (systemd,
	                                                   unit_files,
	                                                   FALSE, /* runtime */
	                                                   FALSE, /* force */
	                                                   NULL,
	                                                   on_systemd_enable,
	                                                   g_object_ref (res));
}

static void
on_systemd_stop (GObject *object,
                 GAsyncResult *result,
                 gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	RealmDbusSystemdManager *systemd = REALM_DBUS_SYSTEMD_MANAGER (object);
	GError *error = NULL;

	realm_dbus_systemd_manager_call_stop_unit_finish (systemd, NULL,
	                                                  result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	g_simple_async_result_complete (res);

	g_object_unref (res);
}

static void
on_systemd_disable (GObject *object,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	RealmDbusSystemdManager *systemd = REALM_DBUS_SYSTEMD_MANAGER (object);
	ServiceClosure *service = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;

	realm_dbus_systemd_manager_call_disable_unit_files_finish (systemd, NULL,
	                                                           result, &error);
	if (error == NULL) {
		realm_diagnostics_info (service->invocation, "Stopping service via systemd: %s",
		                        service->unit_name);
		realm_dbus_systemd_manager_call_stop_unit (systemd, service->unit_name,
		                                           "fail", NULL, on_systemd_stop,
		                                           g_object_ref (res));
	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
perform_disable_and_stop (RealmDbusSystemdManager *systemd,
                          GSimpleAsyncResult *res,
                          ServiceClosure *service)
{
	const gchar *unit_files[] = {
		service->unit_name,
		NULL,
	};

	realm_diagnostics_info (service->invocation, "Disabling service via systemd: %s",
	                        service->unit_name);
	realm_dbus_systemd_manager_call_disable_unit_files (systemd,
	                                                    unit_files,
	                                                    FALSE, /* runtime */
	                                                    NULL,
	                                                    on_systemd_disable,
	                                                    g_object_ref (res));
}

static void
on_systemd_proxy (GObject *object,
                  GAsyncResult *result,
                  gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	ServiceClosure *service = g_simple_async_result_get_op_res_gpointer (res);
	RealmDbusSystemdManager *systemd;
	GError *error = NULL;

	systemd = realm_dbus_systemd_manager_proxy_new_finish (result, &error);
	if (error == NULL) {
		g_assert (service->perform != NULL);
		(service->perform) (systemd, res, service);
		g_object_unref (systemd);

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
begin_service_action (const gchar *service_name,
                      GDBusMethodInvocation *invocation,
                      PerformFunc perform,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
	GSimpleAsyncResult *res;
	ServiceClosure *service;

	res = g_simple_async_result_new (NULL, callback, user_data,
	                                 begin_service_action);
	service = g_slice_new (ServiceClosure);
	service->unit_name = g_strdup (service_name);
	service->invocation = invocation ? g_object_ref (invocation) : NULL;
	service->perform = perform;
	g_simple_async_result_set_op_res_gpointer (res, service, service_closure_free);

	realm_dbus_systemd_manager_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
	                                              G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
	                                              "org.freedesktop.systemd1",
	                                              "/org/freedesktop/systemd1",
	                                              NULL,
	                                              on_systemd_proxy,
	                                              g_object_ref (res));

	g_object_unref (res);
}

void
realm_service_enable_and_restart (const gchar *service_name,
                                  GDBusMethodInvocation *invocation,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	g_return_if_fail (service_name != NULL);
	g_return_if_fail (invocation == NULL || G_IS_DBUS_METHOD_INVOCATION (invocation));

	begin_service_action (service_name, invocation,
	                      perform_enable_and_restart,
	                      callback, user_data);
}

gboolean
realm_service_enable_finish (GAsyncResult *result,
                             GError **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      begin_service_action), FALSE);
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;
	return TRUE;
}

void
realm_service_disable_and_stop (const gchar *service_name,
                                GDBusMethodInvocation *invocation,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	g_return_if_fail (service_name != NULL);
	g_return_if_fail (invocation == NULL || G_IS_DBUS_METHOD_INVOCATION (invocation));

	begin_service_action (service_name, invocation,
	                      perform_disable_and_stop,
	                      callback, user_data);
}

gboolean
realm_service_disable_finish (GAsyncResult *result,
                              GError **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      begin_service_action), FALSE);
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;
	return TRUE;
}
