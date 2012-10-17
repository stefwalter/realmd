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

#include "realm.h"
#include "realm-client.h"
#include "realm-dbus-constants.h"

#include <glib/gi18n.h>

#include <errno.h>
#include <string.h>

struct _RealmClient {
	GDBusObjectManagerClient parent;
	RealmDbusProvider *provider;
};

struct _RealmClientClass {
	GDBusObjectManagerClientClass parent;
};

G_DEFINE_TYPE (RealmClient, realm_client, G_TYPE_DBUS_OBJECT_MANAGER_CLIENT);

typedef struct {
	GAsyncResult *result;
	GMainLoop *loop;
} SyncClosure;

static void
on_complete_get_result (GObject *source,
                        GAsyncResult *result,
                        gpointer user_data)
{
	SyncClosure *sync = user_data;
	sync->result = g_object_ref (result);
	g_main_loop_quit (sync->loop);
}

static void
realm_client_init (RealmClient *self)
{

}

static void
realm_client_finalize (GObject *obj)
{
	RealmClient *self = REALM_CLIENT (obj);

	if (self->provider)
		g_object_unref (self->provider);

	G_OBJECT_CLASS (realm_client_parent_class)->finalize (obj);
}

static void
realm_client_class_init (RealmClientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = realm_client_finalize;
}

static GType
realm_object_client_get_proxy_type (GDBusObjectManagerClient *manager,
                                    const gchar *object_path,
                                    const gchar *interface_name,
                                    gpointer user_data)
{
	static gsize once_init_value = 0;
	static GHashTable *lookup_hash;
	GType ret;

	if (interface_name == NULL)
		return G_TYPE_DBUS_OBJECT_PROXY;

	if (g_once_init_enter (&once_init_value)) {
		lookup_hash = g_hash_table_new (g_str_hash, g_str_equal);
		g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.realmd.Provider", GSIZE_TO_POINTER (REALM_DBUS_TYPE_PROVIDER_PROXY));
		g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.realmd.Service", GSIZE_TO_POINTER (REALM_DBUS_TYPE_SERVICE_PROXY));
		g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.realmd.Realm", GSIZE_TO_POINTER (REALM_DBUS_TYPE_REALM_PROXY));
		g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.realmd.Kerberos", GSIZE_TO_POINTER (REALM_DBUS_TYPE_KERBEROS_PROXY));
		g_hash_table_insert (lookup_hash, (gpointer) "org.freedesktop.realmd.KerberosMembership", GSIZE_TO_POINTER (REALM_DBUS_TYPE_KERBEROS_MEMBERSHIP_PROXY));
		g_once_init_leave (&once_init_value, 1);
	}

	ret = GPOINTER_TO_SIZE (g_hash_table_lookup (lookup_hash, interface_name));
	if (ret == 0)
		ret = G_TYPE_DBUS_OBJECT_PROXY;
	return ret;
}

static void
on_diagnostics_signal (GDBusConnection *connection,
                       const gchar *sender_name,
                       const gchar *object_path,
                       const gchar *interface_name,
                       const gchar *signal_name,
                       GVariant *parameters,
                       gpointer user_data)
{
	const gchar *operation_id;
	const gchar *data;

	g_variant_get (parameters, "(&s&s)", &data, &operation_id);
	g_printerr ("%s", data);
}

RealmClient *
realm_client_new (gboolean verbose)
{
	GDBusConnection *connection;
	RealmDbusProvider *provider;
	GError *error = NULL;
	RealmClient *client = NULL;
	GInitable *ret;

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (error == NULL) {
		if (verbose) {
			g_dbus_connection_signal_subscribe (connection, REALM_DBUS_BUS_NAME,
			                                    REALM_DBUS_SERVICE_INTERFACE,
			                                    REALM_DBUS_DIAGNOSTICS_SIGNAL,
			                                    REALM_DBUS_SERVICE_PATH,
			                                    NULL, G_DBUS_SIGNAL_FLAGS_NONE,
			                                    on_diagnostics_signal, NULL, NULL);
		}

	} else {
		realm_handle_error (error, _("Couldn't connect to system bus"));
		return NULL;
	}

	provider = realm_dbus_provider_proxy_new_sync (connection,
	                                               G_DBUS_PROXY_FLAGS_NONE,
	                                               REALM_DBUS_BUS_NAME,
	                                               REALM_DBUS_SERVICE_PATH,
	                                               NULL, &error);
	if (error != NULL) {
		realm_handle_error (error, _("Couldn't connect to realm service"));
		g_object_unref (connection);
		return NULL;
	}

	ret = g_initable_new (REALM_TYPE_CLIENT, NULL, &error,
	                      "flags", G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
	                      "name", REALM_DBUS_BUS_NAME,
	                      "connection", connection,
	                      "object-path", REALM_DBUS_SERVICE_PATH,
	                      "get-proxy-type-func", realm_object_client_get_proxy_type,
	                      NULL);

	if (ret != NULL) {
		client = REALM_CLIENT (ret);
		client->provider = g_object_ref (provider);
		g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (provider), G_MAXINT);
	}

	g_object_unref (provider);
	g_object_unref (connection);

	if (error != NULL) {
		realm_handle_error (error, _("Couldn't load the realm service"));
		return NULL;
	}

	return client;
}

RealmDbusProvider *
realm_client_get_provider (RealmClient *self)
{
	g_return_val_if_fail (REALM_IS_CLIENT (self), NULL);
	return self->provider;
}

GList *
realm_client_discover (RealmClient *self,
                       const gchar *string,
                       const gchar *client_software,
                       const gchar *server_software,
                       const gchar *dbus_interface,
                       GError **error)
{
	GDBusObjectManager *manager;
	GDBusInterface *iface;
	GVariant *options;
	SyncClosure sync;
	gchar **realm_paths;
	gint relevance;
	GList *realms;
	gboolean ret;
	gint i;

	g_return_val_if_fail (REALM_IS_CLIENT (self), NULL);

	if (string == NULL)
		string = "";

	sync.result = NULL;
	sync.loop = g_main_loop_new (NULL, FALSE);

	options = realm_build_options (REALM_DBUS_OPTION_CLIENT_SOFTWARE, client_software,
	                               REALM_DBUS_OPTION_SERVER_SOFTWARE, server_software,
	                               NULL);

	/* Start actual operation */
	realm_dbus_provider_call_discover (self->provider, string, options,
	                                   NULL, on_complete_get_result, &sync);

	/* This mainloop is quit by on_complete_get_result */
	g_main_loop_run (sync.loop);

	ret = realm_dbus_provider_call_discover_finish (self->provider, &relevance,
	                                                &realm_paths, sync.result, error);

	g_object_unref (sync.result);
	g_main_loop_unref (sync.loop);

	if (!ret)
		return FALSE;

	realms = NULL;
	manager = G_DBUS_OBJECT_MANAGER (self);

	for (i = 0; realm_paths[i] != NULL; i++) {
		iface = g_dbus_object_manager_get_interface (manager, realm_paths[i],
		                                             dbus_interface);
		if (iface != NULL) {
			g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (iface), G_MAXINT);
			realms = g_list_prepend (realms, iface);
		}
	}

	return g_list_reverse (realms);
}

RealmDbusRealm *
realm_client_get_realm (RealmClient *self,
                        const gchar *object_path)
{
	GDBusInterface *iface;

	g_return_val_if_fail (REALM_IS_CLIENT (self), NULL);

	iface = g_dbus_object_manager_get_interface (G_DBUS_OBJECT_MANAGER (self),
	                                             object_path, REALM_DBUS_REALM_INTERFACE);
	if (iface)
		g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (iface), G_MAXINT);
	return REALM_DBUS_REALM (iface);
}

RealmDbusRealm *
realm_client_to_realm (RealmClient *self,
                       gpointer proxy)
{
	g_return_val_if_fail (REALM_IS_CLIENT (self), NULL);
	return realm_client_get_realm (self, g_dbus_proxy_get_object_path (proxy));
}

RealmDbusKerberosMembership *
realm_client_get_kerberos_membership (RealmClient *self,
                                      const gchar *object_path)
{
	GDBusInterface *iface;

	g_return_val_if_fail (REALM_IS_CLIENT (self), NULL);

	iface = g_dbus_object_manager_get_interface (G_DBUS_OBJECT_MANAGER (self),
	                                             object_path, REALM_DBUS_KERBEROS_MEMBERSHIP_INTERFACE);
	if (iface)
		g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (iface), G_MAXINT);
	return REALM_DBUS_KERBEROS_MEMBERSHIP (iface);
}

RealmDbusKerberosMembership *
realm_client_to_kerberos_membership (RealmClient *self,
                                     gpointer proxy)
{
	g_return_val_if_fail (REALM_IS_CLIENT (self), NULL);
	return realm_client_get_kerberos_membership (self, g_dbus_proxy_get_object_path (proxy));
}

RealmDbusKerberos *
realm_client_get_kerberos (RealmClient *self,
                           const gchar *object_path)
{
	GDBusInterface *iface;

	g_return_val_if_fail (REALM_IS_CLIENT (self), NULL);

	iface = g_dbus_object_manager_get_interface (G_DBUS_OBJECT_MANAGER (self),
	                                             object_path, REALM_DBUS_KERBEROS_INTERFACE);
	if (iface)
		g_dbus_proxy_set_default_timeout (G_DBUS_PROXY (iface), G_MAXINT);
	return REALM_DBUS_KERBEROS (iface);
}

RealmDbusKerberos *
realm_client_to_kerberos (RealmClient *self,
                          gpointer proxy)
{
	g_return_val_if_fail (REALM_IS_CLIENT (self), NULL);
	return realm_client_get_kerberos (self, g_dbus_proxy_get_object_path (proxy));
}

static gboolean
is_credential_supported (GVariant *supported,
                         const gchar *desired_type,
                         const gchar **ret_owner)
{
	GVariantIter iter;
	const gchar *type;
	const gchar *owner;

	g_variant_iter_init (&iter, supported);
	while (g_variant_iter_loop (&iter, "(&s&s)", &type, &owner)) {
		if (g_str_equal (desired_type, type)) {
			*ret_owner = owner;
			return TRUE;
		}
	}

	return FALSE;
}

GVariant *
realm_client_build_principal_creds (RealmClient *self,
                                    RealmDbusKerberosMembership *membership,
                                    GVariant *supported,
                                    const gchar *user_name,
                                    GError **error)
{
	RealmDbusKerberos *kerberos;
	const gchar *realm_name;
	const gchar *password = NULL;
	gboolean use_ccache;
	GVariant *contents;
	GVariant *creds;
	const gchar *owner;
	gchar *prompt;

	g_return_val_if_fail (REALM_IS_CLIENT (self), NULL);

	if (is_credential_supported (supported, "ccache", &owner)) {
		use_ccache = TRUE;

	} else if (is_credential_supported (supported, "password", &owner)) {
		use_ccache = FALSE;

	} else {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             _("Realm does not support membership using a password"));
		return NULL;
	}

	if (user_name == NULL)
		user_name = realm_dbus_kerberos_membership_get_suggested_administrator (membership);
	if (user_name == NULL || g_str_equal (user_name, ""))
		user_name = g_get_user_name ();

	/* If passing in a credential cache, then let krb5 do the prompting */
	if (use_ccache) {
		password = NULL;

	/* Passing in a password, we need to know it */
	} else {
		prompt = g_strdup_printf (_("Password for %s: "), user_name);
		password = getpass (prompt);
		g_free (prompt);

		if (password == NULL) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			             _("Couldn't prompt for password: %s"), g_strerror (errno));
			return NULL;
		}
	}

	kerberos = realm_client_to_kerberos (self, membership);
	realm_name = realm_dbus_kerberos_get_realm_name (kerberos);
	contents = realm_kinit_to_kerberos_cache (user_name, realm_name, password, error);
	g_object_unref (kerberos);

	if (!contents) {
		creds = NULL;

	/* Do a kinit for the given realm */
	} else if (use_ccache) {
		creds = g_variant_new ("(ssv)", "ccache", owner, contents);

	/* Just prompt for a password, and pass it in */
	} else {
		g_variant_unref (contents);
		creds = g_variant_new ("(ssv)", "password", owner,
		                       g_variant_new ("(ss)", user_name, password));
	}

	if (password)
		memset ((char *)password, 0, strlen (password));

	return creds;
}

GVariant *
realm_client_build_otp_creds (RealmClient *self,
                              GVariant *supported,
                              const gchar *one_time_password,
                              GError **error)
{
	const gchar *owner;

	g_return_val_if_fail (REALM_IS_CLIENT (self), NULL);

	if (!is_credential_supported (supported, "secret", &owner)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             _("Realm does not support membership using a one time password"));
		return NULL;
	}

	return g_variant_new ("(ssv)", "secret", owner,
	                      g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE,
	                                                 one_time_password,
	                                                 strlen (one_time_password),
	                                                 sizeof (unsigned char)));
}

GVariant *
realm_client_build_automatic_creds (RealmClient *self,
                                    GVariant *supported,
                                    GError **error)
{
	const gchar *owner;

	g_return_val_if_fail (REALM_IS_CLIENT (self), NULL);

	if (!is_credential_supported (supported, "automatic", &owner)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             _("Realm does not support automatic membership"));
		return NULL;
	}

	return g_variant_new ("(ssv)", "automatic", owner, g_variant_new_string (""));
}
