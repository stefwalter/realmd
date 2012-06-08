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

#define REALM_TYPE_SERVICE            (realm_service_get_type ())
#define REALM_SERVICE(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_SERVICE, RealmService))
#define REALM_IS_SERVICE(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_SERVICE))
#define REALM_SERVICE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), REALM_TYPE_SERVICE, RealmServiceClass))
#define REALM_IS_SERVICE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), REALM_TYPE_SERVICE))
#define REALM_SERVICE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), REALM_TYPE_SERVICE, RealmServiceClass))

typedef struct _RealmService RealmService;
typedef struct _RealmServiceClass RealmServiceClass;

struct _RealmService {
	GDBusProxy parent;
};

struct _RealmServiceClass {
	GDBusProxyClass parent_class;

	void      (* enable)                    (RealmService *service,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	gboolean  (* enable_finish)             (RealmService *service,
	                                         GAsyncResult *result,
	                                         GError **error);

	void      (* disable)                   (RealmService *service,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	gboolean  (* disable_finish)            (RealmService *service,
	                                         GAsyncResult *result,
	                                         GError **error);

	void      (* restart)                   (RealmService *service,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	gboolean  (* restart_finish)            (RealmService *service,
	                                         GAsyncResult *result,
	                                         GError **error);

	void      (* stop)                      (RealmService *service,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	gboolean  (* stop_finish)               (RealmService *service,
	                                         GAsyncResult *result,
	                                         GError **error);
};

GType            realm_service_get_type                  (void) G_GNUC_CONST;

void             realm_service_new                       (const gchar *service_name,
                                                   GDBusMethodInvocation *invocation,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data);

RealmService *   realm_service_new_finish         (GAsyncResult *result,
                                                   GError **error);

void             realm_service_enable             (RealmService *service,
                                                   GDBusMethodInvocation *invocation,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data);

gboolean         realm_service_enable_finish      (RealmService *service,
                                                   GAsyncResult *result,
                                                   GError **error);

void             realm_service_disable            (RealmService *service,
                                                   GDBusMethodInvocation *invocation,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data);

gboolean         realm_service_disable_finish     (RealmService *service,
                                                   GAsyncResult *result,
                                                   GError **error);

void             realm_service_restart            (RealmService *service,
                                                   GDBusMethodInvocation *invocation,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data);

gboolean         realm_service_restart_finish     (RealmService *service,
                                                   GAsyncResult *result,
                                                   GError **error);

void             realm_service_stop               (RealmService *service,
                                                   GDBusMethodInvocation *invocation,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data);

gboolean         realm_service_stop_finish        (RealmService *service,
                                                   GAsyncResult *result,
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
