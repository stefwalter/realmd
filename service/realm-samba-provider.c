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

#include "realm-ad-discover.h"
#include "realm-command.h"
#include "realm-daemon.h"
#include "realm-dbus-constants.h"
#include "realm-diagnostics.h"
#include "realm-discovery.h"
#include "realm-errors.h"
#include "realm-kerberos-realm.h"
#include "realm-packages.h"
#include "realm-samba-config.h"
#include "realm-samba-enroll.h"
#include "realm-samba-provider.h"
#include "realm-samba-realm.h"
#include "realm-samba-winbind.h"

#include <glib/gstdio.h>

#include <errno.h>

struct _RealmSambaProvider {
	RealmProvider parent;
	GHashTable *realms;
	RealmIniConfig *config;
	gulong config_sig;
};

typedef struct {
	RealmProviderClass parent_class;
} RealmSambaProviderClass;

enum {
	PROP_0,
	PROP_REALMS,
};

static guint provider_owner_id = 0;

G_DEFINE_TYPE (RealmSambaProvider, realm_samba_provider, REALM_TYPE_PROVIDER);

static void
realm_samba_provider_init (RealmSambaProvider *self)
{
	self->realms = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                      g_free, g_object_unref);

	self->config = realm_samba_config_new (NULL);
}

static RealmKerberosRealm *
lookup_or_register_realm (RealmSambaProvider *self,
                          const gchar *name)
{
	RealmKerberosRealm *realm;
	GDBusConnection *connection;
	static gint unique_number = 0;
	GError *error = NULL;
	gchar *escaped;
	gchar *path;

	realm = g_hash_table_lookup (self->realms, name);
	if (realm == NULL) {
		realm = realm_samba_realm_new (name, self->config);

		escaped = g_strdup (name);
		g_strcanon (escaped, REALM_DBUS_NAME_CHARS, '_');

		path = g_strdup_printf ("%s/%s_%d", REALM_DBUS_SAMBA_PATH,
		                        escaped, ++unique_number);

		g_free (escaped);

		connection = g_dbus_interface_skeleton_get_connection (G_DBUS_INTERFACE_SKELETON (self));
		g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (realm),
		                                  connection, path, &error);

		g_free (path);

		if (error == NULL) {
			g_hash_table_insert (self->realms, g_strdup (name), realm);

		} else {
			g_warning ("couldn't export samba realm on dbus connection: %s",
			           error->message);
			g_object_unref (realm);
			realm = NULL;
		}
	}

	return realm;
}

static void
ensure_local_realm (RealmSambaProvider *self)
{
	RealmIniConfig *config;
	GError *error = NULL;
	gchar *name = NULL;
	gchar *security;

	config = realm_samba_config_new (&error);
	if (error != NULL) {
		g_warning ("Couldn't read samba global configuration file: %s", error->message);
		g_error_free (error);
		return;
	}

	security = realm_ini_config_get (config, REALM_SAMBA_CONFIG_GLOBAL, "security");
	if (security != NULL && g_ascii_strcasecmp (security, "ADS") == 0) {
		name = realm_ini_config_get (config, REALM_SAMBA_CONFIG_GLOBAL, "realm");
	}

	if (name != NULL)
		lookup_or_register_realm (self, name);

	g_free (name);
	g_free (security);
	g_object_unref (config);
}

static GVariant *
build_realms (GHashTable *realms)
{
	GHashTableIter iter;
	RealmKerberosRealm *realm;
	GVariantBuilder builder;
	const gchar *path;

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(sos)"));

	g_hash_table_iter_init (&iter, realms);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer)&realm)) {
		path = g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (realm));
		g_variant_builder_add (&builder, "(sos)", REALM_DBUS_SAMBA_NAME, path,
		                       REALM_DBUS_KERBEROS_REALM_INTERFACE);
	}

	return g_variant_builder_end (&builder);
}

static void
realm_samba_provider_discover_async (RealmProvider *provider,
                                     const gchar *string,
                                     GDBusMethodInvocation *invocation,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
	realm_ad_discover_async (string, invocation, callback, user_data);
}

static gint
realm_samba_provider_discover_finish (RealmProvider *provider,
                                      GAsyncResult *result,
                                      GVariant **realm_info,
                                      GVariant **discovery_info,
                                      GError **error)
{
	RealmSambaProvider *self = REALM_SAMBA_PROVIDER (provider);
	RealmKerberosRealm *realm;
	GHashTable *discovery;
	const gchar *object_path;
	gchar *name;

	name = realm_ad_discover_finish (result, &discovery, error);
	if (name == NULL) {
		g_set_error (error, REALM_ERROR, REALM_ERROR_DISCOVERED_NOTHING,
		             "Nothing found during discovery");
		return -1;
	}

	realm = lookup_or_register_realm (self, name);
	g_free (name);

	if (realm == NULL) {
		g_hash_table_unref (discovery);
		return -1;
	}

	realm_kerberos_realm_set_discovery (realm, discovery);

	if (realm_info) {
		object_path = g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (realm));
		*realm_info = realm_provider_new_realm_info (REALM_DBUS_SAMBA_NAME, object_path,
		                                             REALM_DBUS_KERBEROS_REALM_INTERFACE);
		g_variant_ref_sink (*realm_info);
	}
	if (discovery_info) {
		*discovery_info = realm_discovery_to_variant (discovery);
		g_variant_ref_sink (*discovery_info);
	}

	g_hash_table_unref (discovery);
	return 100;
}

static void
realm_samba_provider_get_property (GObject *obj,
                                   guint prop_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
	RealmSambaProvider *self = REALM_SAMBA_PROVIDER (obj);

	switch (prop_id) {
	case PROP_REALMS:
		ensure_local_realm (self);
		g_value_set_variant (value, build_realms (self->realms));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
realm_samba_provider_set_property (GObject *obj,
                                   guint prop_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
realm_samba_provider_finalize (GObject *obj)
{
	RealmSambaProvider *self = REALM_SAMBA_PROVIDER (obj);

	g_hash_table_unref (self->realms);
	g_signal_handler_disconnect (self->config, self->config_sig);
	g_object_unref (self->config);

	G_OBJECT_CLASS (realm_samba_provider_parent_class)->finalize (obj);
}

void
realm_samba_provider_class_init (RealmSambaProviderClass *klass)
{
	RealmProviderClass *provider_class = REALM_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	provider_class->discover_async = realm_samba_provider_discover_async;
	provider_class->discover_finish = realm_samba_provider_discover_finish;

	object_class->get_property = realm_samba_provider_get_property;
	object_class->set_property = realm_samba_provider_set_property;
	object_class->finalize = realm_samba_provider_finalize;

	g_object_class_override_property (object_class, PROP_REALMS, "realms");
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
	realm_daemon_poke ();
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
	g_warning ("couldn't claim provider name on DBus bus: %s", REALM_DBUS_SAMBA_NAME);
}

void
realm_samba_provider_start (GDBusConnection *connection)
{
	RealmSambaProvider *provider;
	GError *error = NULL;

	g_return_if_fail (provider_owner_id == 0);

	provider = g_object_new (REALM_TYPE_SAMBA_PROVIDER, NULL);

	g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (provider),
	                                  connection, REALM_DBUS_SAMBA_PATH,
	                                  &error);

	if (error != NULL) {
		g_warning ("couldn't export RealmSambaProvider on dbus connection: %s",
		           error->message);
		return;
	}

	provider_owner_id = g_bus_own_name_on_connection (connection,
	                                                  REALM_DBUS_SAMBA_NAME,
	                                                  G_BUS_NAME_OWNER_FLAGS_NONE,
	                                                  on_name_acquired,
	                                                  on_name_lost,
	                                                  provider, g_object_unref);
}

void
realm_samba_provider_stop (void)
{
	if (provider_owner_id != 0)
		g_bus_unown_name (provider_owner_id);
}
