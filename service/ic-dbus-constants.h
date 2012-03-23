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

#define   IC_DBUS_ERROR_INTERNAL                "org.freedesktop.IdentityConfig.Error.Internal"
#define   IC_DBUS_ERROR_DISCOVERY_FAILED        "org.freedesktop.IdentityConfig.Error.DiscoveryFailed"
#define   IC_DBUS_ERROR_ENROLL_FAILED           "org.freedesktop.IdentityConfig.Error.EnrollFailed"
#define   IC_DBUS_ERROR_UNENROLL_FAILED         "org.freedesktop.IdentityConfig.Error.UnenrollFailed"
#define   IC_DBUS_ERROR_SET_LOGINS_FAILED       "org.freedesktop.IdentityConfig.Error.SetLoginsFailed"
#define   IC_DBUS_ERROR_BUSY                    "org.freedesktop.IdentityConfig.Error.Busy"

#define   IC_DBUS_DISCOVERY_TYPE                "type"
#define   IC_DBUS_DISCOVERY_DOMAIN              "domain"
#define   IC_DBUS_DISCOVERY_SERVERS             "kerberos-servers"
#define   IC_DBUS_DISCOVERY_REALM               "kerberos-realm"

G_END_DECLS

#endif /* __IC_AD_ENROLL_H__ */
