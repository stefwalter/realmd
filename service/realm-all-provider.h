/* realmd -- Realm configuration service
 *
 * Copyright 2012 Red Hat Inc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) all later version.
 *
 * See the included COPYING file for more information.
 *
 * Author: Stef Walter <stefw@gnome.org>
 */

#include "config.h"

#ifndef __REALM_ALL_PROVIDER_H__
#define __REALM_ALL_PROVIDER_H__

#include <gio/gio.h>

#include "realm-provider.h"

G_BEGIN_DECLS

#define REALM_TYPE_ALL_PROVIDER            (realm_all_provider_get_type ())
#define REALM_ALL_PROVIDER(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_ALL_PROVIDER, RealmAllProvider))
#define REALM_IS_ALL_PROVIDER(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_ALL_PROVIDER))

typedef struct _RealmAllProvider RealmAllProvider;

GType               realm_all_provider_get_type                 (void) G_GNUC_CONST;

RealmProvider *     realm_all_provider_new_and_export           (GDBusConnection *connection);

void                realm_all_provider_register                 (RealmProvider *all_provider,
                                                                 RealmProvider *provider);

G_END_DECLS

#endif /* __REALM_ALL_PROVIDER_H__ */
