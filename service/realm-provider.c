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
#include "realm-kerberos.h"
#include "realm-provider.h"
#include "realm-settings.h"

#include <glib/gi18n.h>
#include <gio/gio.h>

G_DEFINE_TYPE (RealmProvider, realm_provider, G_TYPE_DBUS_OBJECT_SKELETON);

struct _RealmProviderPrivate {
	GHashTable *realms;
	RealmDbusProvider *provider_iface;
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
		retval = g_variant_new ("(i@ao)", relevance, realms);
		g_dbus_method_invocation_return_value (closure->invocation, retval);
		g_variant_unref (realms);
	} else {
		if (error->domain == REALM_ERROR || error->domain == G_DBUS_ERROR) {
			g_dbus_error_strip_remote_error (error);
			realm_diagnostics_error (closure->invocation, error, NULL);
			g_dbus_method_invocation_return_gerror (closure->invocation, error);

		} else {
			realm_diagnostics_error (closure->invocation, error, "Failed to discover realm");
			g_dbus_method_invocation_return_error (closure->invocation, REALM_ERROR, REALM_ERROR_FAILED,
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
                                GVariant *options,
                                gpointer user_data)
{
	RealmProvider *self = REALM_PROVIDER (user_data);

	/* Make note of the current operation id, for diagnostics */
	realm_diagnostics_setup_options (invocation, options);

	realm_provider_discover (self, string, invocation, on_discover_complete,
	                         method_closure_new (self, invocation));

	return TRUE;
}

static gboolean
realm_provider_authorize_method (GDBusObjectSkeleton *skeleton,
                                 GDBusInterfaceSkeleton *iface,
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

	self->pv->provider_iface = realm_dbus_provider_skeleton_new ();
	g_signal_connect (self->pv->provider_iface, "handle-discover",
	                  G_CALLBACK (realm_provider_handle_discover), self);
	g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (self),
	                                      G_DBUS_INTERFACE_SKELETON (self->pv->provider_iface));
}

static void
realm_provider_constructed (GObject *obj)
{
	RealmProvider *self = REALM_PROVIDER (obj);

	G_OBJECT_CLASS (realm_provider_parent_class)->constructed (obj);

	/* The dbus version property of the provider */
	realm_dbus_provider_set_version (self->pv->provider_iface, VERSION);
}

static void
update_realms_property (RealmProvider *self)
{
	GHashTableIter iter;
	GDBusObject *realm;
	GVariantBuilder builder;
	GPtrArray *realms;

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("ao"));

	realms = g_ptr_array_new ();
	g_hash_table_iter_init (&iter, self->pv->realms);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer)&realm))
		g_ptr_array_add (realms, (gpointer)g_dbus_object_get_object_path (realm));

	g_ptr_array_add (realms, NULL);

	realm_provider_set_realms (self, (const gchar **)realms->pdata);
	g_ptr_array_free (realms, TRUE);
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
	GDBusObjectSkeletonClass *skeleton_class = G_DBUS_OBJECT_SKELETON_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = realm_provider_constructed;
	object_class->finalize = realm_provider_finalize;
	skeleton_class->authorize_method = realm_provider_authorize_method;

	g_type_class_add_private (klass, sizeof (RealmProviderPrivate));
}

RealmKerberos *
realm_provider_lookup_or_register_realm (RealmProvider *self,
                                         GType realm_type,
                                         const gchar *realm_name,
                                         GHashTable *discovery)
{
	RealmKerberos *realm;
	static gint unique_number = 0;
	const gchar *provider_path;
	gchar *escaped;
	gchar *path;

	realm = g_hash_table_lookup (self->pv->realms, realm_name);
	if (realm != NULL) {
		if (discovery != NULL)
			realm_kerberos_set_discovery (realm, discovery);
		return realm;
	}

	escaped = g_strdup (realm_name);
	g_strcanon (escaped, REALM_DBUS_NAME_CHARS, '_');

	provider_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (self));
	path = g_strdup_printf ("%s/%s_%d", provider_path, escaped, ++unique_number);
	g_free (escaped);

	realm = g_object_new (realm_type,
	                      "name", realm_name,
	                      "discovery", discovery,
	                      "provider", self,
	                      "g-object-path", path,
	                      NULL);

	realm_daemon_export_object (G_DBUS_OBJECT_SKELETON (realm));
	g_hash_table_insert (self->pv->realms, g_strdup (realm_name), realm);
	g_free (path);

	update_realms_property (self);
	g_signal_emit_by_name (self, "notify", NULL);

	return realm;
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
realm_provider_set_name (RealmProvider *self,
                         const gchar *value)
{
	g_return_if_fail (REALM_IS_PROVIDER (self));
	g_return_if_fail (value != NULL);
	realm_dbus_provider_set_name (self->pv->provider_iface, value);
}

const gchar **
realm_provider_get_realms (RealmProvider *self)
{
	g_return_val_if_fail (REALM_IS_PROVIDER (self), NULL);
	return (const gchar **)realm_dbus_provider_get_realms (self->pv->provider_iface);
}

void
realm_provider_set_realms (RealmProvider *self,
                           const gchar **value)
{
	g_return_if_fail (REALM_IS_PROVIDER (self));
	g_return_if_fail (value != NULL);
	realm_dbus_provider_set_realms (self->pv->provider_iface, value);
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
			*realms =  g_variant_new_array (G_VARIANT_TYPE ("o"), NULL, 0);
			g_variant_ref_sink (*realms);
		}
	} else {
		g_propagate_error (error, sub_error);
	}

	return relevance;
}
