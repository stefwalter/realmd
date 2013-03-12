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
	GVariant *supported;

	G_OBJECT_CLASS (realm_example_parent_class)->constructed (obj);

	realm_kerberos_set_details (kerberos,
	                            REALM_DBUS_OPTION_SERVER_SOFTWARE, REALM_DBUS_IDENTIFIER_EXAMPLE,
	                            REALM_DBUS_OPTION_CLIENT_SOFTWARE, REALM_DBUS_IDENTIFIER_EXAMPLE,
	                            NULL);

	supported = realm_kerberos_membership_build_supported (
			REALM_KERBEROS_CREDENTIAL_PASSWORD, REALM_KERBEROS_OWNER_ADMIN,
			0);

	g_variant_ref_sink (supported);
	realm_kerberos_set_supported_join_creds (kerberos, supported);
	g_variant_unref (supported);

	supported = realm_kerberos_membership_build_supported (
			REALM_KERBEROS_CREDENTIAL_PASSWORD, REALM_KERBEROS_OWNER_ADMIN,
			REALM_KERBEROS_CREDENTIAL_AUTOMATIC, REALM_KERBEROS_OWNER_NONE,
			0);

	g_variant_ref_sink (supported);
	realm_kerberos_set_supported_leave_creds (kerberos, supported);
	g_variant_unref (supported);

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

typedef struct {
	RealmExample *self;
	GSimpleAsyncResult *async;
} OpData;

static gdouble
settings_delay (const gchar *realm_name, const gchar *key)
{
	return realm_settings_double (realm_name, key, 0.0) * G_USEC_PER_SEC;
}

static void
free_op_data (OpData *data)
{
	g_object_unref (data->self);
	g_object_unref (data->async);
	g_free (data);
}

static void
on_join_sleep_done (GObject *source,
                    GAsyncResult *res,
                    gpointer user_data)
{
	OpData *data = user_data;
	GError *error = NULL;
	const gchar *realm_name;

	if (realm_usleep_finish (res, &error)) {
		realm_name = realm_kerberos_get_name (REALM_KERBEROS (data->self));
		realm_ini_config_change (data->self->config, realm_name, &error,
		                         "login-formats", "%U@%D",
		                         "login-permitted", "",
		                         "login-policy", REALM_DBUS_LOGIN_POLICY_PERMITTED,
		                         NULL);
	}

	if (error)
		g_simple_async_result_take_error (data->async, error);

	g_simple_async_result_complete (data->async);
	free_op_data (data);
}

static void
realm_example_join_async (RealmKerberosMembership *membership,
                          const gchar *name,
                          GBytes *password,
                          RealmKerberosFlags flags,
                          GVariant *options,
                          GDBusMethodInvocation *invocation,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	RealmExample *self = REALM_EXAMPLE (membership);
	RealmKerberos *kerberos = REALM_KERBEROS (self);
	GSimpleAsyncResult *async;
	GError *error = NULL;
	const gchar *realm_name;

	realm_name = realm_kerberos_get_name (kerberos);
	async = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
	                                   realm_example_join_async);

	/* Make sure not already enrolled in a realm */
	if (realm_ini_config_have_section (self->config, realm_name)) {
		g_simple_async_result_set_error (async, REALM_ERROR, REALM_ERROR_ALREADY_CONFIGURED,
		                                 _("Already joined to a domain"));
		g_simple_async_result_complete_in_idle (async);

	} else if (!validate_membership_options (options, &error)) {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete_in_idle (async);

	} else if (!match_admin_and_password (self->config, realm_name, name, password)) {
		g_simple_async_result_set_error (async, REALM_ERROR, REALM_ERROR_AUTH_FAILED,
		                                 _("Admin name or password is not valid"));
		g_simple_async_result_complete_in_idle (async);

	} else {
		OpData *data = g_new0 (OpData, 1);
		data->self = g_object_ref (self);
		data->async = g_object_ref (async);
		realm_usleep_async (settings_delay (realm_name, "example-join-delay"),
		                    realm_invocation_get_cancellable (invocation),
		                    on_join_sleep_done, data);
	}

	g_object_unref (async);
}

static void
on_leave_sleep_done (GObject *source,
                     GAsyncResult *res,
                     gpointer user_data)
{
	OpData *data = user_data;
	GError *error = NULL;
	const gchar *realm_name;

	if (realm_usleep_finish (res, &error)) {
		realm_name = realm_kerberos_get_name (REALM_KERBEROS (data->self));

		if (realm_ini_config_begin_change (data->self->config, &error)) {
			realm_ini_config_remove_section (data->self->config, realm_name);
			realm_ini_config_finish_change (data->self->config, &error);
		}
	}

	if (error)
		g_simple_async_result_take_error (data->async, error);

	g_simple_async_result_complete (data->async);
	free_op_data (data);
}

static GSimpleAsyncResult *
setup_leave (RealmExample *self,
             GVariant *options,
             GDBusMethodInvocation *invocation,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
	GSimpleAsyncResult *async;
	const gchar *realm_name;

	realm_name = realm_kerberos_get_name (REALM_KERBEROS (self));
	async = g_simple_async_result_new (G_OBJECT (self), callback, user_data, setup_leave);

	/* Check that enrolled in this realm */
	if (!realm_ini_config_have_section (self->config, realm_name)) {
		g_simple_async_result_set_error (async, REALM_ERROR, REALM_ERROR_NOT_CONFIGURED,
		                                 _("Not currently joined to this domain"));
		g_simple_async_result_complete_in_idle (async);
		g_object_unref (async);
		return NULL;
	}

	return async;
}

static void
realm_example_leave_password_async (RealmKerberosMembership *membership,
                                    const gchar *name,
                                    GBytes *password,
                                    RealmKerberosFlags flags,
                                    GVariant *options,
                                    GDBusMethodInvocation *invocation,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
	RealmExample *self = REALM_EXAMPLE (membership);
	GSimpleAsyncResult *async;
	const gchar *realm_name;

	async = setup_leave (self, options, invocation, callback, user_data);
	if (async == NULL)
		return;

	realm_name = realm_kerberos_get_name (REALM_KERBEROS (self));

	if (!match_admin_and_password (self->config, realm_name, name, password)) {
		g_simple_async_result_set_error (async, REALM_ERROR, REALM_ERROR_AUTH_FAILED,
		                                 _("Admin name or password is not valid"));
		g_simple_async_result_complete_in_idle (async);

	} else {
		OpData *data = g_new0 (OpData, 1);
		data->self = g_object_ref (self);
		data->async = g_object_ref (async);
		realm_usleep_async (settings_delay (realm_name, "example-leave-delay"),
		                    realm_invocation_get_cancellable (invocation),
		                    on_leave_sleep_done, data);
	}

	g_object_unref (async);
}

static void
realm_example_leave_automatic_async (RealmKerberosMembership *membership,
                                     RealmKerberosFlags flags,
                                     GVariant *options,
                                     GDBusMethodInvocation *invocation,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
	RealmExample *self = REALM_EXAMPLE (membership);
	GSimpleAsyncResult *async;
	const gchar *realm_name;

	async = setup_leave (self, options, invocation, callback, user_data);
	if (async == NULL)
		return;

	realm_name = realm_kerberos_get_name (REALM_KERBEROS (self));

	if (realm_settings_boolean (realm_name, "example-no-auto-leave") == TRUE) {
		g_simple_async_result_set_error (async, REALM_ERROR, REALM_ERROR_AUTH_FAILED,
		                                 _("Need credentials for leaving this domain"));
		g_simple_async_result_complete_in_idle (async);

	} else {
		OpData *data = g_new0 (OpData, 1);
		data->self = g_object_ref (self);
		data->async = g_object_ref (async);
		realm_usleep_async (settings_delay (realm_name, "example-leave-delay"),
		                    realm_invocation_get_cancellable (invocation),
		                    on_leave_sleep_done, data);
	}

	g_object_unref (async);
}

static void
realm_example_logins_async (RealmKerberos *realm,
                            GDBusMethodInvocation *invocation,
                            RealmKerberosLoginPolicy login_policy,
                            const gchar **add,
                            const gchar **remove,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	RealmExample *self = REALM_EXAMPLE (realm);
	GSimpleAsyncResult *async;
	GError *error = NULL;
	const gchar *name;

	async = g_simple_async_result_new (G_OBJECT (realm), callback, user_data,
	                                   realm_example_logins_async);

	name = realm_kerberos_get_name (realm);

	if (realm_ini_config_begin_change (self->config, &error)) {
		realm_ini_config_set (self->config, name, "login-policy",
		                      realm_kerberos_login_policy_to_string (login_policy));
		realm_ini_config_set_list_diff (self->config, name, "login-permitted",
		                                ", ", add, remove);
		realm_ini_config_finish_change (self->config, &error);
	}

	if (error != NULL)
		g_simple_async_result_take_error (async, error);

	g_simple_async_result_complete_in_idle (async);
	g_object_unref (async);
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
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	update_properties (REALM_EXAMPLE (realm));
	return TRUE;
}

static gboolean
realm_example_generic_finish (RealmKerberos *realm,
                              GAsyncResult *result,
                              GError **error)
{
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
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
	iface->enroll_password_async = realm_example_join_async;
	iface->enroll_finish = realm_example_membership_generic_finish;
	iface->unenroll_password_async = realm_example_leave_password_async;
	iface->unenroll_automatic_async = realm_example_leave_automatic_async;
	iface->unenroll_finish = realm_example_membership_generic_finish;
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
