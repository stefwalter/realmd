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
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;
	RealmIniConfig *config;
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

	/* Because of --print-details, we can parse the output */
	if (error == NULL) {
		config = realm_ini_config_new (REALM_INI_NONE);
		realm_ini_config_read_string (config, output->str);
		g_simple_async_result_set_op_res_gpointer (async, config, g_object_unref);

	} else {
		g_simple_async_result_take_error (async, error);

	}

	if (output)
		g_string_free (output, TRUE);
	g_simple_async_result_complete (async);
	g_object_unref (async);
}

void
realm_adcli_enroll_join_async (const gchar *realm,
                               RealmCredential *cred,
                               GVariant *options,
                               GDBusMethodInvocation *invocation,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	gchar *environ[] = { "LANG=C", NULL };
	const gchar *computer_ou;
	GSimpleAsyncResult *async;
	GBytes *input = NULL;
	GPtrArray *args;
	gchar *arg;

	g_return_if_fail (cred != NULL);
	g_return_if_fail (realm != NULL);
	g_return_if_fail (invocation != NULL);

	async = g_simple_async_result_new (NULL, callback, user_data,
	                                   realm_adcli_enroll_join_finish);

	args = g_ptr_array_new ();

	/* Use our custom smb.conf */
	g_ptr_array_add (args, (gpointer)realm_settings_path ("adcli"));
	g_ptr_array_add (args, "join");
	g_ptr_array_add (args, "--verbose");
	g_ptr_array_add (args, "--show-details");
	g_ptr_array_add (args, "--domain");
	g_ptr_array_add (args, (gpointer)realm);

	computer_ou = realm_options_computer_ou (options, realm);
	if (computer_ou) {
		g_ptr_array_add (args, "--computer-ou");
		g_ptr_array_add (args, (gpointer)computer_ou);
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
		arg = g_strdup_printf ("--login-ccache=%s", cred->x.ccache.file);
		g_ptr_array_add (args, arg);
		break;
	case REALM_CREDENTIAL_PASSWORD:
		input = realm_command_build_password_line (cred->x.password.value);
		g_ptr_array_add (args, "--login-type");
		g_ptr_array_add (args, "user");
		g_ptr_array_add (args, "--login-user");
		g_ptr_array_add (args, cred->x.password.name);
		break;
	case REALM_CREDENTIAL_SECRET:
		input = realm_command_build_password_line (cred->x.secret.value);
		g_ptr_array_add (args, "--login-type");
		g_ptr_array_add (args, "computer");
		g_ptr_array_add (args, "--stdin-password");
		break;
	}

	g_ptr_array_add (args, NULL);

	realm_command_runv_async ((gchar **)args->pdata, environ, input,
	                          invocation, on_join_process,
	                          g_object_ref (async));

	g_ptr_array_free (args, TRUE);
	g_object_unref (async);

	if (input)
		g_bytes_unref (input);
	free (arg);
}

gboolean
realm_adcli_enroll_join_finish (GAsyncResult *result,
                                gchar **workgroup,
                                GError **error)
{
	GSimpleAsyncResult *async;
	RealmIniConfig *config;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      realm_adcli_enroll_join_finish), FALSE);

	async = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (async, error))
		return FALSE;

	if (workgroup) {
		config = g_simple_async_result_get_op_res_gpointer (async);
		*workgroup = realm_ini_config_get (config, "domain", "domain-short");
	}

	return TRUE;
}
