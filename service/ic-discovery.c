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

#include "ic-discovery.h"

GHashTable *
ic_discovery_new (void)
{
	return g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
	                              (GDestroyNotify)g_variant_unref);
}

void
ic_discovery_add_string (GHashTable *discovery,
                         const gchar *type,
                         const gchar *value)
{
	g_return_if_fail (type != NULL);
	g_return_if_fail (value != NULL);

	if (discovery != NULL)
		ic_discovery_add_variant (discovery, type, g_variant_new_string (value));
}

void
ic_discovery_add_variant (GHashTable *discovery,
                          const gchar *type,
                          GVariant *value)
{
	g_return_if_fail (type != NULL);
	g_return_if_fail (value != NULL);

	if (discovery != NULL)
		g_hash_table_insert (discovery, g_strdup (type), g_variant_ref_sink (value));
}

GVariant *
ic_discovery_to_variant (GHashTable *discovery)
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
			entry = g_variant_new_dict_entry (g_variant_new_string (key), value);
			g_ptr_array_add (entries, entry);
		}
	}

	result = g_variant_new_array (G_VARIANT_TYPE ("a{sv}"),
	                              (GVariant * const *)entries->pdata,
	                              entries->len);

	g_ptr_array_free (entries, TRUE);
	return result;
}
