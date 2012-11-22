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

#ifndef __REALM_KERBEROS_MEMBERSHIP_H__
#define __REALM_KERBEROS_MEMBERSHIP_H__

#include <gio/gio.h>

#include <krb5/krb5.h>

#include "realm-dbus-generated.h"

G_BEGIN_DECLS

typedef enum {
	REALM_KERBEROS_OWNER_ADMIN = 1 << 1,
	REALM_KERBEROS_OWNER_USER = 1 << 2,
	REALM_KERBEROS_OWNER_COMPUTER = 1 << 3,
	REALM_KERBEROS_OWNER_NONE = 1 << 4,
	REALM_KERBEROS_ASSUME_PACKAGES = 1 << 5,
} RealmKerberosFlags;

#define REALM_KERBEROS_OWNER_MASK \
	(REALM_KERBEROS_OWNER_ADMIN | \
	 REALM_KERBEROS_OWNER_USER | \
	 REALM_KERBEROS_OWNER_COMPUTER | \
	 REALM_KERBEROS_OWNER_NONE)

typedef enum {
	REALM_KERBEROS_CREDENTIAL_CCACHE = 1,
	REALM_KERBEROS_CREDENTIAL_PASSWORD,
	REALM_KERBEROS_CREDENTIAL_SECRET,
	REALM_KERBEROS_CREDENTIAL_AUTOMATIC
} RealmKerberosCredential;

#define REALM_TYPE_KERBEROS_MEMBERSHIP             (realm_kerberos_membership_get_type ())
#define REALM_KERBEROS_MEMBERSHIP(inst)            (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_KERBEROS_MEMBERSHIP, RealmKerberosMembership))
#define REALM_IS_KERBEROS_MEMBERSHIP(inst)         (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_KERBEROS_MEMBERSHIP))
#define REALM_KERBEROS_MEMBERSHIP_GET_IFACE(inst)  (G_TYPE_INSTANCE_GET_INTERFACE ((inst), REALM_TYPE_KERBEROS_MEMBERSHIP, RealmKerberosMembershipIface))

typedef struct _RealmKerberosMembership RealmKerberosMembership;
typedef struct _RealmKerberosMembershipIface RealmKerberosMembershipIface;

struct _RealmKerberosMembershipIface {
	GTypeInterface parent_iface;

	void       (* enroll_password_async)    (RealmKerberosMembership *realm,
	                                         const char *name,
	                                         GBytes *password,
	                                         RealmKerberosFlags flags,
	                                         GVariant *options,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	void       (* enroll_ccache_async)      (RealmKerberosMembership *realm,
	                                         const gchar *ccache_file,
	                                         RealmKerberosFlags flags,
	                                         GVariant *options,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	void       (* enroll_secret_async)      (RealmKerberosMembership *realm,
	                                         GBytes *secret,
	                                         RealmKerberosFlags flags,
	                                         GVariant *options,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	void       (* enroll_automatic_async)   (RealmKerberosMembership *realm,
	                                         RealmKerberosFlags flags,
	                                         GVariant *options,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	gboolean   (* enroll_finish)            (RealmKerberosMembership *realm,
	                                         GAsyncResult *result,
	                                         GError **error);

	void       (* unenroll_password_async)  (RealmKerberosMembership *realm,
	                                         const char *name,
	                                         GBytes *password,
	                                         RealmKerberosFlags flags,
	                                         GVariant *options,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	void       (* unenroll_ccache_async)    (RealmKerberosMembership *realm,
	                                         const gchar *ccache_file,
	                                         RealmKerberosFlags flags,
	                                         GVariant *options,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	void       (* unenroll_secret_async)    (RealmKerberosMembership *realm,
	                                         GBytes *secret,
	                                         RealmKerberosFlags flags,
	                                         GVariant *options,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	void       (* unenroll_automatic_async) (RealmKerberosMembership *realm,
	                                         RealmKerberosFlags flags,
	                                         GVariant *options,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	gboolean   (* unenroll_finish)          (RealmKerberosMembership *realm,
	                                         GAsyncResult *result,
	                                         GError **error);
};

GType               realm_kerberos_membership_get_type        (void) G_GNUC_CONST;

GVariant *          realm_kerberos_membership_build_supported (RealmKerberosCredential cred_type,
                                                               RealmKerberosFlags cred_owner,
                                                               ...);

G_END_DECLS

#endif /* __REALM_KERBEROS_MEMBERSHIP_H__ */
