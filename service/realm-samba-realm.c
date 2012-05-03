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
#include "realm-packages.h"
#include "realm-samba-config.h"
#include "realm-samba-enroll.h"
#include "realm-samba-realm.h"
#include "realm-samba-winbind.h"

#include <glib/gstdio.h>

#include <errno.h>

struct _RealmSambaRealm {
	RealmKerberosRealm parent;
	gchar *name;
};

typedef struct {
	RealmKerberosRealmClass parent_class;
} RealmSambaRealmClass;

enum {
	PROP_0,
	PROP_NAME,
	PROP_USER_FORMAT,
	PROP_ENROLLED,
};

G_DEFINE_TYPE (RealmSambaRealm, realm_samba_realm, REALM_TYPE_KERBEROS_REALM);

static void
realm_samba_realm_init (RealmSambaRealm *self)
{

}

typedef struct {
	GDBusMethodInvocation *invocation;
	GBytes *admin_kerberos_cache;
	gchar *realm_name;
	GHashTable *discovery;
} EnrollClosure;

static void
enroll_closure_free (gpointer data)
{
	EnrollClosure *enroll = data;
	g_free (enroll->realm_name);
	g_object_unref (enroll->invocation);
	g_bytes_unref (enroll->admin_kerberos_cache);
	g_hash_table_unref (enroll->discovery);
	g_slice_free (EnrollClosure, enroll);
}

static void
on_winbind_done (GObject *source,
              GAsyncResult *result,
              gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;

	realm_samba_winbind_configure_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	g_simple_async_result_complete (res);

	g_object_unref (res);
}

static void
on_join_do_winbind (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	EnrollClosure *enroll = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;

	realm_samba_enroll_join_finish (result, &error);
	if (error == NULL) {
		realm_samba_winbind_configure_async (enroll->invocation,
		                                     on_winbind_done, g_object_ref (res));
	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
on_install_do_join (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	EnrollClosure *enroll = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;

	realm_packages_install_finish (result, &error);
	if (error == NULL) {
		realm_samba_enroll_join_async (enroll->realm_name, enroll->admin_kerberos_cache,
		                               enroll->invocation, on_join_do_winbind,
		                               g_object_ref (res));

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
on_discover_do_install (GObject *source,
                        GAsyncResult *result,
                        gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	EnrollClosure *enroll = g_simple_async_result_get_op_res_gpointer (res);
	GHashTable *discovery = NULL;
	GError *error = NULL;

	if (realm_ad_discover_finish (result, &discovery, &error)) {
		enroll->discovery = discovery;
		realm_packages_install_async ("samba-packages", enroll->invocation,
		                              on_install_do_join, g_object_ref (res));

	} else if (error == NULL) {
		/* TODO: a better error code/message here */
		g_simple_async_result_set_error (res, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		                                 "Invalid or unusable realm argument");
		g_simple_async_result_complete (res);

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);

}

static void
realm_samba_realm_enroll_async (RealmKerberosRealm *realm,
                                GBytes *admin_kerberos_cache,
                                GDBusMethodInvocation *invocation,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	RealmSambaRealm *self = REALM_SAMBA_REALM (realm);
	GSimpleAsyncResult *res;
	EnrollClosure *enroll;

	res = g_simple_async_result_new (G_OBJECT (realm), callback, user_data,
	                                 realm_samba_realm_enroll_async);
	enroll = g_slice_new0 (EnrollClosure);
	enroll->realm_name = g_strdup (self->name);
	enroll->invocation = g_object_ref (invocation);
	enroll->admin_kerberos_cache = g_bytes_ref (admin_kerberos_cache);
	g_simple_async_result_set_op_res_gpointer (res, enroll, enroll_closure_free);

	enroll->discovery = realm_kerberos_realm_get_discovery (realm);
	if (enroll->discovery)
		g_hash_table_ref (enroll->discovery);

	/* Caller didn't discover first time around, so do that now */
	if (enroll->discovery == NULL) {
		realm_ad_discover_async (self->name, invocation,
		                         on_discover_do_install, g_object_ref (res));

	/* Already have discovery info, so go straight to install */
	} else {
		realm_packages_install_async ("samba-packages", invocation,
		                              on_install_do_join, g_object_ref (res));
	}

	g_object_unref (res);
}

typedef struct {
	GDBusMethodInvocation *invocation;
	gchar *realm_name;
} UnenrollClosure;

static void
unenroll_closure_free (gpointer data)
{
	UnenrollClosure *unenroll = data;
	g_free (unenroll->realm_name);
	g_object_unref (unenroll->invocation);
	g_slice_free (UnenrollClosure, unenroll);
}

static void
on_remove_winbind_done (GObject *source,
                        GAsyncResult *result,
                        gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;

	realm_samba_winbind_deconfigure_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	g_simple_async_result_complete (res);

	g_object_unref (res);
}

static void
on_leave_do_winbind (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	UnenrollClosure *unenroll = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;

	realm_samba_enroll_leave_finish (result, &error);
	if (error == NULL) {
		realm_samba_winbind_deconfigure_async (unenroll->invocation,
		                                       on_remove_winbind_done,
		                                       g_object_ref (res));

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
realm_samba_realm_unenroll_async (RealmKerberosRealm *realm,
                                  GBytes *admin_kerberos_cache,
                                  GDBusMethodInvocation *invocation,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	RealmSambaRealm *self = REALM_SAMBA_REALM (realm);
	GSimpleAsyncResult *res;
	UnenrollClosure *unenroll;

	res = g_simple_async_result_new (G_OBJECT (realm), callback, user_data,
	                                 realm_samba_realm_unenroll_async);
	unenroll = g_slice_new0 (UnenrollClosure);
	unenroll->realm_name = g_strdup (self->name);
	unenroll->invocation = g_object_ref (invocation);
	g_simple_async_result_set_op_res_gpointer (res, unenroll, unenroll_closure_free);

	/* TODO: Check that we're enrolled as this realm */

	realm_samba_enroll_leave_async (self->name, admin_kerberos_cache, invocation,
	                                on_leave_do_winbind, g_object_ref (res));

	g_object_unref (res);
}

static gboolean
realm_samba_realm_generic_finish (RealmKerberosRealm *realm,
                                  GAsyncResult *result,
                                  GError **error)
{
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

static gchar *
lookup_enrolled_realm (void)
{
	RealmIniConfig *config;
	GError *error = NULL;
	gchar *realm = NULL;
	gchar *security;

	config = realm_samba_config_new (&error);
	if (error != NULL) {
		g_warning ("Couldn't read samba global configuration file: %s", error->message);
		g_error_free (error);
		return NULL;
	}

	security = realm_ini_config_get (config, REALM_SAMBA_CONFIG_GLOBAL, "security");
	if (security != NULL && g_ascii_strcasecmp (security, "ADS") == 0) {
		realm = realm_ini_config_get (config, REALM_SAMBA_CONFIG_GLOBAL, "realm");
	}

	g_free (security);
	g_object_unref (config);

	return realm;
}

static void
realm_samba_realm_get_property (GObject *obj,
                                guint prop_id,
                                GValue *value,
                                GParamSpec *pspec)
{
	RealmSambaRealm *self = REALM_SAMBA_REALM (obj);
	gchar *realm;

	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	case PROP_ENROLLED:
		realm = lookup_enrolled_realm ();
		g_value_set_boolean (value, g_strcmp0 (self->name, realm) == 0);
		g_free (realm);
		break;
	case PROP_USER_FORMAT:
		g_value_take_string (value, g_strdup_printf ("%s\%%s", self->name));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
realm_samba_realm_set_property (GObject *obj,
                                guint prop_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
	RealmSambaRealm *self = REALM_SAMBA_REALM (obj);

	switch (prop_id) {
	case PROP_NAME:
		self->name = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

void
realm_samba_realm_class_init (RealmSambaRealmClass *klass)
{
	RealmKerberosRealmClass *kerberos_class = REALM_KERBEROS_REALM_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	kerberos_class->enroll_async = realm_samba_realm_enroll_async;
	kerberos_class->enroll_finish = realm_samba_realm_generic_finish;
	kerberos_class->unenroll_async = realm_samba_realm_unenroll_async;
	kerberos_class->unenroll_finish = realm_samba_realm_generic_finish;

	object_class->get_property = realm_samba_realm_get_property;
	object_class->set_property = realm_samba_realm_set_property;

	g_object_class_override_property (object_class, PROP_NAME, "name");
	g_object_class_override_property (object_class, PROP_ENROLLED, "enrolled");
	g_object_class_override_property (object_class, PROP_USER_FORMAT, "user-format");
}

RealmKerberosRealm *
realm_samba_realm_new (const gchar *name)
{
	return g_object_new (REALM_TYPE_SAMBA_REALM, "name", name, NULL);
}
