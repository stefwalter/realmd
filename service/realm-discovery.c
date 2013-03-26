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
realm_discovery_add_strings (GHashTable *discovery,
                             const gchar *type,
                             const char **value)
{
	g_return_if_fail (type != NULL);
	g_return_if_fail (value != NULL);

	if (discovery != NULL)
		realm_discovery_add_variant (discovery, type, g_variant_new_strv (value, -1));
}

const gchar **
realm_discovery_get_strings (GHashTable *discovery,
                             const gchar *type)
{
	GVariant *variant;

	g_return_val_if_fail (type != NULL, NULL);

	if (discovery == NULL)
		return NULL;

	variant = g_hash_table_lookup (discovery, type);
	if (variant == NULL)
		return NULL;

	return g_variant_get_strv (variant, NULL);
}
