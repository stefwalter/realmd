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

gboolean             realm_daemon_lock_for_action            (GDBusMethodInvocation *invocation);

void                 realm_daemon_unlock_for_action          (GDBusMethodInvocation *invocation);

void                 realm_daemon_set_locale_until_loop      (GDBusMethodInvocation *invocation);

void                 realm_daemon_hold                       (const gchar *identifier);

void                 realm_daemon_release                    (const gchar *identifier);

void                 realm_daemon_set_locale                 (const gchar *sender,
                                                              const gchar *locale,
                                                              const gchar *operation_id);

gboolean             realm_daemon_has_debug_flag             (void);

void                 realm_daemon_poke                       (void);

gboolean             realm_daemon_check_dbus_action          (const gchar *sender,
                                                              const gchar *action_id);

void                 realm_daemon_export_object              (GDBusObjectSkeleton *object);

G_END_DECLS

#endif /* __REALM_DAEMON_H__ */
