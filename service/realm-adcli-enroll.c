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

static void
begin_join_process (GBytes *input,
                    GDBusMethodInvocation *invocation,
                    GAsyncReadyCallback callback,
                    gpointer user_data,
                    ...) G_GNUC_NULL_TERMINATED;

static void
begin_join_process (GBytes *input,
                    GDBusMethodInvocation *invocation,
                    GAsyncReadyCallback callback,
                    gpointer user_data,
                    ...)
{
	GSimpleAsyncResult *async;
	gchar **environ;
	GPtrArray *args;
	gchar *arg;
	va_list va;

	async = g_simple_async_result_new (NULL, callback, user_data,
	                                   realm_adcli_enroll_join_finish);

	args = g_ptr_array_new ();

	/* Use our custom smb.conf */
	g_ptr_array_add (args, (gpointer)realm_settings_path ("adcli"));
	g_ptr_array_add (args, "join");
	g_ptr_array_add (args, "--verbose");
	g_ptr_array_add (args, "--show-details");

	va_start (va, user_data);
	do {
		arg = va_arg (va, gchar *);
		g_ptr_array_add (args, arg);
	} while (arg != NULL);
	va_end (va);

	g_ptr_array_add (args, NULL);
	environ = g_environ_setenv (g_get_environ (), "LANG", "C", TRUE);

	realm_command_runv_async ((gchar **)args->pdata, environ, input,
	                          invocation, NULL, on_join_process,
	                          g_object_ref (async));

	g_strfreev (environ);
	g_ptr_array_free (args, TRUE);
	g_object_unref (async);
}

void
realm_adcli_enroll_join_ccache_async (const gchar *realm,
                                      const gchar *ccache_file,
                                      const gchar *computer_ou,
                                      GDBusMethodInvocation *invocation,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
	begin_join_process (NULL, invocation, callback, user_data,
	                    "--domain", realm,
	                    "--login-type", "user",
	                    "--login-ccache", ccache_file,
	                    "--no-password",
	                    computer_ou ? "--computer-ou": NULL, computer_ou,
	                    NULL);
}

void
realm_adcli_enroll_join_automatic_async (const gchar *realm,
                                         const gchar *computer_ou,
                                         GDBusMethodInvocation *invocation,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
	begin_join_process (NULL, invocation, callback, user_data,
	                    "--domain", realm,
	                    "--login-type", "computer",
	                    "--no-password",
	                    computer_ou ? "--computer-ou": NULL, computer_ou,
	                    NULL);
}

void
realm_adcli_enroll_join_otp_async (const gchar *realm,
                                   GBytes *secret,
                                   const gchar *computer_ou,
                                   GDBusMethodInvocation *invocation,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
	begin_join_process (secret, invocation, callback, user_data,
	                    "--domain", realm,
	                    "--login-type", "computer",
	                    "--stdin-password",
	                    computer_ou ? "--computer-ou": NULL, computer_ou,
	                    NULL);
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
