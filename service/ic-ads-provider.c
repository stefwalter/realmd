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

#include "ic-ads-provider.h"
#include "ic-dbus-constants.h"
#include "ic-dbus-generated.h"
#include "ic-diagnostics.h"
#include "ic-errors.h"
#include "ic-unix-process.h"

#include <glib/gi18n.h>

struct _IcAdsProvider {
	IcDbusProviderSkeleton parent;
};

typedef struct {
	IcDbusProviderSkeletonClass parent_class;
} IcAdsProviderClass;

static guint ads_provider_owner_id = 0;

G_DEFINE_TYPE (IcAdsProvider, ic_ads_provider, IC_DBUS_TYPE_PROVIDER_SKELETON);

const gchar *AD_SRV_RECORDS[] = { "_kerberos._tcp", "_kerberos._udp", "_msdcs", NULL };

typedef struct {
	GDBusMethodInvocation *invocation;
	gboolean found_kerberos_srv;
	gboolean finished_srv;
	gboolean found_msdcs_soa;
	gboolean finished_soa;
	gboolean failed;
} DiscoverClosure;

static void
discover_closure_free (gpointer data)
{
	DiscoverClosure *closure = data;
	g_object_unref (closure->invocation);
	g_slice_free (DiscoverClosure, closure);
}

static void
maybe_complete_discover (DiscoverClosure *closure)
{
	gint match;

	if (!closure->finished_srv || !closure->finished_soa)
		return;

	if (closure->failed) {
		g_dbus_method_invocation_return_error (closure->invocation,
		                                       IC_ERROR, IC_ERROR_DISCOVERY_FAILED,
		                                       _("Discovery of Active Directory domain failed."));
		discover_closure_free (closure);
		return;
	}

	if (closure->found_kerberos_srv && closure->found_msdcs_soa) {
		ic_diagnostics_info (closure->invocation, "Found AD style DNS records on domain");
		match = 100;
	} else {
		ic_diagnostics_info (closure->invocation, "Couldn't find AD style DNS records on domain");
		match = 0;
	}

	g_dbus_method_invocation_return_value (closure->invocation,
	                                       g_variant_new ("(i)", match));
	discover_closure_free (closure);
}

static void
on_resolve_kerberos_srv (GObject *source,
                         GAsyncResult *result,
                         gpointer user_data)
{
	DiscoverClosure *closure = user_data;
	GError *error = NULL;
	GList *targets = NULL;
	GList *l;
	GString *info;

	targets = g_resolver_lookup_service_finish (G_RESOLVER (source), result, &error);

	/* We don't treat 'host not found' as an error */
	if (g_error_matches (error, G_RESOLVER_ERROR, G_RESOLVER_ERROR_NOT_FOUND))
		g_clear_error (&error);

	if (error == NULL) {
		info = g_string_new ("");
		for (l = targets; l != NULL; l = g_list_next (l)) {
			closure->found_kerberos_srv = TRUE;
			g_string_append_printf (info, "%s:%d\n",
			                        g_srv_target_get_hostname (l->data),
			                        (int)g_srv_target_get_port (l->data));
			g_srv_target_free (l->data);
		}
		if (closure->found_kerberos_srv)
			ic_diagnostics_info (closure->invocation, "%s", info->str);
		else
			ic_diagnostics_info (closure->invocation, "no kerberos SRV records");
		g_string_free (info, TRUE);
	} else {
		ic_diagnostics_error (closure->invocation, error, "Couldn't lookup SRV records for domain");
		closure->failed = TRUE;
	}

	closure->finished_srv = TRUE;
	maybe_complete_discover (closure);
}

static void
on_resolve_msdcs_soa (GObject *source,
                      GAsyncResult *result,
                      gpointer user_data)
{
	DiscoverClosure *closure = user_data;
	IcUnixProcess *process = IC_UNIX_PROCESS (source);
	GError *error = NULL;
	GOutputStream *output;
	gint exit_code;
	gsize size;
	gchar *data;

	exit_code = ic_unix_process_run_finish (process, result, &error);
	if (error == NULL) {
		output = ic_unix_process_get_output_stream (process);
		g_output_stream_close (output, NULL, NULL);

		size = g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (output));
		data = g_memory_output_stream_steal_data (G_MEMORY_OUTPUT_STREAM (output));

		ic_diagnostics_info_data (closure->invocation, data, size);
		g_free (data);

		closure->found_msdcs_soa = (exit_code == 0);
	} else {
		ic_diagnostics_error (closure->invocation, error, "Couldn't use the host command to find domain SOA record");
		closure->failed = TRUE;
	}

	closure->finished_soa = TRUE;
	maybe_complete_discover (closure);
}

static gboolean
handle_discover_provider (IcAdsProvider *self,
                          GDBusMethodInvocation *invocation,
                          const gchar *string,
                          gpointer unused)
{
	GResolver *resolver;
	IcUnixProcess *process;
	DiscoverClosure *closure;
	GOutputStream *output;
	gchar *domain;
	gchar *msdcs;

	const gchar *arguments[] = {
		"-t",
		"SOA",
		NULL,
		NULL
	};

	domain = g_ascii_strdown (string, -1);
	g_strstrip (domain);

	closure = g_slice_new0 (DiscoverClosure);
	closure->invocation = g_object_ref (invocation);

	ic_diagnostics_info (invocation, "searching for kerberos SRV records on %s domain", domain);

	resolver = g_resolver_get_default ();
	g_resolver_lookup_service_async (resolver, "kerberos", "udp", domain, NULL,
	                                 on_resolve_kerberos_srv, closure);
	g_object_unref (resolver);

	ic_diagnostics_info (invocation, "searching for _msdcs zone on %s domain", domain);

	/* Active Directory DNS zones have this subzone */
	msdcs = g_strdup_printf ("_msdcs.%s", domain);
	arguments[2] = msdcs;

	process = ic_unix_process_new (NULL, HOST_PATH);
	output = g_memory_output_stream_new (NULL, 0, g_realloc, g_free);
	ic_unix_process_set_output_stream (process, output);
	ic_unix_process_set_error_stream (process, output);
	ic_unix_process_run_async (process, arguments, NULL, NULL, on_resolve_msdcs_soa, closure);
	g_object_unref (process);
	g_object_unref (output);

	g_free (msdcs);
	g_free (domain);

	return TRUE;
}

static void
ic_ads_provider_init (IcAdsProvider *self)
{
	g_object_set (self, "provider-interface", IC_DBUS_KERBEROS_INTERFACE, NULL);
	g_signal_connect (self, "handle-discover-provider", G_CALLBACK (handle_discover_provider), NULL);
}

static void
ic_ads_provider_class_init (IcAdsProviderClass *klass)
{

}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
	IcAdsProvider *provider = IC_ADS_PROVIDER (user_data);
	GError *error = NULL;

	ic_diagnostics_initialize (connection);

	g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (provider),
	                                  connection, IC_DBUS_ACTIVE_DIRECTORY_PATH,
	                                  &error);

	if (error != NULL) {
		g_warning ("couldn't export IcAdsProvider on dbus connection: %s",
		           error->message);
		g_error_free (error);
	}
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
	/* TODO: timeout for exit? */
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
	/* TODO: timeout for exit? */
}

void
ic_ads_provider_start (void)
{
	IcAdsProvider *provider;

	g_return_if_fail (ads_provider_owner_id == 0);

	provider = g_object_new (IC_TYPE_ADS_PROVIDER, NULL);

	ads_provider_owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
	                                        IC_DBUS_ACTIVE_DIRECTORY_NAME,
	                                        G_BUS_NAME_OWNER_FLAGS_NONE,
	                                        on_bus_acquired,
	                                        on_name_acquired,
	                                        on_name_lost,
	                                        provider, g_object_unref);
}

void
ic_ads_provider_stop (void)
{
	g_return_if_fail (ads_provider_owner_id != 0);
	g_bus_unown_name (ads_provider_owner_id);
}
