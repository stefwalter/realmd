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

#include "realm-ad-discover.h"
#include "realm-dbus-constants.h"
#include "realm-diagnostics.h"
#include "realm-discovery.h"
#include "realm-errors.h"
#include "realm-command.h"

#include <glib/gi18n.h>

/* TODO: Move this elsewhere */
#define HOST_PATH "/usr/bin/host"

typedef struct {
	GDBusMethodInvocation *invocation;
	GHashTable *discovery;
	gchar *domain;
	GVariant *servers;
	gboolean found_kerberos_srv;
	gboolean finished_srv;
	gboolean found_msdcs_soa;
	gboolean finished_soa;
} DiscoverClosure;

static void
discover_closure_free (gpointer data)
{
	DiscoverClosure *discover = data;

	g_object_unref (discover->invocation);
	g_hash_table_unref (discover->discovery);
	g_free (discover->domain);
	g_variant_unref (discover->servers);

	g_slice_free (DiscoverClosure, discover);
}

static void
maybe_complete_discover (GSimpleAsyncResult *res,
                         DiscoverClosure *discover)
{
	if (!discover->finished_srv || !discover->finished_soa)
		return;

	if (discover->found_kerberos_srv && discover->found_msdcs_soa)
		realm_diagnostics_info (discover->invocation, "Found AD style DNS records on domain");
	else
		realm_diagnostics_info (discover->invocation, "Couldn't find AD style DNS records on domain");

	g_simple_async_result_complete (res);
}

static void
on_resolve_kerberos_srv (GObject *source,
                         GAsyncResult *result,
                         gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	DiscoverClosure *discover = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	GPtrArray *servers;
	GString *info;
	GList *targets;
	gchar *server;
	GList *l;

	targets = g_resolver_lookup_service_finish (G_RESOLVER (source),
	                                            result, &error);

	/* We don't treat 'host not found' as an error */
	if (g_error_matches (error, G_RESOLVER_ERROR, G_RESOLVER_ERROR_NOT_FOUND))
		g_clear_error (&error);

	if (error == NULL) {
		info = g_string_new ("");
		servers = g_ptr_array_new ();

		for (l = targets; l != NULL; l = g_list_next (l)) {
			discover->found_kerberos_srv = TRUE;
			server = g_strdup_printf ("%s:%d", g_srv_target_get_hostname (l->data),
			                          (int)g_srv_target_get_port (l->data));
			g_ptr_array_add (servers, g_variant_new_string (server));
			g_string_append_printf (info, "%s\n", server);
			g_free (server);
			g_srv_target_free (l->data);
		}

		g_list_free (targets);

		if (discover->found_kerberos_srv)
			realm_diagnostics_info (discover->invocation, "%s", info->str);
		else
			realm_diagnostics_info (discover->invocation, "no kerberos SRV records");

		g_string_free (info, TRUE);

		discover->servers = g_variant_new_array (G_VARIANT_TYPE_STRING_ARRAY,
		                                         (GVariant * const*)servers->pdata,
		                                         servers->len);

		g_variant_ref_sink (discover->servers);
		g_ptr_array_free (servers, TRUE);

	} else {
		realm_diagnostics_error (discover->invocation, error, "Couldn't lookup SRV records for domain");
		g_simple_async_result_take_error (res, error);
	}

	discover->finished_srv = TRUE;
	maybe_complete_discover (res, discover);
	g_object_unref (res);
}

static void
on_resolve_msdcs_soa (GObject *source,
                      GAsyncResult *result,
                      gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	DiscoverClosure *discover = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	gint exit_code;

	exit_code = realm_command_run_finish (result, NULL, &error);
	if (error == NULL) {
		discover->found_msdcs_soa = (exit_code == 0);

	} else {
		realm_diagnostics_error (discover->invocation, error, "Couldn't use the host command to find domain SOA record");
		g_simple_async_result_take_error (res, error);
	}

	discover->finished_soa = TRUE;
	maybe_complete_discover (res, discover);
	g_object_unref (res);
}

void
realm_ad_discover_async (RealmKerberosProvider *provider,
                         const gchar *string,
                         GDBusMethodInvocation *invocation,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	GSimpleAsyncResult *res;
	GResolver *resolver;
	DiscoverClosure *discover;
	gchar *domain;
	gchar *msdcs;

	g_return_if_fail (REALM_IS_KERBEROS_PROVIDER (provider));
	g_return_if_fail (string != NULL);
	g_return_if_fail (invocation == NULL || G_IS_DBUS_METHOD_INVOCATION (invocation));

	res = g_simple_async_result_new (G_OBJECT (provider), callback, user_data,
	                                 realm_ad_discover_async);

	domain = g_ascii_strdown (string, -1);
	g_strstrip (domain);

	discover = g_slice_new0 (DiscoverClosure);
	discover->invocation = g_object_ref (invocation);
	discover->discovery = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                             g_free, (GDestroyNotify)g_variant_unref);
	discover->domain = domain;
	g_simple_async_result_set_op_res_gpointer (res, discover, discover_closure_free);

	realm_diagnostics_info (invocation, "searching for kerberos SRV records on %s domain", domain);

	resolver = g_resolver_get_default ();
	g_resolver_lookup_service_async (resolver, "kerberos", "udp", domain, NULL,
	                                 on_resolve_kerberos_srv, g_object_ref (res));
	g_object_unref (resolver);

	realm_diagnostics_info (invocation, "searching for _msdcs zone on %s domain", domain);

	/* Active Directory DNS zones have this subzone */
	msdcs = g_strdup_printf ("_msdcs.%s", domain);

	realm_command_run_async (NULL, invocation, NULL, on_resolve_msdcs_soa, g_object_ref (res),
	                      HOST_PATH, "-t", "SOA", msdcs, NULL);

	g_free (msdcs);
}

gchar *
realm_ad_discover_finish (RealmKerberosProvider *provider,
                          GAsyncResult *result,
                          GHashTable *discovery,
                          GError **error)
{
	GSimpleAsyncResult *res;
	DiscoverClosure *discover;
	gchar *realm;

	g_return_val_if_fail (REALM_IS_KERBEROS_PROVIDER (provider), NULL);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (provider),
	                      realm_ad_discover_async), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	res = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (res, error))
		return NULL;

	discover = g_simple_async_result_get_op_res_gpointer (res);

	/* Didn't find a valid domain */
	if (!discover->found_kerberos_srv || !discover->found_msdcs_soa)
		return NULL;

	/* The domain */
	realm_discovery_add_string (discovery, REALM_DBUS_DISCOVERY_DOMAIN, discover->domain);

	/* The realm */
	realm = g_ascii_strup (discover->domain, -1);
	realm_discovery_add_string (discovery, REALM_DBUS_DISCOVERY_REALM, realm);

	/* The servers */
	realm_discovery_add_variant (discovery, REALM_DBUS_DISCOVERY_SERVERS, discover->servers);

	/* The type */
	realm_discovery_add_string (discovery, REALM_DBUS_DISCOVERY_TYPE, "kerberos-ad");

	return realm;
}
