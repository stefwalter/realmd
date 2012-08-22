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
#include "realm-command.h"
#include "realm-dbus-constants.h"
#include "realm-diagnostics.h"
#include "realm-errors.h"
#include "realm-packages.h"
#include "realm-samba-enroll.h"
#include "realm-service.h"
#include "realm-sssd.h"
#include "realm-sssd-ad.h"
#include "realm-sssd-config.h"

#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include <errno.h>
#include <string.h>

struct _RealmSssdAd {
	RealmSssd parent;
};

typedef struct {
	RealmSssdClass parent_class;
} RealmSssdAdClass;

G_DEFINE_TYPE (RealmSssdAd, realm_sssd_ad, REALM_TYPE_SSSD);

static void
realm_sssd_ad_init (RealmSssdAd *self)
{

}

static void
realm_sssd_ad_constructed (GObject *obj)
{
	RealmKerberos *kerberos = REALM_KERBEROS (obj);
	GVariant *supported;

	G_OBJECT_CLASS (realm_sssd_ad_parent_class)->constructed (obj);

	realm_kerberos_set_details (kerberos,
	                            REALM_DBUS_OPTION_SERVER_SOFTWARE, REALM_DBUS_IDENTIFIER_ACTIVE_DIRECTORY,
	                            REALM_DBUS_OPTION_CLIENT_SOFTWARE, REALM_DBUS_IDENTIFIER_SSSD,
	                            NULL);

	/*
	 * Each line is a combination of owner and what kind of credentials are supported,
	 * same for enroll/unenroll. We can't accept a ccache, because samba3 needs
	 * to have credentials limited to RC4.
	 */
	supported = realm_kerberos_build_supported_credentials (
			REALM_KERBEROS_CREDENTIAL_PASSWORD, REALM_KERBEROS_CREDENTIAL_ADMIN,
			REALM_KERBEROS_CREDENTIAL_PASSWORD, REALM_KERBEROS_CREDENTIAL_USER,
			0);
	g_variant_ref_sink (supported);
	realm_kerberos_set_supported_join_creds (kerberos, supported);
	realm_kerberos_set_supported_leave_creds (kerberos, supported);
	g_variant_unref (supported);

	realm_kerberos_set_suggested_admin (kerberos, "Administrator");
	realm_kerberos_set_login_policy (kerberos, REALM_KERBEROS_ALLOW_ANY_LOGIN);
}

typedef struct {
	GDBusMethodInvocation *invocation;
	GBytes *ccache;
	gchar *computer_ou;
	gchar *realm_name;
} EnrollClosure;

static void
enroll_closure_free (gpointer data)
{
	EnrollClosure *enroll = data;
	g_free (enroll->realm_name);
	g_object_unref (enroll->invocation);
	g_bytes_unref (enroll->ccache);
	g_free (enroll->computer_ou);
	g_slice_free (EnrollClosure, enroll);
}

static void
on_enable_nss_done (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             _("Enabling SSSD in nsswitch.conf and PAM failed."));
	if (error != NULL)
		g_simple_async_result_take_error (res, error);

	g_simple_async_result_complete (res);
	g_object_unref (res);
}

static void
on_sssd_enable_nss (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	EnrollClosure *enroll = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;

	realm_service_enable_and_restart_finish (result, &error);

	if (error == NULL) {
		realm_command_run_known_async ("sssd-enable-logins", NULL, enroll->invocation,
		                               NULL, on_enable_nss_done, g_object_ref (res));

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

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
		             _("Failed to calculate domain workgroup"));
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
	                                    "enumerate", "False",
	                                    "re_expression", "(?P<domain>[^\\\\]+)\\\\(?P<name>[^\\\\]+)",
	                                    "full_name_format", "%2$s\\%1$s",
	                                    "case_sensitive", "False",
	                                    "cache_credentials", "False",
	                                    "use_fully_qualified_names", "True",

	                                    "id_provider", "ad",
	                                    "auth_provider", "ad",
	                                    "access_provider", "simple",
	                                    "chpass_provider", "ad",

	                                    "ad_domain", domain,
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
	RealmSssd *sssd = REALM_SSSD (g_async_result_get_source_object (user_data));
	GHashTable *settings = NULL;
	GError *error = NULL;

	realm_samba_enroll_join_finish (result, &settings, &error);
	if (error == NULL) {
		configure_sssd_for_domain (realm_sssd_get_config (sssd),
		                           enroll->realm_name,
		                           settings, &error);
		g_hash_table_unref (settings);
	}

	if (error == NULL) {
		realm_service_enable_and_restart ("sssd", enroll->invocation,
		                                  on_sssd_enable_nss, g_object_ref (res));

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	if (settings)
		g_hash_table_unref (settings);
	g_object_unref (sssd);
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
		realm_samba_enroll_join_async (enroll->realm_name, enroll->ccache,
		                               enroll->computer_ou, enroll->invocation,
		                               on_join_do_sssd, g_object_ref (res));

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
on_kinit_do_install (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	EnrollClosure *enroll = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;

	enroll->ccache = realm_kerberos_kinit_ccache_finish (REALM_KERBEROS (source), result, &error);
	if (error == NULL) {
		realm_packages_install_async ("sssd-ad-packages", enroll->invocation,
		                              on_install_do_join, g_object_ref (res));

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
realm_sssd_ad_enroll_async (RealmKerberos *realm,
                            const char *name,
                            const char *password,
                            RealmKerberosFlags flags,
                            GVariant *options,
                            GDBusMethodInvocation *invocation,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	RealmSssd *sssd = REALM_SSSD (realm);
	GSimpleAsyncResult *res;
	EnrollClosure *enroll;

	res = g_simple_async_result_new (G_OBJECT (realm), callback, user_data,
	                                 realm_sssd_ad_enroll_async);
	enroll = g_slice_new0 (EnrollClosure);
	enroll->realm_name = g_strdup (realm_kerberos_get_realm_name (realm));
	enroll->invocation = g_object_ref (invocation);
	enroll->computer_ou = realm_kerberos_calculate_join_computer_ou (realm, options);
	g_simple_async_result_set_op_res_gpointer (res, enroll, enroll_closure_free);

	/* Make sure not already enrolled in a realm */
	if (realm_sssd_get_config_section (sssd) != NULL) {
		g_simple_async_result_set_error (res, REALM_ERROR, REALM_ERROR_ALREADY_CONFIGURED,
		                                 _("Already joined to this domain"));
		g_simple_async_result_complete_in_idle (res);

	} else if (realm_sssd_config_have_domain (realm_sssd_get_config (sssd), enroll->realm_name)) {
		g_simple_async_result_set_error (res, REALM_ERROR, REALM_ERROR_ALREADY_CONFIGURED,
		                                 _("A domain with this name is already configured"));
		g_simple_async_result_complete_in_idle (res);

	/* Already have discovery info, so go straight to install */
	} else {
		realm_kerberos_kinit_ccache_async (realm, name, password, REALM_SAMBA_ENROLL_ENC_TYPES,
		                                   invocation, on_kinit_do_install, g_object_ref (res));
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

	realm_service_restart_finish (result, &error);
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
	RealmSssd *sssd = REALM_SSSD (g_async_result_get_source_object (user_data));
	GError *error = NULL;
	RealmIniConfig *config;
	gchar **domains;

	realm_samba_enroll_leave_finish (result, NULL);

	/* We don't care if we can leave or not, just continue with other steps */
	config = realm_sssd_get_config (sssd);
	realm_sssd_config_remove_domain (config, realm_sssd_get_config_domain (sssd), &error);

	if (error != NULL) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
		g_error_free (error);

	} else {
		/* If no domains, then disable sssd */
		domains = realm_sssd_config_get_domains (config);
		if (domains == NULL || g_strv_length (domains) == 0) {
			realm_service_disable_and_stop ("sssd", unenroll->invocation,
			                                on_service_disable_done, g_object_ref (res));

		/* If any domains left, then restart sssd */
		} else {
			realm_service_restart ("sssd", unenroll->invocation,
			                       on_service_restart_done, g_object_ref (res));
		}
		g_strfreev (domains);
	}

	g_object_unref (sssd);
	g_object_unref (res);
}

static void
on_kinit_do_leave (GObject *source,
                   GAsyncResult *result,
                   gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	UnenrollClosure *unenroll = g_simple_async_result_get_op_res_gpointer (async);
	RealmSssd *self = REALM_SSSD (source);
	GError *error = NULL;
	GBytes *ccache;

	ccache = realm_kerberos_kinit_ccache_finish (REALM_KERBEROS (self), result, &error);
	if (error == NULL) {
		realm_samba_enroll_leave_async (unenroll->realm_name, ccache, unenroll->invocation,
		                                on_leave_do_sssd, g_object_ref (async));
		g_bytes_unref (ccache);

	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (async);
}

static void
realm_sssd_ad_unenroll_async (RealmKerberos *realm,
                              const gchar *name,
                              const gchar *password,
                              RealmKerberosFlags flags,
                              GVariant *options,
                              GDBusMethodInvocation *invocation,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	RealmSssd *sssd = REALM_SSSD (realm);
	GSimpleAsyncResult *res;
	UnenrollClosure *unenroll;
	const gchar *computer_ou;

	res = g_simple_async_result_new (G_OBJECT (realm), callback, user_data,
	                                 realm_sssd_ad_unenroll_async);
	unenroll = g_slice_new0 (UnenrollClosure);
	unenroll->realm_name = g_strdup (realm_kerberos_get_realm_name (realm));
	unenroll->invocation = g_object_ref (invocation);
	g_simple_async_result_set_op_res_gpointer (res, unenroll, unenroll_closure_free);

	/* Check that enrolled in this realm */
	if (!realm_sssd_get_config_section (sssd)) {
		g_simple_async_result_set_error (res, REALM_ERROR, REALM_ERROR_NOT_CONFIGURED,
		                                 _("Not currently joined to this domain"));
		g_simple_async_result_complete_in_idle (res);

	} else if (g_variant_lookup (options, REALM_DBUS_OPTION_COMPUTER_OU, "&s", &computer_ou)) {
		g_simple_async_result_set_error (res, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		                                 "The computer-ou argument is not supported when leaving a domain (using samba).");
		g_simple_async_result_complete_in_idle (res);

	} else {
		realm_kerberos_kinit_ccache_async (realm, name, password, REALM_SAMBA_ENROLL_ENC_TYPES,
		                                   invocation, on_kinit_do_leave, g_object_ref (res));
	}

	g_object_unref (res);
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

void
realm_sssd_ad_class_init (RealmSssdAdClass *klass)
{
	RealmKerberosClass *kerberos_class = REALM_KERBEROS_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = realm_sssd_ad_constructed;

	kerberos_class->enroll_password_async = realm_sssd_ad_enroll_async;
	kerberos_class->enroll_finish = realm_sssd_ad_generic_finish;
	kerberos_class->unenroll_password_async = realm_sssd_ad_unenroll_async;
	kerberos_class->unenroll_finish = realm_sssd_ad_generic_finish;
}
