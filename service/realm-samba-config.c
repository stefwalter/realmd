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

#include "realm-ini-config.h"
#include "realm-samba-config.h"
#include "realm-settings.h"

#include <string.h>

RealmIniConfig *
realm_samba_config_new_with_flags (RealmIniFlags flags,
                                   GError **error)
{
	RealmIniConfig *config;
	const gchar *filename;
	GError *err = NULL;

	config = realm_ini_config_new (REALM_INI_LINE_CONTINUATIONS | flags);

	filename = realm_settings_path ("smb.conf");

	realm_ini_config_read_file (config, filename, &err);

	if (err != NULL) {
		/* If the caller wants errors, then don't return an invalid samba config */
		if (error) {
			g_propagate_error (error, err);
			g_object_unref (config);
			config = NULL;

		/* If the caller doesn't care, then warn but continue */
		} else {
			g_warning ("Couldn't load config file: %s: %s", filename,
			           err->message);
			g_error_free (err);
		}
	}

	return config;
}

RealmIniConfig *
realm_samba_config_new (GError **error)
{
	return realm_samba_config_new_with_flags (REALM_INI_NONE, error);
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

	config = realm_samba_config_new_with_flags (REALM_INI_NO_WATCH, error);
	if (config != NULL) {
		realm_ini_config_set_all (config, section, parameters);
		ret = realm_ini_config_write_file (config, NULL, error);
		g_object_unref (config);
	}

	return ret;
}
