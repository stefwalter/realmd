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
	RealmDisco *disco;
	gboolean use_adcli;
	const gchar **packages;
} JoinClosure;

static void
join_closure_free (gpointer data)
{
	JoinClosure *join = data;
	realm_disco_unref (join->disco);
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
	EggTask *task = EGG_TASK (user_data);
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             _("Enabling SSSD in nsswitch.conf and PAM failed."));
	if (error != NULL)
		egg_task_return_error (task, error);
	else
		egg_task_return_boolean (task, TRUE);
	g_object_unref (task);
}

static void
on_sssd_enable_nss (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	EggTask *task = EGG_TASK (user_data);
	JoinClosure *join = egg_task_get_task_data (task);
	GError *error = NULL;

	realm_service_enable_and_restart_finish (result, &error);

	if (error == NULL) {
		realm_command_run_known_async ("sssd-enable-logins", NULL, join->invocation,
		                               on_enable_nss_done, g_object_ref (task));

	} else {
		egg_task_return_error (task, error);
	}

	g_object_unref (task);
}

static gboolean
configure_sssd_for_domain (RealmIniConfig *config,
                           RealmDisco *disco,
                           GVariant *options,
                           GError **error)
{
	const gchar *access_provider;
	gboolean qualify;
	gboolean ret;
	gchar *section;
	gchar *home;

	home = realm_sssd_build_default_home (realm_settings_string ("users", "default-home"));
	qualify = realm_options_qualify_names (disco->domain_name);

	ret = realm_sssd_config_add_domain (config, disco->domain_name, error,
	                                    "cache_credentials", "True",
		                            "use_fully_qualified_names", qualify ? "True" : "False",

	                                    "id_provider", "ad",

	                                    "ad_domain", disco->domain_name,
	                                    "krb5_realm", disco->kerberos_realm,
	                                    "krb5_store_password_if_offline", "True",
	                                    "ldap_id_mapping", realm_options_automatic_mapping (disco->domain_name) ? "True" : "False",

	                                    "fallback_homedir", home,
	                                    disco->explicit_server ? "ad_server" : NULL, disco->explicit_server,
	                                    NULL);

	if (ret) {
		if (realm_options_manage_system (options, disco->domain_name))
			access_provider = "ad";
		else
			access_provider = "simple";
		section = realm_sssd_config_domain_to_section (disco->domain_name);
		ret = realm_sssd_set_login_policy (config, section, access_provider, NULL, NULL, FALSE, error);
		free (section);
	}

	g_free (home);

	return ret;
}

static void
on_join_do_sssd (GObject *source,
                 GAsyncResult *result,
                 gpointer user_data)
{
	EggTask *task = EGG_TASK (user_data);
	JoinClosure *join = egg_task_get_task_data (task);
	RealmSssd *sssd = egg_task_get_source_object (task);
	GError *error = NULL;

	if (join->use_adcli) {
		if (!realm_adcli_enroll_join_finish (result, &error)) {
			if (join->cred->type == REALM_CREDENTIAL_AUTOMATIC &&
			    g_error_matches (error, REALM_ERROR, REALM_ERROR_AUTH_FAILED)) {
				g_clear_error (&error);
				g_set_error (&error, REALM_ERROR, REALM_ERROR_AUTH_FAILED,
				             _("Unable to automatically join the domain"));
			}
		}
	} else {
		realm_samba_enroll_join_finish (result, &error);
	}

	if (error == NULL) {
		configure_sssd_for_domain (realm_sssd_get_config (sssd), join->disco,
		                           join->options, &error);
	}

	if (error == NULL) {
		realm_service_enable_and_restart ("sssd", join->invocation,
		                                  on_sssd_enable_nss, g_object_ref (task));

	} else {
		egg_task_return_error (task, error);
	}

	g_object_unref (task);
}

static void
on_install_do_join (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	EggTask *task = EGG_TASK (user_data);
	JoinClosure *join = egg_task_get_task_data (task);
	GError *error = NULL;

	realm_packages_install_finish (result, &error);
	if (error == NULL) {
		if (join->use_adcli) {
			realm_adcli_enroll_join_async (join->disco,
			                               join->cred,
			                               join->options,
			                               join->invocation,
			                               on_join_do_sssd,
			                               g_object_ref (task));
		} else {
			realm_samba_enroll_join_async (join->disco,
			                               join->cred,
			                               join->options,
			                               join->invocation, on_join_do_sssd,
			                               g_object_ref (task));
		}

	} else {
		egg_task_return_error (task, error);
	}

	g_object_unref (task);
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
			g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
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
	 * by default. Samba falls over with hostnames that are not perfectly
	 * specified, so use adcli there.
	 */
	} else if (cred->type == REALM_CREDENTIAL_PASSWORD && cred->owner == REALM_CREDENTIAL_OWNER_ADMIN) {
		if (!software && join->disco->explicit_server)
			software = REALM_DBUS_IDENTIFIER_ADCLI;
		else if (!software)
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
	EggTask *task;
	JoinClosure *join;
	GError *error = NULL;

	task = egg_task_new (realm, NULL, callback, user_data);
	join = g_slice_new0 (JoinClosure);
	join->disco = realm_disco_ref (realm_kerberos_get_disco (realm));
	join->invocation = g_object_ref (invocation);
	join->options = g_variant_ref (options);
	join->cred = realm_credential_ref (cred);
	egg_task_set_task_data (task, join, join_closure_free);

	/* Make sure not already enrolled in a realm */
	if (realm_sssd_get_config_section (sssd) != NULL) {
		egg_task_return_new_error (task, REALM_ERROR, REALM_ERROR_ALREADY_CONFIGURED,
		                           _("Already joined to this domain"));

	} else if (realm_sssd_config_have_domain (realm_sssd_get_config (sssd), realm_kerberos_get_realm_name (realm))) {
		egg_task_return_new_error (task, REALM_ERROR, REALM_ERROR_ALREADY_CONFIGURED,
		                           _("A domain with this name is already configured"));

	} else if (!parse_join_options (join, cred, options, &error)) {
		egg_task_return_error (task, error);

	/* Prepared successfully without an error */
	} else {
		realm_packages_install_async (join->packages, join->invocation, options,
		                              on_install_do_join, g_object_ref (task));
	}

	g_object_unref (task);
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
	EggTask *task = EGG_TASK (user_data);
	LeaveClosure *leave = egg_task_get_task_data (task);
	RealmSssd *sssd = egg_task_get_source_object (task);
	GError *error = NULL;

	/* We don't care if we can leave or not, just continue with other steps */
	realm_samba_enroll_leave_finish (result, &error);
	if (error != NULL) {
		realm_diagnostics_error (leave->invocation, error, NULL);
		g_error_free (error);
	}

	realm_sssd_deconfigure_domain_tail (sssd, task, leave->invocation);

	g_object_unref (task);
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
	RealmKerberos *realm = REALM_KERBEROS (self);
	EggTask *task;
	LeaveClosure *leave;

	task = egg_task_new (self, NULL, callback, user_data);

	/* Check that enrolled in this realm */
	if (!realm_sssd_get_config_section (REALM_SSSD (self))) {
		egg_task_return_new_error (task, REALM_ERROR, REALM_ERROR_NOT_CONFIGURED,
		                           _("Not currently joined to this domain"));
		g_object_unref (task);
		return;
	}

	switch (cred->type) {
	case REALM_CREDENTIAL_AUTOMATIC:
		realm_sssd_deconfigure_domain_tail (REALM_SSSD (self), task, invocation);
		break;
	case REALM_CREDENTIAL_CCACHE:
	case REALM_CREDENTIAL_PASSWORD:
		leave = g_slice_new0 (LeaveClosure);
		leave->realm_name = g_strdup (realm_kerberos_get_realm_name (realm));
		leave->invocation = g_object_ref (invocation);
		egg_task_set_task_data (task, leave, leave_closure_free);
		realm_samba_enroll_leave_async (realm_kerberos_get_disco (realm), cred, options, invocation,
		                                on_leave_do_deconfigure, g_object_ref (task));
		break;
	default:
		g_return_if_reached ();
	}

	g_object_unref (task);
}

static gboolean
realm_sssd_ad_generic_finish (RealmKerberosMembership *realm,
                              GAsyncResult *result,
                              GError **error)
{
	return egg_task_propagate_boolean (EGG_TASK (result), error);
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
