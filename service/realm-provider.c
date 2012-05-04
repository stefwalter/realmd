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

#include "realm-daemon.h"
#define DEBUG_FLAG REALM_DEBUG_SERVICE
#include "realm-debug.h"
#include "realm-dbus-constants.h"
#include "realm-dbus-generated.h"
#include "realm-diagnostics.h"
#include "realm-discovery.h"
#include "realm-errors.h"
#include "realm-provider.h"

#include <glib/gi18n.h>

static void realm_provider_iface_init (RealmDbusProviderIface *iface);

G_DEFINE_TYPE_WITH_CODE (RealmProvider, realm_provider, REALM_DBUS_TYPE_PROVIDER_SKELETON,
	G_IMPLEMENT_INTERFACE (REALM_DBUS_TYPE_PROVIDER, realm_provider_iface_init)
);

typedef struct {
	RealmProvider *self;
	GDBusMethodInvocation *invocation;
} MethodClosure;

static MethodClosure *
method_closure_new (RealmProvider *self,
                    GDBusMethodInvocation *invocation)
{
	MethodClosure *closure = g_slice_new (MethodClosure);
	closure->self = g_object_ref (self);
	closure->invocation = g_object_ref (invocation);
	return closure;
}

static void
method_closure_free (MethodClosure *closure)
{
	g_object_unref (closure->self);
	g_object_unref (closure->invocation);
	g_slice_free (MethodClosure, closure);
}

static void
on_discover_complete (GObject *source,
                      GAsyncResult *result,
                      gpointer user_data)
{
	MethodClosure *closure = user_data;
	RealmProviderClass *klass;
	GVariant *discovery = NULL;
	GVariant *retval;
	GError *error = NULL;
	GVariant *realm;
	gint relevance;

	klass = REALM_PROVIDER_GET_CLASS (closure->self);
	g_return_if_fail (klass->discover_finish != NULL);

	relevance = (klass->discover_finish) (closure->self, result, &realm, &discovery, &error);
	if (relevance >= 0) {
		realm_diagnostics_info (closure->invocation, "Successfully discovered realm");
		retval = g_variant_new ("(i@(sos)@a{sv})", relevance, realm, discovery);
		g_dbus_method_invocation_return_value (closure->invocation, retval);
		g_variant_unref (discovery);
		g_variant_unref (realm);

	} else if (error != NULL && error->domain == REALM_ERROR) {
		realm_diagnostics_error (closure->invocation, error, NULL);
		g_dbus_method_invocation_return_gerror (closure->invocation, error);

	} else {
		realm_diagnostics_error (closure->invocation, error, "Failed to discover realm");
		g_dbus_method_invocation_return_error (closure->invocation, REALM_ERROR, REALM_ERROR_DISCOVERY_FAILED,
		                                       "Failed to discover realm. See diagnostics.");
		g_error_free (error);
	}

	method_closure_free (closure);
}

static gboolean
realm_provider_handle_discover (RealmDbusProvider *provider,
                                GDBusMethodInvocation *invocation,
                                const gchar *string)
{
	RealmProvider *self = REALM_PROVIDER (provider);
	RealmProviderClass *klass;

	klass = REALM_PROVIDER_GET_CLASS (self);
	g_return_val_if_fail (klass->discover_async != NULL, FALSE);
	g_return_val_if_fail (klass->discover_finish != NULL, FALSE);

	(klass->discover_async) (self, string, invocation, on_discover_complete,
	                         method_closure_new (self, invocation));

	return TRUE;
}

static gboolean
on_authorize_method (GDBusInterfaceSkeleton *skeleton,
                     GDBusMethodInvocation  *invocation,
                     gpointer user_data)
{
	const gchar *interface = g_dbus_method_invocation_get_interface_name (invocation);
	const gchar *method = g_dbus_method_invocation_get_method_name (invocation);
	const gchar *action_id = NULL;
	gboolean ret;

	/* Reading properties is authorized */
	if (g_str_equal (interface, DBUS_PROPERTIES_INTERFACE)) {
		if (g_str_equal (method, "Get") ||
		    g_str_equal (method, "GetAll"))
			ret = TRUE;
		else
			ret = FALSE; /* we have no setters */

	/* Each method has its own polkit authorization */
	} else if (g_str_equal (interface, REALM_DBUS_PROVIDER_INTERFACE)) {
		if (g_str_equal (method, "Discover")) {
			action_id = "org.freedesktop.realmd.discover-realm";
		} else {
			g_warning ("encountered unknown method during auth checks: %s.%s",
			           interface, method);
			action_id = NULL;
		}

		if (action_id != NULL)
			ret = realm_daemon_check_dbus_action (g_dbus_method_invocation_get_sender (invocation),
			                                       action_id);
		else
			ret = FALSE;
	}

	if (ret == FALSE) {
		realm_debug ("rejecting access to: %s.%s method on %s",
		             interface, method, g_dbus_method_invocation_get_object_path (invocation));
		g_dbus_method_invocation_return_dbus_error (invocation, REALM_DBUS_ERROR_NOT_AUTHORIZED,
		                                            "Not authorized to perform this action");
	}

	return ret;
}

static void
realm_provider_init (RealmProvider *self)
{

}

static void
realm_provider_class_init (RealmProviderClass *klass)
{
	GDBusInterfaceSkeletonClass *skeleton_class = G_DBUS_INTERFACE_SKELETON_CLASS (klass);

	skeleton_class->g_authorize_method = realm_provider_authorize_method;
}

static void
realm_provider_iface_init (RealmDbusProviderIface *iface)
{
	memcpy (iface, g_type_interface_peek_parent (iface), sizeof (*iface));
	iface->handle_discover = realm_provider_handle_discover;
}

GVariant *
realm_provider_new_realm_info (const gchar *bus_name,
                               const gchar *object_path,
                               const gchar *interface)
{
	g_return_val_if_fail (g_dbus_is_name (bus_name), NULL);
	g_return_val_if_fail (g_variant_is_object_path (object_path), NULL);
	g_return_val_if_fail (g_dbus_is_interface_name (interface), NULL);
	return g_variant_new ("(sos)", bus_name, object_path, interface);
}
