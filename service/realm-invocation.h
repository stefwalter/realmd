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

#ifndef __REALM_INVOCATION_H__
#define __REALM_INVOCATION_H__

#include <gio/gio.h>

G_BEGIN_DECLS

void                 realm_invocation_initialize             (GDBusConnection *connection);

void                 realm_invocation_cleanup                (void);

gboolean             realm_invocation_authorize              (GDBusMethodInvocation *invocation);

GCancellable *       realm_invocation_get_cancellable        (GDBusMethodInvocation *invocation);

const gchar *        realm_invocation_get_operation          (GDBusMethodInvocation *invocation);

const gchar *        realm_invocation_get_key                (GDBusMethodInvocation *invocation);

gboolean             realm_invocation_lock_daemon            (GDBusMethodInvocation *invocation);

void                 realm_invocation_unlock_daemon          (GDBusMethodInvocation *invocation);

G_END_DECLS

#endif /* __REALM_INVOCATION_H__ */
