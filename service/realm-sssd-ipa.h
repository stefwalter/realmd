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

#ifndef __REALM_SSSD_IPA_H__
#define __REALM_SSSD_IPA_H__

#include <gio/gio.h>

#include "realm-kerberos.h"
#include "realm-provider.h"

G_BEGIN_DECLS

#define REALM_TYPE_SSSD_IPA            (realm_sssd_ipa_get_type ())
#define REALM_SSSD_IPA(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_SSSD_IPA, RealmSssdAd))
#define REALM_IS_SSSD_IPA(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_SSSD_IPA))

typedef struct _RealmSssdIpa RealmSssdIpa;

GType                   realm_sssd_ipa_get_type    (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __REALM_SSSD_IPA_H__ */
