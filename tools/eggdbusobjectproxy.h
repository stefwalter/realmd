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

#ifndef __EGG_DBUS_OBJECT_PROXY_H__
#define __EGG_DBUS_OBJECT_PROXY_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define EGG_TYPE_DBUS_OBJECT_PROXY         (egg_dbus_object_proxy_get_type ())
#define EGG_DBUS_OBJECT_PROXY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EGG_TYPE_DBUS_OBJECT_PROXY, EggDBusObjectProxy))
#define EGG_DBUS_OBJECT_PROXY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EGG_TYPE_DBUS_OBJECT_PROXY, EggDBusObjectProxyClass))
#define EGG_DBUS_OBJECT_PROXY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EGG_TYPE_DBUS_OBJECT_PROXY, EggDBusObjectProxyClass))
#define EGG_IS_DBUS_OBJECT_PROXY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EGG_TYPE_DBUS_OBJECT_PROXY))
#define EGG_IS_DBUS_OBJECT_PROXY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EGG_TYPE_DBUS_OBJECT_PROXY))

typedef struct _EggDBusObjectProxy        EggDBusObjectProxy;
typedef struct _EggDBusObjectProxyClass   EggDBusObjectProxyClass;
typedef struct _EggDBusObjectProxyPrivate EggDBusObjectProxyPrivate;

/**
 * EggDBusObjectProxy:
 *
 * The #EggDBusObjectProxy structure contains private data and should
 * only be accessed using the provided API.
 *
 * Since: 2.30
 */
struct _EggDBusObjectProxy
{
  /*< private >*/
  GObject parent_instance;
  EggDBusObjectProxyPrivate *priv;
};

/**
 * EggDBusObjectProxyClass:
 * @parent_class: The parent class.
 *
 * Class structure for #EggDBusObjectProxy.
 *
 * Since: 2.30
 */
struct _EggDBusObjectProxyClass
{
  GObjectClass parent_class;

  /*< private >*/
  gpointer padding[8];
};

GType             egg_dbus_object_proxy_get_type       (void) G_GNUC_CONST;
EggDBusObjectProxy *egg_dbus_object_proxy_new            (GDBusConnection   *connection,
                                                      const gchar       *object_path);
GDBusConnection  *egg_dbus_object_proxy_get_connection (EggDBusObjectProxy  *proxy);

void
egg_dbus_object_proxy_add_interface (EggDBusObjectProxy *proxy,
                                    GDBusProxy       *interface_proxy);

void
egg_dbus_object_proxy_remove_interface (EggDBusObjectProxy *proxy,
                                       const gchar      *interface_name);

G_END_DECLS

#endif /* __EGG_DBUS_OBJECT_PROXY_H */
