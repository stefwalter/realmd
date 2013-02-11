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

#ifndef __REALM_SSSD_CONFIG_H__
#define __REALM_SSSD_CONFIG_H__

#include <gio/gio.h>

#include "realm-ini-config.h"

RealmIniConfig *    realm_sssd_config_new                      (GError **error);

RealmIniConfig *    realm_sssd_config_new_with_flags           (RealmIniFlags flags,
                                                                GError **error);

gchar **            realm_sssd_config_get_domains              (RealmIniConfig *config);

gchar *             realm_sssd_config_domain_to_section        (const gchar *domain);

gboolean            realm_sssd_config_have_domain              (RealmIniConfig *config,
                                                                const gchar *domain);

gboolean            realm_sssd_config_add_domain               (RealmIniConfig *config,
                                                                const gchar *domain,
                                                                GError **error,
                                                                ...) G_GNUC_NULL_TERMINATED;

gboolean            realm_sssd_config_update_domain            (RealmIniConfig *config,
                                                                const gchar *domain,
                                                                GError **error,
                                                                ...);

gboolean            realm_sssd_config_remove_domain            (RealmIniConfig *config,
                                                                const gchar *domain,
                                                                GError **error);

gboolean            realm_sssd_config_load_domain              (RealmIniConfig *config,
                                                                const gchar *domain,
                                                                gchar **section,
                                                                gchar **id_provider,
                                                                gchar **realm_name);

G_END_DECLS

#endif /* __REALM_SSSD_CONFIG_H__ */
