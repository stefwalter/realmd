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
#include "realm-settings.h"

#include <glib/gi18n.h>
#include <gio/gio.h>

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
	GVariant *retval;
	GError *error = NULL;
	GVariant *realms = NULL;
	gint relevance;

	relevance = realm_provider_discover_finish (closure->self, result, &realms, &error);
	if (error == NULL) {
		retval = g_variant_new ("(i@a(os))", relevance, realms);
		g_dbus_method_invocation_return_value (closure->invocation, retval);
		g_variant_unref (realms);
	} else {
		if (error->domain == REALM_ERROR || error->domain == G_DBUS_ERROR) {
			g_dbus_error_strip_remote_error (error);
			realm_diagnostics_error (closure->invocation, error, NULL);
			g_dbus_method_invocation_return_gerror (closure->invocation, error);

		} else {
			realm_diagnostics_error (closure->invocation, error, "Failed to discover realm");
			g_dbus_method_invocation_return_error (closure->invocation, REALM_ERROR, REALM_ERROR_DISCOVERY_FAILED,
			                                       _("Failed to discover realm. See diagnostics."));
		}
		g_error_free (error);
	}

	method_closure_free (closure);
}

static gboolean
realm_provider_handle_discover (RealmDbusProvider *provider,
                                GDBusMethodInvocation *invocation,
                                const gchar *string,
                                GVariant *options)
{
	RealmProvider *self = REALM_PROVIDER (provider);

	/* Make note of the current operation id, for diagnostics */
	realm_diagnostics_setup_options (invocation, options);

	realm_provider_discover (self, string, invocation, on_discover_complete,
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
	gboolean ret = FALSE;

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
		                                            _("Not authorized to perform this action"));
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

	/* The dbus version property of the provider */
	g_object_set (self, "version", VERSION, NULL);
}

static void
update_realms_property (RealmProvider *self)
{
	GHashTableIter iter;
	GDBusInterfaceSkeleton *realm;
	GVariantBuilder builder;
	const gchar *path;
	GVariant *variant;

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(os)"));

	g_hash_table_iter_init (&iter, self->pv->realms);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer)&realm)) {
		path = g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (realm));
		g_variant_builder_add (&builder, "(os)", path, REALM_DBUS_KERBEROS_REALM_INTERFACE);
	}

	variant = g_variant_builder_end (&builder);
	g_object_set (self, "realms", g_variant_ref_sink (variant), NULL);
	g_variant_unref (variant);
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
realm_provider_new_realm_info (const gchar *object_path,
                               const gchar *interface)
{
	g_return_val_if_fail (g_variant_is_object_path (object_path), NULL);
	g_return_val_if_fail (g_dbus_is_interface_name (interface), NULL);
	return g_variant_new ("(os)", object_path, interface);
}

static void
export_realm_if_possible (RealmProvider *self,
                          GDBusInterfaceSkeleton *realm)
{
	GDBusConnection *connection;
	static gint unique_number = 0;
	const char *provider_path;
	GError *error = NULL;
	gchar *realm_name;
	gchar *escaped;
	gchar *path;

	connection = g_dbus_interface_skeleton_get_connection (G_DBUS_INTERFACE_SKELETON (self));
	if (connection == NULL)
		return;
	if (g_dbus_interface_skeleton_has_connection (realm, connection))
		return;

	g_object_get (realm, "name", &realm_name, NULL);
	escaped = g_strdup (realm_name);
	g_strcanon (escaped, REALM_DBUS_NAME_CHARS, '_');
	g_free (realm_name);

	provider_path = g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self));
	path = g_strdup_printf ("%s/%s_%d", provider_path, escaped, ++unique_number);
	g_free (escaped);
	g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (realm),
	                                  connection, path, &error);
	g_free (path);

	if (error != NULL) {
		g_warning ("couldn't export realm on dbus connection: %s",
		           error->message);
		g_error_free (error);
	}

	update_realms_property (self);
}

GDBusInterfaceSkeleton *
realm_provider_lookup_or_register_realm (RealmProvider *self,
                                         GType realm_type,
                                         const gchar *realm_name)
{
	GDBusInterfaceSkeleton *realm;

	realm = g_hash_table_lookup (self->pv->realms, realm_name);
	if (realm != NULL)
		return realm;

	realm = g_object_new (realm_type,
	                      "name", realm_name,
	                      "provider", self, NULL);

	export_realm_if_possible (self, realm);

	g_hash_table_insert (self->pv->realms, g_strdup (realm_name), realm);
	return realm;
}

static void
provider_object_start (GDBusConnection *connection,
                       RealmProvider *self)
{
	GDBusInterfaceSkeleton *realm;
	RealmProviderClass *provider_class;
	GError *error = NULL;
	GHashTableIter iter;

	provider_class = REALM_PROVIDER_GET_CLASS (self);

	g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
	                                  connection, provider_class->dbus_path,
	                                  &error);
	if (error != NULL) {
		g_warning ("Couldn't export new realm provider: %s: %s",
		           G_OBJECT_TYPE_NAME (self), error->message);
		g_error_free (error);
		return;
	}

	g_hash_table_iter_init (&iter, self->pv->realms);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&realm))
		export_realm_if_possible (self, realm);
}

RealmProvider *
realm_provider_start (GDBusConnection *connection,
                      GType type)
{
	RealmProvider *provider;
	GError *error = NULL;
	g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
	g_return_val_if_fail (g_type_is_a (type, REALM_TYPE_PROVIDER), NULL);

	if (g_type_is_a (type, G_TYPE_INITABLE)) {
		provider = g_initable_new (type, NULL, &error, NULL);
		if (error == NULL) {
			provider_object_start (connection, provider);
			return provider;

		} else {
			g_warning ("Failed to initialize realm provider: %s: %s",
			           g_type_name (type), error->message);
			g_error_free (error);
			return NULL;
		}

	} else {
		provider = g_object_new (type, NULL);
		provider_object_start (connection, provider);
		return provider;
	}
}

gboolean
realm_provider_is_default (const gchar *type,
                           const gchar *name)
{
	gboolean result;
	gchar *client;

	client = g_ascii_strdown (realm_settings_string (type, "default-client"), -1);
	result = client != NULL && strstr (client, name);
	g_free (client);

	return result;
}

void
realm_provider_discover (RealmProvider *self,
                         const gchar *string,
                         GDBusMethodInvocation *invocation,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	RealmProviderClass *klass;

	klass = REALM_PROVIDER_GET_CLASS (self);
	g_return_if_fail (klass->discover_async != NULL);

	(klass->discover_async) (self, string, invocation, callback, user_data);
}

gint
realm_provider_discover_finish (RealmProvider *self,
                                GAsyncResult *result,
                                GVariant **realms,
                                GError **error)
{
	RealmProviderClass *klass;
	gint relevance;
	GError *sub_error = NULL;

	klass = REALM_PROVIDER_GET_CLASS (self);
	g_return_val_if_fail (klass->discover_finish != NULL, -1);

	*realms = NULL;

	relevance = (klass->discover_finish) (self, result, realms, &sub_error);
	if (sub_error == NULL) {
		if (realms != NULL && *realms == NULL) {
			*realms =  g_variant_new_array (G_VARIANT_TYPE ("(os)"), NULL, 0);
			g_variant_ref_sink (*realms);
		}
	} else {
		g_propagate_error (error, sub_error);
	}

	return relevance;
}
