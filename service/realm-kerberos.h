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

#ifndef __REALM_KERBEROS_H__
#define __REALM_KERBEROS_H__

#include <gio/gio.h>

#include "realm-dbus-generated.h"

G_BEGIN_DECLS

#define REALM_TYPE_KERBEROS            (realm_kerberos_get_type ())
#define REALM_KERBEROS(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_KERBEROS, RealmKerberos))
#define REALM_IS_KERBEROS(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_KERBEROS))
#define REALM_KERBEROS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), REALM_TYPE_KERBEROS, RealmKerberosClass))
#define REALM_IS_KERBEROS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), REALM_TYPE_KERBEROS))
#define REALM_KERBEROS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), REALM_TYPE_KERBEROS, RealmKerberosClass))

typedef struct _RealmKerberos RealmKerberos;
typedef struct _RealmKerberosClass RealmKerberosClass;
typedef struct _RealmKerberosPrivate RealmKerberosPrivate;

struct _RealmKerberos {
	RealmDbusKerberosRealmSkeleton parent;
	RealmKerberosPrivate *pv;
};

struct _RealmKerberosClass {
	RealmDbusKerberosRealmSkeletonClass parent_class;

	void       (* enroll_async)    (RealmKerberos *realm,
	                                GBytes *admin_kerberos_cache,
	                                GDBusMethodInvocation *invocation,
	                                GAsyncReadyCallback callback,
	                                gpointer user_data);

	gboolean   (* enroll_finish)   (RealmKerberos *realm,
	                                GAsyncResult *result,
	                                GError **error);

	void       (* unenroll_async)  (RealmKerberos *realm,
	                                GBytes *admin_kerberos_cache,
	                                GDBusMethodInvocation *invocation,
	                                GAsyncReadyCallback callback,
	                                gpointer user_data);

	gboolean   (* unenroll_finish) (RealmKerberos *realm,
	                                GAsyncResult *result,
	                                GError **error);
};

GType               realm_kerberos_get_type              (void) G_GNUC_CONST;

void                realm_kerberos_set_discovery         (RealmKerberos *self,
                                                          GHashTable *discovery);

GHashTable *        realm_kerberos_get_discovery         (RealmKerberos *self);

G_END_DECLS

#endif /* __REALM_KERBEROS_H__ */
