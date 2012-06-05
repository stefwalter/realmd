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

gboolean            realm_samba_config_change                   (const gchar *section,
                                                                 GError **error,
                                                                 ...) G_GNUC_NULL_TERMINATED;

gboolean            realm_samba_config_changev                  (const gchar *section,
                                                                 GHashTable *parameters,
                                                                 GError **error);

gboolean            realm_samba_config_change_list              (const gchar *section,
                                                                 const gchar *name,
                                                                 const gchar *delimiters,
                                                                 const gchar **add,
                                                                 const gchar **remove,
                                                                 GError **error);

G_END_DECLS

#endif /* __REALM_SAMBA_CONFIG_H__ */
