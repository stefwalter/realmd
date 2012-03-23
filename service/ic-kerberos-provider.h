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

#ifndef __IC_KERBEROS_PROVIDER_H__
#define __IC_KERBEROS_PROVIDER_H__

#include <gio/gio.h>

#include "ic-dbus-generated.h"

G_BEGIN_DECLS

#define IC_TYPE_KERBEROS_PROVIDER            (ic_kerberos_provider_get_type ())
#define IC_KERBEROS_PROVIDER(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), IC_TYPE_KERBEROS_PROVIDER, IcKerberosProvider))
#define IC_IS_KERBEROS_PROVIDER(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), IC_TYPE_KERBEROS_PROVIDER))
#define IC_KERBEROS_PROVIDER_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), IC_TYPE_KERBEROS_PROVIDER, IcKerberosProviderClass))
#define IC_IS_KERBEROS_PROVIDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), IC_TYPE_KERBEROS_PROVIDER))
#define IC_KERBEROS_PROVIDER_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), IC_TYPE_KERBEROS_PROVIDER, IcKerberosProviderClass))

typedef struct _IcKerberosProvider IcKerberosProvider;
typedef struct _IcKerberosProviderClass IcKerberosProviderClass;
typedef struct _IcKerberosProviderPrivate IcKerberosProviderPrivate;

struct _IcKerberosProvider {
	IcDbusKerberosSkeleton parent;
	IcKerberosProviderPrivate *pv;
};

struct _IcKerberosProviderClass {
	IcDbusKerberosSkeletonClass parent_class;

	void       (* discover_async)  (IcKerberosProvider *provider,
	                                const gchar *string,
	                                GDBusMethodInvocation *invocation,
	                                GAsyncReadyCallback callback,
	                                gpointer user_data);

	gchar *    (* discover_finish) (IcKerberosProvider *provider,
	                                GAsyncResult *result,
	                                GHashTable *discovery,
	                                GError **error);

	void       (* enroll_async)    (IcKerberosProvider *provider,
	                                const gchar *realm,
	                                GBytes *admin_kerberos_cache,
	                                GDBusMethodInvocation *invocation,
	                                GAsyncReadyCallback callback,
	                                gpointer user_data);

	gboolean   (* enroll_finish)   (IcKerberosProvider *provider,
	                                GAsyncResult *result,
	                                GError **error);

	void       (* unenroll_async)  (IcKerberosProvider *provider,
	                                const gchar *realm,
	                                GBytes *admin_kerberos_cache,
	                                GDBusMethodInvocation *invocation,
	                                GAsyncReadyCallback callback,
	                                gpointer user_data);

	gboolean   (* unenroll_finish) (IcKerberosProvider *provider,
	                                GAsyncResult *result,
	                                GError **error);

	void       (* logins_async)    (IcKerberosProvider *provider,
	                                gboolean enabled,
	                                GDBusMethodInvocation *invocation,
	                                GAsyncReadyCallback callback,
	                                gpointer user_data);

	gboolean   (* logins_finish)   (IcKerberosProvider *provider,
	                                GAsyncResult *result,
	                                GError **error);
};

GType               ic_kerberos_provider_get_type                 (void) G_GNUC_CONST;

GHashTable *        ic_kerberos_provider_lookup_discovery         (IcKerberosProvider *provider,
                                                                   const gchar *realm);

G_END_DECLS

#endif /* __IC_KERBEROS_PROVIDER_H__ */
