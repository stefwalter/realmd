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

#ifndef __REALM_SERVICE_H__
#define __REALM_SERVICE_H__

#include <gio/gio.h>

G_BEGIN_DECLS

void             realm_service_enable                     (const gchar *service_name,
                                                           GDBusMethodInvocation *invocation,
                                                           GAsyncReadyCallback callback,
                                                           gpointer user_data);

gboolean         realm_service_enable_finish              (GAsyncResult *result,
                                                           GError **error);

void             realm_service_disable                    (const gchar *service_name,
                                                           GDBusMethodInvocation *invocation,
                                                           GAsyncReadyCallback callback,
                                                           gpointer user_data);

gboolean         realm_service_disable_finish             (GAsyncResult *result,
                                                           GError **error);

void             realm_service_stop                       (const gchar *service_name,
                                                           GDBusMethodInvocation *invocation,
                                                           GAsyncReadyCallback callback,
                                                           gpointer user_data);

gboolean         realm_service_stop_finish                (GAsyncResult *result,
                                                           GError **error);

void             realm_service_restart                    (const gchar *service_name,
                                                           GDBusMethodInvocation *invocation,
                                                           GAsyncReadyCallback callback,
                                                           gpointer user_data);

gboolean         realm_service_restart_finish             (GAsyncResult *result,
                                                           GError **error);

void             realm_service_enable_and_restart         (const gchar *service_name,
                                                           GDBusMethodInvocation *invocation,
                                                           GAsyncReadyCallback callback,
                                                           gpointer user_data);

gboolean         realm_service_enable_and_restart_finish  (GAsyncResult *result,
                                                           GError **error);

void             realm_service_disable_and_stop           (const gchar *service_name,
                                                           GDBusMethodInvocation *invocation,
                                                           GAsyncReadyCallback callback,
                                                           gpointer user_data);

gboolean         realm_service_disable_and_stop_finish    (GAsyncResult *result,
                                                           GError **error);


G_END_DECLS

#endif /* __REALM_KERBEROS_SERVICE_H__ */
