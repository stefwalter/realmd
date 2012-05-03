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

#ifndef __REALM_INI_CONFIG_H__
#define __REALM_INI_CONFIG_H__

#include <gio/gio.h>

typedef enum {
	REALM_INI_LINE_CONTINUATIONS = 1 << 1,
} RealmIniFlags;

#define REALM_TYPE_INI_CONFIG            (realm_ini_config_get_type ())
#define REALM_INI_CONFIG(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_INI_CONFIG, RealmIniConfig))
#define REALM_IS_INI_CONFIG(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_INI_CONFIG))

typedef struct _RealmIniConfig RealmIniConfig;

GType               realm_ini_config_get_type                 (void) G_GNUC_CONST;

RealmIniConfig *    realm_ini_config_new                      (RealmIniFlags flags);

void                realm_ini_config_read_string              (RealmIniConfig *self,
                                                               const gchar *data);

void                realm_ini_config_read_bytes               (RealmIniConfig *self,
                                                               GBytes *data);

GBytes *            realm_ini_config_write_bytes              (RealmIniConfig *self);

gboolean            realm_ini_config_read_file                (RealmIniConfig *self,
                                                               const gchar *filename,
                                                               GError **error);

gboolean            realm_ini_config_write_file               (RealmIniConfig *self,
                                                               const gchar *filename,
                                                               GError **error);

void                realm_ini_config_set                      (RealmIniConfig *self,
                                                               const gchar *section,
                                                               const gchar *name,
                                                               const gchar *value);

gchar *             realm_ini_config_get                      (RealmIniConfig *self,
                                                               const gchar *section,
                                                               const gchar *name);

GHashTable *        realm_ini_config_get_all                  (RealmIniConfig *self,
                                                                 const gchar *section);

void                realm_ini_config_set_all                  (RealmIniConfig *self,
                                                               const gchar *section,
                                                               GHashTable *parameters);

gboolean            realm_ini_config_change                   (const gchar *section,
                                                               GError **error,
                                                               ...) G_GNUC_NULL_TERMINATED;

gboolean            realm_ini_config_changev                  (const gchar *section,
                                                               GHashTable *parameters,
                                                               GError **error);

G_END_DECLS

#endif /* __REALM_INI_CONFIG_H__ */
