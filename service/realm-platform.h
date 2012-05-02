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

#ifndef __REALM_PLATFORM_H__
#define __REALM_PLATFORM_H__

#include <glib.h>

G_BEGIN_DECLS

void                 realm_platform_init                  (void);

void                 realm_platform_uninit                (void);

gboolean             realm_platform_load                  (const gchar *filename,
                                                           GError **error);

void                 realm_platform_add                   (const gchar *section,
                                                           const gchar *key,
                                                           const gchar *value);

const gchar *        realm_platform_path                  (const gchar *name);

GHashTable *         realm_platform_settings              (const gchar *section);

const gchar *        realm_platform_value                 (const gchar *section,
                                                           const gchar *key);

const gchar *        realm_platform_string                (const gchar *section,
                                                           const gchar *key);

G_END_DECLS

#endif /* __REALM_PLATFORM_H__ */
