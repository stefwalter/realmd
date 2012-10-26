/*
 * This file is copied from glib in order to fix:
 * https://bugzilla.gnome.org/show_bug.cgi?id=686920
 */

/* GDBus - GLib D-Bus Library
 *
 * Copyright (C) 2008-2010 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#ifndef __EGG_DBUS_OBJECT_MANAGER_CLIENT_H__
#define __EGG_DBUS_OBJECT_MANAGER_CLIENT_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define EGG_TYPE_DBUS_OBJECT_MANAGER_CLIENT         (egg_dbus_object_manager_client_get_type ())
#define EGG_DBUS_OBJECT_MANAGER_CLIENT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EGG_TYPE_DBUS_OBJECT_MANAGER_CLIENT, EggDBusObjectManagerClient))
#define EGG_DBUS_OBJECT_MANAGER_CLIENT_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EGG_TYPE_DBUS_OBJECT_MANAGER_CLIENT, EggDBusObjectManagerClientClass))
#define EGG_DBUS_OBJECT_MANAGER_CLIENT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EGG_TYPE_DBUS_OBJECT_MANAGER_CLIENT, EggDBusObjectManagerClientClass))
#define EGG_IS_DBUS_OBJECT_MANAGER_CLIENT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EGG_TYPE_DBUS_OBJECT_MANAGER_CLIENT))
#define EGG_IS_DBUS_OBJECT_MANAGER_CLIENT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EGG_TYPE_DBUS_OBJECT_MANAGER_CLIENT))

typedef struct _EggDBusObjectManagerClient        EggDBusObjectManagerClient;
typedef struct _EggDBusObjectManagerClientClass   EggDBusObjectManagerClientClass;
typedef struct _EggDBusObjectManagerClientPrivate EggDBusObjectManagerClientPrivate;

/**
 * EggDBusObjectManagerClient:
  *
 * The #EggDBusObjectManagerClient structure contains private data and should
 * only be accessed using the provided API.
 *
 * Since: 2.30
 */
struct _EggDBusObjectManagerClient
{
  /*< private >*/
  GObject parent_instance;
  EggDBusObjectManagerClientPrivate *priv;
};

/**
 * EggDBusObjectManagerClientClass:
 * @parent_class: The parent class.
 * @interface_proxy_signal: Signal class handler for the #EggDBusObjectManagerClient::interface-proxy-signal signal.
 * @interface_proxy_properties_changed: Signal class handler for the #EggDBusObjectManagerClient::interface-proxy-properties-changed signal.
 *
 * Class structure for #EggDBusObjectManagerClient.
 *
 * Since: 2.30
 */
struct _EggDBusObjectManagerClientClass
{
  GObjectClass parent_class;

  /* signals */
  void    (*interface_proxy_signal)             (EggDBusObjectManagerClient *manager,
                                                 GDBusObjectProxy         *object_proxy,
                                                 GDBusProxy               *interface_proxy,
                                                 const gchar              *sender_name,
                                                 const gchar              *signal_name,
                                                 GVariant                 *parameters);

  void    (*interface_proxy_properties_changed) (EggDBusObjectManagerClient   *manager,
                                                 GDBusObjectProxy           *object_proxy,
                                                 GDBusProxy                 *interface_proxy,
                                                 GVariant                   *changed_properties,
                                                 const gchar* const         *invalidated_properties);

  /*< private >*/
  gpointer padding[8];
};

typedef GType (*EggDBusProxyTypeFunc) (EggDBusObjectManagerClient   *manager,
                                       const gchar                *object_path,
                                       const gchar                *interface_name,
                                       gpointer                    user_data);

GType                         egg_dbus_object_manager_client_get_type           (void) G_GNUC_CONST;
void                          egg_dbus_object_manager_client_new                (GDBusConnection               *connection,
                                                                               GDBusObjectManagerClientFlags  flags,
                                                                               const gchar                   *name,
                                                                               const gchar                   *object_path,
                                                                               EggDBusProxyTypeFunc           get_proxy_type_func,
                                                                               gpointer                       get_proxy_type_user_data,
                                                                               GDestroyNotify                 get_proxy_type_destroy_notify,
                                                                               GCancellable                  *cancellable,
                                                                               GAsyncReadyCallback            callback,
                                                                               gpointer                       user_data);
GDBusObjectManager           *egg_dbus_object_manager_client_new_finish         (GAsyncResult                  *res,
                                                                               GError                       **error);
GDBusObjectManager           *egg_dbus_object_manager_client_new_sync           (GDBusConnection               *connection,
                                                                               GDBusObjectManagerClientFlags  flags,
                                                                               const gchar                   *name,
                                                                               const gchar                   *object_path,
                                                                               EggDBusProxyTypeFunc           get_proxy_type_func,
                                                                               gpointer                       get_proxy_type_user_data,
                                                                               GDestroyNotify                 get_proxy_type_destroy_notify,
                                                                               GCancellable                  *cancellable,
                                                                               GError                       **error);
void                          egg_dbus_object_manager_client_new_for_bus        (GBusType                       bus_type,
                                                                               GDBusObjectManagerClientFlags  flags,
                                                                               const gchar                   *name,
                                                                               const gchar                   *object_path,
                                                                               EggDBusProxyTypeFunc           get_proxy_type_func,
                                                                               gpointer                       get_proxy_type_user_data,
                                                                               GDestroyNotify                 get_proxy_type_destroy_notify,
                                                                               GCancellable                  *cancellable,
                                                                               GAsyncReadyCallback            callback,
                                                                               gpointer                       user_data);
GDBusObjectManager           *egg_dbus_object_manager_client_new_for_bus_finish (GAsyncResult                  *res,
                                                                               GError                       **error);
GDBusObjectManager           *egg_dbus_object_manager_client_new_for_bus_sync   (GBusType                       bus_type,
                                                                               GDBusObjectManagerClientFlags  flags,
                                                                               const gchar                   *name,
                                                                               const gchar                   *object_path,
                                                                               EggDBusProxyTypeFunc           get_proxy_type_func,
                                                                               gpointer                       get_proxy_type_user_data,
                                                                               GDestroyNotify                 get_proxy_type_destroy_notify,
                                                                               GCancellable                  *cancellable,
                                                                               GError                       **error);
GDBusConnection              *egg_dbus_object_manager_client_get_connection     (EggDBusObjectManagerClient      *manager);
GDBusObjectManagerClientFlags egg_dbus_object_manager_client_get_flags          (EggDBusObjectManagerClient      *manager);
const gchar                  *egg_dbus_object_manager_client_get_name           (EggDBusObjectManagerClient      *manager);
gchar                        *egg_dbus_object_manager_client_get_name_owner     (EggDBusObjectManagerClient      *manager);

G_END_DECLS

#endif /* __EGG_DBUS_OBJECT_MANAGER_CLIENT_H */
