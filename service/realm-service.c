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

#define DEBUG_FLAG REALM_DEBUG_SERVICE
#include "realm-debug.h"
#include "realm-service.h"
#include "realm-service-systemd.h"
#include "realm-service-upstart.h"
#include "realm-settings.h"

#include <glib/gi18n.h>

static void           (* discovered_service_new)        (const gchar *service_name,
                                                         GAsyncReadyCallback callback,
                                                         gpointer user_data);

static RealmService * (* discovered_service_new_finish) (GAsyncResult *result,
                                                         GError **error);

G_DEFINE_TYPE (RealmService, realm_service, G_TYPE_DBUS_PROXY);

static void
realm_service_init (RealmService *self)
{

}

static void
realm_service_class_init (RealmServiceClass *klass)
{

}

typedef struct {
	gchar *name;
	RealmService *service;
} InitClosure;

static void
init_closure_free (gpointer data)
{
	InitClosure *init = data;
	g_free (init->name);
	if (init->service)
		g_object_unref (init->service);
	g_slice_free (InitClosure, init);
}

static void
on_service_new_upstart (GObject *source,
                        GAsyncResult *result,
                        gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	InitClosure *init = g_simple_async_result_get_op_res_gpointer (async);
	GError *error = NULL;
	RealmService *service;

	service = realm_service_upstart_new_finish (result, &error);

	if (error != NULL) {
		g_simple_async_result_take_error (async, error);

	} else {
		realm_debug ("Connected to Upstart, discovered the service manager");
		discovered_service_new = realm_service_upstart_new;
		discovered_service_new_finish = realm_service_upstart_new_finish;
		init->service = service;
	}

	g_simple_async_result_complete (async);
	g_object_unref (async);
}

static void
on_service_new_systemd (GObject *source,
                        GAsyncResult *result,
                        gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	InitClosure *init = g_simple_async_result_get_op_res_gpointer (async);
	GError *error = NULL;
	RealmService *service;

	service = realm_service_systemd_new_finish (result, &error);

	/* If no such service, then try Upstart */
	if (g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN)) {
		realm_debug ("Couldn't connect to systemd, trying Upstart");
		realm_service_upstart_new (init->name, on_service_new_upstart,
		                           g_object_ref (async));

	/* Some other error? */
	} else if (error != NULL) {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);

	/* Success yay */
	} else {
		realm_debug ("Connected to systemd, discovered the service manager");
		discovered_service_new = realm_service_systemd_new;
		discovered_service_new_finish = realm_service_systemd_new_finish;
		init->service = service;
		g_simple_async_result_complete (async);
	}

	g_object_unref (async);
}

void
realm_service_new (const gchar *service_name,
                   GDBusMethodInvocation *invocation,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
	GSimpleAsyncResult *async;
	InitClosure *init;
	const gchar *name;

	g_return_if_fail (service_name != NULL);

	name = realm_settings_string ("services", "winbind");
	if (name == NULL)
		name = service_name;

	/* Discover which service type works */
	if (discovered_service_new == NULL) {
		realm_debug ("No service manager discovered, trying systemd");
		async = g_simple_async_result_new (NULL, callback, user_data,
		                                   realm_service_new);
		init = g_slice_new0 (InitClosure);
		init->name = g_strdup (name);
		g_simple_async_result_set_op_res_gpointer (async, init, init_closure_free);
		realm_service_systemd_new (init->name, on_service_new_systemd,
		                           g_object_ref (async));
		g_object_unref (async);

	/* Already discovered which service type works */
	} else {
		discovered_service_new (name, callback, user_data);
	}
}

RealmService *
realm_service_new_finish (GAsyncResult *result,
                          GError **error)
{
	GSimpleAsyncResult *async;
	RealmService *service = NULL;
	InitClosure *init;

	if (g_simple_async_result_is_valid (result, NULL, realm_service_new)) {
		async = G_SIMPLE_ASYNC_RESULT (result);
		if (g_simple_async_result_propagate_error (async, error))
			return NULL;
		init = g_simple_async_result_get_op_res_gpointer (async);
		if (init->service == NULL)
			return NULL;
		else
			return g_object_ref (init->service);
	} else {
		return discovered_service_new_finish (result, error);
	}

	return service;
}

void
realm_service_enable (RealmService *self,
                      GDBusMethodInvocation *invocation,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
	RealmServiceClass *klass;

	g_return_if_fail (REALM_IS_SERVICE (self));

	klass = REALM_SERVICE_GET_CLASS (self);
	g_return_if_fail (klass->enable != NULL);

	(klass->enable) (self, invocation, callback, user_data);
}

gboolean
realm_service_enable_finish (RealmService *self,
                             GAsyncResult *result,
                             GError **error)
{
	RealmServiceClass *klass;

	g_return_val_if_fail (REALM_IS_SERVICE (self), FALSE);

	klass = REALM_SERVICE_GET_CLASS (self);
	g_return_val_if_fail (klass->enable_finish != NULL, FALSE);

	return (klass->enable_finish) (self, result, error);
}

void
realm_service_disable (RealmService *self,
                       GDBusMethodInvocation *invocation,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	RealmServiceClass *klass;

	g_return_if_fail (REALM_IS_SERVICE (self));

	klass = REALM_SERVICE_GET_CLASS (self);
	g_return_if_fail (klass->disable != NULL);

	(klass->disable) (self, invocation, callback, user_data);
}

gboolean
realm_service_disable_finish (RealmService *self,
                              GAsyncResult *result,
                              GError **error)
{
	RealmServiceClass *klass;

	g_return_val_if_fail (REALM_IS_SERVICE (self), FALSE);

	klass = REALM_SERVICE_GET_CLASS (self);
	g_return_val_if_fail (klass->disable_finish != NULL, FALSE);

	return (klass->disable_finish) (self, result, error);
}

void
realm_service_restart (RealmService *self,
                       GDBusMethodInvocation *invocation,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	RealmServiceClass *klass;

	g_return_if_fail (REALM_IS_SERVICE (self));

	klass = REALM_SERVICE_GET_CLASS (self);
	g_return_if_fail (klass->restart != NULL);

	(klass->restart) (self, invocation, callback, user_data);
}

gboolean
realm_service_restart_finish (RealmService *self,
                              GAsyncResult *result,
                              GError **error)
{
	RealmServiceClass *klass;

	g_return_val_if_fail (REALM_IS_SERVICE (self), FALSE);

	klass = REALM_SERVICE_GET_CLASS (self);
	g_return_val_if_fail (klass->restart_finish != NULL, FALSE);

	return (klass->restart_finish) (self, result, error);
}

void
realm_service_stop (RealmService *self,
                    GDBusMethodInvocation *invocation,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
	RealmServiceClass *klass;

	g_return_if_fail (REALM_IS_SERVICE (self));

	klass = REALM_SERVICE_GET_CLASS (self);
	g_return_if_fail (klass->stop != NULL);

	(klass->stop) (self, invocation, callback, user_data);
}

gboolean
realm_service_stop_finish (RealmService *self,
                           GAsyncResult *result,
                           GError **error)
{
	RealmServiceClass *klass;

	g_return_val_if_fail (REALM_IS_SERVICE (self), FALSE);

	klass = REALM_SERVICE_GET_CLASS (self);
	g_return_val_if_fail (klass->stop_finish != NULL, FALSE);

	return (klass->stop_finish) (self, result, error);
}

static void
on_enable_restarted (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	RealmService *service = REALM_SERVICE (source);
	GError *error = NULL;

	realm_service_restart_finish (service, result, &error);
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
	RealmService *service = REALM_SERVICE (source);
	GDBusMethodInvocation *invocation;
	GError *error = NULL;

	realm_service_enable_finish (service, result, &error);
	if (error == NULL) {
		invocation = g_simple_async_result_get_op_res_gpointer (async);
		realm_service_restart (service, invocation, on_enable_restarted,
		                       g_object_ref (async));
	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (async);
}

static void
on_enable_created (GObject *source,
                   GAsyncResult *result,
                   gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	GDBusMethodInvocation *invocation;
	RealmService *service;
	GError *error = NULL;

	service = realm_service_new_finish (result, &error);
	if (error == NULL) {
		invocation = g_simple_async_result_get_op_res_gpointer (async);
		realm_service_enable (service, invocation, on_enable_enabled,
		                      g_object_ref (async));
		g_object_unref (service);
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

	async = g_simple_async_result_new (NULL, callback, user_data,
	                                   realm_service_enable_and_restart);
	if (invocation) {
		g_simple_async_result_set_op_res_gpointer (async,
		                                           g_object_ref (invocation),
		                                           g_object_unref);
	}

	realm_service_new (service_name, invocation,
	                   on_enable_created, g_object_ref (async));

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
	RealmService *service = REALM_SERVICE (source);
	GError *error = NULL;

	realm_service_stop_finish (service, result, &error);
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
	RealmService *service = REALM_SERVICE (source);
	GDBusMethodInvocation *invocation;
	GError *error = NULL;

	realm_service_disable_finish (service, result, &error);
	if (error == NULL) {
		invocation = g_simple_async_result_get_op_res_gpointer (async);
		realm_service_stop (service, invocation, on_disable_stopped,
		                    g_object_ref (async));
	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (async);
}

static void
on_disable_created (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	GDBusMethodInvocation *invocation;
	RealmService *service;
	GError *error = NULL;

	service = realm_service_new_finish (result, &error);
	if (error == NULL) {
		invocation = g_simple_async_result_get_op_res_gpointer (async);
		realm_service_disable (service, invocation, on_disable_disabled,
		                       g_object_ref (async));
		g_object_unref (service);
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

	async = g_simple_async_result_new (NULL, callback, user_data,
	                                   realm_service_disable_and_stop);
	if (invocation) {
		g_simple_async_result_set_op_res_gpointer (async,
		                                           g_object_ref (invocation),
		                                           g_object_unref);
	}

	realm_service_new (service_name, invocation,
	                   on_disable_created, g_object_ref (async));

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
