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

#define REALM_SAMBA_CONFIG_GLOBAL    "global"

#define REALM_TYPE_SAMBA_CONFIG            (realm_samba_config_get_type ())
#define REALM_SAMBA_CONFIG(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_SAMBA_CONFIG, RealmSambaConfig))
#define REALM_IS_SAMBA_CONFIG(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_SAMBA_CONFIG))

typedef struct _RealmSambaConfig RealmSambaConfig;

GType               realm_samba_config_get_type                 (void) G_GNUC_CONST;

RealmSambaConfig *  realm_samba_config_new                      (void);

void                realm_samba_config_read_string              (RealmSambaConfig *self,
                                                                 const gchar *data);

void                realm_samba_config_read_bytes               (RealmSambaConfig *self,
                                                                 GBytes *data);

GBytes *            realm_samba_config_write_bytes              (RealmSambaConfig *self);

gboolean            realm_samba_config_read_file                (RealmSambaConfig *self,
                                                                 const gchar *filename,
                                                                 GError **error);

gboolean            realm_samba_config_read_system              (RealmSambaConfig *self,
                                                                 GError **error);

gboolean            realm_samba_config_write_file               (RealmSambaConfig *self,
                                                                 const gchar *filename,
                                                                 GError **error);

gboolean            realm_samba_config_write_system             (RealmSambaConfig *self,
                                                                 GError **error);

void                realm_samba_config_set                      (RealmSambaConfig *self,
                                                                 const gchar *section,
                                                                 const gchar *name,
                                                                 const gchar *value);

gchar *             realm_samba_config_get                      (RealmSambaConfig *self,
                                                                 const gchar *section,
                                                                 const gchar *name);

GHashTable *        realm_samba_config_get_all                  (RealmSambaConfig *self,
                                                                 const gchar *section);

void                realm_samba_config_set_all                  (RealmSambaConfig *self,
                                                                 const gchar *section,
                                                                 GHashTable *parameters);

gboolean            realm_samba_config_change                   (const gchar *section,
                                                                 GError **error,
                                                                 ...) G_GNUC_NULL_TERMINATED;

gboolean            realm_samba_config_changev                  (const gchar *section,
                                                                 GHashTable *parameters,
                                                                 GError **error);

G_END_DECLS

#endif /* __REALM_SAMBA_CONFIG_H__ */
