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
#include "realm-diagnostics.h"
#include "realm-disco.h"
#include "realm-errors.h"
#include "realm-example.h"
#include "realm-example-provider.h"
#include "realm-ini-config.h"
#include "realm-settings.h"
#include "realm-kerberos.h"
#include "realm-usleep-async.h"
#include "realm-invocation.h"

#include <string.h>

struct _RealmExampleProvider {
	RealmProvider parent;
	RealmIniConfig *config;
};

typedef struct {
	RealmProviderClass parent_class;
} RealmExampleProviderClass;

enum {
	PROP_0,
	PROP_EXAMPLE_CONFIG,
};

#define  REALM_EXAMPLE_CONFIG       STATE_DIR "/example.conf"

#define  REALM_DBUS_EXAMPLE_PATH    "/org/freedesktop/realmd/Example"

#define  ALLOWED_CHARS              "abcdefghijklmnopqrstuvwxyz012346789-."

G_DEFINE_TYPE (RealmExampleProvider, realm_example_provider, REALM_TYPE_PROVIDER);

static void
realm_example_provider_init (RealmExampleProvider *self)
{
	self->config = realm_ini_config_new (REALM_INI_NONE);
}

static void
realm_example_provider_constructed (GObject *obj)
{
	RealmExampleProvider *self;
	GError *error = NULL;
	gchar **sections;
	gint i;

	G_OBJECT_CLASS (realm_example_provider_parent_class)->constructed (obj);

	self = REALM_EXAMPLE_PROVIDER (obj);

	realm_ini_config_read_file (self->config, REALM_EXAMPLE_CONFIG, &error);
	if (error != NULL) {
		g_warning ("Couldn't load config file: %s: %s",
		           REALM_EXAMPLE_CONFIG, error->message);
		g_error_free (error);
	}

	realm_provider_set_name (REALM_PROVIDER (self), "Example");

	sections = realm_ini_config_get_sections (self->config);
	for (i = 0; sections != NULL && sections[i] != NULL; i++) {
		realm_provider_lookup_or_register_realm (REALM_PROVIDER (self),
		                                         REALM_TYPE_EXAMPLE,
		                                         sections[i], NULL);
	}

	g_strfreev (sections);
}

static void
on_discover_sleep_done (GObject *source,
                        GAsyncResult *res,
                        gpointer user_data)
{
	GTask *task = G_TASK (user_data);
	GError *error = NULL;

	if (!realm_usleep_finish (res, &error))
		g_task_return_error (task, error);
	else
		g_task_return_boolean (task, TRUE);
	g_object_unref (task);
}

static gchar *
parse_example_name (const char *string)
{
	gchar *domain;
	gsize length;

	domain = g_ascii_strdown (string, -1);
	g_strstrip (domain);

	length = strlen (domain);

	if (!length ||
	    strspn (domain, ALLOWED_CHARS) != length ||
	    strstr (domain, "..") != NULL ||
	    domain[0] == '.') {
		g_free (domain);
		return NULL;
	}

	if (g_str_has_suffix (domain, ".")) {
		domain[length] = '\0';
		length--;
	}

	/* No, I don't care */
	if (!g_str_has_suffix (domain, "example.org") &&
	    !g_str_has_suffix (domain, "example.com") &&
	    !g_str_has_suffix (domain, "example.net")) {
		g_free (domain);
		return NULL;
	}

	if (length > 11) {
		if (domain[length - 12] != '.') {
			g_free (domain);
			return NULL;
		}
	}

	return domain;
}

static void
realm_example_provider_discover_async (RealmProvider *provider,
                                       const gchar *string,
                                       GVariant *options,
                                       GDBusMethodInvocation *invocation,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
	GTask *task;

	task = g_task_new (provider, NULL, callback, user_data);

	if (!realm_provider_match_software (options,
	                                    REALM_DBUS_IDENTIFIER_EXAMPLE,
	                                    REALM_DBUS_IDENTIFIER_EXAMPLE,
	                                    REALM_DBUS_IDENTIFIER_EXAMPLE)) {
		g_task_return_boolean (task, TRUE);

	/* A valid example domain name */
	} else {
		gchar *domain;
		gdouble delay;

		if (string == NULL || strlen (string) == 0)
			domain = g_strdup (realm_settings_value ("example", "default"));
		else
			domain = parse_example_name (string);

		if (domain && realm_settings_section(domain))
			delay = realm_settings_double (domain, "example-discovery-delay", 0.0);
		else {
			delay = realm_settings_double ("example", "non-discovery-delay", 0.0);
			g_free (domain);
			domain = NULL;
		}

		g_object_set_data_full (G_OBJECT (task), "the-domain", domain, g_free);

		realm_usleep_async (delay * G_USEC_PER_SEC,
		                    realm_invocation_get_cancellable (invocation),
		                    on_discover_sleep_done,
		                    g_object_ref (task));
	}

	g_object_unref (task);
}

static GList *
realm_example_provider_discover_finish (RealmProvider *provider,
                                        GAsyncResult *result,
                                        gint *relevance,
                                        GError **error)
{
	RealmKerberos *realm = NULL;
	gchar *domain;

	g_return_val_if_fail (g_task_is_valid (result, provider), NULL);

	if (!g_task_propagate_boolean (G_TASK (result), error))
		return NULL;

	domain = g_object_get_data (G_OBJECT (result), "the-domain");
	if (domain == NULL || realm_settings_section (domain) == NULL)
		return NULL;

	realm = realm_provider_lookup_or_register_realm (provider,
	                                                 REALM_TYPE_EXAMPLE,
	                                                 domain, NULL);

	if (realm == NULL)
		return NULL;

	*relevance = 10;
	return g_list_append (NULL, g_object_ref (realm));
}

static void
realm_example_provider_get_property (GObject *obj,
                                     guint prop_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
	RealmExampleProvider *self = REALM_EXAMPLE_PROVIDER (obj);

	switch (prop_id) {
	case PROP_EXAMPLE_CONFIG:
		g_value_set_object (value, self->config);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
realm_example_provider_finalize (GObject *obj)
{
	RealmExampleProvider *self = REALM_EXAMPLE_PROVIDER (obj);

	g_object_unref (self->config);

	G_OBJECT_CLASS (realm_example_provider_parent_class)->finalize (obj);
}

void
realm_example_provider_class_init (RealmExampleProviderClass *klass)
{
	RealmProviderClass *provider_class = REALM_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	provider_class->discover_async = realm_example_provider_discover_async;
	provider_class->discover_finish = realm_example_provider_discover_finish;

	object_class->constructed = realm_example_provider_constructed;
	object_class->get_property = realm_example_provider_get_property;
	object_class->finalize = realm_example_provider_finalize;

	g_object_class_install_property (object_class, PROP_EXAMPLE_CONFIG,
	            g_param_spec_object ("example-config", "Example Config", "Example Config",
	                                 REALM_TYPE_INI_CONFIG, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

RealmProvider *
realm_example_provider_new (void)
{
	return g_object_new (REALM_TYPE_EXAMPLE_PROVIDER,
	                     "g-object-path", REALM_DBUS_EXAMPLE_PATH,
	                     NULL);
}
