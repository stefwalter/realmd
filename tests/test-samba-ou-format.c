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

#include "service/realm-samba-util.h"

#include <glib/gstdio.h>

#include <string.h>

typedef struct {
	const gchar *ldap_dn;
	const gchar *domain;
	const gchar *ou_format;
} Fixture;

static void
test_samba_ou_format (gconstpointer user_data)
{
	const Fixture *fixture = user_data;
	gchar *result;

	result = realm_samba_util_build_strange_ou (fixture->ldap_dn, fixture->domain);
	g_assert_cmpstr (result, ==, fixture->ou_format);
	g_free (result);
}

static const Fixture samba_ou_fixtures[] = {
	{ "OU=One", "domain.example.com", "One" },
	{ "OU=One,ou=two", "domain.example.com", "two/One" },
	{ "Ou=One Long,OU=two", "domain.example.com", "two/One Long" },
	{ "Ou=One,OU=two, ou=Three", "domain.example.com", "Three/two/One" },
	{ "Ou=Test/Escape,Ou=Two", "domain.example.com", "Two/Test\\/Escape" },
	{ "Ou=Test\\\\Escape,Ou=Two", "domain.example.com", "Two/Test\\\\Escape" },
	{ "OU=One,DC=domain,dc=example,Dc=COM", "domain.example.com", "One" },
	{ "OU=One,OU=Two Here,DC=domain,dc=example,Dc=COM", "domain.example.com", "Two Here/One" },
	{ "OU=One,OU=Two Here,DC=invalid,Dc=COM", "domain.example.com", NULL },
	{ " ", "domain.example.com", NULL },
	{ "", "domain.example.com", NULL },
	{ "OU", "domain.example.com", NULL },
	{ "OU=One,", "domain.example.com", NULL },
	{ "CN=Unsupported", "domain.example.com", NULL },
	{ "OU=One+CN=Unsupported", "domain.example.com", NULL },
	{ "DC=radi07, DC=segad, DC=lab, DC=sjc, DC=redhat, DC=com", "radi08.segad.lab.sjc.redhat.com", NULL },

};

int
main (int argc,
      char **argv)
{
	gchar *escaped;
	gchar *name;
	gint i;

	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_set_prgname ("test-samba-ou-format");

	for (i = 0; i < G_N_ELEMENTS (samba_ou_fixtures); i++) {
		if (g_str_equal (samba_ou_fixtures[i].ldap_dn, ""))
			escaped = g_strdup ("_empty_");
		else
			escaped = g_strdup (samba_ou_fixtures[i].ldap_dn);
		g_strdelimit (escaped, ", =\\/", '_');
		name = g_strdup_printf ("/realmd/samba-ou-format/%s", escaped);
		g_free (escaped);

		g_test_add_data_func (name, samba_ou_fixtures + i, test_samba_ou_format);
		g_free (name);
	}

	return g_test_run ();
}
