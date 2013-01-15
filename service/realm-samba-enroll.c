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

#include "realm-command.h"
#include "realm-daemon.h"
#include "realm-diagnostics.h"
#include "realm-errors.h"
#include "realm-samba-config.h"
#include "realm-samba-enroll.h"
#include "realm-samba-provider.h"
#include "realm-samba-util.h"
#include "realm-settings.h"

#include <glib/gstdio.h>

#include <ldap.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>

typedef struct {
	GDBusMethodInvocation *invocation;
	gchar *create_computer_arg;
	GHashTable *settings;
	gchar *realm;
	gchar *user_name;
	GBytes *password_input;
} JoinClosure;

static void
clear_and_free_password (gpointer data)
{
	gchar *password = data;
	memset ((char *)password, 0, strlen (password));
	g_free (password);
}

static void
join_closure_free (gpointer data)
{
	JoinClosure *join = data;

	g_bytes_unref (join->password_input);
	g_free (join->user_name);
	g_free (join->create_computer_arg);
	g_free (join->realm);
	if (join->settings)
		g_hash_table_unref (join->settings);
	g_clear_object (&join->invocation);

	g_slice_free (JoinClosure, join);
}

static JoinClosure *
join_closure_init (const gchar *realm,
                   const gchar *user_name,
                   GBytes *password,
                   GDBusMethodInvocation *invocation)
{
	JoinClosure *join;
	GByteArray *array;
	const guchar *data;
	guchar *input;
	gsize length;

	join = g_slice_new0 (JoinClosure);
	join->realm = g_strdup (realm);
	join->invocation = invocation ? g_object_ref (invocation) : NULL;

	if (password) {
		array = g_byte_array_new ();
		data = g_bytes_get_data (password, &length);
		g_byte_array_append (array, data, length);

		/*
		 * We add a new line, which getpass() used inside net
		 * command expects
		 */
		g_byte_array_append (array, (guchar *)"\n", 1);
		length = array->len;

		/*
		 * In addition we add null terminator. This is not
		 * written to 'net' command, but used by clear_and_free_password().
		 */
		g_byte_array_append (array, (guchar *)"\0", 1);

		input = g_byte_array_free (array, FALSE);
		join->password_input = g_bytes_new_with_free_func (input, length,
		                                                   clear_and_free_password, input);
	}

	join->user_name = g_strdup (user_name);

	return join;
}

static void
begin_net_process (JoinClosure *join,
                   GBytes *input,
                   GAsyncReadyCallback callback,
                   gpointer user_data,
                   ...) G_GNUC_NULL_TERMINATED;

static void
begin_net_process (JoinClosure *join,
                   GBytes *input,
                   GAsyncReadyCallback callback,
                   gpointer user_data,
                   ...)
{
	gchar *environ[] = { "LANG=C", NULL };
	GPtrArray *args;
	gchar *arg;
	va_list va;

	args = g_ptr_array_new ();

	/* Use our custom smb.conf */
	g_ptr_array_add (args, (gpointer)realm_settings_path ("net"));
	g_ptr_array_add (args, "-s");
	g_ptr_array_add (args, PRIVATE_DIR "/net-ads-smb.conf");

	va_start (va, user_data);
	do {
		arg = va_arg (va, gchar *);
		g_ptr_array_add (args, arg);
	} while (arg != NULL);
	va_end (va);

	realm_command_runv_async ((gchar **)args->pdata, environ, input,
	                          join->invocation, callback, user_data);

	g_ptr_array_free (args, TRUE);
}

static void
on_list_complete (GObject *source,
                  GAsyncResult *result,
                  gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (res);
	GString *output = NULL;
	RealmIniConfig *config;
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, &output, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Listing samba registry failed");

	if (error == NULL) {
		/* Read the command output as a samba config */
		config = realm_ini_config_new (REALM_INI_LINE_CONTINUATIONS);
		realm_ini_config_read_string (config, output->str);
		join->settings = realm_ini_config_get_all (config, REALM_SAMBA_CONFIG_GLOBAL);
		g_hash_table_insert (join->settings,
		                     g_strdup ("kerberos method"),
		                     g_strdup ("secrets and keytab"));

		g_object_unref (config);
	}

	if (error != NULL)
		g_simple_async_result_take_error (res, error);

	if (output)
		g_string_free (output, TRUE);
	g_simple_async_result_complete (res);
	g_object_unref (res);
}

static void
on_keytab_do_list (GObject *source,
                   GAsyncResult *result,
                   gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Extracting host keytab failed");

	/*
	 * So at this point we're done joining, and want to get some settings
	 * that the net process wrote to the registry, and put them in the
	 * main smb.conf
	 */
	if (error == NULL) {
		begin_net_process (join, NULL,
		                   on_list_complete, g_object_ref (res),
		                   "conf", "list", NULL);

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
on_join_do_keytab (GObject *source,
                   GAsyncResult *result,
                   gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	GString *output = NULL;
	gint status;

	status = realm_command_run_finish (result, &output, &error);
	if (error == NULL && status != 0) {

		/*
		 * This is bad and ugly. We run the process with LC_ALL=C so
		 * at least we know these messages will be in english.
		 *
		 * At first I thought this was a deficiency in samba's 'net'
		 * command. It's true that 'net' could be better at returning
		 * different error codes for different types of failures.
		 *
		 * But in the end this is a deficiency in Windows. When you use
		 * LDAP to do enrollment, and the permissions aren't correct
		 * it often returns stupid errors such as 'Constraint violation'
		 * or 'Object class invalid' instead of 'Insufficient access'.
		 */
		if (g_pattern_match_simple ("*NT_STATUS_ACCESS_DENIED*", output->str) ||
		    g_pattern_match_simple ("*failed*: Constraint violation*", output->str) ||
		    g_pattern_match_simple ("*failed*: Object class violation*", output->str) ||
		    g_pattern_match_simple ("*failed*: Insufficient access*", output->str) ||
		    g_pattern_match_simple ("*: Access denied*", output->str) ||
		    g_pattern_match_simple ("*not have administrator privileges*", output->str)) {
			g_set_error (&error, REALM_ERROR, REALM_ERROR_AUTH_FAILED,
			             "Insufficient permissions to join the domain %s",
			             join->realm);
		} else {
			g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
			             "Joining the domain %s failed", join->realm);
		}
	}

	if (output)
		g_string_free (output, TRUE);

	if (error == NULL) {
		begin_net_process (join, join->password_input,
		                   on_keytab_do_list, g_object_ref (res),
		                   "-U", join->user_name, "ads", "keytab", "create", NULL);
	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
on_conf_kerberos_method_do_join (GObject *source,
                 GAsyncResult *result,
                 gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (error == NULL && status != 0) {
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Configuring samba failed");
	}

	if (error == NULL) {
		begin_net_process (join, join->password_input,
		                   on_join_do_keytab, g_object_ref (res),
		                   "-U", join->user_name, "ads", "join", join->realm,
		                   join->create_computer_arg, NULL);

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
on_conf_realm_do_kerberos_method (GObject *source,
                                  GAsyncResult *result,
                                  gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (error == NULL && status != 0) {
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Configuring samba failed");
	}

	if (error == NULL) {
		begin_net_process (join, NULL,
		                   on_conf_kerberos_method_do_join, g_object_ref (res),
		                   "conf", "setparm", REALM_SAMBA_CONFIG_GLOBAL,
		                   "kerberos method", "system keytab", NULL);
	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

void
realm_samba_enroll_join_async (const gchar *realm,
                               const gchar *user_name,
                               GBytes *password,
                               const gchar *computer_ou,
                               GDBusMethodInvocation *invocation,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	GSimpleAsyncResult *res;
	JoinClosure *join;
	GError *error = NULL;
	gchar *strange_ou;

	g_return_if_fail (realm != NULL);
	g_return_if_fail (user_name != NULL);
	g_return_if_fail (password != NULL);

	res = g_simple_async_result_new (NULL, callback, user_data,
	                                 realm_samba_enroll_join_async);

	join = join_closure_init (realm, user_name, password, invocation);

	g_simple_async_result_set_op_res_gpointer (res, join, join_closure_free);

	if (computer_ou != NULL) {
		strange_ou = realm_samba_util_build_strange_ou (computer_ou, realm);
		if (strange_ou) {
			if (!g_str_equal (strange_ou, ""))
				join->create_computer_arg = g_strdup_printf ("createcomputer=%s", strange_ou);
			g_free (strange_ou);
		} else {
			g_set_error (&error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
			             "The computer-ou argument must be a valid LDAP DN and contain only OU=xxx RDN values.");
		}
	}

	if (error != NULL) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete_in_idle (res);
	} else {
		begin_net_process (join, NULL,
		                   on_conf_realm_do_kerberos_method, g_object_ref (res),
		                   "conf", "setparm", REALM_SAMBA_CONFIG_GLOBAL,
		                   "realm", join->realm, NULL);
	}

	g_object_unref (res);
}

gboolean
realm_samba_enroll_join_finish (GAsyncResult *result,
                                GHashTable **settings,
                                GError **error)
{
	JoinClosure *join;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      realm_samba_enroll_join_async), FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	if (settings != NULL) {
		join = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
		if (join->settings)
			*settings = g_hash_table_ref (join->settings);
		else
			*settings = NULL;
	}

	return TRUE;
}

static void
on_leave_complete (GObject *source,
                   GAsyncResult *result,
                   gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Leaving the domain %s failed", join->realm);

	if (error != NULL)
		g_simple_async_result_take_error (res, error);

	g_simple_async_result_complete (res);
	g_object_unref (res);
}

void
realm_samba_enroll_leave_async (const gchar *realm,
                                const gchar *user_name,
                                GBytes *password,
                                GDBusMethodInvocation *invocation,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	GSimpleAsyncResult *async;
	JoinClosure *join;

	async = g_simple_async_result_new (NULL, callback, user_data,
	                                   realm_samba_enroll_leave_async);

	join = join_closure_init (realm, user_name, password, invocation);
	g_simple_async_result_set_op_res_gpointer (async, join, join_closure_free);

	begin_net_process (join, join->password_input,
	                   on_leave_complete, g_object_ref (async),
	                   "-U", join->user_name, "ads", "leave", NULL);

	g_object_unref (async);
}

gboolean
realm_samba_enroll_leave_finish (GAsyncResult *result,
                                 GError **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      realm_samba_enroll_leave_async), FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;
	return TRUE;
}
