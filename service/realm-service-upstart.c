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
#include "realm-diagnostics.h"
#include "realm-service.h"
#include "realm-service-upstart.h"

enum {
	PROP_0,
	PROP_SERVICE_NAME
};

struct _RealmServiceUpstart {
	RealmService parent;
	gchar *name;
};

typedef struct _RealmServiceUpstartClass {
	RealmServiceClass parent_class;
} RealmServiceUpstartClass;

G_DEFINE_TYPE (RealmServiceUpstart, realm_service_upstart, REALM_TYPE_SERVICE);

static gboolean
realm_service_upstart_dbus_finish (RealmService *service,
                                   GAsyncResult *result,
                                   GError **error)
{
	RealmServiceUpstart *self = REALM_SERVICE_UPSTART (service);
	GVariant *retval;
	GError *lerror = NULL;

	retval = g_dbus_proxy_call_finish (G_DBUS_PROXY (service), result, &lerror);
	if (retval != NULL)
		g_variant_unref (retval);

	if (lerror != NULL) {
		realm_debug ("Service call failed: %s: %s", self->name, lerror->message);
		g_propagate_error (error, lerror);
	}

	return retval != NULL;
}

static gboolean
realm_service_upstart_stub_finish (RealmService *service,
                                   GAsyncResult *result,
                                   GError **error)
{
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;
	return TRUE;
}

static void
realm_service_upstart_enable (RealmService *service,
                              GDBusMethodInvocation *invocation,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	RealmServiceUpstart *self = REALM_SERVICE_UPSTART (service);
	GSimpleAsyncResult *async;

	/* TODO: Not sure what to do here for upstart */
	realm_debug ("Enabling Upstart service '%s' is not implemented", self->name);

	async = g_simple_async_result_new (G_OBJECT (service), callback, user_data,
	                                   realm_service_upstart_stub_finish);
	g_simple_async_result_complete_in_idle (async);

	g_object_unref (async);
}

static void
realm_service_upstart_disable (RealmService *service,
                               GDBusMethodInvocation *invocation,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	RealmServiceUpstart *self = REALM_SERVICE_UPSTART (service);
	GSimpleAsyncResult *async;

	/* TODO: Not sure what to do here for upstart */
	realm_debug ("Disabling Upstart service '%s' is not implemented", self->name);

	async = g_simple_async_result_new (G_OBJECT (service), callback, user_data,
	                                   realm_service_upstart_stub_finish);
	g_simple_async_result_complete_in_idle (async);
	g_object_unref (async);
}

static void
realm_service_upstart_restart (RealmService *service,
                               GDBusMethodInvocation *invocation,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	RealmServiceUpstart *self = REALM_SERVICE_UPSTART (service);
	const char *environ = { NULL };

	realm_diagnostics_info (invocation, "Restarting service via upstart: %s", self->name);

	g_dbus_proxy_call (G_DBUS_PROXY (self), "Restart",
	                   g_variant_new ("(^asb)", environ, TRUE),
	                   G_DBUS_CALL_FLAGS_NONE, -1, NULL,
	                   callback, user_data);
}

static void
realm_service_upstart_stop (RealmService *service,
                            GDBusMethodInvocation *invocation,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	RealmServiceUpstart *self = REALM_SERVICE_UPSTART (service);
	const char *environ = { NULL };

	realm_diagnostics_info (invocation, "Stopping service via upstart: %s", self->name);

	g_dbus_proxy_call (G_DBUS_PROXY (self), "Stop",
	                   g_variant_new ("(^asb)", environ, TRUE),
	                   G_DBUS_CALL_FLAGS_NONE, -1, NULL,
	                   callback, user_data);
}

static void
realm_service_upstart_init (RealmServiceUpstart *self)
{

}

static void
realm_service_upstart_set_property (GObject *obj,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
	RealmServiceUpstart *self = REALM_SERVICE_UPSTART (obj);

	switch (property_id) {
	case PROP_SERVICE_NAME:
		self->name = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
		break;
	}
}

static void
realm_service_upstart_get_property (GObject *obj,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
	RealmServiceUpstart *self = REALM_SERVICE_UPSTART (obj);

	switch (property_id) {
	case PROP_SERVICE_NAME:
		g_value_set_string (value, self->name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
		break;
	}
}

static void
realm_service_upstart_finalize (GObject *obj)
{
	RealmServiceUpstart *self = REALM_SERVICE_UPSTART (obj);

	g_free (self->name);

	G_OBJECT_CLASS (realm_service_upstart_parent_class)->finalize (obj);
}

static void
realm_service_upstart_class_init (RealmServiceUpstartClass *klass)
{
	RealmServiceClass *service_class = REALM_SERVICE_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = realm_service_upstart_get_property;
	object_class->set_property = realm_service_upstart_set_property;
	object_class->finalize = realm_service_upstart_finalize;

	service_class->enable = realm_service_upstart_enable;
	service_class->enable_finish = realm_service_upstart_stub_finish;
	service_class->disable = realm_service_upstart_disable;
	service_class->disable_finish = realm_service_upstart_stub_finish;
	service_class->restart = realm_service_upstart_restart;
	service_class->restart_finish = realm_service_upstart_dbus_finish;
	service_class->stop = realm_service_upstart_stop;
	service_class->stop_finish = realm_service_upstart_dbus_finish;

	g_object_class_install_property (object_class, PROP_SERVICE_NAME,
	            g_param_spec_string ("service-name", "Service Name", "Service Name",
	                                 "", G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

typedef struct {
	gchar *name;
	RealmService *service;
} UpstartClosure;

static void
upstart_closure_free (gpointer data)
{
	UpstartClosure *upstart = data;
	g_free (upstart->name);
	if (upstart->service)
		g_object_unref (upstart->service);
	g_slice_free (UpstartClosure, upstart);
}
static void
on_upstart_created (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	UpstartClosure *upstart = g_simple_async_result_get_op_res_gpointer (async);
	RealmService *self;
	GError *error = NULL;

	self = REALM_SERVICE (g_async_initable_new_finish (G_ASYNC_INITABLE (source),
	                                                   result, &error));

	if (error == NULL) {
		realm_debug ("Connected to Upstart job for service: %s", upstart->name);
		upstart->service = self;

	} else {
		realm_debug ("Failed to create proxy for Upstart job: %s", error->message);
		g_simple_async_result_take_error (async, error);
	}

	g_simple_async_result_complete (async);
	g_object_unref (async);
}

static void
on_upstart_get_job (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	UpstartClosure *upstart = g_simple_async_result_get_op_res_gpointer (async);
	const gchar *job_path;
	GVariant *retval;
	GError *error = NULL;

	retval = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
	if (error == NULL) {
		g_variant_get (retval, "(&o)", &job_path);
		realm_debug ("GetJobByName returned object path '%s', creating proxy", job_path);

		g_async_initable_new_async (REALM_TYPE_SERVICE_UPSTART,
		                            G_PRIORITY_DEFAULT, NULL,
		                            on_upstart_created, g_object_ref (async),
		                            "service-name", upstart->name,
		                            "g-flags", G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
		                            "g-name", "com.ubuntu.Upstart",
		                            "g-connection", G_DBUS_CONNECTION (source),
		                            "g-object-path", job_path,
		                            "g-interface-name", "com.ubuntu.Upstart0_6.Job",
		                            NULL);
		g_variant_unref (retval);
	} else {
		realm_debug ("GetJobByName failed: %s", error->message);
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (async);
}

static void
on_upstart_bus (GObject *source,
                GAsyncResult *result,
                gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	UpstartClosure *upstart = g_simple_async_result_get_op_res_gpointer (async);
	GError *error = NULL;
	GDBusConnection *conn;

	conn = g_bus_get_finish (result, &error);
	if (error == NULL) {
		realm_debug ("Calling Upstart GetJobByName for service: %s", upstart->name);
		g_dbus_connection_call (conn, "com.ubuntu.Upstart",
		                        "/com/ubuntu/Upstart",
		                        "com.ubuntu.Upstart0_6",
		                        "GetJobByName",
		                        g_variant_new ("(s)", upstart->name),
		                        G_VARIANT_TYPE ("(o)"),
		                        G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
		                        -1, NULL, on_upstart_get_job,
		                        g_object_ref (async));
	} else {
		realm_debug ("Failed to connect to system bus: %s", error->message);
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (async);
}

void
realm_service_upstart_new (const gchar *service_name,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	GSimpleAsyncResult *async;
	UpstartClosure *upstart;

	realm_debug ("Connecting to Upstart for service: %s", service_name);

	async = g_simple_async_result_new (NULL, callback, user_data,
	                                   realm_service_upstart_new);
	upstart = g_slice_new0 (UpstartClosure);
	upstart->name = g_strdup (service_name);
	g_simple_async_result_set_op_res_gpointer (async, upstart, upstart_closure_free);

	g_bus_get (G_BUS_TYPE_SYSTEM, NULL, on_upstart_bus, g_object_ref (async));

	g_object_unref (async);
}

RealmService *
realm_service_upstart_new_finish (GAsyncResult *result,
                                  GError **error)
{
	GSimpleAsyncResult *async;
	UpstartClosure *upstart;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      realm_service_upstart_new), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	async = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (async, error))
		return NULL;

	upstart = g_simple_async_result_get_op_res_gpointer (async);
	if (upstart->service == NULL)
		return NULL;
	return g_object_ref (upstart->service);
}
