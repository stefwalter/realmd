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

#ifndef __REALM_EXAMPLE_PROVIDER_H__
#define __REALM_EXAMPLE_PROVIDER_H__

#include <gio/gio.h>

#include "realm-provider.h"

G_BEGIN_DECLS

#define REALM_TYPE_EXAMPLE_PROVIDER            (realm_example_provider_get_type ())
#define REALM_EXAMPLE_PROVIDER(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_EXAMPLE_PROVIDER, RealmExampleProvider))
#define REALM_IS_EXAMPLE_PROVIDER(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_EXAMPLE_PROVIDER))

typedef struct _RealmExampleProvider RealmExampleProvider;

GType               realm_example_provider_get_type               (void) G_GNUC_CONST;

RealmProvider *     realm_example_provider_new                    (void);

G_END_DECLS

#endif /* __REALM_EXAMPLE_PROVIDER_H__ */
