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
#include "realm-options.h"
#include "realm-packages.h"
#include "realm-samba-enroll.h"
#include "realm-service.h"
#include "realm-settings.h"
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

static const gchar *ADCLI_PACKAGES[] = {
	REALM_DBUS_IDENTIFIER_SSSD,
	REALM_DBUS_IDENTIFIER_ADCLI,
	NULL
};

static const gchar *SAMBA_PACKAGES[] = {
	REALM_DBUS_IDENTIFIER_SSSD,
	REALM_DBUS_IDENTIFIER_SAMBA,
	NULL
};

static const gchar *ALL_PACKAGES[] = {
	REALM_DBUS_IDENTIFIER_SSSD,
	REALM_DBUS_IDENTIFIER_ADCLI,
	REALM_DBUS_IDENTIFIER_SAMBA,
	NULL
};

static const gchar *NO_PACKAGES[] = {
	NULL,
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

	G_OBJECT_CLASS (realm_sssd_ad_parent_class)->constructed (obj);

	realm_kerberos_set_details (kerberos,
	                            REALM_DBUS_OPTION_SERVER_SOFTWARE, REALM_DBUS_IDENTIFIER_ACTIVE_DIRECTORY,
	                            REALM_DBUS_OPTION_CLIENT_SOFTWARE, REALM_DBUS_IDENTIFIER_SSSD,
	                            NULL);

	realm_kerberos_set_suggested_admin (kerberos, "Administrator");
	realm_kerberos_set_login_policy (kerberos, REALM_KERBEROS_ALLOW_REALM_LOGINS);
	realm_kerberos_set_required_package_sets (kerberos, ALL_PACKAGES);
}

typedef struct {
	GDBusMethodInvocation *invocation;
	RealmCredential *cred;
	GVariant *options;
	gchar *realm_name;
	gboolean use_adcli;
	const gchar **packages;
} JoinClosure;

static void
join_closure_free (gpointer data)
{
	JoinClosure *join = data;
	g_free (join->realm_name);
	g_object_unref (join->invocation);
	realm_credential_unref (join->cred);
	g_variant_ref (join->options);
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
		                               on_enable_nss_done, g_object_ref (async));

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
                           GVariant *options,
                           GError **error)
{
	const gchar *access_provider;
	gboolean ret;
	gchar *domain;
	gchar *section;
	gchar **parts;
	gchar *rdn;
	gchar *dn;
	gchar *home;
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

	home = realm_sssd_build_default_home (realm_settings_string ("users", "default-home"));

	ret = realm_sssd_config_add_domain (config, workgroup, error,
	                                    "re_expression", "(?P<domain>[^\\\\]+)\\\\(?P<name>[^\\\\]+)",
	                                    "full_name_format", "%2$s\\%1$s",
	                                    "cache_credentials", "True",
	                                    "use_fully_qualified_names", "True",

	                                    "id_provider", "ad",

	                                    "ad_domain", domain,
	                                    "krb5_realm", realm,
	                                    "krb5_store_password_if_offline", "True",
	                                    "ldap_id_mapping", realm_options_automatic_mapping (domain) ? "True" : "False",

	                                    "fallback_homedir", home,
	                                    NULL);

	if (ret) {
		if (realm_options_manage_system (options, domain))
			access_provider = "ad";
		else
			access_provider = "simple";
		section = realm_sssd_config_domain_to_section (workgroup);
		ret = realm_sssd_set_login_policy (config, section, access_provider, NULL, NULL, error);
		free (section);
	}

	g_free (home);
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
		if (!realm_adcli_enroll_join_finish (result, &workgroup, &error)) {
			workgroup = NULL;
			if (join->cred->type == REALM_CREDENTIAL_AUTOMATIC &&
			    g_error_matches (error, REALM_ERROR, REALM_ERROR_AUTH_FAILED)) {
				g_clear_error (&error);
				g_set_error (&error, REALM_ERROR, REALM_ERROR_AUTH_FAILED,
				             _("Unable to automatically join the domain"));
			}
		}
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
		                           join->realm_name, workgroup,
		                           join->options, &error);
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
	RealmKerberos *kerberos = REALM_KERBEROS (g_async_result_get_source_object (user_data));
	GError *error = NULL;

	realm_packages_install_finish (result, &error);
	if (error == NULL) {
		if (join->use_adcli) {
			realm_adcli_enroll_join_async (join->realm_name,
			                               join->cred,
			                               join->options,
			                               join->invocation,
			                               on_join_do_sssd,
			                               g_object_ref (async));
		} else {
			realm_samba_enroll_join_async (join->realm_name,
			                               join->cred,
			                               join->options,
			                               realm_kerberos_get_discovery (kerberos),
			                               join->invocation, on_join_do_sssd,
			                               g_object_ref (async));
		}

	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (kerberos);
	g_object_unref (async);
}

static gboolean
parse_join_options (JoinClosure *join,
                    RealmCredential *cred,
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
	if ((cred->type == REALM_CREDENTIAL_SECRET && cred->owner == REALM_CREDENTIAL_OWNER_NONE) ||
	    (cred->type == REALM_CREDENTIAL_AUTOMATIC && cred->owner == REALM_CREDENTIAL_OWNER_NONE)) {
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
	} else if (cred->type == REALM_CREDENTIAL_PASSWORD && cred->owner == REALM_CREDENTIAL_OWNER_USER) {
		if (!software)
			software = REALM_DBUS_IDENTIFIER_SAMBA;
		if (!g_str_equal (software, REALM_DBUS_IDENTIFIER_SAMBA)) {
			g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
			             _("Joining a domain with a user password is only supported with the '%s' membership software"),
			             REALM_DBUS_IDENTIFIER_SAMBA);
			return FALSE;
		}

	/*
	 * If we are enrolling with a ccache, then prefer to use adcli over samba.
	 * There have been some strange corner case problems when using samba with
	 * a ccache.
	 */
	} else if (cred->type == REALM_CREDENTIAL_CCACHE) {
		if (!software)
			software = REALM_DBUS_IDENTIFIER_ADCLI;

	/*
	 * For other supported enrolling credentials, we support either adcli or
	 * samba. But since adcli is pretty immature at this point, we use samba
	 * by default.
	 */
	} else if (cred->type == REALM_CREDENTIAL_PASSWORD && cred->owner == REALM_CREDENTIAL_OWNER_ADMIN) {
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
		join->packages = ADCLI_PACKAGES;
	} else {
		join->use_adcli = FALSE;
		join->packages = SAMBA_PACKAGES;
	}

	return TRUE;
}

static void
realm_sssd_ad_join_async (RealmKerberosMembership *membership,
                          RealmCredential *cred,
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
	join->options = g_variant_ref (options);
	join->cred = realm_credential_ref (cred);
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

	} else if (!parse_join_options (join, cred, options, &error)) {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete_in_idle (async);

	/* Prepared successfully without an error */
	} else {
		if (realm_options_assume_packages (options))
			join->packages = NO_PACKAGES;
		realm_packages_install_async (join->packages, join->invocation,
		                              on_install_do_join, g_object_ref (async));
	}

	g_object_unref (async);
}

typedef struct {
	GDBusMethodInvocation *invocation;
	gchar *realm_name;
} LeaveClosure;

static void
leave_closure_free (gpointer data)
{
	LeaveClosure *leave = data;
	g_free (leave->realm_name);
	g_object_unref (leave->invocation);
	g_slice_free (LeaveClosure, leave);
}

static void
on_leave_do_deconfigure (GObject *source,
                         GAsyncResult *result,
                         gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	LeaveClosure *leave = g_simple_async_result_get_op_res_gpointer (res);
	RealmSssd *sssd = REALM_SSSD (g_async_result_get_source_object (user_data));
	GError *error = NULL;

	/* We don't care if we can leave or not, just continue with other steps */
	realm_samba_enroll_leave_finish (result, &error);
	if (error != NULL) {
		realm_diagnostics_error (leave->invocation, error, NULL);
		g_error_free (error);
	}

	realm_sssd_deconfigure_domain_tail (sssd, res, leave->invocation);

	g_object_unref (sssd);
	g_object_unref (res);
}

static void
realm_sssd_ad_leave_async (RealmKerberosMembership *membership,
                           RealmCredential *cred,
                           GVariant *options,
                           GDBusMethodInvocation *invocation,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	RealmSssdAd *self = REALM_SSSD_AD (membership);
	GSimpleAsyncResult *async;
	LeaveClosure *leave;

	async = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
	                                   realm_sssd_ad_leave_async);

	/* Check that enrolled in this realm */
	if (!realm_sssd_get_config_section (REALM_SSSD (self))) {
		g_simple_async_result_set_error (async, REALM_ERROR, REALM_ERROR_NOT_CONFIGURED,
		                                 _("Not currently joined to this domain"));
		g_simple_async_result_complete_in_idle (async);
		g_object_unref (async);
		return;
	}

	switch (cred->type) {
	case REALM_CREDENTIAL_AUTOMATIC:
		realm_sssd_deconfigure_domain_tail (REALM_SSSD (self), async, invocation);
		break;
	case REALM_CREDENTIAL_CCACHE:
	case REALM_CREDENTIAL_PASSWORD:
		leave = g_slice_new0 (LeaveClosure);
		leave->realm_name = g_strdup (realm_kerberos_get_realm_name (REALM_KERBEROS (self)));
		leave->invocation = g_object_ref (invocation);
		g_simple_async_result_set_op_res_gpointer (async, leave, leave_closure_free);
		realm_samba_enroll_leave_async (leave->realm_name, cred, options, invocation,
		                                on_leave_do_deconfigure, g_object_ref (async));
		break;
	default:
		g_return_if_reached ();
	}

	g_object_unref (async);
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
	RealmSssdClass *sssd_class = REALM_SSSD_CLASS (klass);

	object_class->constructed = realm_sssd_ad_constructed;

	/* The provider in sssd.conf relevant to this realm type */
	sssd_class->sssd_conf_provider_name = "ad";
}

static void
realm_sssd_ad_kerberos_membership_iface (RealmKerberosMembershipIface *iface)
{
	/*
	 * Each line is a combination of owner and what kind of credentials are supported,
	 * same for enroll/leave. We can't accept a ccache with samba because of certain
	 * corner cases. However we do accept ccache for an admin user, and then we use
	 * adcli with that ccache.
	 */

	static const RealmCredential join_supported[] = {
		{ REALM_CREDENTIAL_PASSWORD, REALM_CREDENTIAL_OWNER_ADMIN, },
		{ REALM_CREDENTIAL_PASSWORD, REALM_CREDENTIAL_OWNER_USER, },
		{ REALM_CREDENTIAL_CCACHE, REALM_CREDENTIAL_OWNER_ADMIN, },
		{ REALM_CREDENTIAL_AUTOMATIC, REALM_CREDENTIAL_OWNER_NONE, },
		{ REALM_CREDENTIAL_SECRET, REALM_CREDENTIAL_OWNER_NONE, },
		{ 0, },
	};

	/* For leave, we don't support one-time-password (ie: secret/none) */
	static const RealmCredential leave_supported[] = {
		{ REALM_CREDENTIAL_PASSWORD, REALM_CREDENTIAL_OWNER_ADMIN, },
		{ REALM_CREDENTIAL_CCACHE, REALM_CREDENTIAL_OWNER_ADMIN, },
		{ REALM_CREDENTIAL_AUTOMATIC, REALM_CREDENTIAL_OWNER_NONE, },
		{ 0, },
	};

	iface->join_async = realm_sssd_ad_join_async;
	iface->join_finish = realm_sssd_ad_generic_finish;
	iface->join_creds_supported = join_supported;

	iface->leave_async = realm_sssd_ad_leave_async;
	iface->leave_finish = realm_sssd_ad_generic_finish;
	iface->leave_creds_supported = leave_supported;
}
