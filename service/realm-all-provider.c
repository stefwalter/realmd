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
#define DEBUG_FLAG REALM_DEBUG_PROVIDER
#include "realm-debug.h"
#include "realm-errors.h"
#include "realm-dbus-constants.h"
#include "realm-dbus-generated.h"
#include "realm-provider.h"

#include <glib/gstdio.h>

#include <errno.h>

struct _RealmAllProvider {
	RealmProvider parent;
	GList *providers;
};

typedef struct {
	RealmProviderClass parent_class;
} RealmAllProviderClass;

static guint provider_owner_id = 0;

static void realm_all_provider_async_initable_iface (GAsyncInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (RealmAllProvider, realm_all_provider, REALM_TYPE_PROVIDER,
	G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, realm_all_provider_async_initable_iface);
);

static void
realm_all_provider_init (RealmAllProvider *self)
{

}

static gboolean
provider_load (const gchar *filename,
               gchar **name,
               gchar **path)
{
	gboolean ret = TRUE;
	GError *error = NULL;
	GKeyFile *key_file;

	g_assert (name != NULL);
	g_assert (path != NULL);

	key_file = g_key_file_new ();
	g_key_file_load_from_file (key_file, filename, G_KEY_FILE_NONE, &error);
	if (error == NULL)
		*name = g_key_file_get_string (key_file, "provider", "name", &error);
	if (error == NULL)
		*path = g_key_file_get_string (key_file, "provider", "path", &error);
	if (error == NULL && (!g_dbus_is_name (*name) || g_dbus_is_unique_name (*name)))
		g_set_error (&error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE,
		             "Invalid DBus name: %s", *name);
	if (error == NULL && !g_variant_is_object_path (*path)) {
		g_set_error (&error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE,
		             "Invalid DBus object path: %s", *path);
	}

	if (error != NULL) {
		g_warning ("Couldn't load provider information from: %s: %s",
		           filename, error->message);
		g_error_free (error);
		g_free (*name);
		g_free (*path);
		*name = *path = NULL;
		ret = FALSE;
	}

	g_key_file_free (key_file);
	return ret;
}

static GVariant *
reduce_array (GQueue *input,
              const gchar *array_sig)
{
	GVariantBuilder builder;
	GVariant *element;
	GVariant *array;
	GVariantIter iter;

	g_variant_builder_init (&builder, G_VARIANT_TYPE (array_sig));

	for (;;) {
		array = g_queue_pop_head (input);
		if (!array)
			break;
		g_variant_iter_init (&iter, array);
		for (;;) {
			element = g_variant_iter_next_value (&iter);
			if (!element)
				break;
			g_variant_builder_add_value (&builder, element);
			g_variant_unref (element);
		}
		g_variant_unref (array);
	}

	return g_variant_builder_end (&builder);
}

static void
update_realms_property (RealmAllProvider *self)
{
	GQueue realms = G_QUEUE_INIT;
	GVariant *variant;
	GList *l;

	for (l = self->providers; l != NULL; l = g_list_next (l)) {
		variant = g_dbus_proxy_get_cached_property (l->data, "Realms");
		if (variant)
			g_queue_push_tail (&realms, variant);
	}

	variant = g_variant_ref_sink (reduce_array (&realms, "a(sos)"));
	g_object_set (self, "realms", variant, NULL);
	g_variant_unref (variant);
}

static void
update_all_properties (RealmAllProvider *self)
{
	update_realms_property (self);
}

static void
on_proxy_properties_changed (GDBusProxy *proxy,
                             GVariant   *changed_properties,
                             GStrv       invalidated_properties,
                             gpointer    user_data)
{
	RealmAllProvider *self = REALM_ALL_PROVIDER (user_data);
	update_all_properties (self);
}

typedef struct {
	GDBusMethodInvocation *invocation;
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

	g_variant_get ((GVariant *)a, "(i@a(sos))", &relevance_a, &realms);
	g_variant_unref (realms);

	g_variant_get ((GVariant *)b, "(i@a(sos))", &relevance_b, &realms);
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
		g_variant_get (result, "(i@a(sos))", &relevance, &realms);
		g_variant_iter_init (&iter, realms);
		while ((realm = g_variant_iter_next_value (&iter)) != NULL)
			g_ptr_array_add (results, realm);
		if (relevance > discover->relevance)
			discover->relevance = relevance;
		g_variant_unref (realms);
		g_variant_unref (result);
		any = TRUE;
	}

	discover->realms = g_variant_new_array (G_VARIANT_TYPE ("(sos)"),
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
on_proxy_discover (GObject *source,
                   GAsyncResult *result,
                   gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	DiscoverClosure *discover = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	GVariant *retval;

	retval = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);
	if (error == NULL)
		g_queue_push_tail (&discover->results, retval);
	else
		g_queue_push_tail (&discover->failures, error);

	g_assert (discover->outstanding > 0);
	discover->outstanding--;

	/* All done at this point? */
	if (discover->outstanding == 0) {
		discover_process_results (res, discover);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
realm_all_provider_discover_async (RealmProvider *provider,
                                   const gchar *string,
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
	g_simple_async_result_set_op_res_gpointer (res, discover, discover_closure_free);

	for (l = self->providers; l != NULL; l = g_list_next (l)) {
		g_dbus_proxy_call (l->data, "Discover", g_variant_new ("(s)", string),
		                   G_DBUS_CALL_FLAGS_NONE, G_MAXINT, NULL,
		                   on_proxy_discover, g_object_ref (res));
		discover->outstanding++;
	}

	if (discover->outstanding == 0) {
		discover_process_results (res, discover);
		g_simple_async_result_complete_in_idle (res);
	}
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

	g_list_free_full (self->providers, g_object_unref);

	G_OBJECT_CLASS (realm_all_provider_parent_class)->finalize (obj);
}

void
realm_all_provider_class_init (RealmAllProviderClass *klass)
{
	RealmProviderClass *provider_class = REALM_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = realm_all_provider_finalize;

	provider_class->discover_async = realm_all_provider_discover_async;
	provider_class->discover_finish = realm_all_provider_discover_finish;
}

typedef struct {
	gint outstanding;
} InitClosure;

static void
init_closure_free (gpointer data)
{
	InitClosure *init = data;
	g_slice_free (InitClosure, init);
}

static void
on_provider_proxy (GObject *source,
                   GAsyncResult *result,
                   gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	InitClosure *init = g_simple_async_result_get_op_res_gpointer (res);
	RealmAllProvider *self = REALM_ALL_PROVIDER (g_async_result_get_source_object (user_data));
	GDBusProxy *proxy;
	GError *error = NULL;

	proxy = g_dbus_proxy_new_for_bus_finish (result, &error);
	if (error == NULL) {
		g_signal_connect (proxy, "g-properties-changed",
		                  G_CALLBACK (on_proxy_properties_changed), self);
		self->providers = g_list_prepend (self->providers, proxy);
	} else {
		g_warning ("Couldn't load realm provider: %s", error->message);
		g_error_free (error);
	}

	init->outstanding--;
	if (init->outstanding == 0) {
		update_all_properties (self);
		g_simple_async_result_complete (res);
	}

	g_object_unref (self);
	g_object_unref (res);
}

static void
realm_all_provider_init_async (GAsyncInitable *initable,
                               int io_priority,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	RealmAllProvider *self = REALM_ALL_PROVIDER (initable);
	GSimpleAsyncResult *res;
	InitClosure *init;
	GError *error = NULL;
	GDir *dir = NULL;
	gchar *filename;
	const gchar *name;
	gchar *provider_name;
	gchar *provider_path;

	res = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
	                                 realm_all_provider_init_async);
	init = g_slice_new0 (InitClosure);
	g_simple_async_result_set_op_res_gpointer (res, init, init_closure_free);

	dir = g_dir_open (PROVIDER_DIR, 0, &error);
	if (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
		g_clear_error (&error);
	if (error != NULL) {
		g_warning ("Couldn't list provider directory: %s: %s",
		           PROVIDER_DIR, error->message);
		g_clear_error (&error);
		dir = NULL;
	}

	for (;;) {
		if (dir == NULL)
			name = NULL;
		else
			name = g_dir_read_name (dir);
		if (name == NULL)
			break;

		/* Only files ending in *.provider are loaded */
		if (!g_pattern_match_simple ("*.provider", name))
			continue;

		filename = g_build_filename (PROVIDER_DIR, name, NULL);
		if (provider_load (filename, &provider_name, &provider_path)) {
			g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE,
			                          realm_dbus_provider_interface_info (),
			                          provider_name, provider_path,
			                          REALM_DBUS_PROVIDER_INTERFACE,
			                          cancellable, on_provider_proxy,
			                          g_object_ref (res));
			g_free (provider_name);
			g_free (provider_path);
			init->outstanding++;
		}

		g_free (filename);
	}

	if (init->outstanding == 0)
		g_simple_async_result_complete_in_idle (res);

	g_object_unref (res);
}

static gboolean
realm_all_provider_init_finish (GAsyncInitable *initable,
                                GAsyncResult *result,
                                GError **error)
{
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

static void
realm_all_provider_async_initable_iface (GAsyncInitableIface *iface)
{
	iface->init_async = realm_all_provider_init_async;
	iface->init_finish = realm_all_provider_init_finish;
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
	realm_daemon_poke ();
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
	g_warning ("couldn't claim service name on DBus bus: %s",
	           REALM_DBUS_ALL_PROVIDER_NAME);
}

static void
on_all_provider_inited (GObject *source,
                        GAsyncResult *result,
                        gpointer user_data)
{
	GDBusConnection *connection = G_DBUS_CONNECTION (user_data);
	GError *error = NULL;
	GObject *self;

	self = g_async_initable_new_finish (G_ASYNC_INITABLE (source),
	                                    result, &error);

	if (error == NULL) {
		g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
		                                  connection, REALM_DBUS_ALL_PROVIDER_PATH,
		                                  &error);
	}

	if (error == NULL) {
		provider_owner_id = g_bus_own_name_on_connection (connection,
		                                                  REALM_DBUS_ALL_PROVIDER_NAME,
		                                                  G_BUS_NAME_OWNER_FLAGS_NONE,
		                                                  on_name_acquired, on_name_lost,
		                                                  g_object_ref (self), g_object_unref);

	} else {
		g_warning ("Couldn't create new realm provider: %s", error->message);
		g_clear_error (&error);
	}

	if (self != NULL)
		g_object_unref (self);
	g_object_unref (connection);
}

void
realm_all_provider_start (GDBusConnection *connection)
{
	g_return_if_fail (provider_owner_id == 0);

	g_async_initable_new_async (REALM_TYPE_ALL_PROVIDER, G_PRIORITY_DEFAULT, NULL,
	                            on_all_provider_inited, g_object_ref (connection),
	                            NULL);
}

void
realm_all_provider_stop (void)
{
	if (provider_owner_id != 0)
		g_bus_unown_name (provider_owner_id);
}
