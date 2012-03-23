/* identity-config - Identity configuration service
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

#ifndef __IC_PACKAGES_H__
#define __IC_PACKAGES_H__

#include <gio/gio.h>

G_BEGIN_DECLS

gboolean          ic_packages_check_paths             (const gchar **paths,
                                                       GDBusMethodInvocation *invocation);

void              ic_packages_install_async           (const gchar **required_files,
                                                       const gchar **package_names,
                                                       GDBusMethodInvocation *invocation,
                                                       GAsyncReadyCallback callback,
                                                       gpointer user_data);

gboolean          ic_packages_install_finish          (GAsyncResult *result,
                                                       GError **error);

G_END_DECLS

#endif /* __IC_PACKAGES_H__ */
