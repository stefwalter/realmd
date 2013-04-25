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

#ifndef __REALM_SAMBA_CONFIG_H__
#define __REALM_SAMBA_CONFIG_H__

#include <gio/gio.h>

#include "realm-ini-config.h"

#define REALM_SAMBA_CONFIG_GLOBAL    "global"

RealmIniConfig *    realm_samba_config_new                      (GError **error);

RealmIniConfig *    realm_samba_config_new_with_flags           (RealmIniFlags flags,
                                                                 GError **error);

gboolean            realm_samba_config_get_boolean              (RealmIniConfig *config,
                                                                 const gchar *section,
                                                                 const gchar *key,
                                                                 gboolean defalt);

G_END_DECLS

#endif /* __REALM_SAMBA_CONFIG_H__ */
