/* identity-config - Identity configuration service
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

#include "ic-dbus-constants.h"
#include "ic-dbus-generated.h"
#include "ic-diagnostics.h"
#include "ic-discovery.h"
#include "ic-errors.h"
#include "ic-kerberos-provider.h"
#include "ic-service.h"

#include <glib/gi18n.h>

struct _IcKerberosProviderPrivate {
	GHashTable *cached_discovery;
};

G_DEFINE_TYPE (IcKerberosProvider, ic_kerberos_provider,
               IC_DBUS_TYPE_KERBEROS_SKELETON);

typedef struct {
	IcKerberosProvider *self;
	GDBusMethodInvocation *invocation;
} MethodClosure;

static MethodClosure *
method_closure_new (IcKerberosProvider *self,
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
	IcKerberosProviderClass *klass;
	GHashTable *discovery = NULL;
	GVariant *variant;
	GError *error = NULL;
	gchar *realm;

	klass = IC_KERBEROS_PROVIDER_GET_CLASS (closure->self);
	g_return_if_fail (klass->discover_finish != NULL);

	discovery = ic_discovery_new ();

	realm = (klass->discover_finish) (closure->self, result, discovery, &error);
	if (realm != NULL) {
		/* Cache this discovery information for later use */
		g_hash_table_insert (closure->self->pv->cached_discovery,
		                     g_strdup (realm), g_hash_table_ref (discovery));

		ic_diagnostics_info (closure->invocation, "Successfully discovered realm: %s", realm);
		variant = ic_discovery_to_variant (discovery);
		g_dbus_method_invocation_return_value (closure->invocation, g_variant_new ("(s@a{sv})", realm, variant));
		g_free (realm);

	} else if (error == NULL) {
		ic_diagnostics_info (closure->invocation, "The realm was not valid or not discoverable.");
		variant = g_variant_new_array (G_VARIANT_TYPE_DICTIONARY, NULL, 0);
		g_dbus_method_invocation_return_value (closure->invocation, g_variant_new ("(s@a{sv})", "", variant));

	} else {
		ic_diagnostics_error (closure->invocation, error, "Failed to discover realm.");
		g_dbus_method_invocation_return_error (closure->invocation, IC_ERROR, IC_ERROR_DISCOVERY_FAILED,
		                                       "Failed to discover realm. See diagnostics.");
		g_error_free (error);
	}

	g_hash_table_unref (discovery);
	method_closure_free (closure);
}

static gboolean
handle_discover_realm (IcKerberosProvider *self,
                       GDBusMethodInvocation *invocation,
                       const gchar *string,
                       gpointer unused)
{
	IcKerberosProviderClass *klass;

	klass = IC_KERBEROS_PROVIDER_GET_CLASS (self);
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
	IcKerberosProviderClass *klass;
	GError *error = NULL;

	klass = IC_KERBEROS_PROVIDER_GET_CLASS (closure->self);
	g_return_if_fail (klass->enroll_finish != NULL);

	(klass->enroll_finish) (closure->self, result, &error);
	if (error == NULL) {
		ic_diagnostics_info (closure->invocation, "Successfully enrolled machine in domain");
		g_dbus_method_invocation_return_value (closure->invocation, g_variant_new ("()"));

	} else {
		ic_diagnostics_error (closure->invocation, error, "Failed to enroll machine in domain");
		g_dbus_method_invocation_return_error (closure->invocation, IC_ERROR, IC_ERROR_ENROLL_FAILED,
		                                       "Failed to enroll machine in domain. See diagnostics.");
		g_error_free (error);
	}

	ic_service_unlock_for_action (closure->invocation);
	method_closure_free (closure);
}

static gboolean
handle_enroll_machine_with_kerberos_cache (IcKerberosProvider *self,
                                           GDBusMethodInvocation *invocation,
                                           const gchar *realm,
                                           GVariant *admin_cache,
                                           gpointer unused)
{
	GBytes *admin_kerberos_cache;
	IcKerberosProviderClass *klass;
	const guchar *data;
	gsize length;

	if (!ic_service_lock_for_action (invocation)) {
		g_dbus_method_invocation_return_error (invocation, IC_ERROR, IC_ERROR_BUSY,
		                                       "Already running another action");
		return TRUE;
	}

	klass = IC_KERBEROS_PROVIDER_GET_CLASS (self);
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
	IcKerberosProviderClass *klass;
	GError *error = NULL;

	klass = IC_KERBEROS_PROVIDER_GET_CLASS (closure->self);
	g_return_if_fail (klass->unenroll_finish != NULL);

	if ((klass->unenroll_finish) (closure->self, result, &error)) {
		ic_diagnostics_info (closure->invocation, "Successfully unenrolled machine from domain");
		g_dbus_method_invocation_return_value (closure->invocation, g_variant_new ("()"));
	} else {
		ic_diagnostics_error (closure->invocation, error, "Failed to unenroll machine from domain");
		g_dbus_method_invocation_return_error (closure->invocation, IC_ERROR, IC_ERROR_UNENROLL_FAILED,
		                                       "Failed to unenroll machine from domain. See diagnostics.");
		g_error_free (error);
	}

	ic_service_unlock_for_action (closure->invocation);
	method_closure_free (closure);
}

static gboolean
handle_unenroll_machine_with_kerberos_cache (IcKerberosProvider *self,
                                             GDBusMethodInvocation *invocation,
                                             const gchar *realm,
                                             GVariant *admin_cache,
                                             gpointer unused)
{
	IcKerberosProviderClass *klass;
	GBytes *admin_kerberos_cache;
	const guchar *data;
	gsize length;

	if (!ic_service_lock_for_action (invocation)) {
		g_dbus_method_invocation_return_error (invocation, IC_ERROR, IC_ERROR_BUSY,
		                                       "Already running another action");
		return TRUE;
	}

	klass = IC_KERBEROS_PROVIDER_GET_CLASS (self);
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

static void
on_set_logins_complete (GObject *source,
                        GAsyncResult *result,
                        gpointer user_data)
{
	MethodClosure *closure = user_data;
	IcKerberosProviderClass *klass;
	GError *error = NULL;

	klass = IC_KERBEROS_PROVIDER_GET_CLASS (closure->self);
	g_return_if_fail (klass->logins_finish != NULL);

	if ((klass->logins_finish) (closure->self, result, &error)) {
		ic_diagnostics_info (closure->invocation, "Successfully enabled/disabled logins");
		g_dbus_method_invocation_return_value (closure->invocation, g_variant_new ("()"));
	} else {
		ic_diagnostics_error (closure->invocation, error, "Failed to enable/disable logins");
		g_dbus_method_invocation_return_error (closure->invocation, IC_ERROR, IC_ERROR_SET_LOGINS_FAILED,
		                                       "Failed to configure logins. See diagnostics.");
		g_error_free (error);
	}

	ic_service_unlock_for_action (closure->invocation);
	method_closure_free (closure);
}

static gboolean
handle_set_logins_enabled (IcKerberosProvider *self,
                           GDBusMethodInvocation *invocation,
                           gboolean enabled,
                           gpointer unused)
{
	IcKerberosProviderClass *klass;

	if (!ic_service_lock_for_action (invocation)) {
		g_dbus_method_invocation_return_error (invocation, IC_ERROR, IC_ERROR_BUSY,
		                                       "Already running another action");
		return TRUE;
	}

	klass = IC_KERBEROS_PROVIDER_GET_CLASS (self);
	g_return_val_if_fail (klass->logins_async != NULL, FALSE);
	g_return_val_if_fail (klass->logins_finish != NULL, FALSE);

	(klass->logins_async) (self, enabled, invocation, on_set_logins_complete,
	                       method_closure_new (self, invocation));

	return TRUE;
}

static void
ic_kerberos_provider_init (IcKerberosProvider *self)
{
	self->pv = G_TYPE_INSTANCE_GET_PRIVATE (self, IC_TYPE_KERBEROS_PROVIDER,
	                                        IcKerberosProviderPrivate);

	self->pv->cached_discovery = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
	                                                    (GDestroyNotify)g_hash_table_unref);

	g_signal_connect (self, "discover-realm",
	                  G_CALLBACK (handle_discover_realm), NULL);
	g_signal_connect (self, "handle-enroll-machine-with-kerberos-cache",
	                  G_CALLBACK (handle_enroll_machine_with_kerberos_cache), NULL);
	g_signal_connect (self, "handle-unenroll-machine-with-kerberos-cache",
	                  G_CALLBACK (handle_unenroll_machine_with_kerberos_cache), NULL);
	g_signal_connect (self, "handle-set-logins-enabled",
	                  G_CALLBACK (handle_set_logins_enabled), NULL);
}

static void
ic_kerberos_provider_finalize (GObject *obj)
{
	IcKerberosProvider *self = IC_KERBEROS_PROVIDER (obj);

	g_hash_table_destroy (self->pv->cached_discovery);

	G_OBJECT_CLASS (ic_kerberos_provider_parent_class)->finalize (obj);
}

static void
ic_kerberos_provider_class_init (IcKerberosProviderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = ic_kerberos_provider_finalize;

	g_type_class_add_private (klass, sizeof (IcKerberosProviderPrivate));


}

GHashTable *
ic_kerberos_provider_lookup_discovery (IcKerberosProvider *self,
                                       const gchar *realm)
{
	GHashTable *discovery;

	g_return_val_if_fail (IC_IS_KERBEROS_PROVIDER (self), NULL);
	g_return_val_if_fail (realm != NULL, NULL);

	discovery = g_hash_table_lookup (self->pv->cached_discovery, realm);
	if (discovery != NULL)
		g_hash_table_ref (discovery);

	return discovery;
}
