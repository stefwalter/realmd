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

#ifndef __IC_DBUS_CONSTANTS_H__
#define __IC_DBUS_CONSTANTS_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define   IC_DBUS_ACTIVE_DIRECTORY_NAME         "org.freedesktop.IdentityConfig.ActiveDirectory"
#define   IC_DBUS_ACTIVE_DIRECTORY_PATH         "/org/freedesktop/IdentityConfig/ActiveDirectory"

#define   IC_DBUS_PROVIDER_INTERFACE            "org.freedesktop.IdentityConfig.Provider"
#define   IC_DBUS_KERBEROS_INTERFACE            "org.freedesktop.IdentityConfig.Kerberos"

#define   IC_DBUS_DIAGNOSTICS_SIGNAL            "Diagnostics"

#define   IC_DBUS_ERROR_DISCOVERY_FAILED        "org.freedesktop.IdentityConfig.Error.DiscoveryFailed"

G_END_DECLS

#endif /* __IC_ADS_ENROLL_H__ */
