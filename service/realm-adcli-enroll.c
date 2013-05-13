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

#include "egg-task.h"
#include "realm-adcli-enroll.h"
#include "realm-command.h"
#include "realm-daemon.h"
#include "realm-diagnostics.h"
#include "realm-errors.h"
#include "realm-ini-config.h"
#include "realm-options.h"
#include "realm-settings.h"

static void
on_join_process (GObject *source,
                 GAsyncResult *result,
                 gpointer user_data)
{
	EggTask *task = EGG_TASK (user_data);
	GError *error = NULL;
	GString *output = NULL;
	gint status;

	status = realm_command_run_finish (result, &output, &error);
	if (error == NULL && status != 0) {
		switch (status) {
		case 2: /* ADCLI_ERR_UNEXPECTED */
			g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
			             "Internal unexpected error joining the domain");
			break;
		case 6: /* ADCLI_ERR_CREDENTIALS */
			g_set_error (&error, REALM_ERROR, REALM_ERROR_AUTH_FAILED,
			             "Insufficient permissions to join the domain");
			break;
		default:
			g_set_error (&error, REALM_ERROR, REALM_ERROR_FAILED,
			             "Failed to join the domain");
			break;
		}
	}

	if (error == NULL) {
		egg_task_return_boolean (task, TRUE);

	} else {
		egg_task_return_error (task, error);
	}

	if (output)
		g_string_free (output, TRUE);
	g_object_unref (task);
}

void
realm_adcli_enroll_join_async (RealmDisco *disco,
                               RealmCredential *cred,
                               GVariant *options,
                               GDBusMethodInvocation *invocation,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	gchar *environ[] = { "LANG=C", NULL };
	const gchar *computer_ou;
	EggTask *task;
	GBytes *input = NULL;
	const gchar *upn;
	GPtrArray *args;
	const gchar *os;
	gchar *ccache_arg = NULL;
	gchar *upn_arg = NULL;

	g_return_if_fail (cred != NULL);
	g_return_if_fail (disco != NULL);
	g_return_if_fail (invocation != NULL);

	task = egg_task_new (NULL, NULL, callback, user_data);
	args = g_ptr_array_new ();

	/* Use our custom smb.conf */
	g_ptr_array_add (args, (gpointer)realm_settings_path ("adcli"));
	g_ptr_array_add (args, "join");
	g_ptr_array_add (args, "--verbose");
	g_ptr_array_add (args, "--domain");
	g_ptr_array_add (args, (gpointer)disco->domain_name);
	g_ptr_array_add (args, "--domain-realm");
	g_ptr_array_add (args, (gpointer)disco->kerberos_realm);

	if (disco->explicit_server) {
		g_ptr_array_add (args, "--domain-controller");
		g_ptr_array_add (args, (gpointer)disco->explicit_server);
	}

	computer_ou = realm_options_computer_ou (options, disco->domain_name);
	if (computer_ou) {
		g_ptr_array_add (args, "--computer-ou");
		g_ptr_array_add (args, (gpointer)computer_ou);
	}

	os = realm_settings_value ("active-directory", "os-name");
	if (os != NULL && !g_str_equal (os, "")) {
		g_ptr_array_add (args, "--os-name");
		g_ptr_array_add (args, (gpointer)os);
	}

	os = realm_settings_value ("active-directory", "os-version");
	if (os != NULL && !g_str_equal (os, "")) {
		g_ptr_array_add (args, "--os-version");
		g_ptr_array_add (args, (gpointer)os);
	}

	switch (cred->type) {
	case REALM_CREDENTIAL_AUTOMATIC:
		g_ptr_array_add (args, "--login-type");
		g_ptr_array_add (args, "computer");
		g_ptr_array_add (args, "--no-password");
		break;
	case REALM_CREDENTIAL_CCACHE:
		g_ptr_array_add (args, "--login-type");
		g_ptr_array_add (args, "user");
		ccache_arg = g_strdup_printf ("--login-ccache=%s", cred->x.ccache.file);
		g_ptr_array_add (args, ccache_arg);
		break;
	case REALM_CREDENTIAL_PASSWORD:
		input = g_bytes_ref (cred->x.password.value);
		g_ptr_array_add (args, "--login-type");
		g_ptr_array_add (args, "user");
		g_ptr_array_add (args, "--login-user");
		g_ptr_array_add (args, cred->x.password.name);
		g_ptr_array_add (args, "--stdin-password");
		break;
	case REALM_CREDENTIAL_SECRET:
		input = g_bytes_ref (cred->x.secret.value);
		g_ptr_array_add (args, "--login-type");
		g_ptr_array_add (args, "computer");
		g_ptr_array_add (args, "--stdin-password");
		break;
	}

	upn = realm_options_user_principal (options, disco->domain_name);
	if (upn) {
		if (g_str_equal (upn, "")) {
			g_ptr_array_add (args, "--user-principal");
		} else {
			upn_arg = g_strdup_printf ("--user-principal=%s", upn);
			g_ptr_array_add (args, upn_arg);
		}
	}

	g_ptr_array_add (args, NULL);

	realm_command_runv_async ((gchar **)args->pdata, environ, input,
	                          invocation, on_join_process,
	                          g_object_ref (task));

	g_ptr_array_free (args, TRUE);
	g_object_unref (task);

	if (input)
		g_bytes_unref (input);

	free (ccache_arg);
	free (upn_arg);
}

gboolean
realm_adcli_enroll_join_finish (GAsyncResult *result,
                                GError **error)
{
	g_return_val_if_fail (egg_task_is_valid (result, NULL), FALSE);
	return egg_task_propagate_boolean (EGG_TASK (result), error);
}
