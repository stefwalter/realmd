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
update_realms_property (RealmProvider *self)
{
	GPtrArray *paths;
	GList *realms;
	GList *l;

	realms = realm_provider_get_realms (self);

	paths = g_ptr_array_new ();
	for (l = realms; l != NULL; l = g_list_next (l))
		g_ptr_array_add (paths, (gchar *)g_dbus_object_get_object_path (l->data));
	g_ptr_array_add (paths, NULL);

	g_list_free (realms);

	realm_provider_set_realm_paths (REALM_PROVIDER (self), (const gchar **)paths->pdata);
	g_ptr_array_free (paths, TRUE);
}

static void
update_all_properties (RealmProvider *self)
{
	update_realms_property (self);
}

static void
on_provider_notify (GObject *obj,
                    GParamSpec *spec,
                    gpointer user_data)
{
	RealmProvider *self = REALM_PROVIDER (user_data);
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
	GList *realms;
} DiscoverClosure;

typedef struct {
	GList *realms;
	gint relevance;
} DiscoverResult;

static void
discover_result_free (gpointer data)
{
	DiscoverResult *disco = data;
	g_list_free_full (disco->realms, g_object_unref);
	g_free (disco);
}

static void
discover_closure_free (gpointer data)
{
	DiscoverClosure *discover = data;
	g_free (discover->operation_id);
	g_object_unref (discover->invocation);
	while (!g_queue_is_empty (&discover->results))
		discover_result_free (g_queue_pop_head (&discover->results));
	while (!g_queue_is_empty (&discover->failures))
		g_error_free (g_queue_pop_head (&discover->failures));
	g_list_free_full (discover->realms, g_object_unref);
	g_free (discover);
}

static gint
compare_relevance (gconstpointer a,
                   gconstpointer b,
                   gpointer user_data)
{
	const DiscoverResult *disco_a = a;
	const DiscoverResult *disco_b = b;

	return disco_b->relevance - disco_a->relevance;
}

static void
discover_process_results (GSimpleAsyncResult *res,
                          DiscoverClosure *discover)
{
	GError *error;
	DiscoverResult *disco;
	gboolean any = FALSE;

	g_queue_sort (&discover->results, compare_relevance, NULL);

	for (;;) {
		disco = g_queue_pop_head (&discover->results);
		if (disco == NULL)
			break;
		discover->realms = g_list_concat (discover->realms, disco->realms);
		disco->realms = NULL;
		if (disco->relevance > discover->relevance)
			discover->relevance = disco->relevance;
		discover_result_free (disco);
		any = TRUE;
	}

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
	DiscoverResult *disco;
	GError *error = NULL;
	GList *realms;
	gint relevance;

	realms = realm_provider_discover_finish (REALM_PROVIDER (source), result, &relevance, &error);
	if (error == NULL) {
		disco = g_new0 (DiscoverResult, 1);
		disco->realms = realms;
		disco->relevance = relevance;
		g_queue_push_tail (&discover->results, disco);
	} else {
		g_queue_push_tail (&discover->failures, error);
	}

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
	discover = g_new0 (DiscoverClosure, 1);
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

static GList *
realm_all_provider_discover_finish (RealmProvider *provider,
                                    GAsyncResult *result,
                                    gint *relevance,
                                    GError **error)
{
	GSimpleAsyncResult *res;
	DiscoverClosure *discover;
	GList *realms;

	res = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (res, error))
		return NULL;

	discover = g_simple_async_result_get_op_res_gpointer (res);
	*relevance = discover->relevance;
	realms = discover->realms;
	discover->realms = NULL;
	return realms;
}

static GList *
realm_all_provider_get_realms (RealmProvider *provider)
{
	RealmAllProvider *self = REALM_ALL_PROVIDER (provider);
	GList *realms = NULL;
	GList *l;

	for (l = self->providers; l != NULL; l = g_list_next (l))
		realms = g_list_concat (realms, realm_provider_get_realms (l->data));

	return realms;
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
	provider_class->get_realms = realm_all_provider_get_realms;
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

	update_all_properties (all_provider);
	g_signal_connect (provider, "notify", G_CALLBACK (on_provider_notify), self);
}
