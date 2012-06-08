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

#ifndef __REALM_SERVICE_SYSTEMD_H__
#define __REALM_SERVICE_SYSTEMD_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define REALM_TYPE_SERVICE_SYSTEMD      (realm_service_systemd_get_type ())
#define REALM_SERVICE_SYSTEMD(inst)     (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_SERVICE_SYSTEMD, RealmServiceSystemd))
#define REALM_IS_SERVICE_SYSTEMD(inst)  (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_SERVICE_SYSTEMD))

typedef struct _RealmServiceSystemd RealmServiceSystemd;

GType           realm_service_systemd_get_type       (void) G_GNUC_CONST;

void            realm_service_systemd_new            (const gchar *service_name,
                                                      GAsyncReadyCallback callback,
                                                      gpointer user_data);

RealmService *  realm_service_systemd_new_finish     (GAsyncResult *result,
                                                      GError **error);

G_END_DECLS

#endif /* __REALM_SERVICE_SYSTEMD_H__ */
