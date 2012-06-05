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
	const gchar *filename;

	g_return_val_if_fail (section != NULL, FALSE);
	g_return_val_if_fail (parameters != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	config = realm_ini_config_new (REALM_INI_LINE_CONTINUATIONS | REALM_INI_NO_WATCH);
	filename = realm_settings_path ("smb.conf");
	realm_ini_config_set_filename (config, filename);

	ret = realm_ini_config_changev (config, section, parameters, error);
	g_object_unref (config);
	return ret;
}

static gchar **
update_lists_for_changes (const gchar **original,
                          const gchar **add,
                          const gchar **remove)
{
	GPtrArray *changed;
	gchar *value;
	gint i, j;

	changed = g_ptr_array_new ();

	/* Filter the remove logins */
	for (i = 0; original != NULL && original[i] != NULL; i++) {
		value = g_strstrip (g_strdup (original[i]));
		for (j = 0; remove != NULL && remove[j] != NULL; j++) {
			if (g_ascii_strcasecmp (remove[j], value) == 0)
				break;
		}
		if ((remove == NULL || remove[j] == NULL) && !g_str_equal (value, ""))
			g_ptr_array_add (changed, value);
		else
			g_free (value);
	}

	/* Add the logins */
	for (j = 0; add != NULL && add[j] != NULL; j++) {
		for (i = 0; original != NULL && original[i] != NULL; i++) {
			if (g_ascii_strcasecmp (add[j], original[i]) == 0)
				break;
		}
		if (original == NULL || original[i] == NULL)
			g_ptr_array_add (changed, g_strdup (add[j]));
	}

	g_ptr_array_add (changed, NULL);
	return (gchar **)g_ptr_array_free (changed, FALSE);
}

gboolean
realm_samba_config_change_list (const gchar *section,
                                const gchar *name,
                                const gchar *delimiters,
                                const gchar **add,
                                const gchar **remove,
                                GError **error)
{
	RealmIniConfig *config;
	gboolean ret = FALSE;
	gchar **original;
	gchar **changed;
	gchar *delim;

	g_return_val_if_fail (section != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	config = realm_samba_config_new_with_flags (REALM_INI_NO_WATCH, error);
	if (config != NULL) {
		original = realm_ini_config_get_list (config, section, name, delimiters);
		changed = update_lists_for_changes ((const gchar **)original, add, remove);
		g_strfreev (original);

		delim = g_strdup_printf ("%c ", delimiters[0]);
		realm_ini_config_set_list (config, section, name, delim,
		                           (const gchar **)changed);
		g_strfreev (changed);
		g_free (delim);

		ret = realm_ini_config_write_file (config, NULL, error);
		g_object_unref (config);
	}

	return ret;
}
