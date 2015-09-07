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

#include "realm-dn-util.h"

#include <glib.h>

#include <ldap.h>

static gboolean
berval_is_string (const struct berval *bv,
                  const gchar *string,
                  gsize length)
{
	return (bv->bv_len == length &&
	        g_ascii_strncasecmp (bv->bv_val, string, length) == 0);

}

static gboolean
berval_case_equals (const struct berval *v1,
                    const struct berval *v2)
{
	return (v1->bv_len == v2->bv_len &&
	        g_ascii_strncasecmp (v1->bv_val, v2->bv_val, v1->bv_len) == 0);
}

static gboolean
dn_equals_domain (LDAPDN dn,
                  const gchar *domain_dn_str,
                  const gchar *domain)
{
	LDAPDN domain_dn;
	gboolean ret;
	int rc;
	gint i, j;

	rc = ldap_str2dn (domain_dn_str, &domain_dn, LDAP_DN_FORMAT_LDAPV3);
	g_return_val_if_fail (rc == LDAP_SUCCESS, FALSE);

	for (i = 0; dn[i] != NULL && domain_dn[i] != NULL; i++) {
		for (j = 0; dn[i][j] != NULL && domain_dn[i][j] != NULL; j++) {
			if (!berval_case_equals (&(dn[i][j]->la_attr), &(domain_dn[i][j]->la_attr)) ||
			    !berval_case_equals (&(dn[i][j]->la_value), &(domain_dn[i][j]->la_value)))
				break;
		}

		if (dn[i][j] != NULL && domain_dn[i][j] != NULL)
			break;
	}

	/* Did we reach end of both DNs? */
	ret = (dn[i] == NULL && domain_dn[i] == NULL);

	ldap_dnfree (domain_dn);

	return ret;
}

gchar *
realm_dn_util_build_samba_ou (const gchar *ldap_dn,
                              const gchar *domain)
{
	gchar *domain_dn_str = NULL;
	GArray *parts;
	GString *part;
	gchar **strv;
	gchar *str;
	LDAPAVA* ava;
	gboolean ret;
	LDAPDN dn;
	int rc;
	gint i, j;

	/*
	 * Here we convert a standard LDAP DN to the strange samba net format,
	 * as "documented" here:
	 *
	 * createcomputer=OU  Precreate the computer account in a specific OU.
	 *                    The OU string read from top to bottom without RDNs and delimited by a '/'.
	 *                    E.g. "createcomputer=Computers/Servers/Unix"
	 *                    NB: A backslash '\' is used as escape at multiple levels and may
	 *                        need to be doubled or even quadrupled.  It is not used as a separator.
	 */

	/* ldap_str2dn doesn't like empty strings */
	while (g_ascii_isspace (ldap_dn[0]))
		ldap_dn++;
	if (g_str_equal (ldap_dn, ""))
		return NULL;

	rc = ldap_str2dn (ldap_dn, &dn, LDAP_DN_FORMAT_LDAPV3);
	if (rc != LDAP_SUCCESS)
		return NULL;

	ret = TRUE;
	parts = g_array_new (TRUE, TRUE, sizeof (gchar *));

	for (i = 0; dn[i] != NULL; i++) {
		ava = dn[i][0];

		/*
		 * Make sure this is a valid DN, we only support one value per
		 * RDN, string values, and must be an OU. DC values are allowed
		 * but only at the end of the DN.
		 */

		if (ava == NULL || dn[i][1] != NULL || !(ava->la_flags & LDAP_AVA_STRING)) {
			ret = FALSE;
			break;

		/* A DC, remainder must match the domain */
		} else if (berval_is_string (&ava->la_attr, "DC", 2)) {
			rc = ldap_domain2dn (domain, &domain_dn_str);
			if (rc != LDAP_SUCCESS)
				ret = FALSE;
			else
				ret = dn_equals_domain (dn + i, domain_dn_str, domain);
			break;

		/* An OU, include */
		} else if (berval_is_string (&ava->la_attr, "OU", 2)) {
			part = g_string_sized_new (ava->la_value.bv_len);
			for (j = 0; j < ava->la_value.bv_len; j++) {
				switch (ava->la_value.bv_val[j]) {
				case '\\':
					g_string_append (part, "\\\\");
					break;
				case '/':
					g_string_append (part, "\\/");
					break;
				default:
					g_string_append_c (part, ava->la_value.bv_val[j]);
					break;
				}
			}
			str = g_string_free (part, FALSE);
			g_array_insert_val (parts, 0, str);

		/* Invalid, stop */
		} else {
			ret = FALSE;
			break;
		}
	}

	ldap_dnfree (dn);
	if (domain_dn_str)
		ldap_memfree (domain_dn_str);

	strv = (gchar **)g_array_free (parts, FALSE);
	str = NULL;

	/* Loop completed successfully */
	if (ret)
		str = g_strjoinv ("/", strv);

	g_strfreev (strv);

	return str;
}

gchar *
realm_dn_util_build_qualified (const gchar *ldap_dn,
                               const gchar *domain)
{
	gchar *domain_dn_str = NULL;
	gboolean had_dc = FALSE;
	gchar *str;
	LDAPAVA* ava;
	gboolean ret;
	LDAPDN dn;
	int rc;
	gint i;

	/* ldap_str2dn doesn't like empty strings */
	while (g_ascii_isspace (ldap_dn[0]))
		ldap_dn++;
	if (g_str_equal (ldap_dn, ""))
		return NULL;

	rc = ldap_str2dn (ldap_dn, &dn, LDAP_DN_FORMAT_LDAPV3);
	if (rc != LDAP_SUCCESS)
		return NULL;

	rc = ldap_domain2dn (domain, &domain_dn_str);
	if (rc != LDAP_SUCCESS) {
		ldap_dnfree (dn);
		return NULL;
	}

	ret = TRUE;

	for (i = 0; dn[i] != NULL; i++) {
		ava = dn[i][0];

		/*
		 * Make sure this is a valid DN, we only support one value per
		 * RDN, string values. DC values are allowed but only at the end of the DN.
		 */

		if (ava == NULL || dn[i][1] != NULL || !(ava->la_flags & LDAP_AVA_STRING)) {
			ret = FALSE;
			break;

		/* A DC, remainder must match the domain */
		} else if (berval_is_string (&ava->la_attr, "DC", 2)) {
			had_dc = TRUE;
			ret = dn_equals_domain (dn + i, domain_dn_str, domain);
			break;
		}
	}

	ldap_dnfree (dn);

	if (!ret)
		return NULL;

	if (had_dc)
		str = g_strdup (ldap_dn);
	else
		str = g_strdup_printf ("%s,%s", ldap_dn, domain_dn_str);

	ldap_memfree (domain_dn_str);
	return str;
}
