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

#include "realm-command.h"
#include "realm-dbus-constants.h"
#include "realm-diagnostics.h"
#include "realm-discovery.h"
#include "realm-errors.h"
#include "realm-ipa-discover.h"
#include "realm-kerberos-discover.h"
#include "realm-network.h"

#include <glib/gi18n.h>

typedef struct {
	gchar *string;
	GDBusMethodInvocation *invocation;
} Key;

typedef struct _Callback {
	GAsyncReadyCallback function;
	gpointer user_data;
	struct _Callback *next;
} Callback;

typedef struct {
	GObject parent;
	Key key;
	gchar *domain;
	GList *servers;
	gboolean found_kerberos;
	gint outstanding_kerberos;
	gboolean found_msdcs;
	gint outstanding_msdcs;
	gboolean found_ipa;
	gint outstanding_ipa;
	gboolean completed;
	GError *error;
	Callback *callback;
} RealmKerberosDiscover;

typedef struct {
	GObjectClass parent;
} RealmKerberosDiscoverClass;

#define REALM_TYPE_KERBEROS_DISCOVER  (realm_kerberos_discover_get_type ())
#define REALM_KERBEROS_DISCOVER(inst)  (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_KERBEROS_DISCOVER, RealmKerberosDiscover))
#define REALM_IS_KERBEROS_DISCOVER(inst)  (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_KERBEROS_DISCOVER))

static GHashTable *discover_cache = NULL;

GType realm_kerberos_discover_get_type (void) G_GNUC_CONST;

void  realm_kerberos_discover_async_result_init (GAsyncResultIface *iface);

G_DEFINE_TYPE_WITH_CODE (RealmKerberosDiscover, realm_kerberos_discover, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_RESULT, realm_kerberos_discover_async_result_init);
);

static void
realm_kerberos_discover_init (RealmKerberosDiscover *self)
{

}

static void
realm_kerberos_discover_finalize (GObject *obj)
{
	RealmKerberosDiscover *self = REALM_KERBEROS_DISCOVER (obj);

	g_object_unref (self->key.invocation);
	g_free (self->key.string);
	g_free (self->domain);
	if (self->servers)
		g_list_free_full (self->servers, (GDestroyNotify)g_srv_target_free);
	g_clear_error (&self->error);
	g_assert (self->callback == NULL);

	G_OBJECT_CLASS (realm_kerberos_discover_parent_class)->finalize (obj);
}

static void
realm_kerberos_discover_class_init (RealmKerberosDiscoverClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = realm_kerberos_discover_finalize;
}

static GObject *
realm_kerberos_discover_get_source_object (GAsyncResult *result)
{
	return g_object_ref (result);
}

static gpointer
realm_kerberos_discover_get_user_data (GAsyncResult *result)
{
	/* What does this do? */
	g_return_val_if_reached (NULL);
}

void
realm_kerberos_discover_async_result_init (GAsyncResultIface *iface)
{
	iface->get_source_object = realm_kerberos_discover_get_source_object;
	iface->get_user_data = realm_kerberos_discover_get_user_data;
}

typedef struct {
	Callback *clinger;
} DiscoverClosure;

static void
kerberos_discover_complete (RealmKerberosDiscover *self)
{
	Callback *call, *next;

	g_object_ref (self);

	g_assert (!self->completed);
	self->completed = TRUE;
	call = self->callback;
	self->callback = NULL;

	if (self->error == NULL && self->found_kerberos)
		realm_diagnostics_info (self->key.invocation, "Successfully discovered: %s", self->domain);

	while (call != NULL) {
		next = call->next;
		if (call->function)
			(call->function) (NULL, G_ASYNC_RESULT (self), call->user_data);
		g_slice_free (Callback, call);
		call = next;
	}

	g_object_unref (self);
}

static void
maybe_complete_discover (RealmKerberosDiscover *self)
{
	GDBusMethodInvocation *invocation = self->key.invocation;
	gboolean complete;

	/* If discovered either AD or IPA successfully, then complete */
	complete = (!self->outstanding_kerberos &&
	            (self->found_msdcs || self->found_ipa ||
	             (!self->outstanding_msdcs && !self->outstanding_ipa)));

	if (!complete)
		return;

	if (self->found_kerberos) {
		realm_diagnostics_info (invocation, "Found kerberos DNS records for: %s", self->domain);
		if (self->found_msdcs)
			realm_diagnostics_info (invocation, "Found AD style DNS records for: %s", self->domain);
		else if (self->found_ipa)
			realm_diagnostics_info (invocation, "Found IPA style certificate for: %s", self->domain);
	} else {
		realm_diagnostics_info (invocation, "Couldn't find kerberos DNS records for: %s", self->domain);
	}

	kerberos_discover_complete (self);
}

static void
on_discover_ipa (GObject *source,
                 GAsyncResult *result,
                 gpointer user_data)
{
	RealmKerberosDiscover *self = REALM_KERBEROS_DISCOVER (user_data);
	GError *error = NULL;

	g_assert (self->outstanding_ipa > 0);
	self->outstanding_ipa--;

	if (!self->completed && !self->found_ipa) {
		self->found_ipa = realm_ipa_discover_finish (result, &error);

		/*
		 * No errors from the IPA discovery are treated as discovery
		 * failures, but merely the abscence of IPA.
		 */
		if (error) {
			realm_diagnostics_error (self->key.invocation, error,
			                         "Couldn't discover IPA KDC");
			g_clear_error (&self->error);
		}

		maybe_complete_discover (self);
	}

	g_object_unref (self);
}

static void
on_resolve_kerberos (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	RealmKerberosDiscover *self = REALM_KERBEROS_DISCOVER (user_data);
	GDBusMethodInvocation *invocation = self->key.invocation;
	GError *error = NULL;
	GString *info;
	GList *l;
	gint i;

	self->outstanding_kerberos = 0;

	if (self->completed) {
		g_object_unref (self);
		return;
	}

	self->servers = g_resolver_lookup_service_finish (G_RESOLVER (source), result, &error);

	/* We don't treat 'host not found' or 'temporarily unable to resolve' as errors */
	if (g_error_matches (error, G_RESOLVER_ERROR, G_RESOLVER_ERROR_NOT_FOUND) ||
	    g_error_matches (error, G_RESOLVER_ERROR, G_RESOLVER_ERROR_TEMPORARY_FAILURE))
		g_clear_error (&error);

	if (error == NULL) {
		info = g_string_new ("");

		for (l = self->servers; l != NULL; l = g_list_next (l)) {
			self->found_kerberos = TRUE;
			g_string_append_printf (info, "%s:%d ", g_srv_target_get_hostname (l->data),
			                        (int)g_srv_target_get_port (l->data));
		}

		if (self->found_kerberos)
			realm_diagnostics_info (invocation, "%s", info->str);

		g_string_free (info, TRUE);

		/* Unless sure domain is AD, start IPA discovery, for first N KDCs */
		if (!self->found_msdcs) {
			for (l = self->servers, i = 0; l != NULL && i < 3; l = g_list_next (l), i++) {
				realm_ipa_discover_async (l->data, invocation,
				                          on_discover_ipa, g_object_ref (self));
				self->outstanding_ipa++;
			}
		}

	} else {
		realm_diagnostics_error (invocation, error, "Couldn't lookup SRV records for domain");
		g_clear_error (&self->error);
		self->error = error;
	}

	maybe_complete_discover (self);
	g_object_unref (self);
}

static void
on_resolve_msdcs (GObject *source,
                  GAsyncResult *result,
                  gpointer user_data)
{
	RealmKerberosDiscover *self = REALM_KERBEROS_DISCOVER (user_data);
	GDBusMethodInvocation *invocation = self->key.invocation;
	GResolver *resolver = G_RESOLVER (source);
	GError *error = NULL;
	GList *records;

	self->outstanding_msdcs = 0;

	if (self->completed) {
		g_object_unref (self);
		return;
	}

	records = g_resolver_lookup_service_finish (resolver, result, &error);

	/* We don't treat 'host not found' or 'temporarily unable to resolve' as errors */
	if (g_error_matches (error, G_RESOLVER_ERROR, G_RESOLVER_ERROR_NOT_FOUND) ||
	    g_error_matches (error, G_RESOLVER_ERROR, G_RESOLVER_ERROR_TEMPORARY_FAILURE))
		g_clear_error (&error);

	if (error == NULL) {
		self->found_msdcs = (records != NULL);
		g_list_free_full (records, (GDestroyNotify)g_srv_target_free);

	} else {
		realm_diagnostics_error (invocation, error, "Failure to lookup domain MSDCS records");
		g_clear_error (&self->error);
		self->error = error;
	}

	maybe_complete_discover (self);
	g_object_unref (self);
}

static void
kerberos_discover_domain_begin (RealmKerberosDiscover *self)
{
	GDBusMethodInvocation *invocation = self->key.invocation;
	GResolver *resolver;
	gchar *msdcs;

	g_assert (self->domain != NULL);

	realm_diagnostics_info (invocation,
	                        "Searching for kerberos SRV records for domain: _kerberos._udp.%s",
	                        self->domain);

	resolver = g_resolver_get_default ();
	g_resolver_lookup_service_async (resolver, "kerberos", "udp", self->domain, NULL,
	                                 on_resolve_kerberos, g_object_ref (self));
	self->outstanding_kerberos = 1;

	/* Active Directory DNS zones have this subzone */
	msdcs = g_strdup_printf ("dc._msdcs.%s", self->domain);

	realm_diagnostics_info (invocation,
	                        "Searching for MSDCS SRV records on domain: _kerberos._tcp.%s",
	                        msdcs);

	g_resolver_lookup_service_async (resolver, "kerberos", "tcp", msdcs, NULL,
	                                 on_resolve_msdcs, g_object_ref (self));
	self->outstanding_msdcs = 1;

	g_free (msdcs);

	g_object_unref (resolver);
}

static void
on_get_dhcp_domain (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	RealmKerberosDiscover *self = REALM_KERBEROS_DISCOVER (user_data);
	GDBusMethodInvocation *invocation = self->key.invocation;
	GError *error = NULL;

	self->domain = realm_network_get_dhcp_domain_finish (result, &error);
	if (error != NULL) {
		realm_diagnostics_error (invocation, error, "Failure to lookup DHCP domain");
		g_error_free (error);
	}

	if (self->domain) {
		realm_diagnostics_info (invocation, "Discovering for DHCP domain: %s", self->domain);
		kerberos_discover_domain_begin (self);
	} else {
		realm_diagnostics_info (invocation, "No DHCP domain available");
		kerberos_discover_complete (self);
	}

	g_object_unref (self);
}

static inline guint
str_hash0 (gconstpointer p)
{
	return p == NULL ? 0 : g_str_hash (p);
}

static guint
discover_key_hash (gconstpointer p)
{
	const Key *key = p;

	return str_hash0 (key->string) ^
	       str_hash0 (realm_diagnostics_get_operation_id (key->invocation)) ^
	       str_hash0 (g_dbus_method_invocation_get_sender (key->invocation));
}

static gboolean
discover_key_equal (gconstpointer v1,
                    gconstpointer v2)
{
	const Key *k1 = v1;
	const Key *k2 = v2;

	return g_strcmp0 (k1->string, k2->string) == 0 &&
	       g_strcmp0 (realm_diagnostics_get_operation_id (k1->invocation),
	                  realm_diagnostics_get_operation_id (k2->invocation)) == 0 &&
	       g_strcmp0 (g_dbus_method_invocation_get_sender (k1->invocation),
	                  g_dbus_method_invocation_get_sender (k2->invocation)) == 0;
}

static gboolean
on_timeout_remove_cache (gpointer user_data)
{
	Key *key = user_data;

	if (discover_cache != NULL) {
		g_hash_table_remove (discover_cache, key);
		if (g_hash_table_size (discover_cache) == 0) {
			g_hash_table_destroy (discover_cache);
			discover_cache = NULL;
		}
	}

	return FALSE;
}

static gboolean
on_idle_complete (gpointer user_data)
{
	RealmKerberosDiscover *self = REALM_KERBEROS_DISCOVER (user_data);
	g_assert (self->completed);
	kerberos_discover_complete (self);
	return FALSE;
}

void
realm_kerberos_discover_async (const gchar *string,
                               GDBusMethodInvocation *invocation,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	GDBusConnection *connection;
	RealmKerberosDiscover *self;
	Callback *call;
	gchar *domain;
	Key key;

	g_return_if_fail (string != NULL);
	g_return_if_fail (invocation == NULL || G_IS_DBUS_METHOD_INVOCATION (invocation));

	key.string = (gchar *)string;
	key.invocation = invocation;

	if (!discover_cache) {
		discover_cache = g_hash_table_new_full (discover_key_hash, discover_key_equal,
		                                        NULL, g_object_unref);
	}

	self = g_hash_table_lookup (discover_cache, &key);

	if (self == NULL) {
		self = g_object_new (REALM_TYPE_KERBEROS_DISCOVER, NULL);
		self->key.string = g_strdup (string);
		self->key.invocation = g_object_ref (invocation);

		if (g_str_equal (string, "")) {
			connection = g_dbus_method_invocation_get_connection (invocation);
			realm_diagnostics_info (invocation, "Looking up our DHCP domain");
			realm_network_get_dhcp_domain_async (connection, on_get_dhcp_domain,
			                                     g_object_ref (self));

		} else {
			domain = g_ascii_strdown (string, -1);
			g_strstrip (domain);
			self->domain = domain;
			kerberos_discover_domain_begin (self);
		}

		g_hash_table_insert (discover_cache, &self->key, self);
		g_timeout_add_seconds (5, on_timeout_remove_cache, &self->key);
		g_assert (!self->completed);

	} else if (self->completed) {
		g_idle_add_full (G_PRIORITY_DEFAULT, on_idle_complete,
		                 g_object_ref (self), g_object_unref);
	}

	call = g_slice_new0 (Callback);
	call->function = callback;
	call->user_data = user_data;
	call->next = self->callback;
	self->callback = call;
}

gchar *
realm_kerberos_discover_finish (GAsyncResult *result,
                                GHashTable **discovery,
                                GError **error)
{
	RealmKerberosDiscover *self;
	gchar *realm;

	g_return_val_if_fail (REALM_IS_KERBEROS_DISCOVER (result), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	self = REALM_KERBEROS_DISCOVER (result);

	/* A failure */
	if (self->error) {
		if (error)
			*error = g_error_copy (self->error);
		return NULL;
	}

	/* Didn't find a valid domain */
	if (!self->found_kerberos)
		return NULL;

	realm = g_ascii_strup (self->domain, -1);

	if (discovery) {
		*discovery = realm_discovery_new ();

		/* The domain */
		realm_discovery_add_string (*discovery, REALM_DBUS_DISCOVERY_DOMAIN,
		                            self->domain);

		/* The realm */
		realm_discovery_add_string (*discovery, REALM_DBUS_DISCOVERY_REALM, realm);

		/* The servers */
		realm_discovery_add_srv_targets (*discovery, REALM_DBUS_DISCOVERY_KDCS,
		                                 self->servers);

		/* The type */
		if (self->found_msdcs) {
			realm_discovery_add_string (*discovery,
			                            REALM_DBUS_OPTION_SERVER_SOFTWARE,
			                            REALM_DBUS_IDENTIFIER_ACTIVE_DIRECTORY);

		} else if (self->found_ipa) {
			realm_discovery_add_string (*discovery,
			                            REALM_DBUS_OPTION_SERVER_SOFTWARE,
			                            REALM_DBUS_IDENTIFIER_FREEIPA);
		}
	}

	return realm;
}
