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

#include "realm-daemon.h"
#include "realm-dbus-constants.h"
#include "realm-dbus-generated.h"
#include "realm-service.h"

#include <glib/gstdio.h>

#include <errno.h>

struct _RealmService {
	RealmDbusServiceSkeleton parent;
};

typedef struct {
	RealmDbusServiceSkeletonClass parent_class;
} RealmServiceClass;

enum {
	PROP_0,
	PROP_PROVIDERS
};

static guint service_owner_id = 0;

G_DEFINE_TYPE (RealmService, realm_service, REALM_DBUS_TYPE_SERVICE_SKELETON);

static void
realm_service_init (RealmService *self)
{

}

static GVariant *
load_provider (const gchar *filename)
{
	GVariant *provider = NULL;
	GError *error = NULL;
	GKeyFile *key_file;
	gchar *name;
	gchar *path;
	gchar *type;

	key_file = g_key_file_new ();
	g_key_file_load_from_file (key_file, filename, G_KEY_FILE_NONE, &error);
	if (error == NULL)
		name = g_key_file_get_string (key_file, "provider", "name", &error);
	if (error == NULL)
		path = g_key_file_get_string (key_file, "provider", "path", &error);
	if (error == NULL)
		type = g_key_file_get_string (key_file, "provider", "type", &error);
	if (error == NULL && !g_variant_is_object_path (path)) {
		g_set_error (&error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE,
		             "Invalid DBus object path: %s", path);
	}

	if (error == NULL) {
		provider = g_variant_new ("(sso)", name, type, path);

	} else {
		g_warning ("Couldn't load provider information from: %s: %s",
		           filename, error->message);
		g_error_free (error);
	}

	g_key_file_free (key_file);
	g_free (name);
	g_free (path);
	g_free (type);

	return provider;
}

static GVariant *
load_provider_list (void)
{
	GPtrArray *providers;
	GError *error = NULL;
	GDir *dir = NULL;
	gchar *filename;
	const gchar *name;
	GVariant *provider;
	GVariant *result;

	dir = g_dir_open (PROVIDER_DIR, 0, &error);
	if (error != NULL) {
		if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			g_warning ("Couldn't list provider files in: %s: %s",
			           PROVIDER_DIR, error->message);
		g_error_free (error);
		dir = NULL;
	}

	providers = g_ptr_array_new ();
	for (;;) {
		if (dir == NULL)
			name = NULL;
		else
			name = g_dir_read_name (dir);
		if (name == NULL)
			break;

		/* Only files ending in *.provider are loaded */
		if (!g_pattern_match_simple ("*.provider", name))
			continue;

		filename = g_build_filename (PROVIDER_DIR, name, NULL);
		provider = load_provider (filename);
		g_free (filename);

		if (provider != NULL)
			g_ptr_array_add (providers, provider);
	}

	result = g_variant_new_array (G_VARIANT_TYPE ("(sso)"),
	                              (GVariant * const *)providers->pdata,
	                              providers->len);
	g_ptr_array_free (providers, TRUE);
	return g_variant_ref_sink (result);
}

static void
realm_service_constructed (GObject *obj)
{
	GVariant *providers;

	G_OBJECT_CLASS (realm_service_parent_class)->constructed (obj);

	providers = load_provider_list ();
	g_object_set (obj, "providers", providers, NULL);
	g_variant_unref (providers);
}


void
realm_service_class_init (RealmServiceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = realm_service_constructed;
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
	g_warning ("couldn't claim service name on DBus bus: %s", REALM_DBUS_SERVICE_NAME);
}

void
realm_service_start (GDBusConnection *connection)
{
	RealmService *service;
	GError *error = NULL;

	g_return_if_fail (service_owner_id == 0);

	service = g_object_new (REALM_TYPE_SERVICE, NULL);

	g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (service),
	                                  connection, REALM_DBUS_SERVICE_PATH,
	                                  &error);

	if (error != NULL) {
		g_warning ("couldn't export RealmService on dbus connection: %s",
		           error->message);
		g_object_unref (service);
		return;
	}

	service_owner_id = g_bus_own_name_on_connection (connection,
	                                                 REALM_DBUS_SERVICE_NAME,
	                                                 G_BUS_NAME_OWNER_FLAGS_NONE,
	                                                 on_name_acquired,
	                                                 on_name_lost,
	                                                 service, g_object_unref);
}

void
realm_service_stop (void)
{
	if (service_owner_id != 0)
		g_bus_unown_name (service_owner_id);
}
