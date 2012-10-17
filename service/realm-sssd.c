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
#include "realm-packages.h"
#include "realm-provider.h"
#include "realm-service.h"
#include "realm-sssd.h"
#include "realm-sssd-config.h"

#include <glib/gstdio.h>

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

static void  update_properties   (RealmSssd *self);

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
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	RealmSssd *self = REALM_SSSD (g_async_result_get_source_object (user_data));
	GError *error = NULL;

	realm_service_restart_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (async, error);

	update_properties (self);
	g_simple_async_result_complete (async);

	g_object_unref (async);
	g_object_unref (self);
}

static gboolean
sssd_config_change_login_policy (RealmIniConfig *config,
                                 const gchar *section,
                                 const gchar *access_provider,
                                 const gchar **add_names,
                                 const gchar **remove_names,
                                 GError **error)
{
	gchar *allow;

	if (!realm_ini_config_begin_change (config, error))
		return FALSE;

	if (access_provider)
		realm_ini_config_set (config, section, "access_provider", access_provider);
	realm_ini_config_set_list_diff (config, section, "simple_allow_users", ",",
	                                add_names, remove_names);

	/*
	 * HACK: Work around for sssd problem where it allows users if
	 * simple_allow_users is empty. Set it to a comma in this case.
	 */
	allow = realm_ini_config_get (config, section, "simple_allow_users");
	if (allow != NULL)
		g_strstrip (allow);
	if (allow == NULL || g_str_equal (allow, ""))
		realm_ini_config_set (config, section, "simple_allow_users", ",");
	g_free (allow);

	return realm_ini_config_finish_change (config, error);
}

static void
realm_sssd_logins_async (RealmKerberos *realm,
                         GDBusMethodInvocation *invocation,
                         RealmKerberosLoginPolicy login_policy,
                         const gchar **add,
                         const gchar **remove,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	RealmSssd *self = REALM_SSSD (realm);
	GSimpleAsyncResult *async;
	gchar **remove_names = NULL;
	gchar **add_names = NULL;
	gboolean ret = FALSE;
	GError *error = NULL;
	const gchar *access_provider;

	if (!self->pv->section) {
		async = g_simple_async_result_new_error (G_OBJECT (realm), callback, user_data,
		                                         REALM_ERROR, REALM_ERROR_NOT_CONFIGURED,
		                                         "Not joined to this domain");
		g_simple_async_result_complete_in_idle (async);
		g_object_unref (async);
		return;
	}

	async = g_simple_async_result_new (G_OBJECT (realm), callback, user_data,
	                                   realm_sssd_logins_async);

	switch (login_policy) {
	case REALM_KERBEROS_POLICY_NOT_SET:
		access_provider = NULL;
		break;
	case REALM_KERBEROS_ALLOW_ANY_LOGIN:
		access_provider = "permit";
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

	add_names = realm_kerberos_parse_logins (realm, TRUE, add, &error);
	if (add_names != NULL)
		remove_names = realm_kerberos_parse_logins (realm, TRUE, remove, &error);

	if (add_names == NULL || remove_names == NULL) {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete_in_idle (async);
		g_object_unref (async);
		return;
	}

	if (add_names && remove_names) {
		ret = sssd_config_change_login_policy (self->pv->config,
		                                       self->pv->section,
		                                       access_provider,
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
realm_sssd_generic_finish (RealmKerberos *realm,
                           GAsyncResult *result,
                           GError **error)
{
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

static void
update_enrolled (RealmSssd *self)
{
	realm_kerberos_set_configured (REALM_KERBEROS (self),
	                               self->pv->section ? TRUE : FALSE);
}

static void
update_realm_name (RealmSssd *self)
{
	RealmKerberos *kerberos = REALM_KERBEROS (self);
	const char *name;
	gchar *realm = NULL;

	if (self->pv->section == NULL) {
		realm = g_strdup (realm_discovery_get_string (realm_kerberos_get_discovery (kerberos),
		                                              REALM_DBUS_DISCOVERY_REALM));
	} else {
		realm = realm_ini_config_get (self->pv->config, self->pv->section, "krb5_realm");
	}

	if (realm == NULL) {
		name = realm_kerberos_get_name (kerberos);
		realm = name ? g_ascii_strup (name, -1) : NULL;
	}

	realm_kerberos_set_realm_name (kerberos, realm);
	g_free (realm);
}

static void
update_domain (RealmSssd *self)
{
	RealmKerberos *kerberos = REALM_KERBEROS (self);
	const char *name;
	gchar *domain = NULL;

	if (self->pv->section == NULL) {
		domain = g_strdup (realm_discovery_get_string (realm_kerberos_get_discovery (kerberos),
		                                               REALM_DBUS_DISCOVERY_DOMAIN));
	} else {
		domain = realm_ini_config_get (self->pv->config, self->pv->section, "dns_discovery_domain");
	}

	if (domain == NULL) {
		name = realm_kerberos_get_name (kerberos);
		domain = name ? g_ascii_strdown (name, -1) : NULL;
	}

	realm_kerberos_set_domain_name (kerberos, domain);
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
update_login_formats (RealmSssd *self)
{
	RealmKerberos *kerberos = REALM_KERBEROS (self);
	gchar *login_formats[2] = { NULL, NULL };
	gchar *format = NULL;

	if (self->pv->section == NULL) {
		realm_kerberos_set_login_formats (kerberos, (const gchar **)login_formats);
		return;
	}

	/* Setup the login formats */
	format = realm_ini_config_get (self->pv->config, self->pv->section, "full_name_format");

	/* Here we place a '%s' in the place of the user in the format */
	login_formats[0] = build_login_format (format, "%U", self->pv->domain);
	realm_kerberos_set_login_formats (kerberos, (const gchar **)login_formats);
	g_free (login_formats[0]);
	g_free (format);
}

static void
update_login_policy (RealmSssd *self)
{
	RealmKerberosLoginPolicy policy = REALM_KERBEROS_POLICY_NOT_SET;
	RealmKerberos *kerberos = REALM_KERBEROS (self);
	GPtrArray *permitted;
	gchar *access = NULL;
	gchar **values;
	gint i;

	permitted = g_ptr_array_new_full (0, g_free);
	if (self->pv->section != NULL)
		access = realm_ini_config_get (self->pv->config, self->pv->section, "access_provider");
	if (g_strcmp0 (access, "simple") == 0) {
		values = realm_ini_config_get_list (self->pv->config, self->pv->section,
		                                    "simple_allow_users", ",");
		for (i = 0; values != NULL && values[i] != NULL; i++)
			g_ptr_array_add (permitted, realm_kerberos_format_login (kerberos, values[i]));
		g_strfreev (values);
		g_free (access);
		policy = REALM_KERBEROS_ALLOW_PERMITTED_LOGINS;
	} else if (g_strcmp0 (access, "permit") == 0) {
		policy = REALM_KERBEROS_ALLOW_ANY_LOGIN;
	} else if (g_strcmp0 (access, "deny") == 0) {
		policy = REALM_KERBEROS_DENY_ANY_LOGIN;
	} else {
		policy = REALM_KERBEROS_POLICY_NOT_SET;
	}

	g_ptr_array_add (permitted, NULL);

	realm_kerberos_set_login_policy (kerberos, policy);
	realm_kerberos_set_permitted_logins (kerberos, (const gchar **)permitted->pdata);

	g_ptr_array_free (permitted, TRUE);
}

static void
update_properties (RealmSssd *self)
{
	GObject *obj = G_OBJECT (self);
	const gchar *name;
	gchar *section = NULL;
	gchar *domain = NULL;
	gchar **domains;
	gchar *realm;
	gint i;

	g_object_freeze_notify (obj);

	/* Find the config domain with our realm */
	domains = realm_sssd_config_get_domains (self->pv->config);
	name = realm_kerberos_get_name (REALM_KERBEROS (self));
	for (i = 0; domains && domains[i]; i++) {
		section = realm_sssd_config_domain_to_section (domains[i]);
		realm = realm_ini_config_get (self->pv->config, section, "krb5_realm");
		if (realm && name && g_ascii_strcasecmp (realm, name) == 0) {
			domain = g_strdup (domains[i]);
			break;
		} else {
			g_free (section);
			section = NULL;
		}
	}
	g_strfreev (domains);

	g_free (self->pv->section);
	self->pv->section = section;
	g_free (self->pv->domain);
	self->pv->domain = domain;

	/* Update all the other properties */
	update_enrolled (self);
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
	update_properties (REALM_SSSD (user_data));
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
		update_properties (REALM_SSSD (obj));

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
	GSimpleAsyncResult *async;
	GDBusMethodInvocation *invocation;
	RealmIniConfig *config;
	gchar *domain;
} DeconfClosure;

static void
deconfigure_closure_free (gpointer data)
{
	DeconfClosure *deconf = data;
	g_object_unref (deconf->async);
	g_object_unref (deconf->invocation);
	g_object_unref (deconf->config);
	g_free (deconf->domain);
	g_slice_free (DeconfClosure, deconf);
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

	g_simple_async_result_complete (deconf->async);
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

	g_simple_async_result_complete (deconf->async);
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

	/* Deconfigure sssd.conf */
	realm_diagnostics_info (deconf->invocation, "Removing domain configuration from sssd.conf");
	if (!realm_sssd_config_remove_domain (deconf->config, deconf->domain, &error)) {
		g_simple_async_result_take_error (deconf->async, error);
		g_simple_async_result_complete (deconf->async);
		deconfigure_closure_free (deconf);
		return;
	}

	/* If no domains, then disable sssd */
	domains = realm_sssd_config_get_domains (deconf->config);
	if (domains == NULL || g_strv_length (domains) == 0) {
		realm_command_run_known_async ("sssd-disable-logins", NULL, deconf->invocation,
		                               NULL, on_disable_nss_service, deconf);

	/* If any domains left, then restart sssd */
	} else {
		realm_service_restart ("sssd", deconf->invocation,
		                       on_service_restart_done, deconf);
	}

	g_strfreev (domains);
}

void
realm_sssd_deconfigure_domain_tail (RealmSssd *self,
                                    GSimpleAsyncResult *async,
                                    GDBusMethodInvocation *invocation)
{
	DeconfClosure *deconf;
	GError *error = NULL;
	const gchar *realm_name;

	realm_name = realm_kerberos_get_realm_name (REALM_KERBEROS (self));

	/* Flush the keytab of all the entries for this realm */
	realm_diagnostics_info (invocation, "Removing entries from keytab for realm");
	if (!realm_kerberos_flush_keytab (realm_name, &error)) {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete_in_idle (async);
		return;
	}

	deconf = g_new0 (DeconfClosure, 1);
	deconf->async = g_object_ref (async);
	deconf->invocation = g_object_ref (invocation);
	deconf->config = g_object_ref (self->pv->config);
	deconf->domain = g_strdup (self->pv->domain);

	/*
	 * TODO: We would really like to do this after removing the domain, to prevent races
	 * but we can't because otherwise sss_cache doesn't clear that domain :S
	 */

	realm_command_run_known_async ("sssd-caches-flush", NULL, deconf->invocation,
	                               NULL, on_sssd_clear_cache, deconf);
}
