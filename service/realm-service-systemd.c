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
#include "realm-service-systemd.h"

enum {
	PROP_0,
	PROP_SERVICE_NAME
};

struct _RealmServiceSystemd {
	RealmService parent;
	gchar *name;
};

typedef struct _RealmServiceSystemdClass {
	RealmServiceClass parent_class;
} RealmServiceSystemdClass;

G_DEFINE_TYPE (RealmServiceSystemd, realm_service_systemd, REALM_TYPE_SERVICE);

static gboolean
realm_service_systemd_dbus_finish (RealmService *service,
                                   GAsyncResult *result,
                                   GError **error)
{
	RealmServiceSystemd *self = REALM_SERVICE_SYSTEMD (service);
	GError *lerror = NULL;
	GVariant *retval;

	retval = g_dbus_proxy_call_finish (G_DBUS_PROXY (service), result, &lerror);
	if (retval != NULL)
		g_variant_unref (retval);

	if (lerror != NULL) {
		realm_debug ("Service call failed: %s: %s", self->name, lerror->message);
		g_propagate_error (error, lerror);
	}

	return retval != NULL;
}

static void
realm_service_systemd_enable (RealmService *service,
                              GDBusMethodInvocation *invocation,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	RealmServiceSystemd *self = REALM_SERVICE_SYSTEMD (service);

	const gchar *unit_files[] = {
		self->name,
		NULL,
	};

	realm_diagnostics_info (invocation, "Enabling service via systemd: %s", self->name);

	g_dbus_proxy_call (G_DBUS_PROXY (self), "EnableUnitFiles",
	                   g_variant_new ("(^asbb)", unit_files, FALSE, FALSE),
	                   G_DBUS_CALL_FLAGS_NONE, -1, NULL, callback, user_data);
}

static void
realm_service_systemd_disable (RealmService *service,
                               GDBusMethodInvocation *invocation,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	RealmServiceSystemd *self = REALM_SERVICE_SYSTEMD (service);

	const gchar *unit_files[] = {
		self->name,
		NULL,
	};

	realm_diagnostics_info (invocation, "Disabling service via systemd: %s", self->name);

	g_dbus_proxy_call (G_DBUS_PROXY (self), "DisableUnitFiles",
	                   g_variant_new ("(^asb)", unit_files, FALSE),
	                   G_DBUS_CALL_FLAGS_NONE, -1, NULL, callback, user_data);
}

static void
realm_service_systemd_restart (RealmService *service,
                               GDBusMethodInvocation *invocation,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	RealmServiceSystemd *self = REALM_SERVICE_SYSTEMD (service);

	realm_diagnostics_info (invocation, "Restarting service via systemd: %s", self->name);

	g_dbus_proxy_call (G_DBUS_PROXY (self), "RestartUnit",
	                   g_variant_new ("(ss)", self->name, "fail"),
	                   G_DBUS_CALL_FLAGS_NONE, -1, NULL, callback, user_data);
}

static void
realm_service_systemd_stop (RealmService *service,
                            GDBusMethodInvocation *invocation,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	RealmServiceSystemd *self = REALM_SERVICE_SYSTEMD (service);

	realm_diagnostics_info (invocation, "Stopping service via systemd: %s", self->name);

	g_dbus_proxy_call (G_DBUS_PROXY (self), "StopUnit",
	                   g_variant_new ("(ss)", self->name, "fail"),
	                   G_DBUS_CALL_FLAGS_NONE, -1, NULL, callback, user_data);
}

static void
realm_service_systemd_init (RealmServiceSystemd *self)
{

}

static void
realm_service_systemd_set_property (GObject *obj,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
	RealmServiceSystemd *self = REALM_SERVICE_SYSTEMD (obj);

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
realm_service_systemd_get_property (GObject *obj,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
	RealmServiceSystemd *self = REALM_SERVICE_SYSTEMD (obj);

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
realm_service_systemd_finalize (GObject *obj)
{
	RealmServiceSystemd *self = REALM_SERVICE_SYSTEMD (obj);

	g_free (self->name);

	G_OBJECT_CLASS (realm_service_systemd_parent_class)->finalize (obj);
}

static void
realm_service_systemd_class_init (RealmServiceSystemdClass *klass)
{
	RealmServiceClass *service_class = REALM_SERVICE_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = realm_service_systemd_get_property;
	object_class->set_property = realm_service_systemd_set_property;
	object_class->finalize = realm_service_systemd_finalize;

	service_class->enable = realm_service_systemd_enable;
	service_class->enable_finish = realm_service_systemd_dbus_finish;
	service_class->disable = realm_service_systemd_disable;
	service_class->disable_finish = realm_service_systemd_dbus_finish;
	service_class->restart = realm_service_systemd_restart;
	service_class->restart_finish = realm_service_systemd_dbus_finish;
	service_class->stop = realm_service_systemd_stop;
	service_class->stop_finish = realm_service_systemd_dbus_finish;

	g_object_class_install_property (object_class, PROP_SERVICE_NAME,
	            g_param_spec_string ("service-name", "Service Name", "Service Name",
	                                 "", G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
on_systemd_ping (GObject *source,
                 GAsyncResult *result,
                 gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;
	GVariant *retval;

	retval = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source),
	                                        result, &error);
	if (error == NULL) {
		realm_debug ("Pinged systemd successfully");
		g_variant_unref (retval);
	} else {
		realm_debug ("Pinging systemd failed: %s", error->message);
		g_simple_async_result_take_error (async, error);
	}

	g_simple_async_result_complete (async);
	g_object_unref (async);
}

static void
on_systemd_created (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;
	GDBusProxy *self;

	self = G_DBUS_PROXY (g_async_initable_new_finish (G_ASYNC_INITABLE (source),
	                                                  result, &error));

	if (error == NULL) {
		realm_debug ("Pinging systemd to make sure it's running");
		g_simple_async_result_set_op_res_gpointer (async, self, g_object_unref);
		g_dbus_connection_call (g_dbus_proxy_get_connection (self),
		                        g_dbus_proxy_get_name (self),
		                        "/", "org.freedesktop.DBus.Peer",
		                        "Ping", g_variant_new ("()"),
		                        G_VARIANT_TYPE ("()"),
		                        G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
		                        -1, NULL, on_systemd_ping, g_object_ref (async));
	} else {
		realm_debug ("Failed to connect to systemd: %s", error->message);
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (async);
}

void
realm_service_systemd_new (const gchar *service_name,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	GSimpleAsyncResult *async;
	gchar *service;

	realm_debug ("Connecting to systemd for service: %s", service_name);

	service = g_strdup_printf ("%s.service", service_name);
	async = g_simple_async_result_new (NULL, callback, user_data,
	                                   realm_service_systemd_new);

	g_async_initable_new_async (REALM_TYPE_SERVICE_SYSTEMD, G_PRIORITY_DEFAULT, NULL,
	                            on_systemd_created, g_object_ref (async),
	                            "service-name", service,
	                            "g-flags", G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
	                            "g-name", "org.freedesktop.systemd1",
	                            "g-bus-type", G_BUS_TYPE_SYSTEM,
	                            "g-object-path", "/org/freedesktop/systemd1",
	                            "g-interface-name", "org.freedesktop.systemd1.Manager",
	                            NULL);

	g_free (service);
	g_object_unref (async);
}

RealmService *
realm_service_systemd_new_finish (GAsyncResult *result,
                                  GError **error)
{
	GSimpleAsyncResult *async;
	RealmService *service;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      realm_service_systemd_new), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	async = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (async, error))
		return NULL;

	service = REALM_SERVICE (g_simple_async_result_get_op_res_gpointer (async));
	if (service != NULL)
		g_object_ref (service);
	return service;
}
