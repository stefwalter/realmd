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

#include "realm-errors.h"
#include "realm-ini-config.h"
#include "realm-sssd-config.h"
#include "realm-settings.h"

#include <string.h>

RealmIniConfig *
realm_sssd_config_new_with_flags (RealmIniFlags flags,
                                  GError **error)
{
	RealmIniConfig *config;
	const gchar *filename;
	GError *err = NULL;

	config = realm_ini_config_new (flags | REALM_INI_PRIVATE);

	filename = realm_settings_path ("sssd.conf");
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
realm_sssd_config_new (GError **error)
{
	return realm_sssd_config_new_with_flags (REALM_INI_NONE, error);
}

gchar **
realm_sssd_config_get_domains (RealmIniConfig *config)
{
	g_return_val_if_fail (REALM_IS_INI_CONFIG (config), NULL);
	return realm_ini_config_get_list (config, "sssd", "domains", ",");
}

gchar *
realm_sssd_config_domain_to_section (const gchar *domain)
{
	g_return_val_if_fail (domain != NULL, NULL);
	return g_strdup_printf ("domain/%s", domain);
}

gboolean
realm_sssd_config_have_domain (RealmIniConfig *config,
                               const gchar *domain)
{
	gchar **domains;
	gboolean have = FALSE;
	gint i;

	g_return_val_if_fail (REALM_IS_INI_CONFIG (config), FALSE);
	g_return_val_if_fail (domain != NULL, FALSE);

	domains = realm_sssd_config_get_domains (config);
	for (i = 0; domains && domains[i] != NULL; i++) {
		if (g_str_equal (domain, domains[i])) {
			have = TRUE;
			break;
		}
	}
	g_strfreev (domains);

	return have;
}

gboolean
realm_sssd_config_add_domain (RealmIniConfig *config,
                              const gchar *domain,
                              GError **error,
                              ...)
{
	GHashTable *parameters;
	const gchar *name;
	const gchar *value;
	const gchar *domains[2];
	gchar *section;
	va_list va;

	g_return_val_if_fail (REALM_IS_INI_CONFIG (config), FALSE);
	g_return_val_if_fail (domain != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!realm_ini_config_begin_change (config, error))
		return FALSE;

	section = realm_sssd_config_domain_to_section (domain);
	if (realm_ini_config_have_section (config, section)) {
		realm_ini_config_abort_change (config);
		g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_EXIST,
		             "Already have domain %s in sssd.conf config file", domain);
		g_free (section);
		return FALSE;
	}

	/* Setup a default sssd section */
	if (!realm_ini_config_have_section (config, "sssd")) {
		realm_ini_config_set (config, "sssd", "services", "nss, pam");
		realm_ini_config_set (config, "sssd", "config_file_version", "2");
	}

	domains[0] = domain;
	domains[1] = NULL;
	realm_ini_config_set_list_diff (config, "sssd", "domains", ", ", domains, NULL);

	parameters = g_hash_table_new (g_str_hash, g_str_equal);
	va_start (va, error);
	while ((name = va_arg (va, const gchar *)) != NULL) {
		value = va_arg (va, const gchar *);
		g_hash_table_insert (parameters, (gpointer)name, (gpointer)value);
	}
	va_end (va);

	realm_ini_config_set_all (config, section, parameters);
	g_hash_table_unref (parameters);
	g_free (section);

	return realm_ini_config_finish_change (config, error);
}

gboolean
realm_sssd_config_remove_domain (RealmIniConfig *config,
                                 const gchar *domain,
                                 GError **error)
{
	const gchar *domains[2];
	gchar *section;

	g_return_val_if_fail (REALM_IS_INI_CONFIG (config), FALSE);
	g_return_val_if_fail (domain != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!realm_ini_config_begin_change (config, error))
		return FALSE;

	section = realm_sssd_config_domain_to_section (domain);

	domains[0] = domain;
	domains[1] = NULL;
	realm_ini_config_set_list_diff (config, "sssd", "domains", ", ", NULL, domains);
	realm_ini_config_remove_section (config, section);
	g_free (section);

	return realm_ini_config_finish_change (config, error);
}
