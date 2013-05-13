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

#ifndef __REALM_DAEMON_H__
#define __REALM_DAEMON_H__

#include <gio/gio.h>

G_BEGIN_DECLS

void                 realm_daemon_hold                       (const gchar *identifier);

gboolean             realm_daemon_release                    (const gchar *identifier);

gboolean             realm_daemon_is_dbus_peer               (void);

gboolean             realm_daemon_is_install_mode            (void);

gboolean             realm_daemon_has_debug_flag             (void);

void                 realm_daemon_poke                       (void);

void                 realm_daemon_export_object              (GDBusObjectSkeleton *object);

void                 realm_daemon_syslog                     (const gchar *operation,
                                                              int prio,
                                                              const gchar *format,
                                                              ...) G_GNUC_PRINTF(3, 4);

G_END_DECLS

#endif /* __REALM_DAEMON_H__ */
