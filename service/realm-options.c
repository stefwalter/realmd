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
#include "realm-options.h"
#include "realm-settings.h"

gboolean
realm_options_assume_packages (GVariant *options)
{
	gboolean assume;

	if (!g_variant_lookup (options, REALM_DBUS_OPTION_ASSUME_PACKAGES, "b", &assume))
		assume = FALSE;

	return assume;
}

const gchar *
realm_options_user_principal (GVariant *options,
                              const gchar *realm_name)
{
	const gchar *principal;
	gchar *section;

	if (!g_variant_lookup (options, REALM_DBUS_OPTION_USER_PRINCIPAL, "&s", &principal))
		principal = NULL;

	if (!principal) {
		section = g_utf8_casefold (realm_name, -1);
		if (realm_settings_value (section, REALM_DBUS_OPTION_USER_PRINCIPAL) &&
		    realm_settings_boolean (section, REALM_DBUS_OPTION_USER_PRINCIPAL))
			principal = ""; /* auto-generate */
		g_free (section);
	}

	return principal;
}

const gchar *
realm_options_computer_ou (GVariant *options,
                           const gchar *realm_name)
{
	const gchar *computer_ou = NULL;
	gchar *section;

	if (options) {
		if (!g_variant_lookup (options, REALM_DBUS_OPTION_COMPUTER_OU, "&s", &computer_ou))
			computer_ou = NULL;
	}

	if (!computer_ou) {
		section = g_utf8_casefold (realm_name, -1);
		computer_ou = realm_settings_value (section, REALM_DBUS_OPTION_COMPUTER_OU);
		g_free (section);
	}

	return g_strdup (computer_ou);
}
