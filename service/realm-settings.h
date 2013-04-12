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

#ifndef __REALM_SETTINGS_H__
#define __REALM_SETTINGS_H__

#include <glib.h>

G_BEGIN_DECLS

void                 realm_settings_init                  (void);

void                 realm_settings_uninit                (void);

gboolean             realm_settings_load                  (const gchar *filename,
                                                           GError **error);

void                 realm_settings_add                   (const gchar *section,
                                                           const gchar *key,
                                                           const gchar *value);

const gchar *        realm_settings_path                  (const gchar *name);

GHashTable *         realm_settings_section               (const gchar *section);

const gchar *        realm_settings_value                 (const gchar *section,
                                                           const gchar *key);

const gchar *        realm_settings_string                (const gchar *section,
                                                           const gchar *key);

gdouble              realm_settings_double                (const gchar *section,
                                                           const gchar *key,
                                                           gdouble def);

gboolean             realm_settings_boolean               (const gchar *section,
                                                           const gchar *key,
                                                           gboolean def);

G_END_DECLS

#endif /* __REALM_SETTINGS_H__ */
