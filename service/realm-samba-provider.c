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
#include "realm-samba-enroll.h"
#include "realm-samba-provider.h"
#include "realm-samba-winbind.h"

#include <glib/gstdio.h>

#include <errno.h>

struct _RealmSambaProvider {
	RealmKerberosProvider parent;
};

typedef struct {
	RealmKerberosProviderClass parent_class;
} RealmSambaProviderClass;

static guint provider_owner_id = 0;

G_DEFINE_TYPE (RealmSambaProvider, realm_samba_provider, REALM_TYPE_KERBEROS_PROVIDER);

static void
realm_samba_provider_init (RealmSambaProvider *self)
{

}

typedef struct {
	GDBusMethodInvocation *invocation;
	GBytes *admin_kerberos_cache;
	gchar *realm;
	GHashTable *discovery;
} EnrollClosure;

static void
enroll_closure_free (gpointer data)
{
	EnrollClosure *enroll = data;
	g_free (enroll->realm);
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
		realm_samba_enroll_join_async (enroll->realm, enroll->admin_kerberos_cache,
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
	GError *error = NULL;
	GHashTable *discovery;

	discovery = realm_discovery_new ();
	if (realm_ad_discover_finish (REALM_KERBEROS_PROVIDER (source), result, discovery, &error)) {
		enroll->discovery = discovery;
		discovery = NULL;

		realm_packages_install_async ("active-directory-packages", enroll->invocation,
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

	if (discovery)
		g_hash_table_unref (discovery);
	g_object_unref (res);

}

static void
realm_samba_provider_enroll_async (RealmKerberosProvider *provider,
                                   const gchar *realm,
                                   GBytes *admin_kerberos_cache,
                                   GDBusMethodInvocation *invocation,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
	GSimpleAsyncResult *res;
	EnrollClosure *enroll;

	res = g_simple_async_result_new (G_OBJECT (provider), callback, user_data,
	                                 realm_samba_provider_enroll_async);
	enroll = g_slice_new0 (EnrollClosure);
	enroll->realm = g_strdup (realm);
	enroll->invocation = g_object_ref (invocation);
	enroll->admin_kerberos_cache = g_bytes_ref (admin_kerberos_cache);
	g_simple_async_result_set_op_res_gpointer (res, enroll, enroll_closure_free);

	enroll->discovery = realm_kerberos_provider_lookup_discovery (provider, realm);

	/* Caller didn't discover first time around, so do that now */
	if (enroll->discovery == NULL) {
		realm_ad_discover_async (provider, realm, invocation,
		                         on_discover_do_install, g_object_ref (res));

	/* Already have discovery info, so go straight to install */
	} else {
		realm_packages_install_async ("active-directory-packages", invocation,
		                              on_install_do_join, g_object_ref (res));
	}

	g_object_unref (res);
}

typedef struct {
	GDBusMethodInvocation *invocation;
	gchar *realm;
} UnenrollClosure;

static void
unenroll_closure_free (gpointer data)
{
	UnenrollClosure *unenroll = data;
	g_free (unenroll->realm);
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
realm_samba_provider_unenroll_async (RealmKerberosProvider *provider,
                                     const gchar *realm,
                                     GBytes *admin_kerberos_cache,
                                     GDBusMethodInvocation *invocation,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
	GSimpleAsyncResult *res;
	UnenrollClosure *unenroll;

	res = g_simple_async_result_new (G_OBJECT (provider), callback, user_data,
	                                 realm_samba_provider_unenroll_async);
	unenroll = g_slice_new0 (UnenrollClosure);
	unenroll->realm = g_strdup (realm);
	unenroll->invocation = g_object_ref (invocation);
	g_simple_async_result_set_op_res_gpointer (res, unenroll, unenroll_closure_free);

	/* TODO: Check that we're enrolled as this realm */

	realm_samba_enroll_leave_async (realm, admin_kerberos_cache, invocation,
	                                on_leave_do_winbind, g_object_ref (res));

	g_object_unref (res);
}

static gboolean
realm_samba_provider_generic_finish (RealmKerberosProvider *provider,
                                     GAsyncResult *result,
                                     GError **error)
{
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

void
realm_samba_provider_class_init (RealmSambaProviderClass *klass)
{
	RealmKerberosProviderClass *kerberos_class = REALM_KERBEROS_PROVIDER_CLASS (klass);

	kerberos_class->discover_async = realm_ad_discover_async;
	kerberos_class->discover_finish = realm_ad_discover_finish;
	kerberos_class->enroll_async = realm_samba_provider_enroll_async;
	kerberos_class->enroll_finish = realm_samba_provider_generic_finish;
	kerberos_class->unenroll_async = realm_samba_provider_unenroll_async;
	kerberos_class->unenroll_finish = realm_samba_provider_generic_finish;
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
