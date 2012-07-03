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

#include "realm-ad-discover.h"
#include "realm-daemon.h"
#include "realm-dbus-constants.h"
#include "realm-diagnostics.h"
#include "realm-discovery.h"
#include "realm-errors.h"
#include "realm-packages.h"
#include "realm-provider.h"
#include "realm-samba-enroll.h"
#include "realm-service.h"
#include "realm-sssd-ad.h"
#include "realm-sssd-config.h"

#include <glib/gstdio.h>

#include <errno.h>
#include <string.h>

struct _RealmSssdAd {
	RealmKerberos parent;
	gchar *realm;
	gchar *domain;
	gchar *section;
	RealmIniConfig *config;
	gulong config_sig;
};

typedef struct {
	RealmKerberosClass parent_class;
} RealmSssdAdClass;

enum {
	PROP_0,
	PROP_NAME,
	PROP_DOMAIN,
	PROP_PROVIDER,
};

G_DEFINE_TYPE (RealmSssdAd, realm_sssd_ad, REALM_TYPE_KERBEROS);

static void
realm_sssd_ad_init (RealmSssdAd *self)
{
	GPtrArray *entries;
	GVariant *entry;
	GVariant *details;

	entries = g_ptr_array_new ();

	entry = g_variant_new_dict_entry (g_variant_new_string ("server-software"),
	                                  g_variant_new_string ("active-directory"));
	g_ptr_array_add (entries, entry);

	entry = g_variant_new_dict_entry (g_variant_new_string ("client-software"),
	                                  g_variant_new_string ("sssd"));
	g_ptr_array_add (entries, entry);

	details = g_variant_new_array (G_VARIANT_TYPE ("{ss}"),
	                               (GVariant * const *)entries->pdata,
	                               entries->len);
	g_variant_ref_sink (details);

	g_object_set (self,
	              "details", details,
	              "suggested-administrator", "Administrator",
	              NULL);

	g_variant_unref (details);
	g_ptr_array_free (entries, TRUE);
}

typedef struct {
	GDBusMethodInvocation *invocation;
	GBytes *admin_kerberos_cache;
	gchar *realm_name;
	GHashTable *discovery;
} EnrollClosure;

static void
enroll_closure_free (gpointer data)
{
	EnrollClosure *enroll = data;
	g_free (enroll->realm_name);
	g_object_unref (enroll->invocation);
	g_bytes_unref (enroll->admin_kerberos_cache);
	g_hash_table_unref (enroll->discovery);
	g_slice_free (EnrollClosure, enroll);
}

static void
on_sssd_done (GObject *source,
              GAsyncResult *result,
              gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;

	realm_service_enable_and_restart_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	g_simple_async_result_complete (res);

	g_object_unref (res);
}

static gboolean
configure_sssd_for_domain (RealmIniConfig *config,
                           const gchar *realm,
                           GHashTable *settings,
                           GError **error)
{
	const gchar *workgroup;
	gboolean ret;
	gchar *domain;
	gchar **parts;
	gchar *rdn;
	gchar *dn;
	gint i;

	workgroup = g_hash_table_lookup (settings, "workgroup");
	if (workgroup == NULL) {
		g_set_error (error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Failed to calculate domain workgroup");
		return FALSE;
	}

	/* Calculate the domain and dn */
	domain = g_ascii_strdown (realm, -1);
	parts = g_strsplit (domain, ".", -1);
	for (i = 0; parts[i] != NULL; i++) {
		rdn = g_strdup_printf ("dc=%s", parts[i]);
		g_free (parts[i]);
		parts[i] = rdn;
	}
	dn = g_strjoinv (",", parts);
	g_strfreev (parts);

	ret = realm_sssd_config_add_domain (config, workgroup, error,
	                                    "realmd_type", "ad",
	                                    "enumerate", "False",
	                                    "re_expression", "(?P<domain>[^\\\\]+)\\\\(?P<name>[^\\\\]+)",
	                                    "full_name_format", "%2$s\\%1$s",
	                                    "dns_discovery_domain", domain,
	                                    "case_sensitive", "False",
	                                    "cache_credentials", "False",
	                                    "use_fully_qualified_names", "True",

	                                    "id_provider", "ldap",
	                                    "ldap_user_principal", "userPrincipalName",
	                                    "ldap_schema", "rfc2307bis",
	                                    "ldap_sasl_mech", "GSSAPI",
	                                    "ldap_group_search_base", dn,
	                                    "ldap_force_upper_case_realm", "True",
	                                    "ldap_user_home_directory", "unixHomeDirectory",
	                                    "ldap_user_object_class", "user",
	                                    "ldap_user_search_base", dn,

	                                    "auth_provider", "krb5",
	                                    "chpass_provider", "krb5",
	                                    "krb5_realm", realm,
	                                    "krb5_store_password_if_offline", "True",

	                                    NULL);

	g_free (domain);
	g_free (dn);

	return ret;
}

static void
on_join_do_sssd (GObject *source,
                 GAsyncResult *result,
                 gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	EnrollClosure *enroll = g_simple_async_result_get_op_res_gpointer (res);
	RealmSssdAd *self = REALM_SSSD_AD (g_async_result_get_source_object (user_data));
	GHashTable *settings = NULL;
	GError *error = NULL;

	realm_samba_enroll_join_finish (result, &settings, &error);
	if (error == NULL) {
		configure_sssd_for_domain (self->config, enroll->realm_name,
		                           settings, &error);
		g_hash_table_unref (settings);
	}

	if (error == NULL) {
		realm_service_enable_and_restart ("sssd", enroll->invocation,
		                                  on_sssd_done, g_object_ref (res));

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
		realm_samba_enroll_join_async (enroll->realm_name, enroll->admin_kerberos_cache,
		                               enroll->invocation, on_join_do_sssd,
		                               g_object_ref (res));

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
on_discover_do_install (GObject *source,
                        GAsyncResult *result,
                        gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	EnrollClosure *enroll = g_simple_async_result_get_op_res_gpointer (res);
	GHashTable *discovery = NULL;
	GError *error = NULL;

	if (realm_ad_discover_finish (result, &discovery, &error)) {
		enroll->discovery = discovery;
		realm_packages_install_async ("sssd_ad-packages", enroll->invocation,
		                              on_install_do_join, g_object_ref (res));

	} else if (error == NULL) {
		g_simple_async_result_set_error (res, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		                                 "Invalid or unusable realm argument");
		g_simple_async_result_complete (res);

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);

}

static void
realm_sssd_ad_enroll_async (RealmKerberos *realm,
                            GBytes *admin_kerberos_cache,
                            GDBusMethodInvocation *invocation,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	RealmSssdAd *self = REALM_SSSD_AD (realm);
	GSimpleAsyncResult *res;
	EnrollClosure *enroll;

	res = g_simple_async_result_new (G_OBJECT (realm), callback, user_data,
	                                 realm_sssd_ad_enroll_async);
	enroll = g_slice_new0 (EnrollClosure);
	enroll->realm_name = g_strdup (self->realm);
	enroll->invocation = g_object_ref (invocation);
	enroll->admin_kerberos_cache = g_bytes_ref (admin_kerberos_cache);
	g_simple_async_result_set_op_res_gpointer (res, enroll, enroll_closure_free);

	enroll->discovery = realm_kerberos_get_discovery (realm);
	if (enroll->discovery)
		g_hash_table_ref (enroll->discovery);

	/* Make sure not already enrolled in a realm */
	if (self->section != NULL) {
		g_simple_async_result_set_error (res, REALM_ERROR, REALM_ERROR_ALREADY_ENROLLED,
		                                 "Already enrolled in this realm");
		g_simple_async_result_complete_in_idle (res);

	} else if (realm_sssd_config_have_domain (self->config, enroll->realm_name)) {
		g_simple_async_result_set_error (res, REALM_ERROR, REALM_ERROR_ALREADY_ENROLLED,
		                                 "This domain is already configured");
		g_simple_async_result_complete_in_idle (res);

	/* Caller didn't discover first time around, so do that now */
	} else if (enroll->discovery == NULL) {
		realm_ad_discover_async (self->realm, invocation,
		                         on_discover_do_install, g_object_ref (res));

	/* Already have discovery info, so go straight to install */
	} else {
		realm_packages_install_async ("sssd-ad-packages", invocation,
		                              on_install_do_join, g_object_ref (res));
	}

	g_object_unref (res);
}

typedef struct {
	GDBusMethodInvocation *invocation;
	gchar *realm_name;
} UnenrollClosure;

static void
unenroll_closure_free (gpointer data)
{
	UnenrollClosure *unenroll = data;
	g_free (unenroll->realm_name);
	g_object_unref (unenroll->invocation);
	g_slice_free (UnenrollClosure, unenroll);
}

static void
on_service_disable_done (GObject *source,
                         GAsyncResult *result,
                         gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;

	realm_service_disable_and_stop_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	g_simple_async_result_complete (res);

	g_object_unref (res);
}

static void
on_service_restart_done (GObject *source,
                         GAsyncResult *result,
                         gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;

	realm_service_enable_and_restart_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	g_simple_async_result_complete (res);

	g_object_unref (res);
}

static void
on_leave_do_sssd (GObject *source,
                  GAsyncResult *result,
                  gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	UnenrollClosure *unenroll = g_simple_async_result_get_op_res_gpointer (res);
	RealmSssdAd *self = REALM_SSSD_AD (g_async_result_get_source_object (user_data));
	GError *error = NULL;
	gchar **domains;

	realm_samba_enroll_leave_finish (result, NULL);

	/* We don't care if we can leave or not, just continue with other steps */
	realm_sssd_config_remove_domain (self->config, self->section, &error);
	if (error != NULL) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
		g_error_free (error);

	} else {
		/* If no domains, then disable sssd */
		domains = realm_sssd_config_get_domains (self->config);
		if (domains == NULL || g_strv_length (domains) == 0) {
			realm_service_disable_and_stop ("sssd", unenroll->invocation,
			                                on_service_disable_done, g_object_ref (res));

		/* If any domains left, then restart sssd */
		} else {
			realm_service_restart ("sssd", unenroll->invocation,
			                       on_service_restart_done, g_object_ref (res));
		}
	}

	g_object_unref (self);
	g_object_unref (res);
}

static void
realm_sssd_ad_unenroll_async (RealmKerberos *realm,
                              GBytes *admin_kerberos_cache,
                              GDBusMethodInvocation *invocation,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	RealmSssdAd *self = REALM_SSSD_AD (realm);
	GSimpleAsyncResult *res;
	UnenrollClosure *unenroll;

	res = g_simple_async_result_new (G_OBJECT (realm), callback, user_data,
	                                 realm_sssd_ad_unenroll_async);
	unenroll = g_slice_new0 (UnenrollClosure);
	unenroll->realm_name = g_strdup (self->realm);
	unenroll->invocation = g_object_ref (invocation);
	g_simple_async_result_set_op_res_gpointer (res, unenroll, unenroll_closure_free);

	/* Check that enrolled in this realm */
	if (self->section) {
		realm_samba_enroll_leave_async (self->realm, admin_kerberos_cache, invocation,
		                                on_leave_do_sssd, g_object_ref (res));
	} else {
		g_simple_async_result_set_error (res, REALM_ERROR, REALM_ERROR_NOT_ENROLLED,
		                                 "Not currently enrolled in the realm");
		g_simple_async_result_complete_in_idle (res);
	}

	g_object_unref (res);
}

static void
on_logins_restarted (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;

	realm_service_restart_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (async, error);

	g_simple_async_result_complete (async);
	g_object_unref (async);
}

static void
realm_sssd_ad_logins_async (RealmKerberos *realm,
                            GDBusMethodInvocation *invocation,
                            const gchar **add,
                            const gchar **remove,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	RealmSssdAd *self = REALM_SSSD_AD (realm);
	GSimpleAsyncResult *async;
	gchar **remove_names = NULL;
	gchar **add_names = NULL;
	gboolean ret = FALSE;
	GError *error = NULL;

	if (!self->section) {
		async = g_simple_async_result_new_error (G_OBJECT (realm), callback, user_data,
		                                         REALM_ERROR, REALM_ERROR_NOT_ENROLLED,
		                                         "Not enrolled in this realm");
		g_simple_async_result_complete_in_idle (async);
		g_object_unref (async);
		return;
	}

	async = g_simple_async_result_new (G_OBJECT (realm), callback, user_data,
	                                   realm_sssd_ad_logins_async);

	add_names = realm_kerberos_parse_logins (realm, TRUE, add);
	if (add_names != NULL)
		remove_names = realm_kerberos_parse_logins (realm, TRUE, add);

	if (add_names == NULL || remove_names == NULL) {
		g_simple_async_result_set_error (async, G_DBUS_ERROR,
		                                 G_DBUS_ERROR_INVALID_ARGS,
		                                 "Invalid login argument");
		g_simple_async_result_complete_in_idle (async);
	}

	if (add_names && remove_names) {
		ret = realm_ini_config_change_list (self->config,
		                                    self->section,
		                                    "simple_allow_users", ",",
		                                    (const gchar **)add_names,
		                                    (const gchar **)remove_names,
		                                    &error);

		if (ret) {
			realm_service_restart ("sssd", invocation,
			                       on_logins_restarted,
			                       g_object_ref (async));

		} else {
			g_simple_async_result_take_error (async, error);
			g_simple_async_result_complete_in_idle (async);
		}
	}

	g_strfreev (remove_names);
	g_strfreev (add_names);

	g_object_unref (async);
}

static gboolean
realm_sssd_ad_generic_finish (RealmKerberos *realm,
                            GAsyncResult *result,
                            GError **error)
{
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

static void
update_enrolled (RealmSssdAd *self)
{
	g_object_set (self, "enrolled", self->section ? TRUE : FALSE, NULL);
}

static void
update_domain (RealmSssdAd *self)
{
	gchar *domain = NULL;

	if (self->section != NULL)
		domain = realm_ini_config_get (self->config, self->section, "dns_discovery_domain");
	if (domain == NULL)
		domain = g_ascii_strdown (self->realm, -1);
	g_object_set (self, "domain", domain, NULL);
	g_free (domain);
}

static gchar *
build_login_format (const gchar *format,
                    ...)
{
	gchar *result;
	va_list va;

	/* This function exists mostly to get around gcc warnings */

	if (format == NULL)
		format = "%1$s@%2$s";

	va_start (va, format);
	result = g_strdup_vprintf (format, va);
	va_end (va);

	return result;
}


static void
update_login_format (RealmSssdAd *self)
{
	gchar *login_format;
	gchar *format = NULL;

	if (self->section == NULL) {
		g_object_set (self, "login-format", "", NULL);
		return;
	}

	/* Setup the login format */
	format = realm_ini_config_get (self->config, self->section, "full_name_format");

	/* Here we place a '%s' in the place of the user in the format */
	login_format = build_login_format (format, "%s", self->domain);
	g_object_set (self, "login-format", login_format, NULL);
	g_free (login_format);
	g_free (format);
}

static void
update_permitted_logins (RealmSssdAd *self)
{
	GPtrArray *permitted;
	gchar *access = NULL;
	gchar **values;
	gint i;

	permitted = g_ptr_array_new_full (0, g_free);
	if (self->section != NULL)
		access = realm_ini_config_get (self->config, self->section, "access_provider");
	if (g_strcmp0 (access, "simple") == 0) {
		values = realm_ini_config_get_list (self->config, self->section,
		                                    "simple_allow_users", ",");
		for (i = 0; values != NULL && values[i] != NULL; i++)
			g_ptr_array_add (permitted, realm_kerberos_format_login (REALM_KERBEROS (self), values[i]));
		g_strfreev (values);
		g_free (access);
	}
	g_ptr_array_add (permitted, NULL);

	g_object_set (self, "permitted-logins", (gchar **)permitted->pdata, NULL);
	g_ptr_array_free (permitted, TRUE);
}

static void
update_properties (RealmSssdAd *self)
{
	GObject *obj = G_OBJECT (self);
	gchar *section = NULL;
	gchar *domain = NULL;
	gchar **domains;
	gchar *realm;
	gint i;

	g_object_freeze_notify (obj);

	/* Find the config domain with our realm */
	domains = realm_sssd_config_get_domains (self->config);
	for (i = 0; domains && domains[i]; i++) {
		section = realm_sssd_config_domain_to_section (domains[i]);
		realm = realm_ini_config_get (self->config, section, "krb5_realm");
		if (g_strcmp0 (realm, self->realm) == 0) {
			domain = g_strdup (domains[i]);
			break;
		} else {
			g_free (section);
			section = NULL;
		}
	}
	g_strfreev (domains);

	g_free (self->section);
	self->section = section;
	g_free (self->domain);
	self->domain = domain;

	/* Update all the other properties */
	update_enrolled (self);
	update_domain (self);
	update_login_format (self);
	update_permitted_logins (self);

	g_object_thaw_notify (obj);
}

static void
on_config_changed (RealmIniConfig *config,
                   gpointer user_data)
{
	update_properties (REALM_SSSD_AD (user_data));
}

static void
realm_sssd_ad_get_property (GObject *obj,
                          guint prop_id,
                          GValue *value,
                          GParamSpec *pspec)
{
	RealmSssdAd *self = REALM_SSSD_AD (obj);

	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, self->realm);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
realm_sssd_ad_set_property (GObject *obj,
                          guint prop_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
	RealmSssdAd *self = REALM_SSSD_AD (obj);
	RealmProvider *provider;

	switch (prop_id) {
	case PROP_NAME:
		self->realm = g_value_dup_string (value);
		break;
	case PROP_PROVIDER:
		provider = g_value_get_object (value);
		g_object_get (provider, "sssd-config", &self->config, NULL);
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
realm_sssd_ad_consructed (GObject *obj)
{
	RealmSssdAd *self = REALM_SSSD_AD (obj);

	G_OBJECT_CLASS (realm_sssd_ad_parent_class)->constructed (obj);

	update_properties (self);
}

static void
realm_sssd_ad_finalize (GObject *obj)
{
	RealmSssdAd  *self = REALM_SSSD_AD (obj);

	g_free (self->section);
	if (self->config)
		g_object_unref (self->config);

	G_OBJECT_CLASS (realm_sssd_ad_parent_class)->finalize (obj);
}

void
realm_sssd_ad_class_init (RealmSssdAdClass *klass)
{
	RealmKerberosClass *kerberos_class = REALM_KERBEROS_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	kerberos_class->enroll_async = realm_sssd_ad_enroll_async;
	kerberos_class->enroll_finish = realm_sssd_ad_generic_finish;
	kerberos_class->unenroll_async = realm_sssd_ad_unenroll_async;
	kerberos_class->unenroll_finish = realm_sssd_ad_generic_finish;
	kerberos_class->logins_async = realm_sssd_ad_logins_async;
	kerberos_class->logins_finish = realm_sssd_ad_generic_finish;

	object_class->constructed = realm_sssd_ad_consructed;
	object_class->get_property = realm_sssd_ad_get_property;
	object_class->set_property = realm_sssd_ad_set_property;
	object_class->finalize = realm_sssd_ad_finalize;

	g_object_class_install_property (object_class, PROP_NAME,
	            g_param_spec_string ("name", "Name", "Realm Name",
	                                 "", G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (object_class, PROP_PROVIDER,
	            g_param_spec_object ("provider", "Provider", "SssdAd Provider",
	                                 REALM_TYPE_PROVIDER, G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

RealmKerberos *
realm_sssd_ad_new (const gchar *name,
                 RealmProvider *provider)
{
	return g_object_new (REALM_TYPE_SSSD_AD,
	                     "name", name,
	                     "provider", provider,
	                     NULL);
}
