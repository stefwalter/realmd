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

#ifndef __REALM_DISCOVERY_H__
#define __REALM_DISCOVERY_H__

#include <glib.h>

G_BEGIN_DECLS

GHashTable *   realm_discovery_new              (void);

void           realm_discovery_add_string       (GHashTable *discovery,
                                                 const gchar *type,
                                                 const gchar *value);

const gchar *  realm_discovery_get_string       (GHashTable *discovery,
                                                 const gchar *type);

gboolean       realm_discovery_has_string       (GHashTable *discovery,
                                                 const gchar *type,
                                                 const gchar *value);

void           realm_discovery_add_variant      (GHashTable *discovery,
                                                 const gchar *type,
                                                 GVariant *value);

void           realm_discovery_add_srv_targets  (GHashTable *discovery,
                                                 const gchar *type,
                                                 GList *targets);

GVariant *     realm_discovery_to_variant       (GHashTable *discovery);

G_END_DECLS

#endif /* __REALM_AD_DISCOVER_H__ */
