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

#ifndef __REALM_DBUS_CONSTANTS_H__
#define __REALM_DBUS_CONSTANTS_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define   DBUS_PEER_INTERFACE                      "org.freedesktop.DBus.Peer"
#define   DBUS_PROPERTIES_INTERFACE                "org.freedesktop.DBus.Properties"
#define   DBUS_INTROSPECTABLE_INTERFACE            "org.freedesktop.DBus.Introspectable"

#define   REALM_DBUS_ALL_PROVIDER_NAME             "org.freedesktop.realmd"
#define   REALM_DBUS_ALL_PROVIDER_PATH             "/org/freedesktop/realmd"

#define   REALM_DBUS_SAMBA_NAME                    "org.freedesktop.realmd.Samba"
#define   REALM_DBUS_SAMBA_PATH                    "/org/freedesktop/realmd/Samba"

#define   REALM_DBUS_PROVIDER_INTERFACE            "org.freedesktop.realmd.Provider"
#define   REALM_DBUS_KERBEROS_REALM_INTERFACE      "org.freedesktop.realmd.KerberosRealm"
#define   REALM_DBUS_DIAGNOSTICS_INTERFACE         "org.freedesktop.realmd.Diagnostics"

#define   REALM_DBUS_DIAGNOSTICS_SIGNAL            "Diagnostics"

#define   REALM_DBUS_ERROR_INTERNAL                "org.freedesktop.realmd.Error.Internal"
#define   REALM_DBUS_ERROR_DISCOVERED_NOTHING      "org.freedesktop.realmd.Error.DiscoveredNothing"
#define   REALM_DBUS_ERROR_DISCOVERY_FAILED        "org.freedesktop.realmd.Error.DiscoveryFailed"
#define   REALM_DBUS_ERROR_ENROLL_FAILED           "org.freedesktop.realmd.Error.EnrollFailed"
#define   REALM_DBUS_ERROR_UNENROLL_FAILED         "org.freedesktop.realmd.Error.UnenrollFailed"
#define   REALM_DBUS_ERROR_BUSY                    "org.freedesktop.realmd.Error.Busy"
#define   REALM_DBUS_ERROR_NOT_AUTHORIZED          "org.freedesktop.realmd.Error.NotAuthorized"
#define   REALM_DBUS_ERROR_ALREADY_ENROLLED        "org.freedesktop.realmd.Error.AlreadyEnrolled"
#define   REALM_DBUS_ERROR_NOT_ENROLLED            "org.freedesktop.realmd.Error.NotEnrolled"
#define   REALM_DBUS_ERROR_AUTH_FAILED             "org.freedesktop.realmd.Error.AuthFailed"

#define   REALM_DBUS_DISCOVERY_TYPE                "type"
#define   REALM_DBUS_DISCOVERY_DOMAIN              "domain"
#define   REALM_DBUS_DISCOVERY_KDCS                "kerberos-kdcs"
#define   REALM_DBUS_DISCOVERY_REALM               "kerberos-realm"

#define   REALM_DBUS_NAME_CHARS                    "abcdefghijklnmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"

G_END_DECLS

#endif /* __REALM_DBUS_CONSTANTS_H__ */
