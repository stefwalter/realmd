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
#include "realm-provider.h"
#include "realm-samba.h"
#include "realm-samba-config.h"
#include "realm-samba-enroll.h"
#include "realm-samba-winbind.h"

#include <glib/gstdio.h>

#include <errno.h>
#include <string.h>

struct _RealmSamba {
	RealmKerberos parent;
	gchar *name;
	RealmIniConfig *config;
	gulong config_sig;
};

typedef struct {
	RealmKerberosClass parent_class;
} RealmSambaClass;

enum {
	PROP_0,
	PROP_NAME,
	PROP_DOMAIN,
	PROP_PROVIDER,
};

G_DEFINE_TYPE (RealmSamba, realm_samba, REALM_TYPE_KERBEROS);

static void
realm_samba_init (RealmSamba *self)
{
	GPtrArray *entries;
	GVariant *entry;
	GVariant *details;

	entries = g_ptr_array_new ();

	entry = g_variant_new_dict_entry (g_variant_new_string ("server-software"),
	                                  g_variant_new_string ("active-directory"));
	g_ptr_array_add (entries, entry);

	entry = g_variant_new_dict_entry (g_variant_new_string ("client-software"),
	                                  g_variant_new_string ("samba-winbind"));
	g_ptr_array_add (entries, entry);

	details = g_variant_new_array (G_VARIANT_TYPE ("{ss}"),
	                               (GVariant * const *)entries->pdata,
	                               entries->len);
	g_variant_ref_sink (details);

	g_object_set (self,
	              "details", details,
	              "suggested-administrator", "Administrator",
	              NULL);

	g_variant_unref (details);
	g_ptr_array_free (entries, TRUE);
}

static gchar *
lookup_enrolled_realm (RealmSamba *self)
{
	gchar *enrolled = NULL;
	gchar *security;

	security = realm_ini_config_get (self->config, REALM_SAMBA_CONFIG_GLOBAL, "security");
	if (security != NULL && g_ascii_strcasecmp (security, "ADS") == 0)
		enrolled = realm_ini_config_get (self->config, REALM_SAMBA_CONFIG_GLOBAL, "realm");
	return enrolled;
}

static gboolean
lookup_is_enrolled (RealmSamba *self)
{
	gchar *enrolled;
	gboolean ret;

	enrolled = lookup_enrolled_realm (self);
	ret = g_strcmp0 (self->name, enrolled) == 0;
	g_free (enrolled);

	return ret;
}

static gchar *
lookup_login_prefix (RealmSamba *self)
{
	gchar *workgroup;
	gchar *separator;

	workgroup = realm_ini_config_get (self->config, REALM_SAMBA_CONFIG_GLOBAL, "workgroup");
	if (workgroup == NULL)
		return NULL;

	separator = realm_ini_config_get (self->config, REALM_SAMBA_CONFIG_GLOBAL, "winbind separator");
	if (separator == NULL)
		separator = g_strdup ("\\");

	return g_strdup_printf ("%s%s", workgroup, separator);
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
realm_samba_enroll_async (RealmKerberos *realm,
                          GBytes *admin_kerberos_cache,
                          GDBusMethodInvocation *invocation,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	RealmSamba *self = REALM_SAMBA (realm);
	GSimpleAsyncResult *res;
	EnrollClosure *enroll;
	gchar *enrolled;

	res = g_simple_async_result_new (G_OBJECT (realm), callback, user_data,
	                                 realm_samba_enroll_async);
	enroll = g_slice_new0 (EnrollClosure);
	enroll->realm_name = g_strdup (self->name);
	enroll->invocation = g_object_ref (invocation);
	enroll->admin_kerberos_cache = g_bytes_ref (admin_kerberos_cache);
	g_simple_async_result_set_op_res_gpointer (res, enroll, enroll_closure_free);

	enroll->discovery = realm_kerberos_get_discovery (realm);
	if (enroll->discovery)
		g_hash_table_ref (enroll->discovery);

	/* Make sure not already enrolled in a realm */
	enrolled = lookup_enrolled_realm (self);
	if (enrolled != NULL) {
		g_simple_async_result_set_error (res, REALM_ERROR, REALM_ERROR_ALREADY_ENROLLED,
		                                 "Already enrolled in a realm");
		g_simple_async_result_complete_in_idle (res);

	/* Caller didn't discover first time around, so do that now */
	} else if (enroll->discovery == NULL) {
		realm_ad_discover_async (self->name, invocation,
		                         on_discover_do_install, g_object_ref (res));

	/* Already have discovery info, so go straight to install */
	} else {
		realm_packages_install_async ("samba-packages", invocation,
		                              on_install_do_join, g_object_ref (res));
	}

	g_free (enrolled);
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

	realm_samba_enroll_leave_finish (result, NULL);

	/* We don't care if we can leave or not, just continue with other steps */
	realm_samba_winbind_deconfigure_async (unenroll->invocation,
	                                       on_remove_winbind_done,
	                                       g_object_ref (res));

	g_object_unref (res);
}

static void
realm_samba_unenroll_async (RealmKerberos *realm,
                            GBytes *admin_kerberos_cache,
                            GDBusMethodInvocation *invocation,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	RealmSamba *self = REALM_SAMBA (realm);
	GSimpleAsyncResult *res;
	UnenrollClosure *unenroll;
	gchar *enrolled;

	res = g_simple_async_result_new (G_OBJECT (realm), callback, user_data,
	                                 realm_samba_unenroll_async);
	unenroll = g_slice_new0 (UnenrollClosure);
	unenroll->realm_name = g_strdup (self->name);
	unenroll->invocation = g_object_ref (invocation);
	g_simple_async_result_set_op_res_gpointer (res, unenroll, unenroll_closure_free);

	/* Check that enrolled in this realm */
	enrolled = lookup_enrolled_realm (self);
	if (g_strcmp0 (enrolled, self->name) == 0) {
		realm_samba_enroll_leave_async (self->name, admin_kerberos_cache, invocation,
		                                on_leave_do_winbind, g_object_ref (res));
	} else {
		g_simple_async_result_set_error (res, REALM_ERROR, REALM_ERROR_NOT_ENROLLED,
		                                 "Not currently enrolled in the realm");
		g_simple_async_result_complete_in_idle (res);
	}

	g_object_unref (res);
}

static gboolean
realm_samba_generic_finish (RealmKerberos *realm,
                            GAsyncResult *result,
                            GError **error)
{
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

static gchar **
prune_login_names (const gchar *prefix,
                   const gchar **logins,
                   GError **error)
{
	GPtrArray *names;
	gsize prefix_len;
	gchar *login;
	gint i;

	names = g_ptr_array_new_full (0, g_free);
	prefix_len = strlen (prefix);

	for (i = 0; logins != NULL && logins[i] != NULL; i++) {
		if (g_ascii_strncasecmp (prefix, logins[i], prefix_len) != 0) {
			g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
			             "Invalid login argument: %s", logins[i]);
			g_ptr_array_free (names, TRUE);
			return NULL;
		}

		login = g_utf8_strdown (logins[i] + prefix_len, -1);
		g_ptr_array_add (names, login);
	}

	g_ptr_array_add (names, NULL);
	return (gchar **)g_ptr_array_free (names, FALSE);
}

static gboolean
realm_samba_change_logins (RealmKerberos *realm,
                           GDBusMethodInvocation *invocation,
                           const gchar **add,
                           const gchar **remove,
                           GError **error)
{
	RealmSamba *self = REALM_SAMBA (realm);
	gchar **remove_names = NULL;
	gchar **add_names = NULL;
	gboolean ret = FALSE;
	gchar *prefix;

	if (!lookup_is_enrolled (self)) {
		g_set_error (error, REALM_ERROR, REALM_ERROR_NOT_ENROLLED,
		             "Not enrolled in this realm");
		return FALSE;
	}

	prefix = lookup_login_prefix (self);

	add_names = prune_login_names (prefix, add, error);
	if (add_names != NULL)
		remove_names = prune_login_names (prefix, remove, error);

	if (add_names && remove_names) {
		ret = realm_ini_config_change_list (self->config,
		                                    REALM_SAMBA_CONFIG_GLOBAL,
		                                    "realmd permitted logins", ",",
		                                    (const gchar **)add_names,
		                                    (const gchar **)remove_names,
		                                    error);
	}

	g_strfreev (remove_names);
	g_strfreev (add_names);

	return ret;
}

static void
update_properties (RealmSamba *self)
{
	GPtrArray *permitted;
	gchar *login_format;
	gchar **values;
	gchar *prefix;
	gint i;

	g_object_set (self, "enrolled", lookup_is_enrolled (self), NULL);

	/* Setup the workgroup property */
	prefix = lookup_login_prefix (self);
	if (prefix != NULL) {
		login_format = g_strdup_printf ("%s%%s", prefix);
		g_object_set (self, "login-format", login_format, NULL);
		g_free (login_format);
	} else {
		g_object_set (self, "login-format", "", NULL);
	}

	permitted = g_ptr_array_new_full (0, g_free);
	values = realm_ini_config_get_list (self->config, REALM_SAMBA_CONFIG_GLOBAL,
	                                    "realmd permitted logins", ",");

	for (i = 0; values != NULL && values[i] != NULL; i++)
		g_ptr_array_add (permitted, g_strdup_printf ("%s%s", prefix, values[i]));
	g_ptr_array_add (permitted, NULL);

	g_object_set (self, "permitted-logins", (gchar **)permitted->pdata, NULL);
	g_ptr_array_free (permitted, TRUE);
	g_strfreev (values);

	g_free (prefix);
}

static void
on_config_changed (RealmIniConfig *config,
                   gpointer user_data)
{
	update_properties (REALM_SAMBA (user_data));
}

static void
realm_samba_get_property (GObject *obj,
                          guint prop_id,
                          GValue *value,
                          GParamSpec *pspec)
{
	RealmSamba *self = REALM_SAMBA (obj);

	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
realm_samba_set_property (GObject *obj,
                          guint prop_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
	RealmSamba *self = REALM_SAMBA (obj);
	RealmProvider *provider;
	gchar *domain;

	switch (prop_id) {
	case PROP_NAME:
		self->name = g_value_dup_string (value);
		domain = g_ascii_strdown (self->name, -1);
		g_object_set (self, "domain", domain, NULL);
		g_free (domain);
		break;
	case PROP_PROVIDER:
		provider = g_value_get_object (value);
		g_object_get (provider, "samba-config", &self->config, NULL);
		self->config_sig = g_signal_connect (self->config, "changed",
		                                     G_CALLBACK (on_config_changed),
		                                     self);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
realm_samba_consructed (GObject *obj)
{
	RealmSamba *self = REALM_SAMBA (obj);

	G_OBJECT_CLASS (realm_samba_parent_class)->constructed (obj);

	update_properties (self);
}

static void
realm_samba_finalize (GObject *obj)
{
	RealmSamba  *self = REALM_SAMBA (obj);

	g_free (self->name);
	if (self->config)
		g_object_unref (self->config);

	G_OBJECT_CLASS (realm_samba_parent_class)->finalize (obj);
}

void
realm_samba_class_init (RealmSambaClass *klass)
{
	RealmKerberosClass *kerberos_class = REALM_KERBEROS_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	kerberos_class->enroll_async = realm_samba_enroll_async;
	kerberos_class->enroll_finish = realm_samba_generic_finish;
	kerberos_class->unenroll_async = realm_samba_unenroll_async;
	kerberos_class->unenroll_finish = realm_samba_generic_finish;
	kerberos_class->change_logins = realm_samba_change_logins;

	object_class->constructed = realm_samba_consructed;
	object_class->get_property = realm_samba_get_property;
	object_class->set_property = realm_samba_set_property;
	object_class->finalize = realm_samba_finalize;

	g_object_class_install_property (object_class, PROP_NAME,
	            g_param_spec_string ("name", "Name", "Realm Name",
	                                 "", G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (object_class, PROP_PROVIDER,
	            g_param_spec_object ("provider", "Provider", "Samba Provider",
	                                 REALM_TYPE_PROVIDER, G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

RealmKerberos *
realm_samba_new (const gchar *name,
                 RealmProvider *provider)
{
	return g_object_new (REALM_TYPE_SAMBA,
	                     "name", name,
	                     "provider", provider,
	                     NULL);
}
