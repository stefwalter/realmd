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

#ifndef __REALM_AD_PROVIDER_H__
#define __REALM_AD_PROVIDER_H__

#include <gio/gio.h>

#include "realm-kerberos-provider.h"

G_BEGIN_DECLS

/*
 * Various files that we need to get AD working. The packages above supply
 * these files. Unlike the list above, *all of the files below must exist*
 * in order to proceed.
 *
 * If a distro has a different path for a given file, then add a configure.ac
 * --with-xxx and AC_DEFINE for it, replacing the constant here.
 */

#define REALM_TYPE_AD_PROVIDER            (realm_ad_provider_get_type ())
#define REALM_AD_PROVIDER(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_AD_PROVIDER, RealmAdProvider))
#define REALM_IS_AD_PROVIDER(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_AD_PROVIDER))

typedef struct _RealmAdProvider RealmAdProvider;

GType               realm_ad_provider_get_type                 (void) G_GNUC_CONST;

void                realm_ad_provider_start                    (GDBusConnection *connection);

void                realm_ad_provider_stop                     (void);

G_END_DECLS

#endif /* __REALM_AD_PROVIDER_H__ */
