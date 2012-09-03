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

#include "realm-adcli-enroll.h"
#include "realm-command.h"
#include "realm-dbus-constants.h"
#include "realm-diagnostics.h"
#include "realm-errors.h"
#include "realm-kerberos-membership.h"
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

static const gchar *adcli_packages[] = {
	REALM_DBUS_IDENTIFIER_SSSD,
	REALM_DBUS_IDENTIFIER_ADCLI,
	NULL
};

static const gchar *samba_packages[] = {
	REALM_DBUS_IDENTIFIER_SSSD,
	REALM_DBUS_IDENTIFIER_SAMBA,
	NULL
};

static void realm_sssd_ad_kerberos_membership_iface (RealmKerberosMembershipIface *iface);

G_DEFINE_TYPE_WITH_CODE (RealmSssdAd, realm_sssd_ad, REALM_TYPE_SSSD,
                         G_IMPLEMENT_INTERFACE (REALM_TYPE_KERBEROS_MEMBERSHIP, realm_sssd_ad_kerberos_membership_iface);
);

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
	supported = realm_kerberos_membership_build_supported (
			REALM_KERBEROS_CREDENTIAL_PASSWORD, REALM_KERBEROS_OWNER_ADMIN,
			REALM_KERBEROS_CREDENTIAL_PASSWORD, REALM_KERBEROS_OWNER_USER,
			REALM_KERBEROS_CREDENTIAL_AUTOMATIC, REALM_KERBEROS_OWNER_NONE,
			REALM_KERBEROS_CREDENTIAL_SECRET, REALM_KERBEROS_OWNER_NONE,
			0);
	realm_kerberos_set_supported_join_creds (kerberos, supported);

	/* For leave, we don't support one-time-password (ie: secret/none) */
	supported = realm_kerberos_membership_build_supported (
			REALM_KERBEROS_CREDENTIAL_PASSWORD, REALM_KERBEROS_OWNER_ADMIN,
			REALM_KERBEROS_CREDENTIAL_PASSWORD, REALM_KERBEROS_OWNER_USER,
			0);
	realm_kerberos_set_supported_leave_creds (kerberos, supported);

	realm_kerberos_set_suggested_admin (kerberos, "Administrator");
	realm_kerberos_set_login_policy (kerberos, REALM_KERBEROS_ALLOW_ANY_LOGIN);
}

typedef struct {
	GDBusMethodInvocation *invocation;
	RealmKerberosCredential cred_type;
	gchar *ccache_file;
	gchar *computer_ou;
	gchar *realm_name;
	gboolean use_adcli;
	GBytes *one_time_password;
	const gchar **packages;
} JoinClosure;

static void
join_closure_free (gpointer data)
{
	JoinClosure *join = data;
	g_free (join->realm_name);
	g_object_unref (join->invocation);
	if (join->ccache_file)
		realm_keberos_ccache_delete_and_free (join->ccache_file);
	g_free (join->computer_ou);
	g_bytes_unref (join->one_time_password);
	g_slice_free (JoinClosure, join);
}

static void
on_enable_nss_done (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             _("Enabling SSSD in nsswitch.conf and PAM failed."));
	if (error != NULL)
		g_simple_async_result_take_error (async, error);

	g_simple_async_result_complete (async);
	g_object_unref (async);
}

static void
on_sssd_enable_nss (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (async);
	GError *error = NULL;

	realm_service_enable_and_restart_finish (result, &error);

	if (error == NULL) {
		realm_command_run_known_async ("sssd-enable-logins", NULL, join->invocation,
		                               NULL, on_enable_nss_done, g_object_ref (async));

	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (async);
}

static gboolean
configure_sssd_for_domain (RealmIniConfig *config,
                           const gchar *realm,
                           const gchar *workgroup,
                           GError **error)
{
	gboolean ret;
	gchar *domain;
	gchar **parts;
	gchar *rdn;
	gchar *dn;
	gint i;

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
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (async);
	RealmSssd *sssd = REALM_SSSD (g_async_result_get_source_object (user_data));
	GHashTable *settings = NULL;
	GError *error = NULL;
	gchar *workgroup = NULL;


	if (join->use_adcli) {
		if (!realm_adcli_enroll_join_finish (result, &workgroup, &error))
			workgroup = NULL;
	} else {
		if (realm_samba_enroll_join_finish (result, &settings, &error)) {
			workgroup = g_strdup (g_hash_table_lookup (settings, "workgroup"));
			g_hash_table_unref (settings);
		}
	}

	if (error == NULL && workgroup == NULL) {
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             _("Failed to calculate domain workgroup"));
	}

	if (error == NULL) {
		configure_sssd_for_domain (realm_sssd_get_config (sssd),
		                           join->realm_name, workgroup, &error);
	}

	if (error == NULL) {
		realm_service_enable_and_restart ("sssd", join->invocation,
		                                  on_sssd_enable_nss, g_object_ref (async));

	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_free (workgroup);
	g_object_unref (sssd);
	g_object_unref (async);
}

static void
on_install_do_join (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (async);
	GError *error = NULL;

	realm_packages_install_finish (result, &error);
	if (error == NULL) {
		if (join->use_adcli && join->one_time_password) {
			realm_adcli_enroll_join_otp_async (join->realm_name,
			                                   join->one_time_password,
			                                   join->computer_ou,
			                                   join->invocation,
			                                   on_join_do_sssd,
			                                   g_object_ref (async));

		} else if (join->use_adcli && join->ccache_file) {
			realm_adcli_enroll_join_ccache_async (join->realm_name,
			                                      join->ccache_file,
			                                      join->computer_ou,
			                                      join->invocation,
			                                      on_join_do_sssd,
			                                      g_object_ref (async));

		} else if (join->use_adcli) {
			realm_adcli_enroll_join_automatic_async (join->realm_name,
			                                         join->computer_ou,
			                                         join->invocation,
			                                         on_join_do_sssd,
			                                         g_object_ref (async));

		} else {
			g_assert (join->ccache_file != NULL);
			realm_samba_enroll_join_async (join->realm_name, join->ccache_file,
			                               join->computer_ou, join->invocation,
			                               on_join_do_sssd, g_object_ref (async));
		}

	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (async);
}

static void
on_kinit_do_install (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (async);
	GError *error = NULL;

	join->ccache_file = realm_kerberos_kinit_ccache_finish (REALM_KERBEROS (source), result, &error);
	if (error == NULL) {
		realm_packages_install_async (join->packages, join->invocation,
		                              on_install_do_join, g_object_ref (async));

	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (async);
}

static gboolean
parse_join_options (JoinClosure *join,
                    RealmKerberosCredential cred_type,
                    RealmKerberosFlags owner,
                    GVariant *options,
                    GError **error)
{
	const gchar *software;

	/* Figure out the method that we're going to use to enroll */
	if (g_variant_lookup (options, REALM_DBUS_OPTION_MEMBERSHIP_SOFTWARE, "&s", &software)) {
		if (!g_str_equal (software, REALM_DBUS_IDENTIFIER_ADCLI) &&
		    !g_str_equal (software, REALM_DBUS_IDENTIFIER_SAMBA)) {
			g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
			             _("Unsupported or unknown membership software '%s'"), software);
			return FALSE;
		}
	} else {
		software = NULL;
	}

	/*
	 * If we are enrolling with a one time password, or automatically, use
	 * adcli. Samba doesn't support computer passwords or using reset accounts.
	 */
	if ((cred_type == REALM_KERBEROS_CREDENTIAL_SECRET && owner == REALM_KERBEROS_OWNER_NONE) ||
	    (cred_type == REALM_KERBEROS_CREDENTIAL_AUTOMATIC && owner == REALM_KERBEROS_OWNER_NONE)) {
		if (!software)
			software = REALM_DBUS_IDENTIFIER_ADCLI;
		if (!g_str_equal (software, REALM_DBUS_IDENTIFIER_ADCLI)) {
			g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
			             _("Joining a domain with a one time password is only supported with the '%s' membership software"),
			             REALM_DBUS_IDENTIFIER_ADCLI);
			return FALSE;
		}

	/*
	 * If we are enrolling with a user password, then we have to use samba,
	 * adcli only supports admin passwords.
	 */
	} else if (cred_type == REALM_KERBEROS_CREDENTIAL_PASSWORD && owner == REALM_KERBEROS_OWNER_USER) {
		if (!software)
			software = REALM_DBUS_IDENTIFIER_SAMBA;
		if (!g_str_equal (software, REALM_DBUS_IDENTIFIER_SAMBA)) {
			g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
			             _("Joining a domain with a user password is only supported with the '%s' membership software"),
			             REALM_DBUS_IDENTIFIER_SAMBA);
			return FALSE;
		}

	/*
	 * For other supported enrolling credentials, we support either adcli or
	 * samba. But since adcli is pretty immature at this point, we use samba
	 * by default.
	 */
	} else if (cred_type == REALM_KERBEROS_CREDENTIAL_PASSWORD && owner == REALM_KERBEROS_OWNER_ADMIN) {
		if (!software)
			software = REALM_DBUS_IDENTIFIER_SAMBA;

	/* It would be odd to get here */
	} else {
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		             _("Unsupported credentials for joining a domain"));
		return FALSE;
	}

	g_assert (software != NULL);

	if (g_str_equal (software, REALM_DBUS_IDENTIFIER_ADCLI)) {
		join->use_adcli = TRUE;
		join->packages = adcli_packages;
	} else {
		join->use_adcli = FALSE;
		join->packages = samba_packages;
	}

	return TRUE;
}

static GSimpleAsyncResult *
prepare_join_async_result (RealmKerberosMembership *membership,
                           RealmKerberosCredential cred_type,
                           RealmKerberosFlags flags,
                           GVariant *options,
                           GDBusMethodInvocation *invocation,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	RealmKerberos *realm = REALM_KERBEROS (membership);
	RealmSssd *sssd = REALM_SSSD (realm);
	GSimpleAsyncResult *async;
	JoinClosure *join;
	GError *error = NULL;

	async = g_simple_async_result_new (G_OBJECT (realm), callback, user_data, NULL);
	join = g_slice_new0 (JoinClosure);
	join->realm_name = g_strdup (realm_kerberos_get_realm_name (realm));
	join->invocation = g_object_ref (invocation);
	join->computer_ou = realm_kerberos_calculate_join_computer_ou (realm, options);
	join->cred_type = cred_type;
	g_simple_async_result_set_op_res_gpointer (async, join, join_closure_free);

	/* Make sure not already enrolled in a realm */
	if (realm_sssd_get_config_section (sssd) != NULL) {
		g_simple_async_result_set_error (async, REALM_ERROR, REALM_ERROR_ALREADY_CONFIGURED,
		                                 _("Already joined to this domain"));
		g_simple_async_result_complete_in_idle (async);

	} else if (realm_sssd_config_have_domain (realm_sssd_get_config (sssd), join->realm_name)) {
		g_simple_async_result_set_error (async, REALM_ERROR, REALM_ERROR_ALREADY_CONFIGURED,
		                                 _("A domain with this name is already configured"));
		g_simple_async_result_complete_in_idle (async);

	} else if (!parse_join_options (join, cred_type, flags & REALM_KERBEROS_OWNER_MASK, options, &error)) {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete_in_idle (async);

	/* Prepared successfully without an error */
	} else {
		return async;
	}

	/* Had an error */
	g_object_unref (async);
	return NULL;
}

static void
realm_sssd_ad_join_automatic_async (RealmKerberosMembership *membership,
                                    RealmKerberosFlags flags,
                                    GVariant *options,
                                    GDBusMethodInvocation *invocation,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
	GSimpleAsyncResult *async;
	JoinClosure *join;

	async = prepare_join_async_result (membership, REALM_KERBEROS_CREDENTIAL_AUTOMATIC,
	                                   flags, options, invocation, callback, user_data);

	if (async) {
		join = g_simple_async_result_get_op_res_gpointer (async);
		realm_packages_install_async (join->packages, join->invocation,
		                              on_install_do_join, g_object_ref (async));
		g_object_unref (async);
	}
}

static void
realm_sssd_ad_join_secret_async (RealmKerberosMembership *membership,
                                 GBytes *secret,
                                 RealmKerberosFlags flags,
                                 GVariant *options,
                                 GDBusMethodInvocation *invocation,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
	GSimpleAsyncResult *async;
	JoinClosure *join;

	async = prepare_join_async_result (membership, REALM_KERBEROS_CREDENTIAL_SECRET,
	                                   flags, options, invocation, callback, user_data);

	if (async) {
		join = g_simple_async_result_get_op_res_gpointer (async);
		join->one_time_password = g_bytes_ref (secret);
		realm_packages_install_async (join->packages, join->invocation,
		                              on_install_do_join, g_object_ref (async));
		g_object_unref (async);
	}
}

static void
realm_sssd_ad_join_password_async (RealmKerberosMembership *membership,
                                   const char *name,
                                   const char *password,
                                   RealmKerberosFlags flags,
                                   GVariant *options,
                                   GDBusMethodInvocation *invocation,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
	const krb5_enctype *enctypes;
	GSimpleAsyncResult *async;
	JoinClosure *join;

	async = prepare_join_async_result (membership, REALM_KERBEROS_CREDENTIAL_PASSWORD,
	                                   flags, options, invocation, callback, user_data);

	if (async) {
		join = g_simple_async_result_get_op_res_gpointer (async);

		/* If using samba, then only for a subset of enctypes */
		if (join->use_adcli)
			enctypes = NULL;
		else
			enctypes = REALM_SAMBA_ENROLL_ENC_TYPES;

		realm_kerberos_kinit_ccache_async (REALM_KERBEROS (membership),
		                                   name, password, enctypes,
		                                   invocation, on_kinit_do_install,
		                                   g_object_ref (async));

		g_object_unref (async);
	}
}

typedef struct {
	GDBusMethodInvocation *invocation;
	gchar *realm_name;
	gchar *ccache_file;
} UnenrollClosure;

static void
unenroll_closure_free (gpointer data)
{
	UnenrollClosure *unenroll = data;
	g_free (unenroll->realm_name);
	if (unenroll->ccache_file)
		realm_keberos_ccache_delete_and_free (unenroll->ccache_file);
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

	unenroll->ccache_file = realm_kerberos_kinit_ccache_finish (REALM_KERBEROS (self), result, &error);
	if (error == NULL) {
		realm_samba_enroll_leave_async (unenroll->realm_name, unenroll->ccache_file,
		                                unenroll->invocation, on_leave_do_sssd,
		                                g_object_ref (async));

	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (async);
}

static void
realm_sssd_ad_unenroll_async (RealmKerberosMembership *membership,
                              const gchar *name,
                              const gchar *password,
                              RealmKerberosFlags flags,
                              GVariant *options,
                              GDBusMethodInvocation *invocation,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	RealmKerberos *realm = REALM_KERBEROS (membership);
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
realm_sssd_ad_generic_finish (RealmKerberosMembership *realm,
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
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = realm_sssd_ad_constructed;
}

static void
realm_sssd_ad_kerberos_membership_iface (RealmKerberosMembershipIface *iface)
{
	iface->enroll_automatic_async = realm_sssd_ad_join_automatic_async;
	iface->enroll_password_async = realm_sssd_ad_join_password_async;
	iface->enroll_secret_async = realm_sssd_ad_join_secret_async;
	iface->enroll_finish = realm_sssd_ad_generic_finish;
	iface->unenroll_password_async = realm_sssd_ad_unenroll_async;
	iface->unenroll_finish = realm_sssd_ad_generic_finish;
}
