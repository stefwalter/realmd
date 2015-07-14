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

#ifndef __REALM_SAMBA_WINBIND_H__
#define __REALM_SAMBA_WINBIND_H__

#include <gio/gio.h>

G_BEGIN_DECLS

void       realm_samba_winbind_configure_async      (RealmIniConfig *config,
                                                     const gchar *domain_name,
                                                     GVariant *options,
                                                     GDBusMethodInvocation *invocation,
                                                     GAsyncReadyCallback callback,
                                                     gpointer user_data);

gboolean   realm_samba_winbind_configure_finish     (GAsyncResult *result,
                                                     GError **error);

void       realm_samba_winbind_deconfigure_async    (RealmIniConfig *config,
                                                     GDBusMethodInvocation *invocation,
                                                     GAsyncReadyCallback callback,
                                                     gpointer user_data);

gboolean   realm_samba_winbind_deconfigure_finish   (GAsyncResult *result,
                                                     GError **error);

G_END_DECLS

#endif /* __REALM_SAMBA_WINBIND_H__ */
