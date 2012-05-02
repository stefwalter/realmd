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
#include "realm-kerberos-provider.h"

#include <glib/gi18n.h>

struct _RealmKerberosProviderPrivate {
	GHashTable *cached_discovery;
};

G_DEFINE_TYPE (RealmKerberosProvider, realm_kerberos_provider,
               REALM_DBUS_TYPE_KERBEROS_SKELETON);

typedef struct {
	RealmKerberosProvider *self;
	GDBusMethodInvocation *invocation;
} MethodClosure;

static MethodClosure *
method_closure_new (RealmKerberosProvider *self,
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
	RealmKerberosProviderClass *klass;
	GHashTable *discovery = NULL;
	GVariant *variant;
	GError *error = NULL;
	gchar *realm;

	klass = REALM_KERBEROS_PROVIDER_GET_CLASS (closure->self);
	g_return_if_fail (klass->discover_finish != NULL);

	discovery = realm_discovery_new ();

	realm = (klass->discover_finish) (closure->self, result, discovery, &error);
	if (realm != NULL) {
		/* Cache this discovery information for later use */
		g_hash_table_insert (closure->self->pv->cached_discovery,
		                     g_strdup (realm), g_hash_table_ref (discovery));

		realm_diagnostics_info (closure->invocation, "Successfully discovered realm: %s", realm);
		variant = realm_discovery_to_variant (discovery);
		g_dbus_method_invocation_return_value (closure->invocation, g_variant_new ("(s@a{sv})", realm, variant));
		g_free (realm);

	} else if (error == NULL) {
		realm_diagnostics_info (closure->invocation, "The realm was not valid or not discoverable.");
		variant = g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0);
		g_dbus_method_invocation_return_value (closure->invocation, g_variant_new ("(s@a{sv})", "", variant));

	} else {
		realm_diagnostics_error (closure->invocation, error, "Failed to discover realm.");
		g_dbus_method_invocation_return_error (closure->invocation, REALM_ERROR, REALM_ERROR_DISCOVERY_FAILED,
		                                       "Failed to discover realm. See diagnostics.");
		g_error_free (error);
	}

	g_hash_table_unref (discovery);
	method_closure_free (closure);
}

static gboolean
handle_discover_realm (RealmKerberosProvider *self,
                       GDBusMethodInvocation *invocation,
                       const gchar *string,
                       gpointer unused)
{
	RealmKerberosProviderClass *klass;

	klass = REALM_KERBEROS_PROVIDER_GET_CLASS (self);
	g_return_val_if_fail (klass->discover_async != NULL, FALSE);
	g_return_val_if_fail (klass->discover_finish != NULL, FALSE);

	(klass->discover_async) (self, string, invocation, on_discover_complete,
	                         method_closure_new (self, invocation));

	return TRUE;
}

static void
on_enroll_complete (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	MethodClosure *closure = user_data;
	RealmKerberosProviderClass *klass;
	GError *error = NULL;

	klass = REALM_KERBEROS_PROVIDER_GET_CLASS (closure->self);
	g_return_if_fail (klass->enroll_finish != NULL);

	(klass->enroll_finish) (closure->self, result, &error);
	if (error == NULL) {
		realm_diagnostics_info (closure->invocation, "Successfully enrolled machine in domain");
		g_dbus_method_invocation_return_value (closure->invocation, g_variant_new ("()"));

	} else {
		realm_diagnostics_error (closure->invocation, error, "Failed to enroll machine in domain");
		g_dbus_method_invocation_return_error (closure->invocation, REALM_ERROR, REALM_ERROR_ENROLL_FAILED,
		                                       "Failed to enroll machine in domain. See diagnostics.");
		g_error_free (error);
	}

	realm_daemon_unlock_for_action (closure->invocation);
	method_closure_free (closure);
}

static gboolean
handle_enroll_machine_with_kerberos_cache (RealmKerberosProvider *self,
                                           GDBusMethodInvocation *invocation,
                                           const gchar *realm,
                                           GVariant *admin_cache,
                                           gpointer unused)
{
	GBytes *admin_kerberos_cache;
	RealmKerberosProviderClass *klass;
	const guchar *data;
	gsize length;

	if (!realm_daemon_lock_for_action (invocation)) {
		g_dbus_method_invocation_return_error (invocation, REALM_ERROR, REALM_ERROR_BUSY,
		                                       "Already running another action");
		return TRUE;
	}

	klass = REALM_KERBEROS_PROVIDER_GET_CLASS (self);
	g_return_val_if_fail (klass->enroll_async != NULL, FALSE);
	g_return_val_if_fail (klass->enroll_finish != NULL, FALSE);

	data = g_variant_get_fixed_array (admin_cache, &length, 1);
	admin_kerberos_cache = g_bytes_new_with_free_func (data, length,
	                                                   (GDestroyNotify)g_variant_unref,
	                                                   g_variant_ref (admin_cache));

	(klass->enroll_async) (self, realm, admin_kerberos_cache, invocation,
	                       on_enroll_complete, method_closure_new (self, invocation));

	g_bytes_unref (admin_kerberos_cache);
	return TRUE;
}

static void
on_unenroll_complete (GObject *source,
                      GAsyncResult *result,
                      gpointer user_data)
{
	MethodClosure *closure = user_data;
	RealmKerberosProviderClass *klass;
	GError *error = NULL;

	klass = REALM_KERBEROS_PROVIDER_GET_CLASS (closure->self);
	g_return_if_fail (klass->unenroll_finish != NULL);

	if ((klass->unenroll_finish) (closure->self, result, &error)) {
		realm_diagnostics_info (closure->invocation, "Successfully unenrolled machine from domain");
		g_dbus_method_invocation_return_value (closure->invocation, g_variant_new ("()"));
	} else {
		realm_diagnostics_error (closure->invocation, error, "Failed to unenroll machine from domain");
		g_dbus_method_invocation_return_error (closure->invocation, REALM_ERROR, REALM_ERROR_UNENROLL_FAILED,
		                                       "Failed to unenroll machine from domain. See diagnostics.");
		g_error_free (error);
	}

	realm_daemon_unlock_for_action (closure->invocation);
	method_closure_free (closure);
}

static gboolean
handle_unenroll_machine_with_kerberos_cache (RealmKerberosProvider *self,
                                             GDBusMethodInvocation *invocation,
                                             const gchar *realm,
                                             GVariant *admin_cache,
                                             gpointer unused)
{
	RealmKerberosProviderClass *klass;
	GBytes *admin_kerberos_cache;
	const guchar *data;
	gsize length;

	if (!realm_daemon_lock_for_action (invocation)) {
		g_dbus_method_invocation_return_error (invocation, REALM_ERROR, REALM_ERROR_BUSY,
		                                       "Already running another action");
		return TRUE;
	}

	klass = REALM_KERBEROS_PROVIDER_GET_CLASS (self);
	g_return_val_if_fail (klass->unenroll_async != NULL, FALSE);
	g_return_val_if_fail (klass->unenroll_finish != NULL, FALSE);

	data = g_variant_get_fixed_array (admin_cache, &length, 1);
	admin_kerberos_cache = g_bytes_new_with_free_func (data, length,
	                                                   (GDestroyNotify)g_variant_unref,
	                                                   g_variant_ref (admin_cache));

	(klass->unenroll_async) (self, realm, admin_kerberos_cache, invocation,
	                         on_unenroll_complete, method_closure_new (self, invocation));

	g_bytes_unref (admin_kerberos_cache);
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
	} else if (g_str_equal (interface, REALM_DBUS_KERBEROS_INTERFACE)) {
		if (g_str_equal (method, "DiscoverRealm")) {
			action_id = "org.freedesktop.realmd.discover-realm";
		} else if (g_str_equal (method, "EnrollMachineWithKerberosCache")) {
			action_id = "org.freedesktop.realmd.enroll-machine";
		} else if (g_str_equal (method, "UnenrollMachineWithKerberosCache")) {
			action_id = "org.freedesktop.realmd.unenroll-machine";
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
realm_kerberos_provider_init (RealmKerberosProvider *self)
{
	self->pv = G_TYPE_INSTANCE_GET_PRIVATE (self, REALM_TYPE_KERBEROS_PROVIDER,
	                                        RealmKerberosProviderPrivate);

	self->pv->cached_discovery = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
	                                                    (GDestroyNotify)g_hash_table_unref);

	g_signal_connect (self, "g-authorize-method",
	                  G_CALLBACK (on_authorize_method), NULL);
	g_signal_connect (self, "handle-discover-realm",
	                  G_CALLBACK (handle_discover_realm), NULL);
	g_signal_connect (self, "handle-enroll-machine-with-kerberos-cache",
	                  G_CALLBACK (handle_enroll_machine_with_kerberos_cache), NULL);
	g_signal_connect (self, "handle-unenroll-machine-with-kerberos-cache",
	                  G_CALLBACK (handle_unenroll_machine_with_kerberos_cache), NULL);
}

static void
realm_kerberos_provider_finalize (GObject *obj)
{
	RealmKerberosProvider *self = REALM_KERBEROS_PROVIDER (obj);

	g_hash_table_destroy (self->pv->cached_discovery);

	G_OBJECT_CLASS (realm_kerberos_provider_parent_class)->finalize (obj);
}

static void
realm_kerberos_provider_class_init (RealmKerberosProviderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = realm_kerberos_provider_finalize;

	g_type_class_add_private (klass, sizeof (RealmKerberosProviderPrivate));


}

GHashTable *
realm_kerberos_provider_lookup_discovery (RealmKerberosProvider *self,
                                       const gchar *realm)
{
	GHashTable *discovery;

	g_return_val_if_fail (REALM_IS_KERBEROS_PROVIDER (self), NULL);
	g_return_val_if_fail (realm != NULL, NULL);

	discovery = g_hash_table_lookup (self->pv->cached_discovery, realm);
	if (discovery != NULL)
		g_hash_table_ref (discovery);

	return discovery;
}
