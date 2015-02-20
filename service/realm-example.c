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

#include "realm-dbus-constants.h"
#include "realm-dbus-generated.h"

#include "realm-errors.h"
#include "realm-example.h"
#include "realm-ini-config.h"
#include "realm-invocation.h"
#include "realm-kerberos-membership.h"
#include "realm-settings.h"
#include "realm-usleep-async.h"

#include <glib/gi18n.h>

struct _RealmExample {
	RealmKerberos parent;
	RealmIniConfig *config;
	gulong config_sig;
};

typedef struct {
	RealmKerberosClass parent_class;
} RealmExampleClass;

enum {
	PROP_0,
	PROP_PROVIDER,
};

static void realm_example_kerberos_membership_iface (RealmKerberosMembershipIface *iface);

G_DEFINE_TYPE_WITH_CODE (RealmExample, realm_example, REALM_TYPE_KERBEROS,
                         G_IMPLEMENT_INTERFACE (REALM_TYPE_KERBEROS_MEMBERSHIP, realm_example_kerberos_membership_iface);
);

static void
realm_example_init (RealmExample *self)
{

}

static void
realm_example_constructed (GObject *obj)
{
	RealmKerberos *kerberos = REALM_KERBEROS (obj);

	G_OBJECT_CLASS (realm_example_parent_class)->constructed (obj);

	realm_kerberos_set_details (kerberos,
	                            REALM_DBUS_OPTION_SERVER_SOFTWARE, REALM_DBUS_IDENTIFIER_EXAMPLE,
	                            REALM_DBUS_OPTION_CLIENT_SOFTWARE, REALM_DBUS_IDENTIFIER_EXAMPLE,
	                            NULL);

	realm_kerberos_set_login_policy (kerberos, REALM_KERBEROS_ALLOW_ANY_LOGIN);
}

static gboolean
validate_membership_options (GVariant *options,
                             GError **error)
{
	const gchar *software;

	/* Figure out the method that we're going to use to enroll */
	if (g_variant_lookup (options, REALM_DBUS_OPTION_MEMBERSHIP_SOFTWARE, "&s", &software)) {
		if (!g_str_equal (software, REALM_DBUS_IDENTIFIER_EXAMPLE)) {
			g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
			             _("Unsupported or unknown membership software '%s'"), software);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
match_admin_and_password (RealmIniConfig *config,
                          const gchar *realm_name,
                          const gchar *admin_name,
                          GBytes *password)
{
	const gchar *admin;
	const gchar *pass;
	gconstpointer data;
	gsize size;

	admin = realm_settings_value (realm_name, "example-administrator");
	if (admin == NULL)
		return FALSE;
	if (!g_str_equal (admin_name, admin))
		return FALSE;

	pass = realm_settings_value (realm_name, "example-password");
	if (pass == NULL)
		return FALSE;
	data = g_bytes_get_data (password, &size);
	if (strlen (pass) != size)
		return FALSE;
	return (memcmp (data, pass, size) == 0);
}

static gdouble
settings_delay (const gchar *realm_name, const gchar *key)
{
	return realm_settings_double (realm_name, key, 0.0) * G_USEC_PER_SEC;
}

static void
on_join_sleep_done (GObject *source,
                    GAsyncResult *res,
                    gpointer user_data)
{
	GTask *task = G_TASK (user_data);
	RealmExample *self = g_task_get_source_object (task);
	GError *error = NULL;
	const gchar *realm_name;

	if (realm_usleep_finish (res, &error)) {
		realm_name = realm_kerberos_get_name (REALM_KERBEROS (self));
		realm_ini_config_change (self->config, realm_name, &error,
		                         "login-formats", "%U@%D",
		                         "login-permitted", "",
		                         "login-policy", REALM_DBUS_LOGIN_POLICY_PERMITTED,
		                         NULL);
	}

	if (error)
		g_task_return_error (task, error);
	else
		g_task_return_boolean (task, TRUE);
	g_object_unref (task);
}

static void
realm_example_join_async (RealmKerberosMembership *membership,
                          RealmCredential *cred,
                          GVariant *options,
                          GDBusMethodInvocation *invocation,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	RealmExample *self = REALM_EXAMPLE (membership);
	RealmKerberos *kerberos = REALM_KERBEROS (self);
	GTask *task;
	GError *error = NULL;
	const gchar *realm_name;

	g_return_if_fail (cred->type == REALM_CREDENTIAL_PASSWORD);

	realm_name = realm_kerberos_get_name (kerberos);
	task = g_task_new (self, NULL, callback, user_data);

	/* Make sure not already enrolled in a realm */
	if (realm_ini_config_have_section (self->config, realm_name)) {
		g_task_return_new_error (task, REALM_ERROR, REALM_ERROR_ALREADY_CONFIGURED,
		                         _("Already joined to a domain"));

	} else if (!validate_membership_options (options, &error)) {
		g_task_return_error (task, error);

	} else if (!match_admin_and_password (self->config, realm_name,
	                                      cred->x.password.name, cred->x.password.value)) {
		g_task_return_new_error (task, REALM_ERROR, REALM_ERROR_AUTH_FAILED,
		                         _("Admin name or password is not valid"));

	} else {
		realm_usleep_async (settings_delay (realm_name, "example-join-delay"),
		                    realm_invocation_get_cancellable (invocation),
		                    on_join_sleep_done, g_object_ref (task));
	}

	g_object_unref (task);
}

static const RealmCredential *
realm_example_join_creds (RealmKerberosMembership *membership)
{
	static const RealmCredential creds[] = {
		{ REALM_CREDENTIAL_PASSWORD, REALM_CREDENTIAL_OWNER_ADMIN },
		{ 0, }
	};

	return creds;
}

static void
on_leave_sleep_done (GObject *source,
                     GAsyncResult *res,
                     gpointer user_data)
{
	GTask *task = G_TASK (user_data);
	RealmExample *self = g_task_get_source_object (task);
	GError *error = NULL;
	const gchar *realm_name;

	if (realm_usleep_finish (res, &error)) {
		realm_name = realm_kerberos_get_name (REALM_KERBEROS (self));

		if (realm_ini_config_begin_change (self->config, &error)) {
			realm_ini_config_remove_section (self->config, realm_name);
			realm_ini_config_finish_change (self->config, &error);
		}
	}

	if (error)
		g_task_return_error (task, error);
	else
		g_task_return_boolean (task, TRUE);
	g_object_unref (self);
}

static GTask *
setup_leave (RealmExample *self,
             GVariant *options,
             GDBusMethodInvocation *invocation,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
	GTask *task;
	const gchar *realm_name;

	realm_name = realm_kerberos_get_name (REALM_KERBEROS (self));
	task = g_task_new (self, NULL, callback, user_data);

	/* Check that enrolled in this realm */
	if (!realm_ini_config_have_section (self->config, realm_name)) {
		g_task_return_new_error (task, REALM_ERROR, REALM_ERROR_NOT_CONFIGURED,
		                         _("Not currently joined to this domain"));
		g_object_unref (task);
		return NULL;
	}

	return task;
}

static void
realm_example_leave_password_async (RealmKerberosMembership *membership,
                                    const gchar *name,
                                    GBytes *password,
                                    GVariant *options,
                                    GDBusMethodInvocation *invocation,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
	RealmExample *self = REALM_EXAMPLE (membership);
	GTask *task;
	const gchar *realm_name;

	task = setup_leave (self, options, invocation, callback, user_data);
	if (task == NULL)
		return;

	realm_name = realm_kerberos_get_name (REALM_KERBEROS (self));

	if (!match_admin_and_password (self->config, realm_name, name, password)) {
		g_task_return_new_error (task, REALM_ERROR, REALM_ERROR_AUTH_FAILED,
		                         _("Admin name or password is not valid"));

	} else {
		realm_usleep_async (settings_delay (realm_name, "example-leave-delay"),
		                    realm_invocation_get_cancellable (invocation),
		                    on_leave_sleep_done, g_object_ref (task));
	}

	g_object_unref (task);
}

static void
realm_example_leave_automatic_async (RealmKerberosMembership *membership,
                                     GVariant *options,
                                     GDBusMethodInvocation *invocation,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
	RealmExample *self = REALM_EXAMPLE (membership);
	GTask *task;
	const gchar *realm_name;

	task = setup_leave (self, options, invocation, callback, user_data);
	if (task == NULL)
		return;

	realm_name = realm_kerberos_get_name (REALM_KERBEROS (self));

	if (realm_settings_boolean (realm_name, "example-no-auto-leave", FALSE) == TRUE) {
		g_task_return_new_error (task, REALM_ERROR, REALM_ERROR_AUTH_FAILED,
		                         _("Need credentials for leaving this domain"));

	} else {
		realm_usleep_async (settings_delay (realm_name, "example-leave-delay"),
		                    realm_invocation_get_cancellable (invocation),
		                    on_leave_sleep_done, g_object_ref (task));
	}

	g_object_unref (task);
}

static void
realm_example_leave_async (RealmKerberosMembership *membership,
                           RealmCredential *cred,
                           GVariant *options,
                           GDBusMethodInvocation *invocation,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	switch (cred->type) {
	case REALM_CREDENTIAL_AUTOMATIC:
		realm_example_leave_automatic_async (membership, options, invocation, callback, user_data);
		break;
	case REALM_CREDENTIAL_PASSWORD:
		realm_example_leave_password_async (membership, cred->x.password.name, cred->x.password.value,
		                                    options, invocation, callback, user_data);
		break;
	default:
		g_return_if_reached ();
	}
}

static const RealmCredential *
realm_example_leave_creds (RealmKerberosMembership *membership)
{
	static const RealmCredential creds[] = {
		{ REALM_CREDENTIAL_PASSWORD, REALM_CREDENTIAL_OWNER_ADMIN },
		{ REALM_CREDENTIAL_AUTOMATIC, REALM_CREDENTIAL_OWNER_NONE },
		{ 0, }
	};

	return creds;
}

static void
realm_example_logins_async (RealmKerberos *realm,
                            GDBusMethodInvocation *invocation,
                            RealmKerberosLoginPolicy login_policy,
                            const gchar **add,
                            const gchar **remove,
                            GVariant *options,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	RealmExample *self = REALM_EXAMPLE (realm);
	GTask *task;
	GError *error = NULL;
	const gchar *name;

	task = g_task_new (realm, NULL, callback, user_data);

	name = realm_kerberos_get_name (realm);

	if (realm_ini_config_begin_change (self->config, &error)) {
		realm_ini_config_set (self->config, name, "login-policy",
		                      realm_kerberos_login_policy_to_string (login_policy), NULL);
		realm_ini_config_set_list_diff (self->config, name, "login-permitted",
		                                ", ", add, remove);
		realm_ini_config_finish_change (self->config, &error);
	}

	if (error != NULL)
		g_task_return_error (task, error);
	else
		g_task_return_boolean (task, TRUE);
	g_object_unref (task);
}

static void
update_properties (RealmExample *self)
{
	RealmKerberos *kerberos = REALM_KERBEROS (self);
	GDBusInterface *iface;
	const gchar *name;
	gchar *domain;
	gchar *realm;
	gchar **formats;
	gchar **permitted;
	gchar *policy;
	const gchar *admin;
        gboolean configured;

	g_object_freeze_notify (G_OBJECT (self));

	name = realm_kerberos_get_name (kerberos);

	domain = name ? g_ascii_strdown (name, -1) : NULL;
	realm_kerberos_set_domain_name (kerberos, domain);
	g_free (domain);

	realm = name ? g_ascii_strup (name, -1) : NULL;
	realm_kerberos_set_realm_name (kerberos, realm);
	g_free (realm);

	/* Setup the workgroup property */
	formats = realm_ini_config_get_list (self->config, name, "login-formats", ", ");
	realm_kerberos_set_login_formats (kerberos, (const gchar **)formats);
	g_strfreev (formats);

	permitted = realm_ini_config_get_list (self->config, name, "login-permitted", ", ");
	realm_kerberos_set_permitted_logins (kerberos, (const gchar **)permitted);
	g_strfreev (permitted);

	policy = realm_ini_config_get (self->config, name, "login-policy");
	iface = g_dbus_object_get_interface (G_DBUS_OBJECT (self), REALM_DBUS_REALM_INTERFACE);
	g_object_set (iface, "login-policy", policy, NULL);
	g_free (policy);

	admin = realm_settings_value (name, "example-administrator");
	realm_kerberos_set_suggested_admin (kerberos, admin ? admin : "");

	configured = realm_ini_config_have_section (self->config, name);
        realm_kerberos_set_configured (kerberos, configured);
}

static void
on_config_changed (RealmIniConfig *config,
                   gpointer user_data)
{
	update_properties (REALM_EXAMPLE (user_data));
}

static gboolean
realm_example_membership_generic_finish (RealmKerberosMembership *realm,
                                         GAsyncResult *result,
                                         GError **error)
{
	if (g_task_propagate_boolean (G_TASK (result), error))
		return FALSE;

	update_properties (REALM_EXAMPLE (realm));
	return TRUE;
}

static gboolean
realm_example_generic_finish (RealmKerberos *realm,
                              GAsyncResult *result,
                              GError **error)
{
	if (g_task_propagate_boolean (G_TASK (result), error))
		return FALSE;

	update_properties (REALM_EXAMPLE (realm));
	return TRUE;
}

static void
realm_example_set_property (GObject *obj,
                            guint prop_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
	RealmExample *self = REALM_EXAMPLE (obj);
	RealmProvider *provider;

	switch (prop_id) {
	case PROP_PROVIDER:
		provider = g_value_get_object (value);
		g_object_get (provider, "example-config", &self->config, NULL);
		self->config_sig = g_signal_connect (self->config, "changed",
		                                     G_CALLBACK (on_config_changed),
		                                     self);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
realm_example_notify (GObject *obj,
                      GParamSpec *spec)
{
	if (g_str_equal (spec->name, "name"))
		update_properties (REALM_EXAMPLE (obj));

	if (G_OBJECT_CLASS (realm_example_parent_class)->notify)
		G_OBJECT_CLASS (realm_example_parent_class)->notify (obj, spec);
}

static void
realm_example_finalize (GObject *obj)
{
	RealmExample *self = REALM_EXAMPLE (obj);

	if (self->config)
		g_object_unref (self->config);

	G_OBJECT_CLASS (realm_example_parent_class)->finalize (obj);
}

void
realm_example_class_init (RealmExampleClass *klass)
{
	RealmKerberosClass *kerberos_class = REALM_KERBEROS_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	kerberos_class->logins_async = realm_example_logins_async;
	kerberos_class->logins_finish = realm_example_generic_finish;

	object_class->constructed = realm_example_constructed;
	object_class->set_property = realm_example_set_property;
	object_class->notify = realm_example_notify;
	object_class->finalize = realm_example_finalize;

	g_object_class_override_property (object_class, PROP_PROVIDER, "provider");
}

static void
realm_example_kerberos_membership_iface (RealmKerberosMembershipIface *iface)
{
	iface->join_async = realm_example_join_async;
	iface->join_finish = realm_example_membership_generic_finish;
	iface->join_creds = realm_example_join_creds;

	iface->leave_async = realm_example_leave_async;
	iface->leave_finish = realm_example_membership_generic_finish;
	iface->leave_creds = realm_example_leave_creds;
}

RealmKerberos *
realm_example_new (const gchar *name,
                   RealmProvider *provider)
{
	return g_object_new (REALM_TYPE_EXAMPLE,
	                     "name", name,
	                     "provider", provider,
	                     NULL);
}
