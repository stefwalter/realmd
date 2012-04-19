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

#ifndef __REALM_SERVICE_H__
#define __REALM_SERVICE_H__

#include <gio/gio.h>

G_BEGIN_DECLS

gboolean            realm_service_lock_for_action            (GDBusMethodInvocation *invocation);

void                realm_service_unlock_for_action          (GDBusMethodInvocation *invocation);

const gchar *       realm_service_resolve_file               (const gchar *name);

void                realm_service_hold                       (void);

void                realm_service_release                    (void);

void                realm_service_poke                       (void);

G_END_DECLS

#endif /* __REALM_SERVICE_H__ */
