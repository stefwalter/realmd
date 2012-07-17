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

#include <glib/gstdio.h>

struct _RealmSssdPrivate {
	gchar *realm;
	gchar *domain;
	gchar *section;
	RealmIniConfig *config;
	gulong config_sig;
};

enum {
	PROP_0,
	PROP_NAME,
	PROP_DOMAIN,
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
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;

	realm_service_restart_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (async, error);

	g_simple_async_result_complete (async);
	g_object_unref (async);
}

static gboolean
sssd_config_change_login_policy (RealmIniConfig *config,
                                 const gchar *section,
                                 const gchar *access_provider,
                                 const gchar **add_names,
                                 const gchar **remove_names,
                                 GError **error)
{
	if (!realm_ini_config_begin_change (config, error))
		return FALSE;

	if (access_provider)
		realm_ini_config_set (config, section, "access_provider", access_provider);
	realm_ini_config_set_list_diff (config, section, "simple_allow_users", ",",
	                                add_names, remove_names);
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
		                                         REALM_ERROR, REALM_ERROR_NOT_ENROLLED,
		                                         "Not enrolled in this realm");
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
	g_object_set (self, "enrolled", self->pv->section ? TRUE : FALSE, NULL);
}

static void
update_domain (RealmSssd *self)
{
	gchar *domain = NULL;

	if (self->pv->section != NULL)
		domain = realm_ini_config_get (self->pv->config, self->pv->section, "dns_discovery_domain");
	if (domain == NULL)
		domain = g_ascii_strdown (self->pv->realm, -1);
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
update_login_format (RealmSssd *self)
{
	gchar *login_format;
	gchar *format = NULL;

	if (self->pv->section == NULL) {
		g_object_set (self, "login-format", "", NULL);
		return;
	}

	/* Setup the login format */
	format = realm_ini_config_get (self->pv->config, self->pv->section, "full_name_format");

	/* Here we place a '%s' in the place of the user in the format */
	login_format = build_login_format (format, "%s", self->pv->domain);
	g_object_set (self, "login-format", login_format, NULL);
	g_free (login_format);
	g_free (format);
}

static void
update_login_policy (RealmSssd *self)
{
	const gchar *policy = NULL;
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
			g_ptr_array_add (permitted, realm_kerberos_format_login (REALM_KERBEROS (self), values[i]));
		g_strfreev (values);
		g_free (access);
		policy = REALM_DBUS_LOGIN_POLICY_PERMITTED;
	} else if (g_strcmp0 (access, "permit") == 0) {
		policy = REALM_DBUS_LOGIN_POLICY_ANY;
	} else if (g_strcmp0 (access, "deny") == 0) {
		policy = REALM_DBUS_LOGIN_POLICY_DENY;
	} else {
		policy = NULL;
	}

	g_ptr_array_add (permitted, NULL);

	g_object_set (self,
	              "login-policy", policy,
	              "permitted-logins", (gchar **)permitted->pdata,
	              NULL);

	g_ptr_array_free (permitted, TRUE);
}

static void
update_properties (RealmSssd *self)
{
	GObject *obj = G_OBJECT (self);
	gchar *section = NULL;
	gchar *domain = NULL;
	gchar **domains;
	gchar *realm;
	gint i;

	g_object_freeze_notify (obj);

	/* Find the config domain with our realm */
	domains = realm_sssd_config_get_domains (self->pv->config);
	for (i = 0; domains && domains[i]; i++) {
		section = realm_sssd_config_domain_to_section (domains[i]);
		realm = realm_ini_config_get (self->pv->config, section, "krb5_realm");
		if (g_strcmp0 (realm, self->pv->realm) == 0) {
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
	update_domain (self);
	update_login_format (self);
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
realm_sssd_get_property (GObject *obj,
                         guint prop_id,
                         GValue *value,
                         GParamSpec *pspec)
{
	RealmSssd *self = REALM_SSSD (obj);

	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, self->pv->realm);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
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
	case PROP_NAME:
		self->pv->realm = g_value_dup_string (value);
		break;
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
realm_sssd_consructed (GObject *obj)
{
	RealmSssd *self = REALM_SSSD (obj);

	G_OBJECT_CLASS (realm_sssd_parent_class)->constructed (obj);

	update_properties (self);
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

	object_class->constructed = realm_sssd_consructed;
	object_class->get_property = realm_sssd_get_property;
	object_class->set_property = realm_sssd_set_property;
	object_class->finalize = realm_sssd_finalize;

	g_object_class_install_property (object_class, PROP_NAME,
	            g_param_spec_string ("name", "Name", "Realm Name",
	                                 "", G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (object_class, PROP_PROVIDER,
	            g_param_spec_object ("provider", "Provider", "SssdAd Provider",
	                                 REALM_TYPE_PROVIDER, G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

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
