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

#include "realm-command.h"
#include "realm-daemon.h"
#include "realm-dbus-constants.h"
#include "realm-diagnostics.h"
#include "realm-discovery.h"
#include "realm-errors.h"
#include "realm-kerberos.h"
#include "realm-kerberos-membership.h"
#include "realm-packages.h"
#include "realm-provider.h"
#include "realm-samba.h"
#include "realm-samba-config.h"
#include "realm-samba-enroll.h"
#include "realm-samba-winbind.h"
#include "realm-settings.h"

#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include <errno.h>
#include <string.h>

struct _RealmSamba {
	RealmKerberos parent;
	RealmIniConfig *config;
	gulong config_sig;
};

typedef struct {
	RealmKerberosClass parent_class;
} RealmSambaClass;

enum {
	PROP_0,
	PROP_PROVIDER,
};

static const gchar *SAMBA_PACKAGES[] = {
	REALM_DBUS_IDENTIFIER_WINBIND,
	REALM_DBUS_IDENTIFIER_SAMBA,
	NULL
};

const gchar *NO_PACKAGES[] = {
	NULL
};

static void realm_samba_kerberos_membership_iface (RealmKerberosMembershipIface *iface);

G_DEFINE_TYPE_WITH_CODE (RealmSamba, realm_samba, REALM_TYPE_KERBEROS,
                         G_IMPLEMENT_INTERFACE (REALM_TYPE_KERBEROS_MEMBERSHIP, realm_samba_kerberos_membership_iface);
);

static void
realm_samba_init (RealmSamba *self)
{

}

static void
realm_samba_constructed (GObject *obj)
{
	RealmKerberos *kerberos = REALM_KERBEROS (obj);
	GVariant *supported;

	G_OBJECT_CLASS (realm_samba_parent_class)->constructed (obj);

	realm_kerberos_set_details (kerberos,
	                            REALM_DBUS_OPTION_SERVER_SOFTWARE, REALM_DBUS_IDENTIFIER_ACTIVE_DIRECTORY,
	                            REALM_DBUS_OPTION_CLIENT_SOFTWARE, REALM_DBUS_IDENTIFIER_WINBIND,
	                            NULL);

	/*
	 * Each line is a combination of owner and what kind of credentials are supported,
	 * same for enroll/leave. We can't accept a ccache, because samba3 needs
	 * to have credentials limited to RC4.
	 */
	supported = realm_kerberos_membership_build_supported (
			REALM_KERBEROS_CREDENTIAL_PASSWORD, REALM_KERBEROS_OWNER_ADMIN,
			REALM_KERBEROS_CREDENTIAL_PASSWORD, REALM_KERBEROS_OWNER_USER,
			0);
	g_variant_ref_sink (supported);
	realm_kerberos_set_supported_join_creds (kerberos, supported);
	realm_kerberos_set_supported_leave_creds (kerberos, supported);
	g_variant_unref (supported);

	realm_kerberos_set_suggested_admin (kerberos, "Administrator");
	realm_kerberos_set_login_policy (kerberos, REALM_KERBEROS_ALLOW_ANY_LOGIN);
	realm_kerberos_set_required_package_sets (kerberos, SAMBA_PACKAGES);
}

static gchar *
lookup_enrolled_realm (RealmSamba *self)
{
	gchar *enrolled = NULL;
	gchar *security;

	security = realm_ini_config_get (self->config, REALM_SAMBA_CONFIG_GLOBAL, "security");
	if (security != NULL && g_ascii_strcasecmp (security, "ADS") == 0)
		enrolled = realm_ini_config_get (self->config, REALM_SAMBA_CONFIG_GLOBAL, "realm");
	return enrolled;
}

static gboolean
lookup_is_enrolled (RealmSamba *self)
{
	const gchar *name;
	gchar *enrolled;
	gboolean ret = FALSE;

	enrolled = lookup_enrolled_realm (self);
	if (enrolled != NULL) {
		name = realm_kerberos_get_realm_name (REALM_KERBEROS (self));
		ret = g_strcmp0 (name, enrolled) == 0;
		g_free (enrolled);
	}

	return ret;
}

static gchar *
lookup_login_prefix (RealmSamba *self)
{
	gchar *workgroup;
	gchar *separator;

	workgroup = realm_ini_config_get (self->config, REALM_SAMBA_CONFIG_GLOBAL, "workgroup");
	if (workgroup == NULL)
		return NULL;

	separator = realm_ini_config_get (self->config, REALM_SAMBA_CONFIG_GLOBAL, "winbind separator");
	if (separator == NULL)
		separator = g_strdup ("\\");

	return g_strdup_printf ("%s%s", workgroup, separator);
}

typedef struct {
	GDBusMethodInvocation *invocation;
	gchar *ccache_file;
	gchar *computer_ou;
	gchar *realm_name;
	gchar *user_name;
	GBytes *password;
} EnrollClosure;

static void
enroll_closure_free (gpointer data)
{
	EnrollClosure *enroll = data;
	g_free (enroll->realm_name);
	g_free (enroll->computer_ou);
	g_free (enroll->user_name);
	g_bytes_unref (enroll->password);
	g_object_unref (enroll->invocation);
	if (enroll->ccache_file)
		realm_keberos_ccache_delete_and_free (enroll->ccache_file);
	g_slice_free (EnrollClosure, enroll);
}

static void
on_winbind_done (GObject *source,
                 GAsyncResult *result,
                 gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;

	realm_samba_winbind_configure_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	g_simple_async_result_complete (res);

	g_object_unref (res);
}

static void
on_join_do_winbind (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	EnrollClosure *enroll = g_simple_async_result_get_op_res_gpointer (res);
	RealmSamba *self = REALM_SAMBA (g_async_result_get_source_object (G_ASYNC_RESULT (res)));
	GHashTable *settings = NULL;
	GError *error = NULL;
	const gchar *workgroup = NULL;

	realm_samba_enroll_join_finish (result, &settings, &error);
	if (error == NULL) {
		workgroup = g_hash_table_lookup (settings, "workgroup");
		if (workgroup == NULL) {
			g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
			             _("Failed to calculate domain workgroup"));
		}
	}

	if (error == NULL) {
		realm_ini_config_change (self->config, REALM_SAMBA_CONFIG_GLOBAL, &error,
		                         "security", "ads",
		                         "realm", enroll->realm_name,
		                         "workgroup", workgroup,
		                         "template homedir", realm_settings_string ("users", "default-home"),
		                         "template shell", realm_settings_string ("users", "default-shell"),
		                         NULL);
	}

	if (error == NULL) {
		realm_samba_winbind_configure_async (self->config, enroll->invocation,
		                                     on_winbind_done, g_object_ref (res));
	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	if (settings)
		g_hash_table_unref (settings);
	g_object_unref (self);
	g_object_unref (res);
}

static void
on_install_do_join (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	EnrollClosure *enroll = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;

	realm_packages_install_finish (result, &error);
	if (error == NULL) {
		realm_samba_enroll_join_async (enroll->realm_name, enroll->user_name, enroll->password,
		                               enroll->computer_ou, enroll->invocation,
		                               on_join_do_winbind, g_object_ref (res));

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static gboolean
validate_membership_options (GVariant *options,
                             GError **error)
{
	const gchar *software;

	/* Figure out the method that we're going to use to enroll */
	if (g_variant_lookup (options, REALM_DBUS_OPTION_MEMBERSHIP_SOFTWARE, "&s", &software)) {
		if (!g_str_equal (software, REALM_DBUS_IDENTIFIER_SAMBA)) {
			g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
			             _("Unsupported or unknown membership software '%s'"), software);
			return FALSE;
		}
	}

	return TRUE;
}

static void
realm_samba_enroll_async (RealmKerberosMembership *membership,
                          const gchar *name,
                          GBytes *password,
                          RealmKerberosFlags flags,
                          GVariant *options,
                          GDBusMethodInvocation *invocation,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	RealmKerberos *realm = REALM_KERBEROS (membership);
	RealmSamba *self = REALM_SAMBA (realm);
	GSimpleAsyncResult *res;
	EnrollClosure *enroll;
	const gchar **packages;
	GError *error = NULL;
	gchar *enrolled;

	res = g_simple_async_result_new (G_OBJECT (realm), callback, user_data,
	                                 realm_samba_enroll_async);
	enroll = g_slice_new0 (EnrollClosure);
	enroll->realm_name = g_strdup (realm_kerberos_get_realm_name (realm));
	enroll->invocation = g_object_ref (invocation);
	enroll->computer_ou = realm_kerberos_calculate_join_computer_ou (realm, options);
	enroll->user_name = g_strdup (name);
	enroll->password = g_bytes_ref (password);
	g_simple_async_result_set_op_res_gpointer (res, enroll, enroll_closure_free);

	/* Make sure not already enrolled in a realm */
	enrolled = lookup_enrolled_realm (self);
	if (enrolled != NULL) {
		g_simple_async_result_set_error (res, REALM_ERROR, REALM_ERROR_ALREADY_CONFIGURED,
		                                 _("Already joined to a domain"));
		g_simple_async_result_complete_in_idle (res);

	} else if (!validate_membership_options (options, &error)) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete_in_idle (res);

	} else {
		if (flags & REALM_KERBEROS_ASSUME_PACKAGES)
			packages = NO_PACKAGES;
		else
			packages = SAMBA_PACKAGES;
		realm_packages_install_async (packages, enroll->invocation,
		                              on_install_do_join, g_object_ref (res));
	}

	g_free (enrolled);
	g_object_unref (res);
}

typedef struct {
	GDBusMethodInvocation *invocation;
	gchar *realm_name;
	gchar *ccache_file;
} LeaveClosure;

static void
leave_closure_free (gpointer data)
{
	LeaveClosure *leave = data;
	g_free (leave->realm_name);
	if (leave->ccache_file)
		realm_keberos_ccache_delete_and_free (leave->ccache_file);
	g_object_unref (leave->invocation);
	g_slice_free (LeaveClosure, leave);
}

static void
on_deconfigure_done (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;

	realm_samba_winbind_deconfigure_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (async, error);
	g_simple_async_result_complete (async);
	g_object_unref (async);
}

static void
leave_deconfigure_begin (RealmSamba *self,
                         GSimpleAsyncResult *async)
{
	LeaveClosure *leave;
	GError *error = NULL;

	leave = g_simple_async_result_get_op_res_gpointer (async);

	/* Flush the keytab of all the entries for this realm */
	realm_diagnostics_info (leave->invocation, "Removing entries from keytab for realm");

	if (!realm_kerberos_flush_keytab (leave->realm_name, &error)) {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete_in_idle (async);
		return;
	}

	/* Deconfigure smb.conf */
	realm_diagnostics_info (leave->invocation, "Updating smb.conf file");
	if (!realm_ini_config_change (self->config, REALM_SAMBA_CONFIG_GLOBAL, &error,
	                              "workgroup", NULL,
	                              "realm", NULL,
	                              "security", "user",
	                              NULL)) {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete_in_idle (async);
		return;
	}

	/* And then deconfigure winbind */
	realm_samba_winbind_deconfigure_async (self->config, leave->invocation,
	                                       on_deconfigure_done, g_object_ref (async));
}

static void
on_leave_do_deconfigure (GObject *source,
                         GAsyncResult *result,
                         gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	LeaveClosure *leave = g_simple_async_result_get_op_res_gpointer (res);
	RealmSamba *self = REALM_SAMBA (g_async_result_get_source_object (user_data));
	GError *error = NULL;

	/* We don't care if we can leave or not, just continue with other steps */
	realm_samba_enroll_leave_finish (result, &error);
	if (error != NULL) {
		realm_diagnostics_error (leave->invocation, error, NULL);
		g_error_free (error);
	}

	leave_deconfigure_begin (self, res);

	g_object_unref (self);
	g_object_unref (res);
}

static GSimpleAsyncResult *
setup_leave (RealmSamba *self,
             GVariant *options,
             GDBusMethodInvocation *invocation,
             GAsyncReadyCallback callback,
             gpointer user_data)
{
	LeaveClosure *leave;
	GSimpleAsyncResult *async;
	const gchar *realm_name;
	gchar *enrolled;

	realm_name = realm_kerberos_get_realm_name (REALM_KERBEROS (self));

	async = g_simple_async_result_new (G_OBJECT (self), callback, user_data, setup_leave);
	leave = g_slice_new0 (LeaveClosure);
	leave->realm_name = g_strdup (realm_name);
	leave->invocation = g_object_ref (invocation);
	g_simple_async_result_set_op_res_gpointer (async, leave, leave_closure_free);

	/* Check that enrolled in this realm */
	enrolled = lookup_enrolled_realm (self);
	if (g_strcmp0 (enrolled, realm_name) != 0) {
		g_simple_async_result_set_error (async, REALM_ERROR, REALM_ERROR_NOT_CONFIGURED,
		                                 _("Not currently joined to this domain"));
		g_simple_async_result_complete_in_idle (async);
		g_object_unref (async);
		return NULL;

	}

	return async;
}

static void
realm_samba_leave_password_async (RealmKerberosMembership *membership,
                                  const gchar *name,
                                  GBytes *password,
                                  RealmKerberosFlags flags,
                                  GVariant *options,
                                  GDBusMethodInvocation *invocation,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	RealmSamba *self = REALM_SAMBA (membership);
	GSimpleAsyncResult *async;
	LeaveClosure *leave;

	async = setup_leave (self, options, invocation, callback, user_data);
	if (async == NULL)
		return;

	leave = g_simple_async_result_get_op_res_gpointer (async);
	realm_samba_enroll_leave_async (leave->realm_name, name, password,
	                                leave->invocation, on_leave_do_deconfigure,
	                                g_object_ref (async));
	g_object_unref (async);
}

static void
realm_samba_leave_automatic_async (RealmKerberosMembership *membership,
                                   RealmKerberosFlags flags,
                                   GVariant *options,
                                   GDBusMethodInvocation *invocation,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
	RealmSamba *self = REALM_SAMBA (membership);
	GSimpleAsyncResult *async;

	async = setup_leave (self, options, invocation, callback, user_data);
	if (async == NULL)
		return;

	leave_deconfigure_begin (self, async);
	g_object_unref (async);
}

static gboolean
realm_samba_change_logins (RealmKerberos *realm,
                           GDBusMethodInvocation *invocation,
                           const gchar **add,
                           const gchar **remove,
                           GError **error)
{
	RealmSamba *self = REALM_SAMBA (realm);
	gchar **names;

	if (!lookup_is_enrolled (self)) {
		g_set_error (error, REALM_ERROR, REALM_ERROR_NOT_CONFIGURED,
		             _("Not joined to this domain"));
		return FALSE;
	}

	/* We cannot handle removing logins */
	names = realm_kerberos_parse_logins (realm, TRUE, remove, error);
	if (names == NULL)
		return FALSE;
	if (names[0] != NULL) {
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
		             _("The Samba provider cannot restrict permitted logins."));
		g_strfreev (names);
		return FALSE;
	}

	g_strfreev (names);
	names = realm_kerberos_parse_logins (realm, TRUE, add, error);
	if (names == NULL)
		return FALSE;

	/*
	 * Samba cannot restrict the set of logins. We allow specific logins to be
	 * added, but not changing the mode to only allow the permitted logins.
	 * In addition we don't keep track of the list of permitted logins.
	 */

	g_strfreev (names);
	return TRUE;
}

static void
realm_samba_logins_async (RealmKerberos *realm,
                          GDBusMethodInvocation *invocation,
                          RealmKerberosLoginPolicy login_policy,
                          const gchar **add,
                          const gchar **remove,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	GSimpleAsyncResult *async;
	GError *error = NULL;

	async = g_simple_async_result_new (G_OBJECT (realm), callback, user_data,
	                                   realm_samba_logins_async);

	/* Sadly we don't support this option */
	if (login_policy != REALM_KERBEROS_ALLOW_ANY_LOGIN &&
	    login_policy != REALM_KERBEROS_POLICY_NOT_SET) {
		g_simple_async_result_set_error (async, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
		                                 _("The Samba provider cannot restrict permitted logins."));

	/* Make note of the permitted logins, so we can return them in the property */
	} else if (!realm_samba_change_logins (realm, invocation, add, remove, &error)) {
		g_simple_async_result_take_error (async, error);
	}

	g_simple_async_result_complete_in_idle (async);
	g_object_unref (async);
}

static void
update_properties (RealmSamba *self)
{
	RealmKerberos *kerberos = REALM_KERBEROS (self);
	GPtrArray *permitted;
	gchar *login_formats[2] = { NULL, NULL };
	const gchar *name;
	gchar *domain;
	gchar *realm;
	gchar *prefix;

	g_object_freeze_notify (G_OBJECT (self));

	name = realm_kerberos_get_name (kerberos);

	domain = name ? g_ascii_strdown (name, -1) : NULL;
	realm_kerberos_set_domain_name (kerberos, domain);
	g_free (domain);

	realm = name ? g_ascii_strup (name, -1) : NULL;
	realm_kerberos_set_realm_name (kerberos, realm);
	g_free (realm);

	realm_kerberos_set_configured (kerberos, lookup_is_enrolled (self));

	/* Setup the workgroup property */
	prefix = lookup_login_prefix (self);
	if (prefix != NULL) {
		login_formats[0] = g_strdup_printf ("%s%%U", prefix);
		realm_kerberos_set_login_formats (kerberos, (const gchar **)login_formats);
		g_free (login_formats[0]);
		g_free (prefix);
	} else {
		login_formats[0] = "%U";
		realm_kerberos_set_login_formats (kerberos, (const gchar **)login_formats);
	}

	permitted = g_ptr_array_new_full (0, g_free);
	g_ptr_array_add (permitted, NULL);

	realm_kerberos_set_permitted_logins (kerberos, (const gchar **)permitted->pdata);
	g_ptr_array_free (permitted, TRUE);

	g_object_thaw_notify (G_OBJECT (self));
}

static void
on_config_changed (RealmIniConfig *config,
                   gpointer user_data)
{
	update_properties (REALM_SAMBA (user_data));
}

static gboolean
realm_samba_membership_generic_finish (RealmKerberosMembership *realm,
                                       GAsyncResult *result,
                                       GError **error)
{
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	update_properties (REALM_SAMBA (realm));
	return TRUE;
}

static gboolean
realm_samba_generic_finish (RealmKerberos *realm,
                            GAsyncResult *result,
                            GError **error)
{
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	update_properties (REALM_SAMBA (realm));
	return TRUE;
}

static void
realm_samba_set_property (GObject *obj,
                          guint prop_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
	RealmSamba *self = REALM_SAMBA (obj);
	RealmProvider *provider;

	switch (prop_id) {
	case PROP_PROVIDER:
		provider = g_value_get_object (value);
		g_object_get (provider, "samba-config", &self->config, NULL);
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
realm_samba_notify (GObject *obj,
                    GParamSpec *spec)
{
	if (g_str_equal (spec->name, "name"))
		update_properties (REALM_SAMBA (obj));

	if (G_OBJECT_CLASS (realm_samba_parent_class)->notify)
		G_OBJECT_CLASS (realm_samba_parent_class)->notify (obj, spec);
}

static void
realm_samba_finalize (GObject *obj)
{
	RealmSamba  *self = REALM_SAMBA (obj);

	if (self->config)
		g_object_unref (self->config);

	G_OBJECT_CLASS (realm_samba_parent_class)->finalize (obj);
}

void
realm_samba_class_init (RealmSambaClass *klass)
{
	RealmKerberosClass *kerberos_class = REALM_KERBEROS_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	kerberos_class->logins_async = realm_samba_logins_async;
	kerberos_class->logins_finish = realm_samba_generic_finish;

	object_class->constructed = realm_samba_constructed;
	object_class->set_property = realm_samba_set_property;
	object_class->notify = realm_samba_notify;
	object_class->finalize = realm_samba_finalize;

	g_object_class_override_property (object_class, PROP_PROVIDER, "provider");
}

static void
realm_samba_kerberos_membership_iface (RealmKerberosMembershipIface *iface)
{
	iface->enroll_password_async = realm_samba_enroll_async;
	iface->enroll_finish = realm_samba_membership_generic_finish;
	iface->unenroll_password_async = realm_samba_leave_password_async;
	iface->unenroll_automatic_async = realm_samba_leave_automatic_async;
	iface->unenroll_finish = realm_samba_membership_generic_finish;
}

RealmKerberos *
realm_samba_new (const gchar *name,
                 RealmProvider *provider)
{
	return g_object_new (REALM_TYPE_SAMBA,
	                     "name", name,
	                     "provider", provider,
	                     NULL);
}
