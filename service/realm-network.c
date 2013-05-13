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

#include "realm-dbus-constants.h"
#include "realm-network.h"

typedef struct {
	gint outstanding;
	GList *values;
} LookupClosure;

static void
lookup_closure_free (gpointer data)
{
	LookupClosure *lookup = data;
	g_assert (lookup->outstanding == 0);
	g_list_free_full (lookup->values, (GDestroyNotify)g_variant_unref);
	g_free (lookup);
}

static GVariant *
lookup_get_property_finish (GDBusConnection *connection,
                            GAsyncResult *result,
                            const GVariantType *variant_type,
                            GError **error)
{
	GVariant *value = NULL;
	GVariant *retval;

	retval = g_dbus_connection_call_finish (connection, result, error);
	if (retval == NULL)
		return NULL;

	g_return_val_if_fail (g_variant_is_of_type (retval, G_VARIANT_TYPE ("(v)")), NULL);

	g_variant_get (retval, "(v)", &value);
	if (!g_variant_is_of_type (value, variant_type)) {
		g_variant_unref (value);
		return NULL;
	}

	return value;
}

static void
lookup_get_property_async (GDBusConnection *connection,
                           const gchar *object_path,
                           const gchar *interface_name,
                           const gchar *prop_name,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	g_dbus_connection_call (connection, "org.freedesktop.NetworkManager",
	                        object_path, DBUS_PROPERTIES_INTERFACE, "Get",
	                        g_variant_new ("(ss)", interface_name, prop_name),
	                        G_VARIANT_TYPE ("(v)"), G_DBUS_CALL_FLAGS_NONE,
	                        -1, NULL, callback, user_data);
}

static void
on_options (GObject *object,
            GAsyncResult *result,
            gpointer user_data)
{
	GDBusConnection *connection = G_DBUS_CONNECTION (object);
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	LookupClosure *lookup = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	GVariant *value;

	value = lookup_get_property_finish (connection, result,
	                                    G_VARIANT_TYPE ("a{sv}"), &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	if (value != NULL)
		lookup->values = g_list_prepend (lookup->values, value);

	if (lookup->outstanding-- == 1)
		g_simple_async_result_complete (res);

	g_object_unref (res);
}

static void
on_dhcp_config (GObject *object,
                GAsyncResult *result,
                const char *interface_name,
                gpointer user_data)
{
	GDBusConnection *connection = G_DBUS_CONNECTION (object);
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	LookupClosure *lookup = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	GVariant *value;
	const gchar *path;

	value = lookup_get_property_finish (connection, result,
	                                    G_VARIANT_TYPE_OBJECT_PATH, &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	if (value != NULL) {
		path = g_variant_get_string (value, NULL);
		if (path && !g_str_equal (path, "") && !g_str_equal (path, "/")) {
			lookup_get_property_async (connection, path,
			                           interface_name, "Options",
			                           on_options, g_object_ref (res));
			lookup->outstanding++;
		}
		g_variant_unref (value);
	}

	if (lookup->outstanding-- == 1)
		g_simple_async_result_complete (res);

	g_object_unref (res);
}

static void
on_dhcp4_config (GObject *object,
                 GAsyncResult *result,
                 gpointer user_data)
{
	on_dhcp_config (object, result, "org.freedesktop.NetworkManager.DHCP4Config", user_data);
}

static void
on_dhcp6_config (GObject *object,
                 GAsyncResult *result,
                 gpointer user_data)
{
	on_dhcp_config (object, result, "org.freedesktop.NetworkManager.DHCP6Config", user_data);
}

static void
on_devices (GObject *object,
            GAsyncResult *result,
            gpointer user_data)
{
	GDBusConnection *connection = G_DBUS_CONNECTION (object);
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	LookupClosure *lookup = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	const gchar **paths;
	GVariant *value;
	gint i = 0;

	value = lookup_get_property_finish (connection, result,
	                                    G_VARIANT_TYPE_OBJECT_PATH_ARRAY, &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	if (value != NULL) {
		paths = g_variant_get_objv (value, NULL);
		for (i = 0; paths[i] != NULL; i++) {
			lookup_get_property_async (connection, paths[i],
			                           "org.freedesktop.NetworkManager.Device", "Dhcp4Config",
			                           on_dhcp4_config, g_object_ref (res));
			lookup->outstanding++;

			lookup_get_property_async (connection, paths[i],
			                           "org.freedesktop.NetworkManager.Device", "Dhcp6Config",
			                           on_dhcp6_config, g_object_ref (res));
			lookup->outstanding++;
		}
		g_variant_unref (value);
	}

	if (lookup->outstanding-- == 1)
		g_simple_async_result_complete (res);

	g_object_unref (res);
}

static void
on_active_connections (GObject *object,
                       GAsyncResult *result,
                       gpointer user_data)
{
	GDBusConnection *connection = G_DBUS_CONNECTION (object);
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	LookupClosure *lookup = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	const gchar **paths;
	GVariant *value;
	gint i;

	value = lookup_get_property_finish (connection, result,
	                                    G_VARIANT_TYPE_OBJECT_PATH_ARRAY, &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	if (value != NULL) {
		paths = g_variant_get_objv (value, NULL);
		for (i = 0; paths[i] != NULL; i++) {
			lookup_get_property_async (connection, paths[i],
			                           "org.freedesktop.NetworkManager.Connection.Active", "Devices",
			                           on_devices, g_object_ref (res));
			lookup->outstanding++;
		}
		g_variant_unref (value);
	}

	if (lookup->outstanding-- == 1)
		g_simple_async_result_complete (res);

	g_object_unref (res);
}

void
realm_network_get_dhcp_domain_async (GDBusConnection *connection,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
	GSimpleAsyncResult *res;
	LookupClosure *lookup;

	g_return_if_fail (G_IS_DBUS_CONNECTION (connection));

	res = g_simple_async_result_new (NULL, callback, user_data,
	                                 realm_network_get_dhcp_domain_async);
	lookup = g_new0 (LookupClosure, 1);
	g_simple_async_result_set_op_res_gpointer (res, lookup, lookup_closure_free);

	lookup_get_property_async (connection, "/org/freedesktop/NetworkManager",
	                           "org.freedesktop.NetworkManager", "ActiveConnections",
	                           on_active_connections, g_object_ref (res));
	lookup->outstanding++;
	g_object_unref (res);
}

gchar *
realm_network_get_dhcp_domain_finish (GAsyncResult *result,
                                      GError **error)
{
	GSimpleAsyncResult *res;
	LookupClosure *lookup;
	gchar *domain;
	GList *l;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      realm_network_get_dhcp_domain_async), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	res = G_SIMPLE_ASYNC_RESULT (result);

	lookup = g_simple_async_result_get_op_res_gpointer (res);
	for (l = lookup->values; l != NULL; l = g_list_next (l)) {
		if (g_variant_lookup (l->data, "domain_name", "s", &domain)) {
			if (domain && domain[0])
				return domain;
			g_free (domain);
		}
	}

	/* Only report errors if no domain was found */
	g_simple_async_result_propagate_error (res, error);
	return NULL;
}
