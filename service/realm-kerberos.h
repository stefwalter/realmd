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

#include <krb5/krb5.h>

#include "realm-dbus-generated.h"

G_BEGIN_DECLS

typedef enum {
	REALM_KERBEROS_POLICY_NOT_SET = 0,
	REALM_KERBEROS_ALLOW_ANY_LOGIN = 1,
	REALM_KERBEROS_ALLOW_PERMITTED_LOGINS = 2,
	REALM_KERBEROS_DENY_ANY_LOGIN = 3,
} RealmKerberosLoginPolicy;

typedef enum {
	REALM_KERBEROS_CREDENTIAL_ADMIN = 1 << 1,
	REALM_KERBEROS_CREDENTIAL_USER = 1 << 2,
	REALM_KERBEROS_CREDENTIAL_COMPUTER = 1 << 3,
	REALM_KERBEROS_CREDENTIAL_SECRET = 1 << 4,
} RealmKerberosFlags;

typedef enum {
	REALM_KERBEROS_CREDENTIAL_CCACHE = 1,
	REALM_KERBEROS_CREDENTIAL_PASSWORD,
	REALM_KERBEROS_CREDENTIAL_AUTOMATIC,
} RealmKerberosCredential;

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
	RealmDbusKerberosSkeleton parent;
	RealmKerberosPrivate *pv;
};

struct _RealmKerberosClass {
	RealmDbusKerberosSkeletonClass parent_class;

	void       (* enroll_password_async)    (RealmKerberos *realm,
	                                         const char *name,
	                                         const char *password,
	                                         RealmKerberosFlags flags,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	void       (* enroll_ccache_async)      (RealmKerberos *realm,
	                                         GBytes *ccache,
	                                         RealmKerberosFlags flags,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	void       (* enroll_automatic_async)   (RealmKerberos *realm,
	                                         RealmKerberosFlags flags,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	gboolean   (* enroll_finish)            (RealmKerberos *realm,
	                                         GAsyncResult *result,
	                                         GError **error);

	void       (* unenroll_password_async)  (RealmKerberos *realm,
	                                         const char *name,
	                                         const char *password,
	                                         RealmKerberosFlags flags,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	void       (* unenroll_ccache_async)    (RealmKerberos *realm,
	                                         GBytes *ccache,
	                                         RealmKerberosFlags flags,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	void       (* unenroll_automatic_async) (RealmKerberos *realm,
	                                         RealmKerberosFlags flags,
	                                         GDBusMethodInvocation *invocation,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	gboolean   (* unenroll_finish)          (RealmKerberos *realm,
	                                         GAsyncResult *result,
	                                         GError **error);

	void       (* logins_async)             (RealmKerberos *realm,
	                                         GDBusMethodInvocation *invocation,
	                                         RealmKerberosLoginPolicy login_policy,
	                                         const gchar **permitted_add,
	                                         const gchar **permitted_remove,
	                                         GAsyncReadyCallback callback,
	                                         gpointer user_data);

	gboolean   (* logins_finish)            (RealmKerberos *realm,
	                                         GAsyncResult *result,
	                                         GError **error);

};

GType               realm_kerberos_get_type              (void) G_GNUC_CONST;

void                realm_kerberos_export_to_dbus        (RealmKerberos *self,
                                                          GDBusConnection *connection,
                                                          const gchar *object_path);

void                realm_kerberos_set_discovery         (RealmKerberos *self,
                                                          GHashTable *discovery);

GHashTable *        realm_kerberos_get_discovery         (RealmKerberos *self);

gchar *             realm_kerberos_parse_login           (RealmKerberos *self,
                                                          gboolean lower,
                                                          const gchar *login);

gchar **            realm_kerberos_parse_logins          (RealmKerberos *self,
                                                          gboolean lower,
                                                          const gchar **logins,
                                                          GError **error);

gchar *             realm_kerberos_format_login          (RealmKerberos *self,
                                                          const gchar *user);

GVariant *          realm_kerberos_build_supported_credentials (RealmKerberosCredential cred_type,
                                                                RealmKerberosFlags cred_owner,
                                                                ...);

void                realm_kerberos_kinit_ccache_async    (RealmKerberos *self,
                                                          const gchar *name,
                                                          const gchar *password,
                                                          const krb5_enctype *enctypes,
                                                          GDBusMethodInvocation *invocation,
                                                          GAsyncReadyCallback callback,
                                                          gpointer user_data);

GBytes *            realm_kerberos_kinit_ccache_finish   (RealmKerberos *self,
                                                          GAsyncResult *result,
                                                          GError **error);

const gchar *       realm_kerberos_get_name                    (RealmKerberos *self);

const gchar *       realm_kerberos_get_realm_name              (RealmKerberos *self);

void                realm_kerberos_set_realm_name              (RealmKerberos *self,
                                                                const gchar *value);

void                realm_kerberos_set_domain_name             (RealmKerberos *self,
                                                                const gchar *value);

void                realm_kerberos_set_suggested_admin         (RealmKerberos *self,
                                                                const gchar *value);

void                realm_kerberos_set_supported_join_creds    (RealmKerberos *self,
                                                                GVariant *value);

void                realm_kerberos_set_supported_leave_creds   (RealmKerberos *self,
                                                                GVariant *value);

void                realm_kerberos_set_permitted_logins        (RealmKerberos *self,
                                                                const gchar **value);

void                realm_kerberos_set_login_policy            (RealmKerberos *self,
                                                                RealmKerberosLoginPolicy value);

void                realm_kerberos_set_login_formats           (RealmKerberos *self,
                                                                const gchar **value);

void                realm_kerberos_set_details                 (RealmKerberos *self,
                                                                ...) G_GNUC_NULL_TERMINATED;

void                realm_kerberos_set_configured              (RealmKerberos *self,
                                                                gboolean configured);

G_END_DECLS

#endif /* __REALM_KERBEROS_H__ */
