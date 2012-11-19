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
#include "realm-kerberos.h"
#include "realm-kerberos-discover.h"
#include "realm-packages.h"
#include "realm-sssd-ad.h"
#include "realm-sssd-ipa.h"
#include "realm-sssd-provider.h"
#include "realm-sssd-config.h"

#include <glib/gstdio.h>

#include <errno.h>

struct _RealmSssdProvider {
	RealmProvider parent;
	RealmIniConfig *config;
};

typedef struct {
	RealmProviderClass parent_class;
} RealmSssdProviderClass;

enum {
	PROP_0,
	PROP_SSSD_CONFIG,
};

#define   REALM_DBUS_SSSD_PATH                    "/org/freedesktop/realmd/Sssd"

G_DEFINE_TYPE (RealmSssdProvider, realm_sssd_provider, REALM_TYPE_PROVIDER);

static void
realm_sssd_provider_init (RealmSssdProvider *self)
{
	self->config = realm_sssd_config_new (NULL);
}

static void
realm_sssd_provider_constructed (GObject *obj)
{
	RealmSssdProvider *self;
	GType realm_type;
	const gchar *name;
	gchar **domains;
	gchar *section;
	gchar *realm;
	gchar *type;
	gchar *domain;
	gint i;

	G_OBJECT_CLASS (realm_sssd_provider_parent_class)->constructed (obj);

	self = REALM_SSSD_PROVIDER (obj);

	realm_provider_set_name (REALM_PROVIDER (self), "Sssd");

	domains = realm_sssd_config_get_domains (self->config);
	for (i = 0; domains && domains[i] != 0; i++) {
		section = realm_sssd_config_domain_to_section (domains[i]);
		type = realm_ini_config_get (self->config, section, "id_provider");
		realm = realm_ini_config_get (self->config, section, "krb5_realm");
		domain = NULL;

		if (g_strcmp0 (type, "ad") == 0) {
			name = domain = realm_ini_config_get (self->config, section, "ad_domain");
			realm_type = REALM_TYPE_SSSD_AD;
		} else if (g_strcmp0 (type, "ipa") == 0) {
			name = domain = realm_ini_config_get (self->config, section, "ipa_domain");
			realm_type = REALM_TYPE_SSSD_IPA;
		} else {
			name = domain = NULL;
			realm_type = 0;
		}

		if (name == NULL)
			name = realm;
		if (name == NULL)
			name = domains[i];

		if (realm_type)
			realm_provider_lookup_or_register_realm (REALM_PROVIDER (self), realm_type, name, NULL);

		g_free (realm);
		g_free (type);
		g_free (domain);
		g_free (section);
	}
	g_strfreev (domains);
}

static void
on_kerberos_discover (GObject *source,
                      GAsyncResult *result,
                      gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	g_simple_async_result_set_op_res_gpointer (async, g_object_ref (result), g_object_unref);
	g_simple_async_result_complete (async);
	g_object_unref (async);
}

static void
realm_sssd_provider_discover_async (RealmProvider *provider,
                                    const gchar *string,
                                    GVariant *options,
                                    GDBusMethodInvocation *invocation,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
	GSimpleAsyncResult *async;

	async = g_simple_async_result_new (G_OBJECT (provider), callback, user_data,
	                                   realm_sssd_provider_discover_async);

	if (!realm_provider_match_software (options,
	                                    REALM_DBUS_IDENTIFIER_ACTIVE_DIRECTORY,
	                                    REALM_DBUS_IDENTIFIER_SSSD,
	                                    REALM_DBUS_IDENTIFIER_SAMBA) &&
	    !realm_provider_match_software (options,
	                                    REALM_DBUS_IDENTIFIER_ACTIVE_DIRECTORY,
	                                    REALM_DBUS_IDENTIFIER_SSSD,
	                                    REALM_DBUS_IDENTIFIER_ADCLI) &&
	    !realm_provider_match_software (options,
	                                    REALM_DBUS_IDENTIFIER_FREEIPA,
	                                    REALM_DBUS_IDENTIFIER_SSSD,
	                                    REALM_DBUS_IDENTIFIER_FREEIPA)) {
		g_simple_async_result_complete_in_idle (async);

	} else {
		realm_kerberos_discover_async (string, invocation, on_kerberos_discover,
		                               g_object_ref (async));
	}

	g_object_unref (async);
}

static GList *
realm_sssd_provider_discover_finish (RealmProvider *provider,
                                     GAsyncResult *result,
                                     gint *relevance,
                                     GError **error)
{
	GSimpleAsyncResult *async;
	GAsyncResult *ad_result;
	RealmKerberos *realm = NULL;
	GHashTable *discovery;
	gint priority;
	gchar *name;

	async = G_SIMPLE_ASYNC_RESULT (result);
	ad_result = g_simple_async_result_get_op_res_gpointer (async);
	if (ad_result == NULL)
		return NULL;

	name = realm_kerberos_discover_finish (ad_result, &discovery, error);
	if (name == NULL)
		return NULL;

	if (realm_discovery_has_string (discovery,
	                                REALM_DBUS_OPTION_SERVER_SOFTWARE,
	                                REALM_DBUS_IDENTIFIER_ACTIVE_DIRECTORY)) {

		realm = realm_provider_lookup_or_register_realm (provider,
		                                                 REALM_TYPE_SSSD_AD,
		                                                 name, discovery);
		priority = realm_provider_is_default (REALM_DBUS_IDENTIFIER_ACTIVE_DIRECTORY, REALM_DBUS_IDENTIFIER_SSSD) ? 100 : 50;

	} else if (realm_discovery_has_string (discovery,
	                                REALM_DBUS_OPTION_SERVER_SOFTWARE,
	                                REALM_DBUS_IDENTIFIER_FREEIPA)) {

		realm = realm_provider_lookup_or_register_realm (provider,
		                                                 REALM_TYPE_SSSD_IPA,
		                                                 name, discovery);
		priority = 100;
	}

	g_free (name);
	g_hash_table_unref (discovery);

	if (realm == NULL)
		return NULL;

	/* Return a higher priority if we're the default */
	*relevance = priority;
	return g_list_append (NULL, g_object_ref (realm));
}

static void
realm_sssd_provider_get_property (GObject *obj,
                                  guint prop_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
	RealmSssdProvider *self = REALM_SSSD_PROVIDER (obj);

	switch (prop_id) {
	case PROP_SSSD_CONFIG:
		g_value_set_object (value, self->config);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
realm_sssd_provider_finalize (GObject *obj)
{
	RealmSssdProvider *self = REALM_SSSD_PROVIDER (obj);

	g_object_unref (self->config);

	G_OBJECT_CLASS (realm_sssd_provider_parent_class)->finalize (obj);
}

void
realm_sssd_provider_class_init (RealmSssdProviderClass *klass)
{
	RealmProviderClass *provider_class = REALM_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	provider_class->discover_async = realm_sssd_provider_discover_async;
	provider_class->discover_finish = realm_sssd_provider_discover_finish;

	object_class->constructed = realm_sssd_provider_constructed;
	object_class->get_property = realm_sssd_provider_get_property;
	object_class->finalize = realm_sssd_provider_finalize;

	g_object_class_install_property (object_class, PROP_SSSD_CONFIG,
	            g_param_spec_object ("sssd-config", "Sssd Config", "Sssd Config",
	                                 REALM_TYPE_INI_CONFIG, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

}

RealmProvider *
realm_sssd_provider_new (void)
{
	return g_object_new (REALM_TYPE_SSSD_PROVIDER,
	                     "g-object-path", REALM_DBUS_SSSD_PATH,
	                     NULL);
}
