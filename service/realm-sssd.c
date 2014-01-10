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
#include "realm-packages.h"
#include "realm-provider.h"
#include "realm-service.h"
#include "realm-sssd.h"
#include "realm-sssd-config.h"
#include "safe-format-string.h"

#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include <string.h>

struct _RealmSssdPrivate {
	gchar *domain;
	gchar *section;
	RealmIniConfig *config;
	gulong config_sig;
};

enum {
	PROP_0,
	PROP_PROVIDER,
};

G_DEFINE_TYPE (RealmSssd, realm_sssd, REALM_TYPE_KERBEROS);

static void
realm_sssd_init (RealmSssd *self)
{
	self->pv = G_TYPE_INSTANCE_GET_PRIVATE (self, REALM_TYPE_SSSD, RealmSssdPrivate);
}

static void
on_logins_restarted (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GTask *task = G_TASK (user_data);
	RealmSssd *self = g_task_get_source_object (task);
	GError *error = NULL;

	realm_service_restart_finish (result, &error);
	if (error != NULL) {
		g_task_return_error (task, error);
	} else {
		realm_sssd_update_properties (self);
		g_task_return_boolean (task, TRUE);
	}
	g_object_unref (task);
}

gboolean
realm_sssd_set_login_policy (RealmIniConfig *config,
                             const gchar *section,
                             const gchar *access_provider,
                             const gchar **add_names,
                             const gchar **remove_names,
                             gboolean names_are_groups,
                             GError **error)
{
	const gchar *field = names_are_groups ? "simple_allow_groups" : "simple_allow_users";
	gchar *allow = NULL;

	if (!realm_ini_config_begin_change (config, error))
		return FALSE;

	if (access_provider)
		realm_ini_config_set (config, section, "access_provider", access_provider, NULL);

	if (!access_provider || g_str_equal (access_provider, "simple")) {
		realm_ini_config_set_list_diff (config, section, field, ",", add_names, remove_names);

		/*
		 * HACK: Work around for sssd problem where it allows users if
		 * simple_allow_users is empty. Set it to a dollar in this case.
		 */
		allow = realm_ini_config_get (config, section, field);
		if (allow != NULL) {
			g_strstrip (allow);
			if (g_str_equal (allow, "") || g_str_equal (allow, "$") || g_str_equal (allow, ",")) {
				g_free (allow);
				allow = NULL;
			}
		}

		if (allow == NULL)
			realm_ini_config_set (config, section, field, "$", NULL);
	} else {
		realm_ini_config_set (config, section, "simple_allow_users", NULL, NULL);
		realm_ini_config_set (config, section, "simple_allow_groups", NULL, NULL);
	}

	g_free (allow);

	return realm_ini_config_finish_change (config, error);
}

static gboolean
sssd_config_check_login_list (const gchar **logins,
                              GError **error)
{
	#define INVALID_CHARS ",$"
	gint i;

	for (i = 0; logins != NULL && logins[i] != NULL; i++) {
		if (strcspn (logins[i], INVALID_CHARS) != strlen (logins[i])) {
			g_set_error (error, G_DBUS_ERROR,
			             G_DBUS_ERROR_INVALID_ARGS,
			             _("Invalid login argument '%s' contains unsupported characters."),
			             logins[i]);
			return FALSE;
		}
	}

	return TRUE;
}

static void
realm_sssd_logins_async (RealmKerberos *realm,
                         GDBusMethodInvocation *invocation,
                         RealmKerberosLoginPolicy login_policy,
                         const gchar **add,
                         const gchar **remove,
                         GVariant *options,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	RealmSssdClass *sssd_class = REALM_SSSD_GET_CLASS (realm);
	RealmSssd *self = REALM_SSSD (realm);
	gboolean names_are_groups = FALSE;
	GTask *task;
	gchar **remove_names = NULL;
	gchar **add_names = NULL;
	GError *error = NULL;
	const gchar *access_provider;

	task = g_task_new (realm, NULL, callback, user_data);

	if (!self->pv->section) {
		g_task_return_new_error (task, REALM_ERROR, REALM_ERROR_NOT_CONFIGURED,
		                         "Not joined to this domain");
		g_object_unref (task);
		return;
	}

	switch (login_policy) {
	case REALM_KERBEROS_POLICY_NOT_SET:
		access_provider = NULL;
		break;
	case REALM_KERBEROS_ALLOW_ANY_LOGIN:
		access_provider = "permit";
		break;
	case REALM_KERBEROS_ALLOW_REALM_LOGINS:
		access_provider = sssd_class->sssd_conf_provider_name;
		break;
	case REALM_KERBEROS_ALLOW_PERMITTED_LOGINS:
		access_provider = "simple";
		break;
	case REALM_KERBEROS_DENY_ANY_LOGIN:
		access_provider = "deny";
		break;
	default:
		g_return_if_reached ();
	}

	if (!g_variant_lookup (options, "groups", "b", &names_are_groups))
		names_are_groups = FALSE;

	if (!names_are_groups) {
		add_names = realm_kerberos_parse_logins (realm, TRUE, add, &error);
		if (add_names != NULL)
			remove_names = realm_kerberos_parse_logins (realm, TRUE, remove, &error);
		add = (const gchar **)add_names;
		remove = (const gchar **)remove_names;
	}

	if (error == NULL)
		sssd_config_check_login_list (add, &error);
	if (error == NULL)
		sssd_config_check_login_list (remove, &error);

	if (error == NULL) {
		realm_sssd_set_login_policy (self->pv->config,
		                             self->pv->section,
		                             access_provider,
		                             add, remove,
		                             names_are_groups,
		                             &error);
	}

	if (error == NULL) {
		realm_service_restart ("sssd", invocation,
		                       on_logins_restarted,
		                       g_object_ref (task));
	} else {
		g_task_return_error (task, error);
	}

	g_strfreev (remove_names);
	g_strfreev (add_names);

	g_object_unref (task);
}

static gboolean
realm_sssd_generic_finish (RealmKerberos *realm,
                           GAsyncResult *result,
                           GError **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
update_configured (RealmSssd *self)
{
	gboolean manages_system;
	gchar *value;

	realm_kerberos_set_configured (REALM_KERBEROS (self),
	                               self->pv->section ? TRUE : FALSE);

	manages_system = FALSE;
	if (self->pv->section) {
		value = realm_ini_config_get (self->pv->config, self->pv->section, "realmd_tags");
		if (value && strstr (value, "manages-system"))
			manages_system = TRUE;
		g_free (value);
	}

	realm_kerberos_set_manages_system (REALM_KERBEROS (self), manages_system);
}

static gchar *
calc_realm_name (RealmSssd *self)
{
	RealmKerberos *kerberos = REALM_KERBEROS (self);
	const char *name;
	RealmDisco *disco;
	gchar *realm = NULL;

	if (self->pv->section == NULL) {
		disco = realm_kerberos_get_disco (kerberos);
		if (disco != NULL)
			realm = g_strdup (disco->kerberos_realm);
	} else {
		realm = realm_ini_config_get (self->pv->config, self->pv->section, "krb5_realm");
	}

	if (realm == NULL) {
		name = realm_kerberos_get_name (kerberos);
		realm = name ? g_ascii_strup (name, -1) : NULL;
	}

	return realm;
}

static void
update_realm_name (RealmSssd *self)
{
	gchar *realm = calc_realm_name (self);
	realm_kerberos_set_realm_name (REALM_KERBEROS (self), realm);
	g_free (realm);
}

static gchar *
calc_domain (RealmSssd *self)
{
	RealmKerberos *kerberos = REALM_KERBEROS (self);
	const char *name;
	RealmDisco *disco;
	gchar *domain = NULL;

	if (self->pv->section == NULL) {
		disco = realm_kerberos_get_disco (kerberos);
		if (disco != NULL)
			domain = g_strdup (disco->domain_name);
	} else {
		domain = realm_ini_config_get (self->pv->config, self->pv->section, "dns_discovery_domain");
	}

	if (domain == NULL) {
		name = realm_kerberos_get_name (kerberos);
		domain = name ? g_ascii_strdown (name, -1) : NULL;
	}

	return domain;
}

static void
update_domain (RealmSssd *self)
{
	gchar *domain = calc_domain (self);
	realm_kerberos_set_domain_name (REALM_KERBEROS (self), domain);
	g_free (domain);
}

static void
format_string_piece (void *data,
                     const char *piece,
                     size_t len)
{
	g_string_append_len (data, piece, len);
}

static void
update_login_formats (RealmSssd *self)
{
	RealmKerberos *kerberos = REALM_KERBEROS (self);
	gchar *login_formats[2] = { NULL, NULL };
	const gchar *args[3];
	GString *formatted;
	gchar *format = NULL;
	gchar *domain_name;
	gboolean qualify;

	if (self->pv->section == NULL) {
		realm_kerberos_set_login_formats (kerberos, (const gchar **)login_formats);
		return;
	}

	qualify = realm_ini_config_get_boolean (self->pv->config, self->pv->section,
	                                        "use_fully_qualified_names", FALSE);

	if (!qualify) {
		login_formats[0] = "%U";
		realm_kerberos_set_login_formats (kerberos, (const gchar **)login_formats);
		return;
	}

	/* Setup the login formats */
	format = realm_ini_config_get (self->pv->config, self->pv->section, "full_name_format");
	if (format == NULL)
		format = realm_ini_config_get (self->pv->config, "sssd", "full_name_format");
	if (format == NULL)
		format = g_strdup ("%1$s@%2$s");

	/* The full domain name */
	domain_name = calc_domain (self);

	/*
	 * In theory we should be discovering the short name or flat name as sssd
	 * calls it. We configured it as the sssd.conf 'domains' name, so we just
	 * use that. Eventually we want to have a way to query sssd for that.
	 */

	/*
	 * Here we place a '%U' in the place of the user in the format, and
	 * fill in the domain appropriately. sssd uses snprintf for this, which
	 * is risky and very compex to do right with positional arguments.
	 *
	 * We only replace the arguments documented in sssd.conf, as well as
	 * other non-field printf replacements.
	 */

	formatted = g_string_new ("");
	args[0] = "%U";
	args[1] = domain_name ? domain_name : "";
	args[2] = self->pv->domain;

	if (safe_format_string_cb (format_string_piece, formatted, format, args, 3) >= 0) {
		login_formats[0] = formatted->str;
		realm_kerberos_set_login_formats (kerberos, (const gchar **)login_formats);
	}
	g_string_free (formatted, TRUE);
	g_free (domain_name);
	g_free (format);
}

#pragma GCC diagnostic pop

static void
update_login_policy (RealmSssd *self)
{
	RealmSssdClass *sssd_class = REALM_SSSD_GET_CLASS (self);
	RealmKerberosLoginPolicy policy = REALM_KERBEROS_POLICY_NOT_SET;
	RealmKerberos *kerberos = REALM_KERBEROS (self);
	GPtrArray *permitted_logins;
	GPtrArray *permitted_groups;
	gchar *access = NULL;
	gchar **values;
	gint i;

	permitted_logins = g_ptr_array_new_full (0, g_free);
	permitted_groups = g_ptr_array_new_full (0, g_free);
	if (self->pv->section != NULL)
		access = realm_ini_config_get (self->pv->config, self->pv->section, "access_provider");
	if (g_strcmp0 (access, "simple") == 0) {
		values = realm_ini_config_get_list (self->pv->config, self->pv->section,
		                                    "simple_allow_users", ",");
		for (i = 0; values != NULL && values[i] != NULL; i++) {
			if (!g_str_equal (values[i], "") && !g_str_equal (values[i], "$"))
				g_ptr_array_add (permitted_logins, realm_kerberos_format_login (kerberos, values[i]));
		}
		g_strfreev (values);
		values = realm_ini_config_get_list (self->pv->config, self->pv->section,
		                                    "simple_allow_groups", ",");
		for (i = 0; values != NULL && values[i] != NULL; i++) {
			if (!g_str_equal (values[i], "") && !g_str_equal (values[i], "$"))
				g_ptr_array_add (permitted_groups, g_strdup (values[i]));
		}
		g_strfreev (values);
		policy = REALM_KERBEROS_ALLOW_PERMITTED_LOGINS;
	} else if (g_strcmp0 (access, sssd_class->sssd_conf_provider_name) == 0) {
		policy = REALM_KERBEROS_ALLOW_REALM_LOGINS;
	} else if (g_strcmp0 (access, "permit") == 0) {
		policy = REALM_KERBEROS_ALLOW_ANY_LOGIN;
	} else if (g_strcmp0 (access, "deny") == 0) {
		policy = REALM_KERBEROS_DENY_ANY_LOGIN;
	} else {
		policy = REALM_KERBEROS_POLICY_NOT_SET;
	}

	g_ptr_array_add (permitted_logins, NULL);
	g_ptr_array_add (permitted_groups, NULL);

	realm_kerberos_set_login_policy (kerberos, policy);
	realm_kerberos_set_permitted_logins (kerberos, (const gchar **)permitted_logins->pdata);
	realm_kerberos_set_permitted_groups (kerberos, (const gchar **)permitted_groups->pdata);

	g_ptr_array_free (permitted_logins, TRUE);
	g_ptr_array_free (permitted_groups, TRUE);
	g_free (access);
}

void
realm_sssd_update_properties (RealmSssd *self)
{
	GObject *obj = G_OBJECT (self);
	const gchar *my_name;
	gchar *name = NULL;
	gchar *section = NULL;
	gchar **domains;
	gint i;

	g_object_freeze_notify (obj);

	g_free (self->pv->section);
	self->pv->section = NULL;

	g_free (self->pv->domain);
	self->pv->domain = NULL;

	/* Find the config domain with our realm */
	domains = realm_sssd_config_get_domains (self->pv->config);
	my_name = realm_kerberos_get_name (REALM_KERBEROS (self));
	for (i = 0; self->pv->section == NULL && domains && domains[i]; i++) {
		if (realm_sssd_config_load_domain (self->pv->config, domains[i], &section, NULL, &name)) {
			if (my_name && name && g_ascii_strcasecmp (my_name, name) == 0) {
				self->pv->domain = g_strdup (domains[i]);
				self->pv->section = section;
				section = NULL;
			}

			g_free (section);
			g_free (name);
		}
	}
	g_strfreev (domains);

	/* Update all the other properties */
	update_configured (self);
	update_realm_name (self);
	update_domain (self);
	update_login_formats (self);
	update_login_policy (self);

	g_object_thaw_notify (obj);
}

static void
on_config_changed (RealmIniConfig *config,
                   gpointer user_data)
{
	realm_sssd_update_properties (REALM_SSSD (user_data));
}

static void
realm_sssd_set_property (GObject *obj,
                         guint prop_id,
                         const GValue *value,
                         GParamSpec *pspec)
{
	RealmSssd *self = REALM_SSSD (obj);
	RealmProvider *provider;

	switch (prop_id) {
	case PROP_PROVIDER:
		provider = g_value_get_object (value);
		g_object_get (provider, "sssd-config", &self->pv->config, NULL);
		self->pv->config_sig = g_signal_connect (self->pv->config, "changed",
		                                         G_CALLBACK (on_config_changed),
		                                         self);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
realm_sssd_notify (GObject *obj,
                   GParamSpec *spec)
{
	if (g_str_equal (spec->name, "name"))
		realm_sssd_update_properties (REALM_SSSD (obj));

	if (G_OBJECT_CLASS (realm_sssd_parent_class)->notify)
		G_OBJECT_CLASS (realm_sssd_parent_class)->notify (obj, spec);
}

static void
realm_sssd_finalize (GObject *obj)
{
	RealmSssd *self = REALM_SSSD (obj);

	g_free (self->pv->section);
	if (self->pv->config)
		g_object_unref (self->pv->config);

	G_OBJECT_CLASS (realm_sssd_parent_class)->finalize (obj);
}

void
realm_sssd_class_init (RealmSssdClass *klass)
{
	RealmKerberosClass *kerberos_class = REALM_KERBEROS_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	kerberos_class->logins_async = realm_sssd_logins_async;
	kerberos_class->logins_finish = realm_sssd_generic_finish;

	object_class->set_property = realm_sssd_set_property;
	object_class->notify = realm_sssd_notify;
	object_class->finalize = realm_sssd_finalize;

	g_object_class_override_property (object_class, PROP_PROVIDER, "provider");

	g_type_class_add_private (klass, sizeof (RealmSssdPrivate));
}

RealmIniConfig *
realm_sssd_get_config (RealmSssd *self)
{
	g_return_val_if_fail (REALM_IS_SSSD (self), NULL);
	return self->pv->config;
}

const gchar *
realm_sssd_get_config_section (RealmSssd *self)
{
	g_return_val_if_fail (REALM_IS_SSSD (self), NULL);
	return self->pv->section;
}

const gchar *
realm_sssd_get_config_domain (RealmSssd *self)
{
	g_return_val_if_fail (REALM_IS_SSSD (self), NULL);
	return self->pv->domain;
}

gchar *
realm_sssd_build_default_home (const gchar *value)
{
	gchar *home;
	char *pos;

	/* Change from our format to the sssd format place-holders */
	home = g_strdup (value);
	pos = strstr (home, "%U");
	if (pos)
		pos[1] = 'u';
	pos = strstr (home, "%D");
	if (pos)
		pos[1] = 'd';

	return home;
}

typedef struct {
	GTask *task;
	GDBusMethodInvocation *invocation;
	RealmIniConfig *config;
	gchar *domain;
} DeconfClosure;

static void
deconfigure_closure_free (gpointer data)
{
	DeconfClosure *deconf = data;
	g_object_unref (deconf->task);
	g_object_unref (deconf->invocation);
	g_object_unref (deconf->config);
	g_free (deconf->domain);
	g_free (deconf);
}

static void
on_service_disable_done (GObject *source,
                         GAsyncResult *result,
                         gpointer user_data)
{
	DeconfClosure *deconf = user_data;
	GError *error = NULL;

	realm_service_disable_and_stop_finish (result, &error);
	if (error != NULL) {
		realm_diagnostics_error (deconf->invocation, error, NULL);
		g_error_free (error);
	}

	g_task_return_boolean (deconf->task, TRUE);
	deconfigure_closure_free (deconf);
}

static void
on_service_restart_done (GObject *source,
                         GAsyncResult *result,
                         gpointer user_data)
{
	DeconfClosure *deconf = user_data;
	GError *error = NULL;

	realm_service_restart_finish (result, &error);
	if (error != NULL) {
		realm_diagnostics_error (deconf->invocation, error, NULL);
		g_error_free (error);
	}

	g_task_return_boolean (deconf->task, TRUE);
	deconfigure_closure_free (deconf);
}

static void
on_disable_nss_service (GObject *source,
                        GAsyncResult *result,
                        gpointer user_data)
{
	DeconfClosure *deconf = user_data;
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (error == NULL && status != 0) {
		realm_diagnostics_error (deconf->invocation, error,
		                         "Disabling sssd in PAM failed.");
		g_clear_error (&error);
	}

	realm_service_disable_and_stop ("sssd", deconf->invocation,
	                                on_service_disable_done, deconf);
}

static void
on_sssd_clear_cache (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	DeconfClosure *deconf = user_data;
	GError *error = NULL;
	gchar **domains;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (status != 0) {
		realm_diagnostics_error (deconf->invocation, error,
		                         "Flushing the sssd cache failed");
		g_clear_error (&error);
	}

	/* Deconfigure sssd.conf, may have already been done, if so NULL */
	if (deconf->domain) {
		realm_diagnostics_info (deconf->invocation, "Removing domain configuration from sssd.conf");
		if (!realm_sssd_config_remove_domain (deconf->config, deconf->domain, &error)) {
			g_task_return_error (deconf->task, error);
			deconfigure_closure_free (deconf);
			return;
		}
	}

	/* If no domains, then disable sssd */
	domains = realm_sssd_config_get_domains (deconf->config);
	if (domains == NULL || g_strv_length (domains) == 0) {
		realm_command_run_known_async ("sssd-disable-logins", NULL, deconf->invocation,
		                               on_disable_nss_service, deconf);

	/* If any domains left, then restart sssd */
	} else {
		realm_service_restart ("sssd", deconf->invocation,
		                       on_service_restart_done, deconf);
	}

	g_strfreev (domains);
}

void
realm_sssd_deconfigure_domain_tail (RealmSssd *self,
                                    GTask *task,
                                    GDBusMethodInvocation *invocation)
{
	DeconfClosure *deconf;
	GError *error = NULL;
	const gchar *realm_name;

	realm_name = realm_kerberos_get_realm_name (REALM_KERBEROS (self));

	/* Flush the keytab of all the entries for this realm */
	realm_diagnostics_info (invocation, "Removing entries from keytab for realm");
	if (!realm_kerberos_flush_keytab (realm_name, &error)) {
		g_task_return_error (task, error);
		return;
	}

	deconf = g_new0 (DeconfClosure, 1);
	deconf->task = g_object_ref (task);
	deconf->invocation = g_object_ref (invocation);
	deconf->config = g_object_ref (self->pv->config);
	deconf->domain = g_strdup (self->pv->domain);

	/*
	 * TODO: We would really like to do this after removing the domain, to prevent races
	 * but we can't because otherwise sss_cache doesn't clear that domain :S
	 */

	realm_command_run_known_async ("sssd-caches-flush", NULL, deconf->invocation,
	                               on_sssd_clear_cache, deconf);
}
