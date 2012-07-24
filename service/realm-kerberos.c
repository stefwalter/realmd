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

#include "realm-daemon.h"
#define DEBUG_FLAG REALM_DEBUG_SERVICE
#include "realm-debug.h"
#include "realm-dbus-constants.h"
#include "realm-dbus-generated.h"
#include "realm-diagnostics.h"
#include "realm-errors.h"
#include "realm-kerberos.h"
#include "realm-login-name.h"

#include <krb5/krb5.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

struct _RealmKerberosPrivate {
	GHashTable *discovery;
};

enum {
	PROP_0,
	PROP_DISCOVERY
};

static void realm_kerberos_iface_init (RealmDbusKerberosIface *iface);

G_DEFINE_TYPE_WITH_CODE (RealmKerberos, realm_kerberos, REALM_DBUS_TYPE_KERBEROS_SKELETON,
	G_IMPLEMENT_INTERFACE (REALM_DBUS_TYPE_KERBEROS, realm_kerberos_iface_init);
);

typedef struct {
	RealmKerberos *self;
	GDBusMethodInvocation *invocation;
} MethodClosure;

static MethodClosure *
method_closure_new (RealmKerberos *self,
                    GDBusMethodInvocation *invocation)
{
	MethodClosure *method = g_slice_new (MethodClosure);
	method->self = g_object_ref (self);
	method->invocation = g_object_ref (invocation);
	return method;
}

static void
method_closure_free (MethodClosure *closure)
{
	g_object_unref (closure->self);
	g_object_unref (closure->invocation);
	g_slice_free (MethodClosure, closure);
}

static void
enroll_method_reply (GDBusMethodInvocation *invocation,
                     GError *error)
{
	if (error == NULL) {
		realm_diagnostics_info (invocation, "Successfully enrolled machine in realm");
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));

	} else if (error->domain == REALM_ERROR || error->domain == G_DBUS_ERROR) {
		realm_diagnostics_error (invocation, error, NULL);
		g_dbus_method_invocation_return_gerror (invocation, error);

	} else {
		realm_diagnostics_error (invocation, error, "Failed to enroll machine in realm");
		g_dbus_method_invocation_return_error (invocation, REALM_ERROR, REALM_ERROR_ENROLL_FAILED,
		                                       "Failed to enroll machine in realm. See diagnostics.");
	}

	realm_daemon_unlock_for_action (invocation);
}

static void
on_enroll_complete (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	MethodClosure *closure = user_data;
	RealmKerberosClass *klass;
	GError *error = NULL;

	klass = REALM_KERBEROS_GET_CLASS (closure->self);
	g_return_if_fail (klass->unenroll_finish != NULL);

	(klass->enroll_finish) (closure->self, result, &error);
	enroll_method_reply (closure->invocation, error);

	g_clear_error (&error);
	method_closure_free (closure);
}

static void
unenroll_method_reply (GDBusMethodInvocation *invocation,
                       GError *error)
{
	if (error == NULL) {
		realm_diagnostics_info (invocation, "Successfully unenrolled machine from realm");
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));

	} else if (error->domain == REALM_ERROR || error->domain == G_DBUS_ERROR) {
		realm_diagnostics_error (invocation, error, NULL);
		g_dbus_method_invocation_return_gerror (invocation, error);

	} else {
		realm_diagnostics_error (invocation, error, "Failed to unenroll machine from realm");
		g_dbus_method_invocation_return_error (invocation, REALM_ERROR, REALM_ERROR_UNENROLL_FAILED,
		                                       "Failed to unenroll machine from domain. See diagnostics.");
	}

	realm_daemon_unlock_for_action (invocation);
}

static void
on_unenroll_complete (GObject *source,
                      GAsyncResult *result,
                      gpointer user_data)
{
	MethodClosure *closure = user_data;
	RealmKerberosClass *klass;
	GError *error = NULL;

	klass = REALM_KERBEROS_GET_CLASS (closure->self);
	g_return_if_fail (klass->unenroll_finish != NULL);

	(klass->unenroll_finish) (closure->self, result, &error);
	unenroll_method_reply (closure->invocation, error);

	g_clear_error (&error);
	method_closure_free (closure);
}

static void
enroll_or_unenroll_with_ccache (RealmKerberos *self,
                                RealmKerberosFlags flags,
                                GDBusMethodInvocation *invocation,
                                GVariant *ccache,
                                gboolean enroll)
{
	RealmKerberosClass *klass = REALM_KERBEROS_GET_CLASS (self);
	const guchar *data;
	GBytes *bytes;
	gsize length;

	if ((enroll && klass->enroll_ccache_async == NULL) ||
	    (!enroll && klass->unenroll_ccache_async == NULL)) {
		g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
		                                       enroll ?
		                                            "Enrolling this realm using a credential cache is not supported" :
		                                            "Unenrolling this realm using a credential cache is not supported");
		return;
	}

	data = g_variant_get_fixed_array (ccache, &length, 1);
	if (length == 0) {
		g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		                                       "Invalid zero length credential cache argument");
		return;
	}

	if (!realm_daemon_lock_for_action (invocation)) {
		g_dbus_method_invocation_return_error (invocation, REALM_ERROR, REALM_ERROR_BUSY,
		                                       "Already running another action");
		return;
	}

	bytes = g_bytes_new_with_free_func (data, length,
	                                    (GDestroyNotify)g_variant_unref,
	                                    g_variant_ref (ccache));

	if (enroll) {
		g_return_if_fail (klass->enroll_finish != NULL);
		(klass->enroll_ccache_async) (self, bytes, flags, invocation, on_enroll_complete,
		                              method_closure_new (self, invocation));
	} else {
		g_return_if_fail (klass->unenroll_finish != NULL);
		(klass->unenroll_ccache_async) (self, bytes, flags, invocation, on_unenroll_complete,
		                                method_closure_new (self, invocation));
	}

	g_bytes_unref (bytes);
}

static void
enroll_or_unenroll_with_password (RealmKerberos *self,
                                  RealmKerberosFlags flags,
                                  GDBusMethodInvocation *invocation,
                                  const gchar *name,
                                  const gchar *password,
                                  gboolean enroll)
{
	RealmKerberosClass *klass = REALM_KERBEROS_GET_CLASS (self);

	if (enroll && klass->enroll_password_async == NULL) {
		g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
		                                       enroll ?
		                                           "Enrolling this realm using a password is not supported" :
		                                           "Unenrolling this realm using a password is not supported");
		return;
	}

	if (!realm_daemon_lock_for_action (invocation)) {
		g_dbus_method_invocation_return_error (invocation, REALM_ERROR, REALM_ERROR_BUSY,
		                                       "Already running another action");
		return;
	}


	if (enroll) {
		g_return_if_fail (klass->enroll_finish != NULL);
		(klass->enroll_password_async) (self, name, password, flags, invocation, on_enroll_complete,
		                                method_closure_new (self, invocation));

	} else {
		g_return_if_fail (klass->unenroll_finish != NULL);
		(klass->unenroll_password_async) (self, name, password, flags, invocation, on_unenroll_complete,
		                                method_closure_new (self, invocation));
	}
}

static void
enroll_or_unenroll_with_automatic (RealmKerberos *self,
                                   RealmKerberosFlags flags,
                                   GDBusMethodInvocation *invocation,
                                   gboolean enroll)
{
	RealmKerberosClass *klass = REALM_KERBEROS_GET_CLASS (self);

	if ((enroll && klass->enroll_automatic_async == NULL) ||
	    (!enroll && klass->unenroll_automatic_async == NULL)) {
		g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
		                                       enroll ?
		                                            "Enrolling this realm without credentials is not supported" :
		                                            "Unenrolling this realm without credentials is not supported");
		return;
	}

	if (!realm_daemon_lock_for_action (invocation)) {
		g_dbus_method_invocation_return_error (invocation, REALM_ERROR, REALM_ERROR_BUSY,
		                                       "Already running another action");
		return;
	}

	if (enroll) {
		g_return_if_fail (klass->enroll_finish != NULL);
		(klass->enroll_automatic_async) (self, flags, invocation, on_enroll_complete,
		                                 method_closure_new (self, invocation));
	} else {
		g_return_if_fail (klass->enroll_finish != NULL);
		(klass->unenroll_automatic_async) (self, flags, invocation, on_unenroll_complete,
		                                   method_closure_new (self, invocation));
	}
}

static gboolean
validate_and_parse_credentials (GDBusMethodInvocation *invocation,
                                GVariant *input,
                                RealmKerberosFlags *flags,
                                RealmKerberosCredential *cred_type,
                                GVariant **creds)
{
	GVariant *outer;
	const char *owner, *type;

	g_variant_get (input, "(&s&s@v)", &type, &owner, &outer);

	if (g_str_equal (owner, "administrator"))
		*flags |= REALM_KERBEROS_CREDENTIAL_ADMIN;
	else if (g_str_equal (owner, "user"))
		*flags |= REALM_KERBEROS_CREDENTIAL_USER;
	else if (g_str_equal (owner, "computer"))
		*flags |= REALM_KERBEROS_CREDENTIAL_COMPUTER;
	else if (g_str_equal (owner, "secret"))
		*flags |= REALM_KERBEROS_CREDENTIAL_SECRET;

	*creds = g_variant_get_variant (outer);
	g_variant_unref (outer);

	if (g_str_equal (type, "ccache")) {
		if (g_variant_is_of_type (*creds, G_VARIANT_TYPE ("ay"))) {
			*cred_type = REALM_KERBEROS_CREDENTIAL_CCACHE;
			return TRUE;
		} else {
			g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
			                                       "Credential cache argument is of wrong type");
		}

	} else if (g_str_equal (type, "password")) {
		if (g_variant_is_of_type (*creds, G_VARIANT_TYPE ("(ss)"))) {
			*cred_type = REALM_KERBEROS_CREDENTIAL_PASSWORD;
			return TRUE;
		} else {
			g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
			                                       "Password credentials are of wrong type");
		}

	} else if (g_str_equal (type, "automatic")) {
		*cred_type = REALM_KERBEROS_CREDENTIAL_AUTOMATIC;
		return TRUE;

	} else {
		g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		                                       "Invalid or unsupported credential type");
	}

	/* Parsing failed */
	g_variant_unref (*creds);
	return FALSE;
}

static gboolean
handle_enroll (RealmDbusKerberos *realm,
               GDBusMethodInvocation *invocation,
               GVariant *credentials,
               GVariant *options,
               const gchar *operation_id)
{
	RealmKerberos *self = REALM_KERBEROS (realm);
	const char *name, *password;
	RealmKerberosFlags flags = 0;
	GVariant *creds;
	RealmKerberosCredential cred_type;

	/* Make note of the current operation id, for diagnostics */
	realm_diagnostics_mark_operation (invocation, operation_id);

	if (!validate_and_parse_credentials (invocation, credentials, &flags, &cred_type, &creds))
		return TRUE;

	switch (cred_type) {
	case REALM_KERBEROS_CREDENTIAL_CCACHE:
		enroll_or_unenroll_with_ccache (self, flags, invocation, creds, TRUE);
		break;
	case REALM_KERBEROS_CREDENTIAL_PASSWORD:
		g_variant_get (creds, "(&s&s)", &name, &password);
		enroll_or_unenroll_with_password (self, flags, invocation, name, password, TRUE);
		break;
	case REALM_KERBEROS_CREDENTIAL_AUTOMATIC:
		enroll_or_unenroll_with_automatic (self, flags, invocation, TRUE);
		break;
	default:
		g_assert_not_reached ();
	}

	g_variant_unref (creds);
	return TRUE;
}

static gboolean
handle_unenroll (RealmDbusKerberos *realm,
                 GDBusMethodInvocation *invocation,
                 GVariant *credentials,
                 GVariant *options,
                 const gchar *operation_id)
{
	RealmKerberos *self = REALM_KERBEROS (realm);
	const char *name, *password;
	RealmKerberosFlags flags = 0;
	GVariant *creds;
	RealmKerberosCredential cred_type;

	/* Make note of the current operation id, for diagnostics */
	realm_diagnostics_mark_operation (invocation, operation_id);

	if (!validate_and_parse_credentials (invocation, credentials, &flags, &cred_type, &creds))
		return TRUE;

	switch (cred_type) {
	case REALM_KERBEROS_CREDENTIAL_CCACHE:
		enroll_or_unenroll_with_ccache (self, flags, invocation, creds, FALSE);
		break;
	case REALM_KERBEROS_CREDENTIAL_PASSWORD:
		g_variant_get (creds, "(&s&s)", &name, &password);
		enroll_or_unenroll_with_password (self, flags, invocation, name, password, FALSE);
		break;
	case REALM_KERBEROS_CREDENTIAL_AUTOMATIC:
		enroll_or_unenroll_with_automatic (self, flags, invocation, FALSE);
		break;
	default:
		g_assert_not_reached ();
	}

	g_variant_unref (creds);
	return TRUE;
}

static void
on_logins_complete (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	MethodClosure *closure = user_data;
	RealmKerberosClass *klass;
	GError *error = NULL;

	klass = REALM_KERBEROS_GET_CLASS (closure->self);
	g_return_if_fail (klass->logins_finish != NULL);

	if ((klass->logins_finish) (closure->self, result, &error)) {
		realm_diagnostics_info (closure->invocation, "Successfully changed permitted logins for realm");
		g_dbus_method_invocation_return_value (closure->invocation, g_variant_new ("()"));

	} else if (error != NULL &&
	           (error->domain == REALM_ERROR || error->domain == G_DBUS_ERROR)) {
		realm_diagnostics_error (closure->invocation, error, NULL);
		g_dbus_method_invocation_return_gerror (closure->invocation, error);
		g_error_free (error);

	} else {
		realm_diagnostics_error (closure->invocation, error, "Failed to change permitted logins");
		g_dbus_method_invocation_return_error (closure->invocation, REALM_ERROR, REALM_ERROR_INTERNAL,
		                                       "Failed to change permitted logins. See diagnostics.");
		g_error_free (error);
	}

	realm_daemon_unlock_for_action (closure->invocation);
	method_closure_free (closure);
}

static gboolean
handle_change_login_policy (RealmDbusKerberos *realm,
                            GDBusMethodInvocation *invocation,
                            const gchar *login_policy,
                            const gchar *const *add,
                            const gchar *const *remove,
                            const gchar *operation_id)
{
	RealmKerberosLoginPolicy policy = REALM_KERBEROS_POLICY_NOT_SET;
	RealmKerberos *self = REALM_KERBEROS (realm);
	RealmKerberosClass *klass;
	gchar **policies;
	gint policies_set = 0;
	gint i;

	/* Make note of the current operation id, for diagnostics */
	realm_diagnostics_mark_operation (invocation, operation_id);

	policies = g_strsplit_set (login_policy, ", \t", -1);
	for (i = 0; policies[i] != NULL; i++) {
		if (g_str_equal (policies[i], REALM_DBUS_LOGIN_POLICY_ANY)) {
			policy = REALM_KERBEROS_ALLOW_ANY_LOGIN;
			policies_set++;
		} else if (g_str_equal (policies[i], REALM_DBUS_LOGIN_POLICY_PERMITTED)) {
			policy = REALM_KERBEROS_ALLOW_PERMITTED_LOGINS;
			policies_set++;
		} else if (g_str_equal (policies[i], REALM_DBUS_LOGIN_POLICY_DENY)) {
			policy = REALM_KERBEROS_DENY_ANY_LOGIN;
			policies_set++;
		} else {
			g_strfreev (policies);
			g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
			                                       G_DBUS_ERROR_INVALID_ARGS,
			                                       "Invalid or unknown login_policy argument");
			return TRUE;
		}
	}

	g_strfreev (policies);

	if (policies_set > 1) {
		g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
		                                       G_DBUS_ERROR_INVALID_ARGS,
		                                       "Conflicting flags in login_policy argument");
		return TRUE;
	}

	if (!realm_daemon_lock_for_action (invocation)) {
		g_dbus_method_invocation_return_error (invocation, REALM_ERROR, REALM_ERROR_BUSY,
		                                       "Already running another action");
		return TRUE;
	}

	klass = REALM_KERBEROS_GET_CLASS (self);
	g_return_val_if_fail (klass->logins_async != NULL, FALSE);

	(klass->logins_async) (self, invocation, policy, (const gchar **)add,
	                       (const gchar **)remove, on_logins_complete,
	                       method_closure_new (self, invocation));

	return TRUE;
}

static gboolean
realm_kerberos_authorize_method (GDBusInterfaceSkeleton *skeleton,
                                 GDBusMethodInvocation  *invocation)
{
	const gchar *interface = g_dbus_method_invocation_get_interface_name (invocation);
	const gchar *method = g_dbus_method_invocation_get_method_name (invocation);
	const gchar *action_id = NULL;
	gboolean ret = FALSE;

	/* Each method has its own polkit authorization */
	if (g_str_equal (interface, REALM_DBUS_KERBEROS_REALM_INTERFACE)) {
		if (g_str_equal (method, "Enroll")) {
			action_id = "org.freedesktop.realmd.enroll-machine";

		} else if (g_str_equal (method, "Unenroll")) {
			action_id = "org.freedesktop.realmd.unenroll-machine";

		} else if (g_str_equal (method, "ChangeLoginPolicy")) {
		        action_id = "org.freedesktop.realmd.login-policy";

		} else {
			g_warning ("encountered unknown method during auth checks: %s.%s",
			           interface, method);
			action_id = NULL;
		}

		if (action_id != NULL)
			ret = realm_daemon_check_dbus_action (g_dbus_method_invocation_get_sender (invocation),
			                                       action_id);
		else
			ret = FALSE;
	}

	if (ret == FALSE) {
		realm_debug ("rejecting access to: %s.%s method on %s",
		             interface, method, g_dbus_method_invocation_get_object_path (invocation));
		g_dbus_method_invocation_return_dbus_error (invocation, REALM_DBUS_ERROR_NOT_AUTHORIZED,
		                                            "Not authorized to perform this action");
	}

	return ret;
}

static void
realm_kerberos_init (RealmKerberos *self)
{
	self->pv = G_TYPE_INSTANCE_GET_PRIVATE (self, REALM_TYPE_KERBEROS,
	                                        RealmKerberosPrivate);
}

static void
realm_kerberos_get_property (GObject *obj,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	RealmKerberos *self = REALM_KERBEROS (obj);

	switch (prop_id) {
	case PROP_DISCOVERY:
		g_value_set_boxed (value, realm_kerberos_get_discovery (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
realm_kerberos_set_property (GObject *obj,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	RealmKerberos *self = REALM_KERBEROS (obj);

	switch (prop_id) {
	case PROP_DISCOVERY:
		realm_kerberos_set_discovery (self, g_value_get_boxed (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
realm_kerberos_finalize (GObject *obj)
{
	RealmKerberos *self = REALM_KERBEROS (obj);

	if (self->pv->discovery)
		g_hash_table_unref (self->pv->discovery);

	G_OBJECT_CLASS (realm_kerberos_parent_class)->finalize (obj);
}

static void
realm_kerberos_class_init (RealmKerberosClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GDBusInterfaceSkeletonClass *skeleton_class = G_DBUS_INTERFACE_SKELETON_CLASS (klass);

	object_class->get_property = realm_kerberos_get_property;
	object_class->set_property = realm_kerberos_set_property;
	object_class->finalize = realm_kerberos_finalize;

	skeleton_class->g_authorize_method = realm_kerberos_authorize_method;

	g_type_class_add_private (klass, sizeof (RealmKerberosPrivate));

	g_object_class_install_property (object_class, PROP_DISCOVERY,
	             g_param_spec_boxed ("discovery", "Discovery", "Discovery Data",
	                                 G_TYPE_HASH_TABLE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
realm_kerberos_iface_init (RealmDbusKerberosIface *iface)
{
	memcpy (iface, g_type_interface_peek_parent (iface), sizeof (*iface));
	iface->handle_enroll = handle_enroll;
	iface->handle_unenroll = handle_unenroll;
	iface->handle_change_login_policy = handle_change_login_policy;
}

void
realm_kerberos_set_discovery (RealmKerberos *self,
                              GHashTable *discovery)
{
	g_return_if_fail (REALM_IS_KERBEROS (self));
	g_return_if_fail (discovery != NULL);

	if (discovery)
		g_hash_table_ref (discovery);
	if (self->pv->discovery)
		g_hash_table_unref (self->pv->discovery);
	self->pv->discovery = discovery;
	g_object_notify (G_OBJECT (self), "discovery");
}

GHashTable *
realm_kerberos_get_discovery (RealmKerberos *self)
{
	g_return_val_if_fail (REALM_IS_KERBEROS (self), NULL);
	return self->pv->discovery;
}

gchar *
realm_kerberos_parse_login (RealmKerberos *self,
                            gboolean lower,
                            const gchar *login)
{
	const gchar *format;

	g_return_val_if_fail (REALM_IS_KERBEROS (self), NULL);
	g_return_val_if_fail (login != NULL, NULL);

	format = realm_dbus_kerberos_get_login_format (REALM_DBUS_KERBEROS (self));
	if (format == NULL)
		return NULL;

	return realm_login_name_parse (format, lower, login);
}

gchar **
realm_kerberos_parse_logins (RealmKerberos *self,
                             gboolean lower,
                             const gchar **logins,
                             GError **error)
{
	const gchar *failed = NULL;
	const gchar *format;
	gchar **result;

	g_return_val_if_fail (REALM_IS_KERBEROS (self), NULL);

	format = realm_dbus_kerberos_get_login_format (REALM_DBUS_KERBEROS (self));
	if (format == NULL) {
		g_set_error (error, REALM_ERROR,
		             REALM_ERROR_NOT_ENROLLED,
		             "The realm does not allow specifying logins");
		return NULL;
	}

	result = realm_login_name_parse_all (format, lower, logins, &failed);
	if (result == NULL) {
		g_set_error (error, G_DBUS_ERROR,
		             G_DBUS_ERROR_INVALID_ARGS,
		             "Invalid login argument%s%s%s does not match the login format '%s'",
		             failed ? " '" : "", failed, failed ? "'" : "", format);
	}

	return result;
}

gchar *
realm_kerberos_format_login (RealmKerberos *self,
                             const gchar *user)
{
	const gchar *format;

	g_return_val_if_fail (REALM_IS_KERBEROS (self), NULL);
	g_return_val_if_fail (user != NULL, NULL);

	format = realm_dbus_kerberos_get_login_format (REALM_DBUS_KERBEROS (self));
	if (format == NULL)
		return NULL;

	return realm_login_name_format (format, user);
}

GVariant *
realm_kerberos_build_supported_credentials (RealmKerberosCredential cred_type,
                                            RealmKerberosFlags cred_owner,
                                            ...)
{
	GPtrArray *elements;
	GVariant *tuple[2];
	const gchar *string;
	GVariant *supported;
	va_list va;

	va_start (va, cred_owner);
	elements = g_ptr_array_new ();

	while (cred_type != 0) {
		if (cred_owner & REALM_KERBEROS_CREDENTIAL_ADMIN)
			string = "administrator";
		else if (cred_owner & REALM_KERBEROS_CREDENTIAL_USER)
			string = "user";
		else if (cred_owner & REALM_KERBEROS_CREDENTIAL_COMPUTER)
			string = "computer";
		else if (cred_owner & REALM_KERBEROS_CREDENTIAL_SECRET)
			string = "secret";
		else
			g_assert_not_reached ();

		tuple[1] = g_variant_new_string (string);

		switch (cred_type) {
		case REALM_KERBEROS_CREDENTIAL_CCACHE:
			string = "ccache";
			break;
		case REALM_KERBEROS_CREDENTIAL_PASSWORD:
			string = "password";
			break;
		case REALM_KERBEROS_CREDENTIAL_AUTOMATIC:
			string = "automatic";
			break;
		default:
			g_assert_not_reached ();
			break;
		}

		tuple[0] = g_variant_new_string (string);

		g_ptr_array_add (elements, g_variant_new_tuple (tuple, 2));

		cred_type = va_arg (va, RealmKerberosCredential);
		if (cred_type != 0)
			cred_owner = va_arg (va, RealmKerberosFlags);
	}

	va_end (va);

	supported = g_variant_new_array (G_VARIANT_TYPE ("(ss)"),
	                                 (GVariant *const *)elements->pdata,
	                                 elements->len);

	g_ptr_array_free (elements, TRUE);
	g_variant_ref_sink (supported);
	return supported;
}

typedef struct {
	GDBusMethodInvocation *invocation;
	gchar *principal;
	gchar *password;
	krb5_enctype *enctypes;
	gint n_enctypes;
	GBytes *ccache;
} KinitClosure;

static void
kinit_closure_free (gpointer data)
{
	KinitClosure *kinit = data;
	g_object_unref (kinit->invocation);
	g_free (kinit->principal);
	g_free (kinit->password);
	g_free (kinit->enctypes);
	if (kinit->ccache)
		g_bytes_unref (kinit->ccache);
	g_slice_free (KinitClosure, kinit);
}


static void
kinit_handle_error (GSimpleAsyncResult *async,
                    krb5_error_code code,
                    krb5_context context,
                    const gchar *message,
                    ...)
{
	gchar *string;
	va_list va;

	va_start (va, message);
	string = g_strdup_vprintf (message, va);
	va_end (va);

	g_simple_async_result_set_error (async, REALM_KRB5_ERROR, code,
	                                 "%s: %s", string, krb5_get_error_message (context, code));
	g_free (string);
}

static void
kinit_ccache_thread_func (GSimpleAsyncResult *async,
                          GObject *object,
                          GCancellable *cancellable)
{
	KinitClosure *kinit = g_simple_async_result_get_op_res_gpointer (async);
	krb5_get_init_creds_opt *options = NULL;
	krb5_context context = NULL;
	krb5_principal principal = NULL;
	krb5_error_code code;
	gchar *filename = NULL;
	krb5_ccache ccache = NULL;
	krb5_creds my_creds;
	gchar *contents;
	gsize length;
	GError *error = NULL;
	int temp_fd;

	code = krb5_init_context (&context);
	if (code != 0) {
		kinit_handle_error (async, code, NULL, "Couldn't initialize kerberos");
		goto cleanup;
	}

	code = krb5_parse_name (context, kinit->principal, &principal);
	if (code != 0) {
		kinit_handle_error (async, code, context,
		                   "Couldn't parse principal: %s", kinit->principal);
		goto cleanup;
	}

	code = krb5_get_init_creds_opt_alloc (context, &options);
	if (code != 0) {
		g_warning ("Couldn't setup kerberos options: %s",
		           krb5_get_error_message (context, code));
		goto cleanup;
	}

	filename = g_build_filename (g_get_tmp_dir (), "realmd-krb5-cache.XXXXXX", NULL);
	temp_fd = g_mkstemp_full (filename, O_RDWR, S_IRUSR | S_IWUSR);
	if (temp_fd == -1) {
		g_simple_async_result_set_error (async, G_FILE_ERROR, g_file_error_from_errno (errno),
		                                 "Couldn't create credential cache file: %s",
		                                 g_strerror (errno));
		goto cleanup;
	}
	close (temp_fd);

	code = krb5_cc_resolve (context, filename, &ccache);
	if (code != 0) {
		kinit_handle_error (async, code, context,
		                    "Couldn't resolve credential cache: %s", filename);
		goto cleanup;
	}

	if (kinit->enctypes)
		krb5_get_init_creds_opt_set_etype_list (options, kinit->enctypes, kinit->n_enctypes);

	code = krb5_get_init_creds_opt_set_out_ccache (context, options, ccache);
	if (code != 0) {
		g_warning ("Couldn't setup credential cache: %s",
		           krb5_get_error_message (context, code));
		goto cleanup;
	}

	code = krb5_get_init_creds_password (context, &my_creds, principal,
	                                     kinit->password, NULL, 0, 0, NULL, options);
	if (code != 0) {
		kinit_handle_error (async, code, context,
		                    "Couldn't authenticate as: %s", kinit->principal);
		goto cleanup;
	}

	krb5_cc_close (context, ccache);
	ccache = NULL;

	g_file_get_contents (filename, &contents, &length, &error);
	if (error != NULL) {
		g_simple_async_result_take_error (async, error);
		goto cleanup;
	}

	kinit->ccache = g_bytes_new_take (contents, length);

cleanup:
	if (filename) {
		if (!realm_debug_flag_is_set (REALM_DEBUG_LEAVE_TEMP_FILES))
			g_unlink (filename);
		g_free (filename);
	}

	if (options)
		krb5_get_init_creds_opt_free (context, options);
	if (principal)
		krb5_free_principal (context, principal);
	if (ccache)
		krb5_cc_close (context, ccache);
	if (context)
		krb5_free_context (context);
}

void
realm_kerberos_kinit_ccache_async (RealmKerberos *self,
                                   const gchar *name,
                                   const gchar *password,
                                   const krb5_enctype *enctypes,
                                   GDBusMethodInvocation *invocation,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
	GSimpleAsyncResult *async;
	KinitClosure *kinit;

	g_return_if_fail (REALM_IS_KERBEROS (self));
	g_return_if_fail (name != NULL);
	g_return_if_fail (password != NULL);

	async = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
	                                   realm_kerberos_kinit_ccache_async);
	kinit = g_slice_new0 (KinitClosure);
	kinit->password = g_strdup (password);
	kinit->invocation = g_object_ref (invocation);

	if (enctypes != NULL) {
		while (enctypes[kinit->n_enctypes])
			kinit->n_enctypes++;
		kinit->enctypes = g_memdup (enctypes, sizeof (krb5_enctype) * kinit->n_enctypes);
	}

	if (strchr (name, '@') == NULL) {
		kinit->principal = g_strdup_printf ("%s@%s", name,
		                                    realm_dbus_kerberos_get_name (REALM_DBUS_KERBEROS (self)));
	} else {
		kinit->principal = g_strdup (name);
	}

	g_simple_async_result_set_op_res_gpointer (async, kinit, kinit_closure_free);
	g_simple_async_result_run_in_thread (async, kinit_ccache_thread_func, G_PRIORITY_DEFAULT, NULL);
	g_object_unref (async);
}

GBytes *
realm_kerberos_kinit_ccache_finish (RealmKerberos *self,
                                    GAsyncResult *result,
                                    GError **error)
{
	GSimpleAsyncResult *async;
	KinitClosure *kinit;
	GError *krb5_error = NULL;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (self),
	                      realm_kerberos_kinit_ccache_async), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	async = G_SIMPLE_ASYNC_RESULT (result);
	kinit = g_simple_async_result_get_op_res_gpointer (async);

	if (g_simple_async_result_propagate_error (async, &krb5_error)) {
		realm_diagnostics_error (kinit->invocation, krb5_error, NULL);

		if (g_error_matches (krb5_error, REALM_KRB5_ERROR, KRB5KDC_ERR_PREAUTH_FAILED) ||
		    g_error_matches (krb5_error, REALM_KRB5_ERROR, KRB5KDC_ERR_C_PRINCIPAL_UNKNOWN) ||
		    g_error_matches (krb5_error, REALM_KRB5_ERROR, KRB5KDC_ERR_KEY_EXP) ||
		    g_error_matches (krb5_error, REALM_KRB5_ERROR, KRB5KDC_ERR_CLIENT_REVOKED) ||
		    g_error_matches (krb5_error, REALM_KRB5_ERROR, KRB5KDC_ERR_POLICY) ||
		    g_error_matches (krb5_error, REALM_KRB5_ERROR, KRB5KDC_ERR_ETYPE_NOSUPP)) {
			g_set_error (error, REALM_ERROR, REALM_ERROR_AUTH_FAILED,
			             "Couldn't authenticate as: %s: %s", kinit->principal,
			             krb5_error->message);
			g_error_free (krb5_error);
			return NULL;
		}

		g_propagate_error (error, krb5_error);
		return NULL;
	}

	g_return_val_if_fail (kinit->ccache != NULL, NULL);
	return g_bytes_ref (kinit->ccache);
}
