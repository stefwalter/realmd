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

#ifndef __REALM_PACKAGES_H__
#define __REALM_PACKAGES_H__

#include <gio/gio.h>

G_BEGIN_DECLS

gchar **          realm_packages_expand_sets             (const gchar **package_sets);

void              realm_packages_install_async           (const gchar **package_sets,
							  GDBusMethodInvocation *invocation,
							  GDBusConnection *connection,
                                                          GAsyncReadyCallback callback,
                                                          gpointer user_data);

gboolean          realm_packages_install_finish          (GAsyncResult *result,
                                                          GError **error);

G_END_DECLS

#endif /* __REALM_PACKAGES_H__ */
