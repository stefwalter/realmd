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

#include "realm-kerberos-membership.h"

typedef RealmKerberosMembershipIface RealmKerberosMembershipInterface;
G_DEFINE_INTERFACE (RealmKerberosMembership, realm_kerberos_membership, 0);

static void
realm_kerberos_membership_default_init (RealmKerberosMembershipIface *iface)
{

}

GVariant *
realm_kerberos_membership_build_supported (RealmKerberosCredential cred_type,
                                           RealmKerberosFlags cred_owner,
                                           ...)
{
	GPtrArray *elements;
	GVariant *tuple[2];
	const gchar *string;
	GVariant *supported;
	va_list va;

	va_start (va, cred_owner);
	elements = g_ptr_array_new ();

	while (cred_type != 0) {
		if (cred_owner & REALM_KERBEROS_CREDENTIAL_ADMIN)
			string = "administrator";
		else if (cred_owner & REALM_KERBEROS_CREDENTIAL_USER)
			string = "user";
		else if (cred_owner & REALM_KERBEROS_CREDENTIAL_COMPUTER)
			string = "computer";
		else if (cred_owner & REALM_KERBEROS_CREDENTIAL_SECRET)
			string = "secret";
		else
			g_assert_not_reached ();

		tuple[1] = g_variant_new_string (string);

		switch (cred_type) {
		case REALM_KERBEROS_CREDENTIAL_CCACHE:
			string = "ccache";
			break;
		case REALM_KERBEROS_CREDENTIAL_PASSWORD:
			string = "password";
			break;
		case REALM_KERBEROS_CREDENTIAL_AUTOMATIC:
			string = "automatic";
			break;
		default:
			g_assert_not_reached ();
			break;
		}

		tuple[0] = g_variant_new_string (string);

		g_ptr_array_add (elements, g_variant_new_tuple (tuple, 2));

		cred_type = va_arg (va, RealmKerberosCredential);
		if (cred_type != 0)
			cred_owner = va_arg (va, RealmKerberosFlags);
	}

	va_end (va);

	supported = g_variant_new_array (G_VARIANT_TYPE ("(ss)"),
	                                 (GVariant *const *)elements->pdata,
	                                 elements->len);

	g_ptr_array_free (elements, TRUE);
	g_variant_ref_sink (supported);
	return supported;
}
