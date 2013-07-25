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
	REALM_INI_NONE = 0,
	REALM_INI_LINE_CONTINUATIONS = 1 << 1,
	REALM_INI_NO_WATCH = 1 << 2,
	REALM_INI_PRIVATE = 1 << 3,
	REALM_INI_STRICT_BOOLEAN = 1 << 4,
} RealmIniFlags;

#define REALM_TYPE_INI_CONFIG            (realm_ini_config_get_type ())
#define REALM_INI_CONFIG(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_INI_CONFIG, RealmIniConfig))
#define REALM_IS_INI_CONFIG(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_INI_CONFIG))

typedef struct _RealmIniConfig RealmIniConfig;

GType               realm_ini_config_get_type                 (void) G_GNUC_CONST;

RealmIniConfig *    realm_ini_config_new                      (RealmIniFlags flags);

void                realm_ini_config_reset                    (RealmIniConfig *self);

gboolean            realm_ini_config_begin_change             (RealmIniConfig *self,
                                                               GError **error);

void                realm_ini_config_abort_change             (RealmIniConfig *self);

gboolean            realm_ini_config_finish_change            (RealmIniConfig *self,
                                                               GError **error);

const gchar *       realm_ini_config_get_filename             (RealmIniConfig *self);

void                realm_ini_config_set_filename             (RealmIniConfig *self,
                                                               const gchar *filename);

void                realm_ini_config_reload                   (RealmIniConfig *self);

void                realm_ini_config_read_string              (RealmIniConfig *self,
                                                               const gchar *data);

gchar *             realm_ini_config_write_string             (RealmIniConfig *self);

void                realm_ini_config_read_bytes               (RealmIniConfig *self,
                                                               GBytes *data);

GBytes *            realm_ini_config_write_bytes              (RealmIniConfig *self);

gboolean            realm_ini_config_read_file                (RealmIniConfig *self,
                                                               const gchar *filename,
                                                               GError **error);

gboolean            realm_ini_config_write_file               (RealmIniConfig *self,
                                                               const gchar *filename,
                                                               GError **error);

gboolean            realm_ini_config_write_fd                 (RealmIniConfig *self,
                                                               gint fd,
                                                               GError **error);

void                realm_ini_config_set                      (RealmIniConfig *self,
                                                               const gchar *section,
                                                               const gchar *name,
                                                               const gchar *value,
                                                               ...) G_GNUC_NULL_TERMINATED;

gchar *             realm_ini_config_get                      (RealmIniConfig *self,
                                                               const gchar *section,
                                                               const gchar *name);

gboolean            realm_ini_config_have                     (RealmIniConfig *self,
                                                               const gchar *section,
                                                               const gchar *name);

gchar **            realm_ini_config_get_list                 (RealmIniConfig *self,
                                                               const gchar *section,
                                                               const gchar *name,
                                                               const gchar *delimiters);

void                realm_ini_config_set_list                 (RealmIniConfig *self,
                                                               const gchar *section,
                                                               const gchar *name,
                                                               const gchar *delimiter,
                                                               const gchar **values);

void                realm_ini_config_set_list_diff            (RealmIniConfig *self,
                                                               const gchar *section,
                                                               const gchar *name,
                                                               const gchar *delimiter,
                                                               const gchar **add,
                                                               const gchar **remove);

GHashTable *        realm_ini_config_get_all                  (RealmIniConfig *self,
                                                               const gchar *section);

void                realm_ini_config_set_all                  (RealmIniConfig *self,
                                                               const gchar *section,
                                                               GHashTable *parameters);

gboolean            realm_ini_config_get_boolean              (RealmIniConfig *config,
                                                               const gchar *section,
                                                               const gchar *name,
                                                               gboolean defahlt);

gchar **            realm_ini_config_get_sections             (RealmIniConfig *self);

gboolean            realm_ini_config_have_section             (RealmIniConfig *self,
                                                               const gchar *section);

void                realm_ini_config_remove_section           (RealmIniConfig *self,
                                                               const gchar *section);

gboolean            realm_ini_config_change                   (RealmIniConfig *self,
                                                               const gchar *section,
                                                               GError **error,
                                                               ...) G_GNUC_NULL_TERMINATED;

gboolean            realm_ini_config_changev                  (RealmIniConfig *self,
                                                               const gchar *section,
                                                               GHashTable *parameters,
                                                               GError **error);

gboolean            realm_ini_config_change_list              (RealmIniConfig *self,
                                                               const gchar *section,
                                                               const gchar *name,
                                                               const gchar *delimiters,
                                                               const gchar **add,
                                                               const gchar **remove,
                                                               GError **error);

G_END_DECLS

#endif /* __REALM_INI_CONFIG_H__ */
