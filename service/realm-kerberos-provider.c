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
#include "realm-invocation.h"
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
	GTask *task = G_TASK (user_data);
	const gchar *domain = g_task_get_task_data (task);
	GError *error = NULL;
	RealmDisco *disco;
	GList *targets;

	targets = g_resolver_lookup_service_finish (G_RESOLVER (source), result, &error);
	if (targets) {
		g_list_free_full (targets, (GDestroyNotify)g_srv_target_free);
		disco = realm_disco_new (domain);
		disco->kerberos_realm = g_ascii_strup (domain, -1);
		g_task_return_pointer (task, disco, realm_disco_unref);

	} else if (error) {
		g_debug ("Resolving %s failed: %s", domain, error->message);
		g_error_free (error);
		g_task_return_pointer (task, NULL, NULL);
	}

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
	GTask *task;
	const gchar *software;
	GResolver *resolver;
	gchar *name;

	task = g_task_new (provider, NULL, callback, user_data);

	/* If filtering for specific software, don't return anything */
	if (g_variant_lookup (options, REALM_DBUS_OPTION_SERVER_SOFTWARE, "&s", &software) ||
	    g_variant_lookup (options, REALM_DBUS_OPTION_CLIENT_SOFTWARE, "&s", &software)) {
		g_task_return_pointer (task, NULL, NULL);

	} else {
		name = g_hostname_to_ascii (string);
		resolver = g_resolver_get_default ();
		g_resolver_lookup_service_async (resolver, "kerberos", "udp", name,
		                                 realm_invocation_get_cancellable (invocation),
		                                 on_kerberos_discover, g_object_ref (task));
		g_task_set_task_data (task, name, g_free);
		g_object_unref (resolver);
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
	RealmDisco *disco;

	disco = g_task_propagate_pointer (G_TASK (result), error);
	if (disco == NULL)
		return NULL;

	realm = realm_provider_lookup_or_register_realm (provider,
	                                                 REALM_TYPE_KERBEROS,
	                                                 disco->domain_name, disco);

	realm_disco_unref (disco);

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
