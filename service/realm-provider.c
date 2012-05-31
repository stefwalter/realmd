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

struct _RealmProviderPrivate {
	GHashTable *realms;
};

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

	} else if (error == NULL) {
		realm_diagnostics_error (closure->invocation, NULL, "Nothing found during discovery");
		g_dbus_method_invocation_return_error (closure->invocation, REALM_ERROR,
		                                       REALM_ERROR_DISCOVERED_NOTHING,
		                                       "Nothing found during discovery");

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
realm_provider_authorize_method (GDBusInterfaceSkeleton *skeleton,
                                 GDBusMethodInvocation  *invocation)
{
	const gchar *interface = g_dbus_method_invocation_get_interface_name (invocation);
	const gchar *method = g_dbus_method_invocation_get_method_name (invocation);
	const gchar *action_id = NULL;
	gboolean ret;

	/* Each method has its own polkit authorization */
	if (g_str_equal (interface, REALM_DBUS_PROVIDER_INTERFACE)) {
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
	self->pv = G_TYPE_INSTANCE_GET_PRIVATE (self, REALM_TYPE_PROVIDER,
	                                        RealmProviderPrivate);
	self->pv->realms = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                          g_free, g_object_unref);
}

static void
update_realms_property (RealmProvider *self)
{
	GHashTableIter iter;
	GDBusInterfaceSkeleton *realm;
	GVariantBuilder builder;
	const gchar *path;
	GVariant *variant;

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(sos)"));

	g_hash_table_iter_init (&iter, self->pv->realms);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer)&realm)) {
		path = g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (realm));
		g_variant_builder_add (&builder, "(sos)", REALM_DBUS_SAMBA_NAME, path,
		                       REALM_DBUS_KERBEROS_REALM_INTERFACE);
	}

	variant = g_variant_builder_end (&builder);
	g_object_set (self, "realms", g_variant_ref_sink (variant), NULL);
	g_variant_unref (variant);
}

static void
realm_provider_constructed (GObject *obj)
{
	RealmProvider *self = REALM_PROVIDER (obj);

	G_OBJECT_CLASS (realm_provider_parent_class)->constructed (obj);

	update_realms_property (self);
}

static void
realm_provider_finalize (GObject *obj)
{
	RealmProvider *self = REALM_PROVIDER (obj);

	g_hash_table_unref (self->pv->realms);

	G_OBJECT_CLASS (realm_provider_parent_class)->finalize (obj);
}

static void
realm_provider_class_init (RealmProviderClass *klass)
{
	GDBusInterfaceSkeletonClass *skeleton_class = G_DBUS_INTERFACE_SKELETON_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = realm_provider_constructed;
	object_class->finalize = realm_provider_finalize;
	skeleton_class->g_authorize_method = realm_provider_authorize_method;

	g_type_class_add_private (klass, sizeof (RealmProviderPrivate));
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

GDBusInterfaceSkeleton *
realm_provider_lookup_or_register_realm (RealmProvider *self,
                                         GType realm_type,
                                         const gchar *realm_name)
{
	GDBusInterfaceSkeleton *realm;
	GDBusConnection *connection;
	static gint unique_number = 0;
	const char *provider_path;
	GError *error = NULL;
	gchar *escaped;
	gchar *path;

	realm = g_hash_table_lookup (self->pv->realms, realm_name);
	if (realm != NULL)
		return realm;

	realm = g_object_new (realm_type,
	                      "name", realm_name,
	                      "provider", self, NULL);

	escaped = g_strdup (realm_name);
	g_strcanon (escaped, REALM_DBUS_NAME_CHARS, '_');

	provider_path = g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self));
	connection = g_dbus_interface_skeleton_get_connection (G_DBUS_INTERFACE_SKELETON (self));

	path = g_strdup_printf ("%s/%s_%d", provider_path, escaped, ++unique_number);
	g_free (escaped);

	g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (realm),
	                                  connection, path, &error);
	g_free (path);

	if (error == NULL) {
		g_hash_table_insert (self->pv->realms, g_strdup (realm_name), realm);
		update_realms_property (self);

	} else {
		g_warning ("couldn't export realm on dbus connection: %s",
		           error->message);
		g_object_unref (realm);
		realm = NULL;
	}

	return realm;
}
