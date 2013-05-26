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
#include "realm-errors.h"

#include <glib.h>

static const GDBusErrorEntry realm_error_entries[] = {
	{ REALM_ERROR_INTERNAL, REALM_DBUS_ERROR_INTERNAL },
	{ REALM_ERROR_FAILED, REALM_DBUS_ERROR_FAILED },
	{ REALM_ERROR_BUSY, REALM_DBUS_ERROR_BUSY },
	{ REALM_ERROR_ALREADY_CONFIGURED, REALM_DBUS_ERROR_ALREADY_CONFIGURED },
	{ REALM_ERROR_NOT_CONFIGURED, REALM_DBUS_ERROR_NOT_CONFIGURED },
	{ REALM_ERROR_AUTH_FAILED, REALM_DBUS_ERROR_AUTH_FAILED },
	{ REALM_ERROR_BAD_HOSTNAME, REALM_DBUS_ERROR_BAD_HOSTNAME },
	{ REALM_ERROR_CANCELLED, REALM_DBUS_ERROR_CANCELLED },
};

/*
 * If there's a compilation error here, then the realm_error_entries
 * array above is not synced with the RealmErrorCodes enum in realm-errors.h.
 */
G_STATIC_ASSERT (G_N_ELEMENTS (realm_error_entries) == _NUM_REALM_ERRORS);

GQuark
realm_error_quark (void)
{
	static volatile gsize quark_volatile = 0;

	g_dbus_error_register_error_domain ("realmd-error",
	                                    &quark_volatile,
	                                    realm_error_entries,
	                                    G_N_ELEMENTS (realm_error_entries));

	return (GQuark)quark_volatile;
}

GQuark
realm_krb5_error_quark (void)
{
	static volatile gsize quark_volatile = 0;

	if (quark_volatile == 0)
		quark_volatile = g_quark_from_static_string ("krb5-error");

	return (GQuark)quark_volatile;
}
