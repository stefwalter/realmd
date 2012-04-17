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

#ifndef __REALM_KERBEROS_PROVIDER_H__
#define __REALM_KERBEROS_PROVIDER_H__

#include <gio/gio.h>

#include "realm-dbus-generated.h"

G_BEGIN_DECLS

#define REALM_TYPE_KERBEROS_PROVIDER            (realm_kerberos_provider_get_type ())
#define REALM_KERBEROS_PROVIDER(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_KERBEROS_PROVIDER, RealmKerberosProvider))
#define REALM_IS_KERBEROS_PROVIDER(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_KERBEROS_PROVIDER))
#define REALM_KERBEROS_PROVIDER_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), REALM_TYPE_KERBEROS_PROVIDER, RealmKerberosProviderClass))
#define REALM_IS_KERBEROS_PROVIDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), REALM_TYPE_KERBEROS_PROVIDER))
#define REALM_KERBEROS_PROVIDER_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), REALM_TYPE_KERBEROS_PROVIDER, RealmKerberosProviderClass))

typedef struct _RealmKerberosProvider RealmKerberosProvider;
typedef struct _RealmKerberosProviderClass RealmKerberosProviderClass;
typedef struct _RealmKerberosProviderPrivate RealmKerberosProviderPrivate;

struct _RealmKerberosProvider {
	RealmDbusKerberosSkeleton parent;
	RealmKerberosProviderPrivate *pv;
};

struct _RealmKerberosProviderClass {
	RealmDbusKerberosSkeletonClass parent_class;

	void       (* discover_async)  (RealmKerberosProvider *provider,
	                                const gchar *string,
	                                GDBusMethodInvocation *invocation,
	                                GAsyncReadyCallback callback,
	                                gpointer user_data);

	gchar *    (* discover_finish) (RealmKerberosProvider *provider,
	                                GAsyncResult *result,
	                                GHashTable *discovery,
	                                GError **error);

	void       (* enroll_async)    (RealmKerberosProvider *provider,
	                                const gchar *realm,
	                                GBytes *admin_kerberos_cache,
	                                GDBusMethodInvocation *invocation,
	                                GAsyncReadyCallback callback,
	                                gpointer user_data);

	gboolean   (* enroll_finish)   (RealmKerberosProvider *provider,
	                                GAsyncResult *result,
	                                GError **error);

	void       (* unenroll_async)  (RealmKerberosProvider *provider,
	                                const gchar *realm,
	                                GBytes *admin_kerberos_cache,
	                                GDBusMethodInvocation *invocation,
	                                GAsyncReadyCallback callback,
	                                gpointer user_data);

	gboolean   (* unenroll_finish) (RealmKerberosProvider *provider,
	                                GAsyncResult *result,
	                                GError **error);

	void       (* logins_async)    (RealmKerberosProvider *provider,
	                                gboolean enabled,
	                                GDBusMethodInvocation *invocation,
	                                GAsyncReadyCallback callback,
	                                gpointer user_data);

	gboolean   (* logins_finish)   (RealmKerberosProvider *provider,
	                                GAsyncResult *result,
	                                GError **error);
};

GType               realm_kerberos_provider_get_type                 (void) G_GNUC_CONST;

GHashTable *        realm_kerberos_provider_lookup_discovery         (RealmKerberosProvider *provider,
                                                                      const gchar *realm);

G_END_DECLS

#endif /* __REALM_KERBEROS_PROVIDER_H__ */
