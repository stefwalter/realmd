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
#include "realm-ini-config.h"
#include "realm-samba-config.h"

#include <string.h>

RealmIniConfig *
realm_samba_config_new (GError **error)
{
	RealmIniConfig *config;
	const gchar *filename;
	GError *err = NULL;

	config = realm_ini_config_new (REALM_INI_LINE_CONTINUATIONS);

	filename = realm_platform_path ("smb.conf");
	realm_ini_config_read_file (config, filename, &err);

	/* Ignore errors of the file not existing */
	if (g_error_matches (err, G_FILE_ERROR, G_FILE_ERROR_NOENT))
		g_clear_error (&err);

	if (err != NULL) {
		g_object_unref (config);
		g_propagate_error (error, err);
		config = NULL;
	}

	return config;
}

gboolean
realm_samba_config_change (const gchar *section,
                           GError **error,
                           ...)
{
	GHashTable *parameters;
	const gchar *name;
	const gchar *value;
	gboolean ret;
	va_list va;

	g_return_val_if_fail (section != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	parameters = g_hash_table_new (g_str_hash, g_str_equal);
	va_start (va, error);
	while ((name = va_arg (va, const gchar *)) != NULL) {
		value = va_arg (va, const gchar *);
		g_hash_table_insert (parameters, (gpointer)name, (gpointer)value);
	}
	va_end (va);

	ret = realm_samba_config_changev (section, parameters, error);
	g_hash_table_unref (parameters);
	return ret;
}

gboolean
realm_samba_config_changev (const gchar *section,
                            GHashTable *parameters,
                            GError **error)
{
	RealmIniConfig *config;
	gboolean ret = FALSE;

	g_return_val_if_fail (section != NULL, FALSE);
	g_return_val_if_fail (parameters != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	config = realm_samba_config_new (error);
	if (config != NULL) {
		realm_ini_config_set_all (config, section, parameters);
		ret = realm_ini_config_write_file (config, NULL, error);
		g_object_unref (config);
	}

	return ret;
}
