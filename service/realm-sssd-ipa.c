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
#include "realm-errors.h"
#include "realm-kerberos.h"
#include "realm-kerberos-membership.h"
#include "realm-options.h"
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

	G_OBJECT_CLASS (realm_sssd_ipa_parent_class)->constructed (obj);

	realm_kerberos_set_details (kerberos,
	                            REALM_DBUS_OPTION_SERVER_SOFTWARE, REALM_DBUS_IDENTIFIER_FREEIPA,
	                            REALM_DBUS_OPTION_CLIENT_SOFTWARE, REALM_DBUS_IDENTIFIER_SSSD,
	                            NULL);

	realm_kerberos_set_suggested_admin (kerberos, "admin");
	realm_kerberos_set_required_package_sets (kerberos, IPA_PACKAGES);
}

void
realm_sssd_ipa_class_init (RealmSssdIpaClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RealmSssdClass *sssd_class = REALM_SSSD_CLASS (klass);

	object_class->constructed = realm_sssd_ipa_constructed;

	/* The provider in sssd.conf relevant to this realm type */
	sssd_class->sssd_conf_provider_name = "ipa";
}

typedef struct {
	GDBusMethodInvocation *invocation;
	GPtrArray *argv;
	GVariant *options;
	GBytes *input;
} EnrollClosure;

static void
enroll_closure_free (gpointer data)
{
	EnrollClosure *enroll = data;
	g_object_unref (enroll->invocation);
	if (enroll->argv)
		g_ptr_array_unref (enroll->argv);
	g_variant_unref (enroll->options);
	g_bytes_unref (enroll->input);
	g_slice_free (EnrollClosure, enroll);
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
on_restart_done (GObject *source,
                 GAsyncResult *result,
                 gpointer user_data)
{
	EggTask *task = EGG_TASK (user_data);
	EnrollClosure *enroll = egg_task_get_task_data (task);
	RealmSssd *sssd = egg_task_get_source_object (task);
	GError *error = NULL;

	realm_service_enable_and_restart_finish (result, &error);
	if (error == NULL) {
		realm_sssd_update_properties (sssd);
		realm_command_run_known_async ("sssd-enable-logins", NULL, enroll->invocation,
		                               on_enable_nss_done, g_object_ref (task));
	} else {
		egg_task_return_error (task, error);
	}

	g_object_unref (task);
}

static void
on_ipa_client_do_restart (GObject *source,
                          GAsyncResult *result,
                          gpointer user_data)
{
	EggTask *task = EGG_TASK (user_data);
	EnrollClosure *enroll = egg_task_get_task_data (task);
	RealmSssd *sssd = egg_task_get_source_object (task);
	RealmKerberos *realm = REALM_KERBEROS (sssd);
	const gchar *access_provider;
	GError *error = NULL;
	GString *output = NULL;
	RealmIniConfig *config;
	const gchar *domain;
	gchar *section;
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

	domain = realm_kerberos_get_name (realm);
	config = realm_sssd_get_config (sssd);

	if (error == NULL) {
		home = realm_sssd_build_default_home (realm_settings_string ("users", "default-home"));

		realm_sssd_config_update_domain (config, domain, &error,
		                                 "re_expression", "(?P<name>[^@]+)@(?P<domain>.+$)",
		                                 "full_name_format", "%1$s@%2$s",
		                                 "cache_credentials", "True",
		                                 "use_fully_qualified_names", "True",
		                                 "krb5_store_password_if_offline", "True",
		                                 "fallback_homedir", home,
		                                 NULL);

		g_free (home);
	}

	if (error == NULL) {
		if (realm_options_manage_system (enroll->options, domain))
			access_provider = "ipa";
		else
			access_provider = "simple";
		section = realm_sssd_config_domain_to_section (domain);
		realm_sssd_set_login_policy (config, section, access_provider, NULL, NULL, &error);
		free (section);
	}

	if (error == NULL) {
		realm_service_enable_and_restart ("sssd", enroll->invocation,
		                                  on_restart_done, g_object_ref (task));

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
	EnrollClosure *enroll = egg_task_get_task_data (task);
	GError *error = NULL;

	const gchar *env[] = {
		"LANG=C",
		NULL,
	};

	realm_packages_install_finish (result, &error);
	if (error == NULL) {
		realm_command_runv_async ((gchar **)enroll->argv->pdata, (gchar **)env,
		                          enroll->input, enroll->invocation,
		                          on_ipa_client_do_restart, g_object_ref (task));
	} else {
		egg_task_return_error (task, error);
	}

	g_object_unref (task);
}

static char *
secret_to_password (GBytes *secret)
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
	return g_strndup (data, length);
}

static void
push_arg (GPtrArray *argv,
          const gchar *value)
{
	g_ptr_array_add (argv, strdup (value));
}

static void
realm_sssd_ipa_join_async (RealmKerberosMembership *membership,
                           RealmCredential *cred,
                           GVariant *options,
                           GDBusMethodInvocation *invocation,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	RealmKerberos *realm = REALM_KERBEROS (membership);
	RealmSssd *sssd = REALM_SSSD (realm);
	EggTask *task;
	EnrollClosure *enroll;
	RealmDisco *disco;
	const gchar *domain_name;
	const gchar *computer_ou;
	const gchar *software;
	const gchar **packages;
	GPtrArray *argv;

	domain_name = realm_kerberos_get_name (realm);

	task = egg_task_new (realm, NULL, callback, user_data);
	enroll = g_slice_new0 (EnrollClosure);
	enroll->invocation = g_object_ref (invocation);
	enroll->options = g_variant_ref (options);
	egg_task_set_task_data (task, enroll, enroll_closure_free);

	if (g_variant_lookup (options, REALM_DBUS_OPTION_COMPUTER_OU, "&s", &computer_ou)) {
		egg_task_return_new_error (task, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		                           _("The computer-ou argument is not supported when joining an IPA domain."));

	} else if (g_variant_lookup (options, REALM_DBUS_OPTION_MEMBERSHIP_SOFTWARE, "&s", &software) &&
	           !g_str_equal (software, REALM_DBUS_IDENTIFIER_FREEIPA)) {
		egg_task_return_new_error (task, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		                           _("Unsupported or unknown membership software '%s'"), software);

	} else if (realm_sssd_get_config_section (sssd) != NULL) {
		egg_task_return_new_error (task, REALM_ERROR, REALM_ERROR_ALREADY_CONFIGURED,
		                           _("Already joined to this domain"));

	} else if (realm_sssd_config_have_domain (realm_sssd_get_config (sssd), domain_name)) {
		egg_task_return_new_error (task, REALM_ERROR, REALM_ERROR_ALREADY_CONFIGURED,
		                           _("A domain with this name is already configured"));

	} else {
		packages = IPA_PACKAGES;
		if (realm_options_assume_packages (options))
			packages = NO_PACKAGES;

		disco = realm_kerberos_get_disco (realm);
		g_return_if_fail (disco != NULL);

		argv = g_ptr_array_new ();
		push_arg (argv, realm_settings_string ("paths", "ipa-client-install"));
		push_arg (argv, "--domain");
		push_arg (argv, disco->domain_name);
		push_arg (argv, "--realm");
		push_arg (argv, disco->kerberos_realm);
		push_arg (argv, "--mkhomedir");
		push_arg (argv, "--enable-dns-updates");
		push_arg (argv, "--unattended");

		/* If the caller specified a server directly */
		if (disco->explicit_server) {
			push_arg (argv, "--server");
			push_arg (argv, disco->explicit_server);
			push_arg (argv, "--fixed-primary");
		}

		switch (cred->type) {
		case REALM_CREDENTIAL_SECRET:
			/*
			 * TODO: Allow passing the password other than command line.
			 *
			 * ipa-client-install won't let us pass a password into a prompt
			 * when used with --unattended. We need --unattended since we can't
			 * handle arbitrary prompts. So pass the one time password on
			 * the command line. It's just a one time password, so in the short
			 * term this should be okay.
			 */

			push_arg (argv, "--password");
			g_ptr_array_add (argv, secret_to_password (cred->x.secret.value));
			break;
		case REALM_CREDENTIAL_PASSWORD:
			enroll->input = realm_command_build_password_line (cred->x.password.value);
			push_arg (argv, "--principal");
			push_arg (argv, cred->x.password.name);
			push_arg (argv, "-W");
			break;
		default:
			g_return_if_reached ();
		}

		if (!realm_options_manage_system (options, domain_name)) {
			push_arg (argv, "--no-ssh");
			push_arg (argv, "--no-sshd");
			push_arg (argv, "--no-ntp");
		}

		g_ptr_array_add (argv, NULL);
		enroll->argv = argv;

		realm_packages_install_async (packages, invocation,
		                              on_install_do_join, g_object_ref (task));
	}

	g_object_unref (task);
}

static void
on_ipa_client_do_disable (GObject *source,
                          GAsyncResult *result,
                          gpointer user_data)
{
	EggTask *task = EGG_TASK (user_data);
	EnrollClosure *enroll = egg_task_get_task_data (task);
	RealmSssd *sssd = egg_task_get_source_object (task);
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);

	if (error == NULL && status != 0) {
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Running ipa-client-install failed");
	}

	if (error == NULL)
		realm_sssd_deconfigure_domain_tail (sssd, task, enroll->invocation);
	else
		egg_task_return_error (task, error);
	g_object_unref (task);
}

static void
realm_sssd_ipa_leave_async (RealmKerberosMembership *membership,
                            RealmCredential *cred,
                            GVariant *options,
                            GDBusMethodInvocation *invocation,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	RealmKerberos *realm = REALM_KERBEROS (membership);
	RealmSssd *sssd = REALM_SSSD (realm);
	EggTask *task;
	EnrollClosure *enroll;
	const gchar *computer_ou;
	GBytes *input;
	const gchar **argv;

	const gchar *env[] = {
		"LANG=C",
		NULL,
	};

	const gchar *automatic_args[] = {
		realm_settings_string ("paths", "ipa-client-install"),
		"--uninstall",
		"--unattended",
		NULL
	};

	const gchar *password_args[] = {
		realm_settings_string ("paths", "ipa-client-install"),
		"--uninstall",
		"--principal", cred->x.password.name,
		"-W",
		"--unattended",
		NULL
	};

	task = egg_task_new (realm, NULL, callback, user_data);
	enroll = g_slice_new0 (EnrollClosure);
	enroll->invocation = g_object_ref (invocation);
	enroll->options = g_variant_ref (options);
	egg_task_set_task_data (task, enroll, enroll_closure_free);

	if (realm_sssd_get_config_section (sssd) == NULL) {
		egg_task_return_new_error (task, REALM_ERROR, REALM_ERROR_NOT_CONFIGURED,
		                           _("Not currently joined to this realm"));

	} else if (g_variant_lookup (options, REALM_DBUS_OPTION_COMPUTER_OU, "&s", &computer_ou)) {
		egg_task_return_new_error (task, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		                           "The computer-ou argument is not supported when leaving an IPA domain.");

	} else {
		switch (cred->type) {
		case REALM_CREDENTIAL_AUTOMATIC:
			argv = automatic_args;
			break;
		case REALM_CREDENTIAL_PASSWORD:
			input = realm_command_build_password_line (cred->x.password.value);
			argv = password_args;
			break;
		default:
			g_return_if_reached ();
		}

		realm_command_runv_async ((gchar **)argv, (gchar **)env, input, invocation,
		                          on_ipa_client_do_disable, g_object_ref (task));

		if (input)
			g_bytes_unref (input);
	}

	g_object_unref (task);
}

static gboolean
realm_sssd_ipa_generic_finish (RealmKerberosMembership *realm,
                               GAsyncResult *result,
                               GError **error)
{
	return egg_task_propagate_boolean (EGG_TASK (result), error);
}

static void
realm_sssd_ipa_kerberos_membership_iface (RealmKerberosMembershipIface *iface)
{

	/*
	 * NOTE: The ipa-client-install service requires that we pass a password directly
	 * to the process, and not a ccache. It also accepts a one time password.
	 */
	static const RealmCredential join_supported[] = {
		{ REALM_CREDENTIAL_PASSWORD, REALM_CREDENTIAL_OWNER_ADMIN },
		{ REALM_CREDENTIAL_SECRET, REALM_CREDENTIAL_OWNER_NONE, },
		{ 0, }
	};

	static const RealmCredential leave_supported[] = {
		{ REALM_CREDENTIAL_PASSWORD, REALM_CREDENTIAL_OWNER_ADMIN, },
		{ REALM_CREDENTIAL_AUTOMATIC, REALM_CREDENTIAL_OWNER_NONE, },
		{ 0, }
	};

	iface->join_async = realm_sssd_ipa_join_async;
	iface->join_finish = realm_sssd_ipa_generic_finish;
	iface->join_creds_supported = join_supported;

	iface->leave_async = realm_sssd_ipa_leave_async;
	iface->leave_finish = realm_sssd_ipa_generic_finish;
	iface->leave_creds_supported = leave_supported;
}
