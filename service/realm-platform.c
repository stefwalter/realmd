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

#include "realm-platform.h"

#include <glib.h>

static GHashTable *realm_conf = NULL;

void
realm_platform_add (const gchar *section,
                    const gchar *key,
                    const gchar *value)
{
	GHashTable *sect;

	sect = g_hash_table_lookup (realm_conf, section);
	if (sect == NULL) {
		sect = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
		g_hash_table_insert (realm_conf, g_strdup (section), sect);
	}

	g_hash_table_insert (sect, g_strdup (key), g_strdup (value));
}

gboolean
realm_platform_load (const gchar *file_path,
                     GError **error)
{
	GKeyFile *key_file = NULL;
	GHashTable *section;
	GError *err = NULL;
	gchar **groups;
	gchar **keys;
	gchar *value;
	gint i;
	gint j;

	key_file = g_key_file_new ();

	if (!g_key_file_load_from_file (key_file, file_path, G_KEY_FILE_NONE, error)) {
		g_key_file_free (key_file);
		return FALSE;
	}

	/* Build into a table of strings, simplifies memory handling */
	groups = g_key_file_get_groups (key_file, NULL);
	for (i = 0; groups[i] != NULL; i++) {
		section = g_hash_table_lookup (realm_conf, groups[i]);
		if (section == NULL) {
			section = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
			g_hash_table_insert (realm_conf, g_strdup (groups[i]), section);
		}

		keys = g_key_file_get_keys (key_file, groups[i], NULL, &err);
		g_return_val_if_fail (err == NULL, FALSE);

		for (j = 0; keys[j] != NULL; j++) {
			value = g_key_file_get_value (key_file, groups[i], keys[j], &err);
			g_return_val_if_fail (err == NULL, FALSE);
			g_hash_table_insert (section, g_strdup (keys[j]), value);
		}
		g_strfreev (keys);
	}
	g_strfreev (groups);

	g_key_file_free (key_file);
	return TRUE;
}

void
realm_platform_init (void)
{
	const gchar *default_conf = SERVICE_DIR "/realmd-defaults.conf";
	const gchar *distro_conf = SERVICE_DIR "/realmd-distro.conf";
	const gchar *admin_conf = SYSCONF_DIR "/realmd.conf";
	GError *error = NULL;

	realm_conf = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
	                                    (GDestroyNotify)g_hash_table_unref);


	/*
	 * These are treated like 'linker error' in that we cannot proceed without
	 * this data. The reason it is not compiled into the daemon itself, is
	 * for easier modification by packagers and distros
	 */
	realm_platform_load (default_conf, &error);
	if (error != NULL) {
		g_error ("couldn't load package configuration file: %s: %s",
		         default_conf, error->message);
		g_clear_error (&error);
	}

	realm_platform_load (distro_conf, &error);
	if (error != NULL) {
		g_error ("couldn't load distro configuration file: %s: %s",
		         distro_conf, error->message);
		g_clear_error (&error);
	}

	/* We allow failure of loading or parsing this data, it's only overrides */
	realm_platform_load (admin_conf, &error);
	if (error != NULL) {
		if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			g_message ("couldn't load admin configuration file: %s: %s",
			           admin_conf, error->message);
		g_clear_error (&error);
	}
}

void
realm_platform_uninit (void)
{
	g_assert (realm_conf != NULL);
	g_hash_table_destroy (realm_conf);
	realm_conf = NULL;
}

const gchar *
realm_platform_path (const gchar *name)
{
	const gchar *path;

	path = realm_platform_value ("paths", name);
	if (path == NULL) {
		g_warning ("no path found for '%s' in realmd config", name);
		return "/invalid/or/misconfigured";
	}

	return path;
}

GHashTable *
realm_platform_settings (const gchar *section)
{
	return g_hash_table_lookup (realm_conf, section);
}

const gchar *
realm_platform_value (const gchar *section,
                      const gchar *key)
{
	GHashTable *settings;

	settings = realm_platform_settings (section);
	if (settings == NULL)
		return NULL;
	return g_hash_table_lookup (settings, key);
}

const gchar *
realm_platform_string (const gchar *section,
                       const gchar *key)
{
	const gchar *string;

	string = realm_platform_value (section, key);
	if (string == NULL) {
		g_warning ("no value found for '%s/%s' in realmd config", section, key);
		return "";
	}

	return string;
}
