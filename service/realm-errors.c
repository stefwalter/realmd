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
	{ REALM_ERROR_DISCOVERED_NOTHING, REALM_DBUS_ERROR_DISCOVERED_NOTHING },
	{ REALM_ERROR_DISCOVERY_FAILED, REALM_DBUS_ERROR_DISCOVERY_FAILED },
	{ REALM_ERROR_ENROLL_FAILED, REALM_DBUS_ERROR_ENROLL_FAILED },
	{ REALM_ERROR_UNENROLL_FAILED, REALM_DBUS_ERROR_UNENROLL_FAILED },
	{ REALM_ERROR_SET_LOGINS_FAILED, REALM_DBUS_ERROR_SET_LOGINS_FAILED },
	{ REALM_ERROR_BUSY, REALM_DBUS_ERROR_BUSY },
	{ REALM_ERROR_ALREADY_ENROLLED, REALM_DBUS_ERROR_ALREADY_ENROLLED },
	{ REALM_ERROR_NOT_ENROLLED, REALM_DBUS_ERROR_NOT_ENROLLED },
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
