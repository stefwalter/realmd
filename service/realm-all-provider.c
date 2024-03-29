/* realmd -- Realm configuration service
 *
 * Copyright 2012 Red Hat Inc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) all later version.
 *
 * See the included COPYING file for more information.
 *
 * Author: Stef Walter <stefw@gnome.org>
 */

#include "config.h"

#include "realm-all-provider.h"
#include "realm-daemon.h"
#include "realm-diagnostics.h"
#include "realm-errors.h"
#include "realm-dbus-constants.h"
#include "realm-dbus-generated.h"
#include "realm-provider.h"

#include <glib/gstdio.h>

#include <errno.h>
#include <string.h>

struct _RealmAllProvider {
	RealmProvider parent;
	GList *providers;
};

typedef struct {
	RealmProviderClass parent_class;
} RealmAllProviderClass;

G_DEFINE_TYPE (RealmAllProvider, realm_all_provider, REALM_TYPE_PROVIDER);

static void
realm_all_provider_init (RealmAllProvider *self)
{

}

static void
realm_all_provider_constructed (GObject *obj)
{
	G_OBJECT_CLASS (realm_all_provider_parent_class)->constructed (obj);

	/* The dbus Name property of the provider */
	realm_provider_set_name (REALM_PROVIDER (obj), "All");
}

static void
update_realms_property (RealmAllProvider *self)
{
	const gchar *const * paths;
	GPtrArray *realms;
	GList *l;
	gint i;

	realms = g_ptr_array_new ();
	for (l = self->providers; l != NULL; l = g_list_next (l)) {
		paths = realm_provider_get_realms (l->data);
		for (i = 0; paths != NULL && paths[i] != NULL; i++)
			g_ptr_array_add (realms, (gchar *)paths[i]);
	}

	g_ptr_array_add (realms, NULL);
	realm_provider_set_realms (REALM_PROVIDER (self), (const gchar **)realms->pdata);
	g_ptr_array_free (realms, TRUE);
}

static void
update_all_properties (RealmAllProvider *self)
{
	update_realms_property (self);
}

static void
on_provider_notify (GObject *obj,
                    GParamSpec *spec,
                    gpointer user_data)
{
	RealmAllProvider *self = REALM_ALL_PROVIDER (user_data);
	update_all_properties (self);
}

typedef struct {
	GDBusMethodInvocation *invocation;
	gchar *operation_id;
	gboolean completed;
	gint outstanding;
	GQueue failures;
	GQueue results;
	gint relevance;
	GVariant *realms;
} DiscoverClosure;

static void
discover_closure_free (gpointer data)
{
	DiscoverClosure *discover = data;
	g_free (discover->operation_id);
	g_object_unref (discover->invocation);
	while (!g_queue_is_empty (&discover->results))
		g_variant_unref (g_queue_pop_head (&discover->results));
	while (!g_queue_is_empty (&discover->failures))
		g_error_free (g_queue_pop_head (&discover->failures));
	if (discover->realms)
		g_variant_unref (discover->realms);
	g_slice_free (DiscoverClosure, discover);
}

static gint
compare_relevance (gconstpointer a,
                   gconstpointer b,
                   gpointer user_data)
{
	gint relevance_a = 0;
	gint relevance_b = 0;
	GVariant *realms;

	g_variant_get ((GVariant *)a, "(i@ao)", &relevance_a, &realms);
	g_variant_unref (realms);

	g_variant_get ((GVariant *)b, "(i@ao)", &relevance_b, &realms);
	g_variant_unref (realms);

	return relevance_b - relevance_a;
}

static void
discover_process_results (GSimpleAsyncResult *res,
                          DiscoverClosure *discover)
{
	gint relevance = 0;
	GError *error;
	GVariant *result;
	GVariant *realms;
	gboolean any = FALSE;
	GPtrArray *results;
	GVariantIter iter;
	GVariant *realm;

	g_queue_sort (&discover->results, compare_relevance, NULL);
	results = g_ptr_array_new_with_free_func ((GDestroyNotify)g_variant_unref);

	for (;;) {
		result = g_queue_pop_head (&discover->results);
		if (result == NULL)
			break;
		g_variant_get (result, "(i@ao)", &relevance, &realms);
		g_variant_iter_init (&iter, realms);
		while ((realm = g_variant_iter_next_value (&iter)) != NULL)
			g_ptr_array_add (results, realm);
		if (relevance > discover->relevance)
			discover->relevance = relevance;
		g_variant_unref (realms);
		g_variant_unref (result);
		any = TRUE;
	}

	discover->realms = g_variant_new_array (G_VARIANT_TYPE ("o"),
	                                        (GVariant *const *)results->pdata,
	                                        results->len);
	g_variant_ref_sink (discover->realms);
	g_ptr_array_free (results, TRUE);

	if (!any) {
		/* If there was a failure, return one of them */
		error = g_queue_pop_head (&discover->failures);
		if (error != NULL)
			g_simple_async_result_take_error (res, error);
	}
}

static void
on_provider_discover (GObject *source,
                      GAsyncResult *result,
                      gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	DiscoverClosure *discover = g_simple_async_result_get_op_res_gpointer (res);
	RealmAllProvider *self = REALM_ALL_PROVIDER (g_async_result_get_source_object (user_data));
	GError *error = NULL;
	GVariant *retval;
	GVariant *realms;
	gint relevance;

	relevance = realm_provider_discover_finish (REALM_PROVIDER (source), result, &realms, &error);
	if (error == NULL) {
		retval = g_variant_new ("(i@ao)", relevance, realms);
		g_queue_push_tail (&discover->results, g_variant_ref_sink (retval));
	} else {
		g_queue_push_tail (&discover->failures, error);
	}

	if (realms)
		g_variant_unref (realms);

	g_assert (discover->outstanding > 0);
	discover->outstanding--;

	/* All done at this point? */
	if (!discover->completed && discover->outstanding == 0) {
		discover_process_results (res, discover);
		discover->completed = TRUE;
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
	g_object_unref (self);
}

static void
realm_all_provider_discover_async (RealmProvider *provider,
                                   const gchar *string,
                                   GVariant *options,
                                   GDBusMethodInvocation *invocation,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
	RealmAllProvider *self = REALM_ALL_PROVIDER (provider);
	GSimpleAsyncResult *res;
	DiscoverClosure *discover;
	GList *l;

	res = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
	                                 realm_all_provider_discover_async);
	discover = g_slice_new0 (DiscoverClosure);
	g_queue_init (&discover->results);
	discover->invocation = g_object_ref (invocation);
	g_simple_async_result_set_op_res_gpointer (res, discover, discover_closure_free);

	for (l = self->providers; l != NULL; l = g_list_next (l)) {
		realm_provider_discover (l->data, string, options, invocation,
		                         on_provider_discover, g_object_ref (res));
		discover->outstanding++;
	}

	/* If no discovery going on then just complete */
	if (discover->outstanding == 0) {
		discover_process_results (res, discover);
		discover->completed = TRUE;
		g_simple_async_result_complete_in_idle (res);
	}

	g_object_unref (res);
}

static gint
realm_all_provider_discover_finish (RealmProvider *provider,
                                    GAsyncResult *result,
                                    GVariant **realms,
                                    GError **error)
{
	GSimpleAsyncResult *res;
	DiscoverClosure *discover;

	res = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (res, error))
		return -1;

	discover = g_simple_async_result_get_op_res_gpointer (res);
	*realms = discover->realms;
	discover->realms = NULL;
	return discover->relevance;
}

static void
realm_all_provider_finalize (GObject *obj)
{
	RealmAllProvider *self = REALM_ALL_PROVIDER (obj);
	GList *l;

	for (l = self->providers; l != NULL; l = g_list_next (l))
		g_signal_handlers_disconnect_by_func (l->data, on_provider_notify, self);
	g_list_free_full (self->providers, g_object_unref);

	G_OBJECT_CLASS (realm_all_provider_parent_class)->finalize (obj);
}

void
realm_all_provider_class_init (RealmAllProviderClass *klass)
{
	RealmProviderClass *provider_class = REALM_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = realm_all_provider_constructed;
	object_class->finalize = realm_all_provider_finalize;

	provider_class->discover_async = realm_all_provider_discover_async;
	provider_class->discover_finish = realm_all_provider_discover_finish;
}

RealmProvider *
realm_all_provider_new_and_export (GDBusConnection *connection)
{
	GDBusObject *self;
	GList *interfaces, *l;
	GError *error = NULL;

	self = g_object_new (REALM_TYPE_ALL_PROVIDER,
	                     "g-object-path", REALM_DBUS_SERVICE_PATH,
	                     NULL);

	interfaces = g_dbus_object_get_interfaces (G_DBUS_OBJECT (self));
	for (l = interfaces; l != NULL; l = g_list_next (l)) {
		g_dbus_interface_skeleton_export (l->data, connection,
		                                  g_dbus_object_get_object_path (self),
		                                  &error);
		if (error != NULL) {
			g_warning ("Couldn't export DBus interface at %s",
			           g_dbus_object_get_object_path (self));
			g_clear_error (&error);
		}
	}

	g_list_free_full (interfaces, g_object_unref);
	return REALM_PROVIDER (self);
}

void
realm_all_provider_register (RealmProvider *all_provider,
                             RealmProvider *provider)
{
	RealmAllProvider *self;

	g_return_if_fail (REALM_IS_ALL_PROVIDER (all_provider));
	g_return_if_fail (REALM_IS_PROVIDER (provider));

	self = REALM_ALL_PROVIDER (all_provider);
	self->providers = g_list_prepend (self->providers, g_object_ref (provider));

	update_all_properties (self);
	g_signal_connect (provider, "notify", G_CALLBACK (on_provider_notify), self);
}
