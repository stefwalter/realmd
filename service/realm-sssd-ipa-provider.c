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

#include "realm-ipa-discover.h"
#include "realm-command.h"
#include "realm-daemon.h"
#include "realm-dbus-constants.h"
#include "realm-diagnostics.h"
#include "realm-discovery.h"
#include "realm-errors.h"
#include "realm-kerberos.h"
#include "realm-packages.h"
#include "realm-sssd-ipa.h"
#include "realm-sssd-ipa-provider.h"
#include "realm-sssd-config.h"

#include <glib/gstdio.h>

#include <errno.h>

struct _RealmSssdIpaProvider {
	RealmProvider parent;
	RealmIniConfig *config;
};

typedef struct {
	RealmProviderClass parent_class;
} RealmSssdIpaProviderClass;

enum {
	PROP_0,
	PROP_SSSD_CONFIG,
};

#define   REALM_DBUS_SSSD_IPA_NAME                    "org.freedesktop.realmd.SssdIpa"
#define   REALM_DBUS_SSSD_IPA_PATH                    "/org/freedesktop/realmd/SssdIpa"

G_DEFINE_TYPE (RealmSssdIpaProvider, realm_sssd_ipa_provider, REALM_TYPE_PROVIDER);

static void
realm_sssd_ipa_provider_init (RealmSssdIpaProvider *self)
{
	self->config = realm_sssd_config_new (NULL);

	/* The dbus Name property of the provider */
	g_object_set (self, "name", "SssdIpa", NULL);
}

static void
realm_sssd_ipa_provider_constructed (GObject *obj)
{
	RealmSssdIpaProvider *self;
	gchar **domains;
	gchar *section;
	gchar *realm;
	gchar *type;
	gint i;

	G_OBJECT_CLASS (realm_sssd_ipa_provider_parent_class)->constructed (obj);

	self = REALM_SSSD_IPA_PROVIDER (obj);

	domains = realm_sssd_config_get_domains (self->config);
	for (i = 0; domains && domains[i] != 0; i++) {
		section = realm_sssd_config_domain_to_section (domains[i]);
		type = realm_ini_config_get (self->config, section, "id_provider");
		realm = realm_ini_config_get (self->config, section, "krb5_realm");
		g_free (section);

		if (g_strcmp0 (type, "ipa") == 0) {
			realm_provider_lookup_or_register_realm (REALM_PROVIDER (self),
			                                         REALM_TYPE_SSSD_IPA,
			                                         realm ? realm : domains[i]);
		}

		g_free (realm);
		g_free (type);
	}
	g_strfreev (domains);
}

static void
realm_sssd_ipa_provider_discover_async (RealmProvider *provider,
                                        const gchar *string,
                                        GDBusMethodInvocation *invocation,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
	realm_ipa_discover_async (string, invocation, callback, user_data);
}

static gint
realm_sssd_ipa_provider_discover_finish (RealmProvider *provider,
                                         GAsyncResult *result,
                                         GVariant **realms,
                                         GError **error)
{
	GDBusInterfaceSkeleton *realm;
	GHashTable *discovery;
	const gchar *object_path;
	GVariant *realm_info;
	gchar *name;

	name = realm_ipa_discover_finish (result, &discovery, error);
	if (name == NULL)
		return 0;

	realm = realm_provider_lookup_or_register_realm (provider,
	                                                 REALM_TYPE_SSSD_IPA,
	                                                 name);
	g_free (name);

	if (realm == NULL) {
		g_hash_table_unref (discovery);
		return 0;
	}

	realm_kerberos_set_discovery (REALM_KERBEROS (realm), discovery);

	object_path = g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (realm));
	realm_info = realm_provider_new_realm_info (REALM_DBUS_SSSD_IPA_NAME, object_path,
	                                            REALM_DBUS_KERBEROS_REALM_INTERFACE);
	*realms = g_variant_new_array (G_VARIANT_TYPE ("(sos)"), &realm_info, 1);
	g_variant_ref_sink (*realms);

	g_hash_table_unref (discovery);
	return 100;
}

static void
realm_sssd_ipa_provider_get_property (GObject *obj,
                                      guint prop_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
	RealmSssdIpaProvider *self = REALM_SSSD_IPA_PROVIDER (obj);

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
realm_sssd_ipa_provider_finalize (GObject *obj)
{
	RealmSssdIpaProvider *self = REALM_SSSD_IPA_PROVIDER (obj);

	g_object_unref (self->config);

	G_OBJECT_CLASS (realm_sssd_ipa_provider_parent_class)->finalize (obj);
}

void
realm_sssd_ipa_provider_class_init (RealmSssdIpaProviderClass *klass)
{
	RealmProviderClass *provider_class = REALM_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	provider_class->dbus_name = REALM_DBUS_SSSD_IPA_NAME;
	provider_class->dbus_path = REALM_DBUS_SSSD_IPA_PATH;
	provider_class->discover_async = realm_sssd_ipa_provider_discover_async;
	provider_class->discover_finish = realm_sssd_ipa_provider_discover_finish;

	object_class->constructed = realm_sssd_ipa_provider_constructed;
	object_class->get_property = realm_sssd_ipa_provider_get_property;
	object_class->finalize = realm_sssd_ipa_provider_finalize;

	g_object_class_install_property (object_class, PROP_SSSD_CONFIG,
	            g_param_spec_object ("sssd-config", "Sssd Config", "Sssd Config",
	                                 REALM_TYPE_INI_CONFIG, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}
