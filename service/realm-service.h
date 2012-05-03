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

#define REALM_TYPE_SERVICE            (realm_service_get_type ())
#define REALM_SERVICE(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_SERVICE, RealmService))
#define REALM_IS_SERVICE(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_SERVICE))

typedef struct _RealmService RealmService;

GType               realm_service_get_type                 (void) G_GNUC_CONST;

void                realm_service_start                    (GDBusConnection *connection);

void                realm_service_stop                     (void);

G_END_DECLS

#endif /* __REALM_SERVICE_H__ */
