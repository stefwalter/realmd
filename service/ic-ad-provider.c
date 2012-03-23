/* identity-config - Identity configuration service
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

#include "ic-ad-discover.h"
#include "ic-ad-enroll.h"
#include "ic-ad-provider.h"
#include "ic-ad-sssd.h"
#include "ic-diagnostics.h"
#include "ic-discovery.h"
#include "ic-errors.h"
#include "ic-packages.h"
#include "ic-command.h"

#include <glib/gstdio.h>

#include <errno.h>

struct _IcAdProvider {
	IcKerberosProvider parent;
};

typedef struct {
	IcKerberosProviderClass parent_class;
} IcAdProviderClass;

G_DEFINE_TYPE (IcAdProvider, ic_ad_provider, IC_TYPE_KERBEROS_PROVIDER);

/*
 * The packages we need to install to get AD working. If a given package
 * doesn't exist on a given distro, then it'll be skipped by PackageKit.
 *
 * Some packages should be dependencies of identity-config itself, rather
 * than listed here. Here are the packages specific to AD kerberos support.
 */
static const gchar *AD_PACKAGES[] = {
	"sssd",
	"libpam-sss", /* Needed on debian */
	"libnss-sss", /* Needed on debian */
	"samba-common",
	"samba-common-bin", /* Needed on debian */
	NULL
};

/*
 * Various files that we need to get AD working. The packages above supply
 * these files. Unlike the list above, *all of the files below must exist*
 * in order to proceed.
 *
 * If a distro has a different path for a given file, then add a configure.ac
 * --with-xxx and AC_DEFINE for it, replacing the constant here.
 */

#ifndef NET_PATH
#define NET_PATH             "/usr/bin/net"
#endif

#ifndef SSSD_PATH
#define SSSD_PATH            "/usr/sbin/sssd"
#endif

static const gchar *AD_FILES[] = {
	NET_PATH,
	SSSD_PATH,
	NULL,
};

/*
 * TODO: Hopefully this will go away once we have support in glib for SOA
 * But if not, should be autodetected in configure
 */
#define   HOST_PATH            "/bin/host"

/*
 * TODO: This is needed by SSSDConfig. Not sure if we can just use GKeyFile
 * But if not, should be autodetected in configure
 */
#define   PYTHON_PATH          "/bin/python"

static void
ic_ad_provider_init (IcAdProvider *self)
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
on_sssd_done (GObject *source,
              GAsyncResult *result,
              gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;

	ic_ad_sssd_configure_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	g_simple_async_result_complete (res);

	g_object_unref (res);
}


static void
on_join_do_sssd (GObject *source,
                 GAsyncResult *result,
                 gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	EnrollClosure *enroll = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;

	ic_ad_enroll_join_finish (result, &error);
	if (error == NULL) {
		ic_ad_sssd_configure_async (IC_AD_SSSD_ADD_REALM,
		                             enroll->realm, enroll->invocation,
		                             on_sssd_done, g_object_ref (res));
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

	ic_packages_install_finish (result, &error);
	if (error == NULL) {
		ic_ad_enroll_join_async (enroll->realm, enroll->admin_kerberos_cache,
		                          enroll->invocation, on_join_do_sssd, g_object_ref (res));

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

	discovery = ic_discovery_new ();
	if (ic_ad_discover_finish (IC_KERBEROS_PROVIDER (source), result, discovery, &error)) {
		enroll->discovery = discovery;
		discovery = NULL;

#ifdef TODO
		ic_packages_install_async ("active-directory", enroll->invocation,
		                           on_install_do_join, g_object_ref (res));
#endif

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
ic_ad_provider_enroll_async (IcKerberosProvider *provider,
                              const gchar *realm,
                              GBytes *admin_kerberos_cache,
                              GDBusMethodInvocation *invocation,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	GSimpleAsyncResult *res;
	EnrollClosure *enroll;

	res = g_simple_async_result_new (G_OBJECT (provider), callback, user_data,
	                                 ic_ad_provider_enroll_async);
	enroll = g_slice_new0 (EnrollClosure);
	enroll->realm = g_strdup (realm);
	enroll->invocation = g_object_ref (invocation);
	g_simple_async_result_set_op_res_gpointer (res, enroll, enroll_closure_free);

	enroll->discovery = ic_kerberos_provider_lookup_discovery (provider, realm);

	/* Caller didn't discover first time around, so do that now */
	if (enroll->discovery == NULL) {
		ic_ad_discover_async (provider, realm, invocation,
		                      on_discover_do_install, g_object_ref (res));

	/* Already have discovery info, so go straight to install */
	} else {
		ic_packages_install_async (AD_FILES, AD_PACKAGES, invocation,
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
on_remove_sssd_done (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;

	ic_ad_sssd_configure_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	g_simple_async_result_complete (res);

	g_object_unref (res);
}

static void
on_leave_do_sssd (GObject *source,
                  GAsyncResult *result,
                  gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	UnenrollClosure *unenroll = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;

	ic_ad_enroll_leave_finish (result, &error);
	if (error == NULL) {
		ic_ad_sssd_configure_async (IC_AD_SSSD_REMOVE_REALM, unenroll->realm,
		                             unenroll->invocation, on_remove_sssd_done,
		                             g_object_ref (res));

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
ic_ad_provider_unenroll_async (IcKerberosProvider *provider,
                                const gchar *realm,
                                GBytes *admin_kerberos_cache,
                                GDBusMethodInvocation *invocation,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	GSimpleAsyncResult *res;
	UnenrollClosure *unenroll;

	res = g_simple_async_result_new (G_OBJECT (provider), callback, user_data,
	                                 ic_ad_provider_unenroll_async);
	unenroll = g_slice_new0 (UnenrollClosure);
	unenroll->realm = g_strdup (realm);
	unenroll->invocation = g_object_ref (invocation);
	g_simple_async_result_set_op_res_gpointer (res, unenroll, unenroll_closure_free);

	/* TODO: Check that we're enrolled as this realm */

	ic_ad_enroll_leave_async (realm, admin_kerberos_cache, invocation,
	                           on_leave_do_sssd, g_object_ref (res));

	g_object_unref (res);
}
static gboolean
ic_ad_provider_generic_finish (IcKerberosProvider *provider,
                                GAsyncResult *result,
                                GError **error)
{
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

void
ic_ad_provider_class_init (IcAdProviderClass *klass)
{
	IcKerberosProviderClass *kerberos_class = IC_KERBEROS_PROVIDER_CLASS (klass);

	kerberos_class->discover_async = ic_ad_discover_async;
	kerberos_class->discover_finish = ic_ad_discover_finish;
	kerberos_class->enroll_async = ic_ad_provider_enroll_async;
	kerberos_class->enroll_finish = ic_ad_provider_generic_finish;
	kerberos_class->unenroll_async = ic_ad_provider_unenroll_async;
	kerberos_class->unenroll_finish = ic_ad_provider_generic_finish;
}
