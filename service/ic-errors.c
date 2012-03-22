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

#include "ic-dbus-constants.h"
#include "ic-errors.h"

#include <glib.h>

static const GDBusErrorEntry ic_error_entries[] = {
	{ IC_ERROR_DISCOVERY_FAILED, IC_DBUS_ERROR_DISCOVERY_FAILED },
};

/*
 * If there's a compilation error here, then the ic_error_entries
 * array above is not synced with the IcErrorCodes enum in ic-errors.h.
 */
G_STATIC_ASSERT (G_N_ELEMENTS (ic_error_entries) == _NUM_IC_ERRORS);

GQuark
ic_error_quark (void)
{
	static volatile gsize quark_volatile = 0;

	g_dbus_error_register_error_domain ("identity-config-error",
	                                    &quark_volatile,
	                                    ic_error_entries,
	                                    G_N_ELEMENTS (ic_error_entries));

	return (GQuark)quark_volatile;
}
