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
#include "realm-dbus-constants.h"
#include "realm-diagnostics.h"
#include "realm-discovery.h"
#include "realm-errors.h"
#include "realm-kerberos-discover.h"
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
	gchar *realm;
	gchar *user_name;
	GBytes *password_input;
	RealmIniConfig *config;
	gchar *custom_smb_conf;
} JoinClosure;

static void
join_closure_free (gpointer data)
{
	JoinClosure *join = data;

	g_bytes_unref (join->password_input);
	g_free (join->user_name);
	g_free (join->create_computer_arg);
	g_free (join->realm);
	g_clear_object (&join->invocation);
	g_clear_object (&join->config);

	if (join->custom_smb_conf) {
		g_unlink (join->custom_smb_conf);
		g_free (join->custom_smb_conf);
	}

	g_slice_free (JoinClosure, join);
}

static JoinClosure *
join_closure_init (const gchar *realm,
                   const gchar *user_name,
                   GBytes *password,
                   GDBusMethodInvocation *invocation)
{
	JoinClosure *join;
	GError *error = NULL;
	int temp_fd;

	join = g_slice_new0 (JoinClosure);
	join->realm = g_strdup (realm);
	join->invocation = invocation ? g_object_ref (invocation) : NULL;

	if (password)
		join->password_input = realm_command_build_password_line (password);

	join->user_name = g_strdup (user_name);

	join->config = realm_ini_config_new (REALM_INI_NO_WATCH | REALM_INI_PRIVATE);
	realm_ini_config_set (join->config, REALM_SAMBA_CONFIG_GLOBAL, "security", "ads");
	realm_ini_config_set (join->config, REALM_SAMBA_CONFIG_GLOBAL, "kerberos method", "system keytab");
	realm_ini_config_set (join->config, REALM_SAMBA_CONFIG_GLOBAL, "realm", join->realm);

	/* Write out the config file for use by various net commands */
	join->custom_smb_conf = g_build_filename (g_get_tmp_dir (), "realmd-smb-conf.XXXXXX", NULL);
	temp_fd = g_mkstemp_full (join->custom_smb_conf, O_WRONLY, S_IRUSR | S_IWUSR);
	if (temp_fd != -1) {
		if (realm_ini_config_write_fd (join->config, temp_fd, &error)) {
			realm_ini_config_set_filename (join->config, join->custom_smb_conf);

		} else {
			g_warning ("couldn't write to a temp file: %s: %s", join->custom_smb_conf, error->message);
			g_error_free (error);
		}

		close (temp_fd);
	}

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
	if (join->custom_smb_conf) {
		g_ptr_array_add (args, "-s");
		g_ptr_array_add (args, join->custom_smb_conf);
	}

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
on_keytab_do_finish (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;
	gint status;

	status = realm_command_run_finish (result, NULL, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Extracting host keytab failed");

	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	g_simple_async_result_complete (res);
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
		    g_pattern_match_simple ("*not have administrator privileges*", output->str) ||
		    g_pattern_match_simple ("*failure*: *not been granted the requested logon type*", output->str) ||
		    g_pattern_match_simple ("*failure*: User not allowed to log on to this computer*", output->str) ||
		    g_pattern_match_simple ("*failure*: *specified account is not allowed to authenticate to the machine*", output->str)) {
			g_set_error (&error, REALM_ERROR, REALM_ERROR_AUTH_FAILED,
			             "Insufficient permissions to join the domain %s",
			             join->realm);
		} else if (g_pattern_match_simple ("*: Logon failure*", output->str) ||
		           g_pattern_match_simple ("*: Password expired*", output->str)) {
			g_set_error (&error, REALM_ERROR, REALM_ERROR_AUTH_FAILED,
			             "The %s account, password, or credentials are invalid",
			             join->user_name);
		} else {
			g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
			             "Joining the domain %s failed", join->realm);
		}
	}

	if (output)
		g_string_free (output, TRUE);

	if (error == NULL) {
		begin_net_process (join, join->password_input,
		                   on_keytab_do_finish, g_object_ref (res),
		                   "-U", join->user_name, "ads", "keytab", "create", NULL);
	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static gchar *
fallback_workgroup (const gchar *realm)
{
	const gchar *pos;

	pos = strchr (realm, '.');
	if (pos == NULL)
		return g_utf8_strup (realm, -1);
	else
		return g_utf8_strup (realm, pos - realm);
}

static void
begin_config_and_join (JoinClosure *join,
                       GSimpleAsyncResult *async)
{
	GError *error = NULL;
	gchar *workgroup;

	/*
	 * Samba complains if we don't set a 'workgroup' setting for the realm we're
	 * going to join. If we didn't yet manage to lookup the workgroup, then go ahead
	 * and assume that the first domain component is the workgroup name.
	 */
	workgroup = realm_ini_config_get (join->config, REALM_SAMBA_CONFIG_GLOBAL, "workgroup");
	if (workgroup == NULL) {
		workgroup = fallback_workgroup (join->realm);
		realm_diagnostics_info (join->invocation, "Calculated workgroup name: %s", workgroup);
		realm_ini_config_set (join->config, REALM_SAMBA_CONFIG_GLOBAL, "workgroup", workgroup);
	}
	free (workgroup);

	/* Write out the config file for various changes */
	realm_ini_config_write_file (join->config, NULL, &error);

	if (error == NULL) {
		begin_net_process (join, join->password_input,
		                   on_join_do_keytab, g_object_ref (async),
		                   "-U", join->user_name, "ads", "join", join->realm,
		                   join->create_computer_arg, NULL);

	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

}

static gchar *
find_workgroup_in_output (GString *output)
{
	const gchar *match = ":";
	const gchar *pos;
	const gchar *end;
	gchar *workgroup;

	/* Beginning */
	pos = g_strstr_len (output->str, output->len, match);
	if (pos == NULL)
		return NULL;
	pos += strlen (match);

	/* Find the end */
	end = strchr (pos, '\n');
	if (end == NULL)
		end = output->str + output->len;

	workgroup = g_strndup (pos, end - pos);
	g_strstrip (workgroup);
	return workgroup;
}

static void
on_net_ads_workgroup (GObject *source,
                      GAsyncResult *result,
                      gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (async);
	GError *error = NULL;
	GString *output = NULL;
	gchar *workgroup;
	gint status;

	status = realm_command_run_finish (result, &output, &error);
	if (error == NULL && status != 0) {
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Couldn't lookup domain info");
	}

	if (error == NULL) {
		workgroup = find_workgroup_in_output (output);
		if (workgroup) {
			realm_diagnostics_info (join->invocation, "Looked up workgroup name: %s", workgroup);
			realm_ini_config_set (join->config, REALM_SAMBA_CONFIG_GLOBAL, "workgroup", workgroup);
			g_free (workgroup);
		}

		g_string_free (output, TRUE);

	} else {
		realm_diagnostics_error (join->invocation, error, NULL);
		g_error_free (error);
	}

	begin_config_and_join (join, async);

	g_object_unref (async);
}


static void
begin_net_lookup (JoinClosure *join,
                  GSimpleAsyncResult *async,
                  GHashTable *discovery)
{
	const gchar **kdcs;

	kdcs = realm_discovery_get_strings (discovery, REALM_DBUS_DISCOVERY_KDCS);

	/* If we discovered KDCs then try to ask first one what the workgroup name is */
	if (kdcs && kdcs[0]) {
		begin_net_process (join, NULL,
		                   on_net_ads_workgroup, g_object_ref (async),
		                   "ads", "workgroup", "-S", kdcs[0], NULL);

	} else {
		begin_config_and_join (join, async);
	}
}

static void
on_discover_do_lookup (GObject *source,
                       GAsyncResult *result,
                       gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (async);
	GError *error = NULL;
	GHashTable *discovery;

	realm_kerberos_discover_finish (result, &discovery, &error);
	if (error == NULL) {
		begin_net_lookup (join, async, discovery);
		g_hash_table_unref (discovery);

	} else {
		g_simple_async_result_take_error (async, error);
		g_simple_async_result_complete (async);
	}

	g_object_unref (async);
}

void
realm_samba_enroll_join_async (const gchar *realm,
                               const gchar *user_name,
                               GBytes *password,
                               const gchar *computer_ou,
                               GHashTable *discovery,
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

	} else if (discovery) {
		begin_net_lookup (join, res, discovery);

	} else {
		realm_kerberos_discover_async (join->realm, join->invocation,
		                               on_discover_do_lookup, g_object_ref (res));
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
		*settings = realm_ini_config_get_all (join->config, REALM_SAMBA_CONFIG_GLOBAL);
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
