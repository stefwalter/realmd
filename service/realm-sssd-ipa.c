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

#include "realm-ipa-discover.h"
#include "realm-command.h"
#include "realm-daemon.h"
#include "realm-dbus-constants.h"
#include "realm-diagnostics.h"
#include "realm-discovery.h"
#include "realm-errors.h"
#include "realm-packages.h"
#include "realm-provider.h"
#include "realm-service.h"
#include "realm-sssd.h"
#include "realm-sssd-ipa.h"
#include "realm-sssd-config.h"

#include <glib/gstdio.h>

#include <errno.h>
#include <string.h>

struct _RealmSssdIpa {
	RealmSssd parent;
};

typedef struct {
	RealmSssdClass parent_class;
} RealmSssdIpaClass;

G_DEFINE_TYPE (RealmSssdIpa, realm_sssd_ipa, REALM_TYPE_SSSD);

static void
realm_sssd_ipa_init (RealmSssdIpa *self)
{

}

static void
realm_sssd_ipa_constructed (GObject *obj)
{
	RealmKerberos *kerberos = REALM_KERBEROS (obj);
	GVariant *supported;

	G_OBJECT_CLASS (realm_sssd_ipa_parent_class)->constructed (obj);

	realm_kerberos_set_details (kerberos,
	                            REALM_DBUS_OPTION_SERVER_SOFTWARE, REALM_DBUS_IDENTIFIER_FREEIPA,
	                            REALM_DBUS_OPTION_CLIENT_SOFTWARE, REALM_DBUS_IDENTIFIER_SSSD,
	                            NULL);

	/*
	 * Each line is a combination of owner and what kind of credentials are supported,
	 * same for enroll/unenroll. Enroll is not currently implemented: empty.
	 */
	supported = realm_kerberos_build_supported_credentials (0, 0);
	g_variant_ref_sink (supported);
	realm_kerberos_set_supported_join_creds (kerberos, supported);
	realm_kerberos_set_supported_leave_creds (kerberos, supported);
	g_variant_unref (supported);

	realm_kerberos_set_suggested_admin (kerberos, "admin");
}

static void
realm_sssd_ipa_enroll_async (RealmKerberos *realm,
                             GBytes *admin_kerberos_cache,
                             RealmKerberosFlags flags,
                             GVariant *options,
                             GDBusMethodInvocation *invocation,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	GSimpleAsyncResult *async;

	async = g_simple_async_result_new (G_OBJECT (realm), callback, user_data,
	                                   realm_sssd_ipa_enroll_async);
	g_simple_async_result_set_error (async, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Enroll not yet supported");
	g_simple_async_result_complete (async);
	g_object_unref (async);
}


static void
realm_sssd_ipa_unenroll_async (RealmKerberos *realm,
                               GBytes *admin_kerberos_cache,
                               RealmKerberosFlags flags,
                               GVariant *options,
                               GDBusMethodInvocation *invocation,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	GSimpleAsyncResult *async;

	async = g_simple_async_result_new (G_OBJECT (realm), callback, user_data,
	                                   realm_sssd_ipa_enroll_async);
	g_simple_async_result_set_error (async, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Unenroll not yet supported");
	g_simple_async_result_complete (async);
	g_object_unref (async);
}

static gboolean
realm_sssd_ipa_generic_finish (RealmKerberos *realm,
                               GAsyncResult *result,
                               GError **error)
{
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

void
realm_sssd_ipa_class_init (RealmSssdIpaClass *klass)
{
	RealmKerberosClass *kerberos_class = REALM_KERBEROS_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = realm_sssd_ipa_constructed;

	kerberos_class->enroll_ccache_async = realm_sssd_ipa_enroll_async;
	kerberos_class->enroll_finish = realm_sssd_ipa_generic_finish;
	kerberos_class->unenroll_ccache_async = realm_sssd_ipa_unenroll_async;
	kerberos_class->unenroll_finish = realm_sssd_ipa_generic_finish;
}
