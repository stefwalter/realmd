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

#include "realm-dbus-constants.h"
#include "realm-discovery.h"
#include "realm-kerberos-discover.h"
#include "realm-kerberos-provider.h"

#include <errno.h>

struct _RealmKerberosProvider {
	RealmProvider parent;
};

typedef struct {
	RealmProviderClass parent_class;
} RealmKerberosProviderClass;

#define   REALM_DBUS_GENERIC_KERBEROS_PATH          "/org/freedesktop/realmd/GenericKerberos"

G_DEFINE_TYPE (RealmKerberosProvider, realm_kerberos_provider, REALM_TYPE_PROVIDER);

static void
realm_kerberos_provider_init (RealmKerberosProvider *self)
{

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
realm_kerberos_provider_discover_async (RealmProvider *provider,
                                        const gchar *string,
                                        GVariant *options,
                                        GDBusMethodInvocation *invocation,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
	GSimpleAsyncResult *async;
	const gchar *software;

	async = g_simple_async_result_new (G_OBJECT (provider), callback, user_data,
	                                   realm_kerberos_provider_discover_async);

	/* If filtering for specific software, don't return anything */
	if (g_variant_lookup (options, REALM_DBUS_OPTION_SERVER_SOFTWARE, "&s", &software) ||
	    g_variant_lookup (options, REALM_DBUS_OPTION_CLIENT_SOFTWARE, "&s", &software)) {
		g_simple_async_result_complete_in_idle (async);

	} else {
		realm_kerberos_discover_async (string, invocation, on_kerberos_discover,
		                               g_object_ref (async));
	}

	g_object_unref (async);
}

static gint
realm_kerberos_provider_discover_finish (RealmProvider *provider,
                                         GAsyncResult *result,
                                         GVariant **realms,
                                         GError **error)
{
	RealmKerberos *realm = NULL;
	GSimpleAsyncResult *async;
	GHashTable *discovery;
	GAsyncResult *kerberos_result;
	const gchar *object_path;
	gchar *name;

	async = G_SIMPLE_ASYNC_RESULT (result);
	kerberos_result = g_simple_async_result_get_op_res_gpointer (async);
	if (kerberos_result == NULL)
		return 0;

	name = realm_kerberos_discover_finish (kerberos_result, &discovery, error);
	if (name == NULL)
		return 0;

	/* If any known software, don't create the realm */
	if (!realm_discovery_get_string (discovery, REALM_DBUS_OPTION_SERVER_SOFTWARE)) {
		realm = realm_provider_lookup_or_register_realm (provider,
		                                                 REALM_TYPE_KERBEROS,
		                                                 name, discovery);
	}

	g_free (name);
	g_hash_table_unref (discovery);

	if (realm == NULL)
		return 0;

	object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (realm));
	*realms = g_variant_new_objv (&object_path, 1);
	g_variant_ref_sink (*realms);

	/* Return a low priority as we can't handle enrollment */
	return 10;
}

void
realm_kerberos_provider_class_init (RealmKerberosProviderClass *klass)
{
	RealmProviderClass *provider_class = REALM_PROVIDER_CLASS (klass);
	provider_class->discover_async = realm_kerberos_provider_discover_async;
	provider_class->discover_finish = realm_kerberos_provider_discover_finish;
}

RealmProvider *
realm_kerberos_provider_new (void)
{
	return g_object_new (REALM_TYPE_KERBEROS_PROVIDER,
	                     "g-object-path", REALM_DBUS_GENERIC_KERBEROS_PATH,
	                     NULL);
}
