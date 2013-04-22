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

#include "egg-task.h"
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
	EggTask *task = EGG_TASK (user_data);
	egg_task_return_pointer (task, g_object_ref (result), g_object_unref);
	g_object_unref (task);
}

static void
realm_kerberos_provider_discover_async (RealmProvider *provider,
                                        const gchar *string,
                                        GVariant *options,
                                        GDBusMethodInvocation *invocation,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
	EggTask *task;
	const gchar *software;

	task = egg_task_new (provider, NULL, callback, user_data);

	/* If filtering for specific software, don't return anything */
	if (g_variant_lookup (options, REALM_DBUS_OPTION_SERVER_SOFTWARE, "&s", &software) ||
	    g_variant_lookup (options, REALM_DBUS_OPTION_CLIENT_SOFTWARE, "&s", &software)) {
		egg_task_return_pointer (task, NULL, NULL);

	} else {
		realm_kerberos_discover_async (string, invocation, on_kerberos_discover,
		                               g_object_ref (task));
	}

	g_object_unref (task);
}

static GList *
realm_kerberos_provider_discover_finish (RealmProvider *provider,
                                         GAsyncResult *result,
                                         gint *relevance,
                                         GError **error)
{
	RealmKerberos *realm = NULL;
	GHashTable *discovery;
	GAsyncResult *kerberos_result;
	gchar *name;

	kerberos_result = egg_task_propagate_pointer (EGG_TASK (result), error);
	if (kerberos_result == NULL)
		return NULL;

	name = realm_kerberos_discover_finish (kerberos_result, &discovery, error);
	if (name == NULL)
		return NULL;

	realm = realm_provider_lookup_or_register_realm (provider,
	                                                 REALM_TYPE_KERBEROS,
	                                                 name, discovery);

	g_free (name);
	g_hash_table_unref (discovery);

	if (realm == NULL)
		return NULL;

	/* Return a low priority as we can't handle enrollment */
	*relevance = 10;
	return g_list_append (NULL, g_object_ref (realm));
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
