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

#include "realm-discovery.h"

#include <gio/gio.h>

GHashTable *
realm_discovery_new (void)
{
	return g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
	                              (GDestroyNotify)g_variant_unref);
}

void
realm_discovery_add_string (GHashTable *discovery,
                            const gchar *type,
                            const gchar *value)
{
	g_return_if_fail (type != NULL);
	g_return_if_fail (value != NULL);

	if (discovery != NULL)
		realm_discovery_add_variant (discovery, type, g_variant_new_string (value));
}

const gchar *
realm_discovery_get_string (GHashTable *discovery,
                            const gchar *type)
{
	GVariant *variant;

	g_return_val_if_fail (type != NULL, NULL);

	if (discovery == NULL)
		return NULL;

	variant = g_hash_table_lookup (discovery, type);
	if (variant == NULL)
		return NULL;

	return g_variant_get_string (variant, NULL);
}

gboolean
realm_discovery_has_string (GHashTable *discovery,
                            const gchar *type,
                            const gchar *value)
{
	const gchar *has;
	has = realm_discovery_get_string (discovery, type);
	return has != NULL && g_str_equal (has, value);
}

void
realm_discovery_add_variant (GHashTable *discovery,
                             const gchar *type,
                             GVariant *value)
{
	g_return_if_fail (type != NULL);
	g_return_if_fail (value != NULL);

	if (discovery != NULL)
		g_hash_table_insert (discovery, g_strdup (type), g_variant_ref_sink (value));
}

void
realm_discovery_add_srv_targets (GHashTable *discovery,
                                 const gchar *type,
                                 GList *targets)
{
	GPtrArray *servers;
	gchar *server;
	GList *l;

	g_return_if_fail (type != NULL);

	servers = g_ptr_array_new ();

	for (l = targets; l != NULL; l = g_list_next (l)) {
		server = g_strdup_printf ("%s:%d", g_srv_target_get_hostname (l->data),
		                          (int)g_srv_target_get_port (l->data));
		g_ptr_array_add (servers, g_variant_new_string (server));
	}

	realm_discovery_add_variant (discovery, type,
	                             g_variant_new_array (G_VARIANT_TYPE_STRING,
	                                                  (GVariant * const*)servers->pdata,
	                                                  servers->len));

	g_ptr_array_free (servers, TRUE);
}

GVariant *
realm_discovery_to_variant (GHashTable *discovery)
{
	GPtrArray *entries;
	GHashTableIter iter;
	GVariant *result;
	gpointer key, value;
	GVariant *entry;

	entries = g_ptr_array_new ();

	if (discovery != NULL) {
		g_hash_table_iter_init (&iter, discovery);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			entry = g_variant_new_dict_entry (g_variant_new_string (key),
			                                  g_variant_new_variant (value));
			g_ptr_array_add (entries, entry);
		}
	}

	result = g_variant_new_array (G_VARIANT_TYPE ("{sv}"),
	                              (GVariant * const *)entries->pdata,
	                              entries->len);

	g_ptr_array_free (entries, TRUE);
	return result;
}
