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
#include "realm-ipa-discover.h"
#include "realm-kerberos.h"
#include "realm-kerberos-membership.h"
#include "realm-packages.h"
#include "realm-provider.h"
#include "realm-service.h"
#include "realm-settings.h"
#include "realm-sssd.h"
#include "realm-sssd-ipa.h"
#include "realm-sssd-config.h"

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <errno.h>
#include <string.h>

struct _RealmSssdIpa {
	RealmSssd parent;
};

typedef struct {
	RealmSssdClass parent_class;
} RealmSssdIpaClass;

static const gchar *NO_PACKAGES[] = {
	NULL,
};

static const gchar *IPA_PACKAGES[] = {
	REALM_DBUS_IDENTIFIER_FREEIPA,
	REALM_DBUS_IDENTIFIER_SSSD,
	NULL
};

static void realm_sssd_ipa_kerberos_membership_iface (RealmKerberosMembershipIface *iface);

G_DEFINE_TYPE_WITH_CODE (RealmSssdIpa, realm_sssd_ipa, REALM_TYPE_SSSD,
                         G_IMPLEMENT_INTERFACE (REALM_TYPE_KERBEROS_MEMBERSHIP, realm_sssd_ipa_kerberos_membership_iface);
);

static void
realm_sssd_ipa_init (RealmSssdIpa *self)
{

}

static void
realm_sssd_ipa_constructed (GObject *obj)
{
	RealmKerberos *kerberos = REALM_KERBEROS (obj);
	GVariant *supported;

	G_OBJECT_CLASS (realm_sssd_ipa_parent_class)->constructed (obj);

	realm_kerberos_set_details (kerberos,
	                            REALM_DBUS_OPTION_SERVER_SOFTWARE, REALM_DBUS_IDENTIFIER_FREEIPA,
	                            REALM_DBUS_OPTION_CLIENT_SOFTWARE, REALM_DBUS_IDENTIFIER_SSSD,
	                            NULL);

	/*
	 * NOTE: The ipa-client-install service requires that we pass a password directly
	 * to the process, and not a ccache. It also accepts a one time password.
	 */
	supported = realm_kerberos_membership_build_supported (
			REALM_KERBEROS_CREDENTIAL_PASSWORD, REALM_KERBEROS_OWNER_ADMIN,
			REALM_KERBEROS_CREDENTIAL_SECRET, REALM_KERBEROS_OWNER_NONE,
			0);

	realm_kerberos_set_supported_join_creds (kerberos, supported);

	supported = realm_kerberos_membership_build_supported (
			REALM_KERBEROS_CREDENTIAL_PASSWORD, REALM_KERBEROS_OWNER_ADMIN,
			REALM_KERBEROS_CREDENTIAL_AUTOMATIC, REALM_KERBEROS_OWNER_NONE,
			0);

	realm_kerberos_set_supported_leave_creds (kerberos, supported);

	realm_kerberos_set_suggested_admin (kerberos, "admin");
	realm_kerberos_set_required_package_sets (kerberos, IPA_PACKAGES);
}

void
realm_sssd_ipa_class_init (RealmSssdIpaClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = realm_sssd_ipa_constructed;
}

typedef struct {
	GDBusMethodInvocation *invocation;
	gchar **argv;
	GBytes *input;
} EnrollClosure;

static void
enroll_closure_free (gpointer data)
{
	EnrollClosure *enroll = data;
	g_object_unref (enroll->invocation);
	g_strfreev (enroll->argv);
	g_bytes_unref (enroll->input);
	g_slice_free (EnrollClosure, enroll);
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
on_restart_done (GObject *source,
                 GAsyncResult *result,
                 gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	EnrollClosure *enroll = g_simple_async_result_get_op_res_gpointer (async);
	RealmSssd *sssd = REALM_SSSD (g_async_result_get_source_object (user_data));
	GError *error = NULL;

	realm_service_enable_and_restart_finish (result, &error);
	if (error == NULL) {
		realm_sssd_update_properties (sssd);
		realm_command_run_known_async ("sssd-enable-logins", NULL, enroll->invocation,
		                               on_enable_nss_done, g_object_ref (async));
	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (sssd);
	g_object_unref (async);
}

static void
on_ipa_client_do_restart (GObject *source,
                          GAsyncResult *result,
                          gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	EnrollClosure *enroll = g_simple_async_result_get_op_res_gpointer (async);
	RealmSssd *sssd = REALM_SSSD (g_async_result_get_source_object (user_data));
	RealmKerberos *realm = REALM_KERBEROS (sssd);
	GError *error = NULL;
	GString *output = NULL;
	gchar *home;
	gint status;

	status = realm_command_run_finish (result, &output, &error);

	if (error == NULL && status != 0) {

		/*
		 * TODO: We need to update ipa-client-install to accept a
		 * ccache so we can get better feedback on invalid passwords.
		 * We run the process with LC_ALL=C so at least we know these
		 * messages will be in english.
		 */
		if (g_pattern_match_simple ("*kinit: Password incorrect*", output->str)) {
			g_set_error (&error, REALM_ERROR, REALM_ERROR_AUTH_FAILED,
			             "Password is incorrect");
		} else {
			g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
			             "Running ipa-client-install failed");
		}
	}

	if (error == NULL) {
		home = realm_sssd_build_default_home (realm_settings_string ("users", "default-home"));

		realm_sssd_config_update_domain (realm_sssd_get_config (sssd),
		                                 realm_kerberos_get_name (realm), &error,
		                                 "re_expression", "(?P<name>[^@]+)@(?P<domain>.+$)",
		                                 "full_name_format", "%1$s@%2$s",
		                                 "cache_credentials", "True",
		                                 "use_fully_qualified_names", "True",
		                                 "simple_allow_users", ",",
		                                 "krb5_store_password_if_offline", "True",
		                                 "fallback_homedir", home,
		                                 NULL);

		g_free (home);
	}

	if (error == NULL) {
		realm_service_enable_and_restart ("sssd", enroll->invocation,
		                                  on_restart_done, g_object_ref (async));

	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (sssd);
	g_object_unref (async);
}

static void
on_install_do_join (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	EnrollClosure *enroll = g_simple_async_result_get_op_res_gpointer (async);
	GError *error = NULL;

	const gchar *env[] = {
		"LANG=C",
		NULL,
	};

	realm_packages_install_finish (result, &error);
	if (error == NULL) {
		realm_command_runv_async (enroll->argv, (gchar **)env,
		                          enroll->input, enroll->invocation,
		                          on_ipa_client_do_restart, g_object_ref (async));
	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (async);
}

static void
join_ipa_async (RealmKerberosMembership *membership,
                const gchar **argv,
                GBytes *input,
                RealmKerberosFlags flags,
                GVariant *options,
                GDBusMethodInvocation *invocation,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
	RealmKerberos *realm = REALM_KERBEROS (membership);
	RealmSssd *sssd = REALM_SSSD (realm);
	GSimpleAsyncResult *async;
	EnrollClosure *enroll;
	const gchar *domain_name;
	const gchar *computer_ou;
	const gchar *software;
	const gchar **packages;

	domain_name = realm_kerberos_get_name (realm);

	async = g_simple_async_result_new (G_OBJECT (realm), callback, user_data, NULL);
	enroll = g_slice_new0 (EnrollClosure);
	enroll->input = input ? g_bytes_ref (input) : NULL;
	enroll->argv = g_strdupv ((gchar **)argv);
	enroll->invocation = g_object_ref (invocation);
	g_simple_async_result_set_op_res_gpointer (async, enroll, enroll_closure_free);

	if (g_variant_lookup (options, REALM_DBUS_OPTION_COMPUTER_OU, "&s", &computer_ou)) {
		g_simple_async_result_set_error (async, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		                                 _("The computer-ou argument is not supported when joining an IPA domain."));
		g_simple_async_result_complete_in_idle (async);

	} else if (g_variant_lookup (options, REALM_DBUS_OPTION_MEMBERSHIP_SOFTWARE, "&s", &software) &&
	           !g_str_equal (software, REALM_DBUS_IDENTIFIER_FREEIPA)) {
		g_simple_async_result_set_error (async, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		                                 _("Unsupported or unknown membership software '%s'"), software);
		g_simple_async_result_complete_in_idle (async);

	} else if (realm_sssd_get_config_section (sssd) != NULL) {
		g_simple_async_result_set_error (async, REALM_ERROR, REALM_ERROR_ALREADY_CONFIGURED,
		                                 _("Already joined to this domain"));
		g_simple_async_result_complete_in_idle (async);

	} else if (realm_sssd_config_have_domain (realm_sssd_get_config (sssd), domain_name)) {
		g_simple_async_result_set_error (async, REALM_ERROR, REALM_ERROR_ALREADY_CONFIGURED,
		                                 _("A domain with this name is already configured"));
		g_simple_async_result_complete_in_idle (async);

	} else {
		packages = IPA_PACKAGES;
		if (flags & REALM_KERBEROS_ASSUME_PACKAGES)
			packages = NO_PACKAGES;
		realm_packages_install_async (packages, invocation,
		                              on_install_do_join, g_object_ref (async));
	}

	g_object_unref (async);
}

static void
realm_sssd_ipa_enroll_password_async (RealmKerberosMembership *membership,
                                      const gchar *name,
                                      GBytes *password,
                                      RealmKerberosFlags flags,
                                      GVariant *options,
                                      GDBusMethodInvocation *invocation,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
	RealmKerberos *realm = REALM_KERBEROS (membership);
	GBytes *input;

	const gchar *argv[] = {
		realm_settings_string ("paths", "ipa-client-install"),
		"--domain", realm_kerberos_get_name (realm),
		"--realm", realm_kerberos_get_realm_name (realm),
		"--principal", name,
		"-W",
		"--mkhomedir",
		"--no-ntp",
		"--enable-dns-updates",
		"--unattended",
		NULL,
	};

	input = realm_command_build_password_line (password);

	join_ipa_async (membership, argv, input, flags, options,
	                invocation, callback, user_data);

	g_bytes_unref (input);
}

static const char *
secret_to_password (GBytes *secret,
                    gchar **password)
{
	gconstpointer data;
	gsize length;

	/*
	 * In theory the password could be binary with embedded nulls.
	 * We don't support that. And we assume that we don't need to
	 * check for that here, because such a password will be wrong,
	 * and ipa-client-install will simply fail to join the domain.
	 */

	data = g_bytes_get_data (secret, &length);
	*password = g_strndup (data, length);
	return *password;
}

static void
realm_sssd_ipa_enroll_secret_async (RealmKerberosMembership *membership,
                                    GBytes *secret,
                                    RealmKerberosFlags flags,
                                    GVariant *options,
                                    GDBusMethodInvocation *invocation,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
	RealmKerberos *realm = REALM_KERBEROS (membership);
	char *password;

	const gchar *argv[] = {
		realm_settings_string ("paths", "ipa-client-install"),
		"--domain", realm_kerberos_get_name (realm),
		"--realm", realm_kerberos_get_realm_name (realm),
		"--password", secret_to_password (secret, &password),
		"--mkhomedir",
		"--no-ntp",
		"--enable-dns-updates",
		"--unattended",
		NULL,
	};

	/*
	 * TODO: Allow passing the password other than command line.
	 *
	 * ipa-client-install won't let us pass a password into a prompt
	 * when used with --unattended. We need --unattended since we can't
	 * handle arbitrary prompts. So pass the one time password on
	 * the command line. It's just a one time password, so in the short
	 * term this should be okay.
	 */

	join_ipa_async (membership, argv, NULL, flags, options,
	                invocation, callback, user_data);

	g_free (password);
}

static void
on_ipa_client_do_disable (GObject *source,
                          GAsyncResult *result,
                          gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	EnrollClosure *enroll = g_simple_async_result_get_op_res_gpointer (async);
	RealmSssd *sssd = REALM_SSSD (g_async_result_get_source_object (user_data));
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);

	if (error == NULL && status != 0) {
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Running ipa-client-install failed");
	}

	if (error == NULL) {
		realm_sssd_deconfigure_domain_tail (sssd, async, enroll->invocation);

	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (sssd);
	g_object_unref (async);
}

static void
leave_ipa_async (RealmKerberosMembership *membership,
                 const gchar **argv,
                 GBytes *input,
                 RealmKerberosFlags flags,
                 GVariant *options,
                 GDBusMethodInvocation *invocation,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
	RealmKerberos *realm = REALM_KERBEROS (membership);
	RealmSssd *sssd = REALM_SSSD (realm);
	GSimpleAsyncResult *async;
	EnrollClosure *enroll;
	const gchar *computer_ou;

	const gchar *env[] = {
		"LANG=C",
		NULL,
	};

	async = g_simple_async_result_new (G_OBJECT (realm), callback, user_data, NULL);
	enroll = g_slice_new0 (EnrollClosure);
	enroll->invocation = g_object_ref (invocation);
	g_simple_async_result_set_op_res_gpointer (async, enroll, enroll_closure_free);

	if (realm_sssd_get_config_section (sssd) == NULL) {
		g_simple_async_result_set_error (async, REALM_ERROR, REALM_ERROR_NOT_CONFIGURED,
		                                 _("Not currently joined to this realm"));
		g_simple_async_result_complete_in_idle (async);

	} else if (g_variant_lookup (options, REALM_DBUS_OPTION_COMPUTER_OU, "&s", &computer_ou)) {
		g_simple_async_result_set_error (async, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		                                 "The computer-ou argument is not supported when leaving an IPA domain.");
		g_simple_async_result_complete_in_idle (async);

	} else {
		realm_command_runv_async ((gchar **)argv, (gchar **)env, NULL, invocation,
		                          on_ipa_client_do_disable, g_object_ref (async));
	}

	g_object_unref (async);
}

static void
realm_sssd_ipa_leave_password_async (RealmKerberosMembership *membership,
                                     const char *name,
                                     GBytes *password,
                                     RealmKerberosFlags flags,
                                     GVariant *options,
                                     GDBusMethodInvocation *invocation,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
	GBytes *input;

	const gchar *argv[] = {
		realm_settings_string ("paths", "ipa-client-install"),
		"--uninstall",
		"--principal", name,
		"-W",
		"--unattended",
		NULL
	};

	input = realm_command_build_password_line (password);
	leave_ipa_async (membership, argv, input, flags, options,
	                 invocation, callback, user_data);
	g_bytes_unref (input);
}

static void
realm_sssd_ipa_leave_automatic_async (RealmKerberosMembership *membership,
                                      RealmKerberosFlags flags,
                                      GVariant *options,
                                      GDBusMethodInvocation *invocation,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
	const gchar *argv[] = {
		realm_settings_string ("paths", "ipa-client-install"),
		"--uninstall",
		"--unattended",
		NULL
	};

	leave_ipa_async (membership, argv, NULL, flags, options,
	                 invocation, callback, user_data);
}

static gboolean
realm_sssd_ipa_generic_finish (RealmKerberosMembership *realm,
                               GAsyncResult *result,
                               GError **error)
{
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

static void
realm_sssd_ipa_kerberos_membership_iface (RealmKerberosMembershipIface *iface)
{
	iface->enroll_password_async = realm_sssd_ipa_enroll_password_async;
	iface->enroll_secret_async = realm_sssd_ipa_enroll_secret_async;
	iface->enroll_finish = realm_sssd_ipa_generic_finish;
	iface->unenroll_password_async = realm_sssd_ipa_leave_password_async;
	iface->unenroll_automatic_async = realm_sssd_ipa_leave_automatic_async;
	iface->unenroll_finish = realm_sssd_ipa_generic_finish;
}
