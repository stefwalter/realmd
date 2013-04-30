/* realmd -- Realm configuration service
 *
 * Copyright 2013 Red Hat Inc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 *
 * Author: Stef Walter <stefw@redhat.com>
 */

#include "config.h"

#include "egg-task.h"
#include "realm-dbus-constants.h"
#include "realm-disco-rootdse.h"
#include "realm-ldap.h"

#include <glib/gi18n.h>

#include <resolv.h>

typedef struct _Closure Closure;

struct _Closure {
	RealmDisco *disco;
	GSource *source;

	gchar *default_naming_context;
	gint msgid;

	gboolean (* request) (EggTask *task,
	                      Closure *clo,
	                      LDAP *ldap);

	gboolean (* result) (EggTask *task,
	                     Closure *clo,
	                     LDAP *ldap,
	                     LDAPMessage *msg);
};

static void
closure_free (gpointer data)
{
	Closure *clo = data;

	ldap_memfree (clo->default_naming_context);

	g_source_destroy (clo->source);
	g_source_unref (clo->source);
	realm_disco_unref (clo->disco);
	g_slice_free (Closure, clo);
}

static gboolean
search_ldap (EggTask *task,
             Closure *clo,
             LDAP *ldap,
             const gchar *base,
             int scope,
             const char *filter,
             const char **attrs)
{
	GError *error = NULL;
	int rc;

	if (!filter)
		filter = "(objectClass=*)";

	g_debug ("Searching %s for %s", base, filter);
	rc = ldap_search_ext (ldap, base, scope, filter,
	                      (char **)attrs, 0, NULL, NULL, NULL, -1, &clo->msgid);

	if (rc != 0) {
		realm_ldap_set_error (&error, ldap, rc);
		egg_task_return_error (task, error);
		return FALSE;
	}

	return TRUE;
}

static gboolean
result_krb_realm (EggTask *task,
                  Closure *clo,
                  LDAP *ldap,
                  LDAPMessage *message)
{
	struct berval **bvs = NULL;
	LDAPMessage *entry;

	entry = ldap_first_entry (ldap, message);

	g_free (clo->disco->kerberos_realm);
	clo->disco->kerberos_realm = NULL;

	if (entry != NULL)
		bvs = ldap_get_values_len (ldap, entry, "cn");
	if (bvs && bvs[0])
		clo->disco->kerberos_realm = g_strndup (bvs[0]->bv_val, bvs[0]->bv_len);
	ldap_value_free_len (bvs);

	g_debug ("Found realm: %s", clo->disco->kerberos_realm);

	/* All done */
	egg_task_return_boolean (task, TRUE);
	return FALSE;
}

static gboolean
request_krb_realm (EggTask *task,
                   Closure *clo,
                   LDAP *ldap)
{
	const char *attrs[] = { "cn", NULL };

	clo->request = NULL;
	clo->result = result_krb_realm;

	return search_ldap (task, clo, ldap, clo->default_naming_context,
	                    LDAP_SCOPE_SUB, "(objectClass=krbRealmContainer)", attrs);
}

static gboolean
result_domain_info (EggTask *task,
                    Closure *clo,
                    LDAP *ldap,
                    LDAPMessage *message)
{
	LDAPMessage *entry;
	struct berval **bvs;

	entry = ldap_first_entry (ldap, message);

	/* If we can't retrieve this, then nothing more to do */
	if (entry == NULL) {
		g_debug ("Couldn't read default naming context");
		egg_task_return_new_error (task, REALM_LDAP_ERROR, LDAP_NO_SUCH_OBJECT,
		                           "Couldn't lookup domain name on LDAP server");
		return FALSE;
	}

	/* What kind of server is it? */
	clo->disco->server_software = NULL;
	bvs = ldap_get_values_len (ldap, entry, "info");
	if (bvs && bvs[0] && bvs[0]->bv_len >= 3) {
		if (g_ascii_strncasecmp (bvs[0]->bv_val, "IPA", 3) == 0)
			clo->disco->server_software = REALM_DBUS_IDENTIFIER_FREEIPA;
	}
	ldap_value_free_len (bvs);

	if (clo->disco->server_software)
		g_debug ("Got server software: %s", clo->disco->server_software);

	/* What is the domain name? */
	g_free (clo->disco->domain_name);
	clo->disco->domain_name = NULL;
	bvs = ldap_get_values_len (ldap, entry, "associatedDomain");
	if (bvs && bvs[0])
		clo->disco->domain_name = g_strndup (bvs[0]->bv_val, bvs[0]->bv_len);
	ldap_value_free_len (bvs);

	g_debug ("Got associatedDomain: %s", clo->disco->domain_name);

	/* Next search for Kerberos container */
	clo->request = request_krb_realm;
	clo->result = NULL;
	return TRUE;
}

static gboolean
request_domain_info (EggTask *task,
                     Closure *clo,
                     LDAP *ldap)
{
	const char *attrs[] = { "info", "associatedDomain", NULL };

	clo->request = NULL;
	clo->result = result_domain_info;

	return search_ldap (task, clo, ldap, clo->default_naming_context,
	                    LDAP_SCOPE_BASE, NULL, attrs);
}

static gboolean
result_root_dse (EggTask *task,
                 Closure *clo,
                 LDAP *ldap,
                 LDAPMessage *message)
{
	struct berval **bvs = NULL;
	LDAPMessage *entry;
	LDAPDN dn;

	entry = ldap_first_entry (ldap, message);

	/* Parse out the default naming context */
	if (entry != NULL)
		bvs = ldap_get_values_len (ldap, entry, "defaultNamingContext");
	if (!bvs || !bvs[0] || ldap_bv2dn (bvs[0], &dn, 0) != 0)
		dn = NULL;
	ldap_value_free_len (bvs);

	if (dn == NULL) {
		egg_task_return_new_error (task, REALM_LDAP_ERROR, LDAP_NO_SUCH_OBJECT,
		                           "Couldn't find default naming context on LDAP server");
		return FALSE;
	}

	ldap_memfree (clo->default_naming_context);
	if (ldap_dn2str (dn, &clo->default_naming_context, LDAP_DN_FORMAT_LDAPV3) != 0)
		g_return_val_if_reached (FALSE);
	ldap_dnfree (dn);

	g_debug ("Got defaultNamingContext: %s", clo->default_naming_context);

	/* Next search for IPA field */
	clo->request = request_domain_info;
	clo->result = NULL;
	return TRUE;
}

static gboolean
request_root_dse (EggTask *task,
                  Closure *clo,
                  LDAP *ldap)
{
	const char *attrs[] = { "defaultNamingContext", NULL };

	clo->request = NULL;
	clo->result = result_root_dse;

	return search_ldap (task, clo, ldap, "", LDAP_SCOPE_BASE, NULL, attrs);
}

static GIOCondition
on_ldap_io (LDAP *ldap,
            GIOCondition cond,
            gpointer user_data)
{
	EggTask *task = EGG_TASK (user_data);
	Closure *clo = egg_task_get_task_data (task);
	struct timeval tvpoll = { 0, 0 };
	LDAPMessage *message;
	GError *error = NULL;
	gboolean ret;

	/* Some failure */
	if (cond & G_IO_ERR) {
		realm_ldap_set_error (&error, ldap, 0);
		egg_task_return_error (task, error);
		return G_IO_NVAL;
	}

	/* Ready to get a result */
	if (cond & G_IO_IN && clo->result != NULL) {
		switch (ldap_result (ldap, clo->msgid, 0, &tvpoll, &message)) {
		case LDAP_RES_INTERMEDIATE:
		case LDAP_RES_SEARCH_REFERENCE:
			ret = TRUE;
			break;
		case -1:
			realm_ldap_set_error (&error, ldap, -1);
			egg_task_return_error (task, error);
			ret = FALSE;
			break;
		case 0:
			ret = TRUE;
			break;
		default:
			ret = clo->result (task, clo, ldap, message);
			ldap_msgfree (message);
			break;
		}

		if (!ret)
			return G_IO_NVAL;
	}

	if (cond & G_IO_OUT && clo->request != NULL) {
		if (!(clo->request) (task, clo, ldap))
			return G_IO_NVAL;
	}

	return (clo->request ? G_IO_OUT : 0) |
	       (clo->result ? G_IO_IN : 0);
}

void
realm_disco_rootdse_async (GSocketAddress *address,
                           const gchar *explicit_server,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	EggTask *task;
	Closure *clo;

	g_return_if_fail (address != NULL);

	task = egg_task_new (NULL, cancellable, callback, user_data);
	clo = g_slice_new0 (Closure);
	clo->disco = realm_disco_new (NULL);
	clo->disco->explicit_server = g_strdup (explicit_server);
	clo->request = request_root_dse;
	egg_task_set_task_data (task, clo, closure_free);

	clo->source = realm_ldap_connect_anonymous (address, G_SOCKET_PROTOCOL_TCP,
	                                            cancellable);
	g_source_set_callback (clo->source, (GSourceFunc)on_ldap_io,
	                       g_object_ref (task), g_object_unref);
	g_source_attach (clo->source, egg_task_get_context (task));

	g_object_unref (task);
}

RealmDisco *
realm_disco_rootdse_finish (GAsyncResult *result,
                            GError **error)
{
	Closure *clo;
	RealmDisco *disco;

	g_return_val_if_fail (egg_task_is_valid (result, NULL), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!egg_task_propagate_boolean (EGG_TASK (result), error))
		return FALSE;

	clo = egg_task_get_task_data (EGG_TASK (result));
	disco = clo->disco;
	clo->disco = NULL;

	/* Should have been set above */
	g_return_val_if_fail (disco->domain_name, NULL);

	if (!disco->kerberos_realm)
		disco->kerberos_realm = g_ascii_strup (disco->domain_name, -1);

	return disco;
}