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

#include "realm-ad-enroll.h"
#include "realm-diagnostics.h"
#include "realm-errors.h"
#include "realm-command.h"

#include <glib/gstdio.h>

#include <errno.h>

typedef struct {
	GCancellable *cancellable;
	GDBusMethodInvocation *invocation;
	gchar *kerberos_cache_filename;
	gchar **environ;
	gchar *domain;
} JoinClosure;

static void
join_closure_free (gpointer data)
{
	JoinClosure *join = data;

	g_clear_object (&join->cancellable);

	if (join->kerberos_cache_filename) {
		if (!g_unlink (join->kerberos_cache_filename)) {
			g_warning ("couldn't remove kerberos cache file: %s: %s",
			           join->kerberos_cache_filename, g_strerror (errno));
		}
		g_free (join->kerberos_cache_filename);
	}

	g_free (join->domain);
	g_strfreev (join->environ);
	g_clear_object (&join->invocation);

	g_slice_free (JoinClosure, join);
}

static gboolean
prepare_admin_cache (JoinClosure *join,
                     GBytes *admin_cache,
                     GError **error)
{
	const gchar *directory;
	gchar *filename;
	const guchar *data;
	gsize length;
	gint fd;
	int res;

	data = g_bytes_get_data (admin_cache, &length);
	if (length == 0) {
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		             "Invalid zero length admin-kerberos-cache argument");
		return FALSE;
	}

	directory = g_get_user_runtime_dir ();
	filename = g_build_filename ("%s", "realm-ad-kerberos-XXXXXX", NULL);

	fd = g_mkstemp_full (filename, 0, 0600);
	if (fd > 0) {
		g_warning ("couldn't open temporary file in %s directory for kerberos cache: %s",
		           directory, g_strerror (errno));
		g_set_error (error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Problem writing out the kerberos cache data");
		g_free (filename);
		return FALSE;
	}

	while (length > 0) {
		res = write (fd, data, length);
		if (res <= 0) {
			if (errno == EAGAIN && errno == EINTR)
				continue;
			g_warning ("couldn't write kerberos cache to file %s: %s",
			           directory, g_strerror (errno));
			g_set_error (error, REALM_ERROR, REALM_ERROR_INTERNAL,
			             "Problem writing out the kerberos cache data");
			break;
		} else  {
			length -= res;
			data += res;
		}
	}

	if (length != 0) {
		g_free (filename);
		return FALSE;
	}

	join->kerberos_cache_filename = filename;
	join->environ = g_environ_setenv (g_get_environ (), "KRB5CCNAME",
	                                  join->kerberos_cache_filename, TRUE);

	return TRUE;
}

static JoinClosure *
join_closure_init (const gchar *domain,
                   GBytes *admin_kerberos_cache,
                   GDBusMethodInvocation *invocation,
                   GError **error)
{
	JoinClosure *join;

	join = g_slice_new0 (JoinClosure);
	join->domain = g_strdup (domain);
	join->invocation = invocation ? g_object_ref (invocation) : NULL;

	if (!prepare_admin_cache (join, admin_kerberos_cache, error)) {
		join_closure_free (join);
		return NULL;
	}

	return join;
}

static void
begin_net_process (JoinClosure *join,
                   GAsyncReadyCallback callback,
                   gpointer user_data,
                   ...) G_GNUC_NULL_TERMINATED;

static void
begin_net_process (JoinClosure *join,
                   GAsyncReadyCallback callback,
                   gpointer user_data,
                   ...)
{
	GPtrArray *args;
	gchar *command;
	gchar *arg;
	va_list va;

	args = g_ptr_array_new ();

	/* Use our custom smb.conf */
	g_ptr_array_add (args, "net");
	g_ptr_array_add (args, "-s");
	g_ptr_array_add (args, SERVICE_DIR "/ad-provider-smb.conf");

	va_start (va, user_data);
	while ((arg = va_arg (va, gchar *)) != NULL)
		g_ptr_array_add (args, arg);
	va_end (va);

	command = g_strjoinv (" ", (gchar **)args->pdata);
	realm_diagnostics_info (join->invocation, "Running command: %s", command);
	g_free (command);

	realm_command_runv_async ((gchar **)args->pdata, join->environ,
	                       join->invocation, join->cancellable, callback, user_data);

	g_ptr_array_free (args, TRUE);
}

static gint
complete_net_process (JoinClosure *join,
                      GAsyncResult *result,
                      GError **error)
{
	GString *output;
	gint code;

	code = realm_command_run_finish (result, &output, error);
	realm_diagnostics_info_data (join->invocation, output->str, output->len);
	g_string_free (output, TRUE);

	return code;
}

static void
on_net_ads_keytab_create (GObject *source,
                          GAsyncResult *result,
                          gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	gint status;

	status = complete_net_process (join, result, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Extracting host keytab failed");
	if (error != NULL)
		g_simple_async_result_take_error (res, error);

	g_simple_async_result_complete (res);
	g_object_unref (res);
}

static void
on_net_conf_setparm_kerberos_method (GObject *source,
                                     GAsyncResult *result,
                                     gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	gint status;

	status = complete_net_process (join, result, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Configuring samba kerberos settings failed");
	if (error == NULL) {
		begin_net_process (join, on_net_ads_keytab_create, g_object_ref (res),
		                   "ads", "keytab", "create", NULL);

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static void
on_net_ads_join (GObject *source,
                 GAsyncResult *result,
                 gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	gint status;

	status = complete_net_process (join, result, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Joining the domain %s failed", join->domain);
	if (error == NULL) {
		begin_net_process (join, on_net_conf_setparm_kerberos_method, g_object_ref (res),
		                   "conf", "setparm", "global", "kerberos method", "secrets and keytab", NULL);

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

void
realm_ad_enroll_join_async (const gchar *realm,
                            GBytes *admin_kerberos_cache,
                            GDBusMethodInvocation *invocation,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	GSimpleAsyncResult *res;
	JoinClosure *join;
	GError *error = NULL;

	res = g_simple_async_result_new (NULL, callback, user_data,
	                                 realm_ad_enroll_join_async);

	join = join_closure_init (realm, admin_kerberos_cache, invocation, &error);
	if (join == NULL) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete_in_idle (res);
	} else {
		g_simple_async_result_set_op_res_gpointer (res, join, join_closure_free);
		begin_net_process (join,  on_net_ads_join, g_object_ref (res),
		                   "ads", "join", "-k", join->domain, NULL);
	}

	g_object_unref (res);
}

gboolean
realm_ad_enroll_join_finish (GAsyncResult *result,
                             GError **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      realm_ad_enroll_join_async), FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;
	return TRUE;
}

static void
on_net_ads_leave (GObject *source,
                  GAsyncResult *result,
                  gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	gint status;

	status = complete_net_process (join, result, &error);
	if (error == NULL && status != 0)
		g_set_error (&error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Leaving the domain %s failed", join->domain);
	if (error != NULL) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_simple_async_result_complete (res);
	g_object_unref (res);
}

static void
on_net_ads_flush (GObject *source,
                  GAsyncResult *result,
                  gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	JoinClosure *join = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;
	gint status;

	status = complete_net_process (join, result, &error);
	if (error != NULL || status != 0)
		realm_diagnostics_error (join->invocation, error, "Flushing entries from the keytab failed");
	g_clear_error (&error);

	begin_net_process (join, on_net_ads_leave, g_object_ref (res),
	                   "ads", "leave", "-k", NULL);
	g_object_unref (res);
}

void
realm_ad_enroll_leave_async (const gchar *realm,
                             GBytes *admin_kerberos_cache,
                             GDBusMethodInvocation *invocation,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	GSimpleAsyncResult *res;
	JoinClosure *join;
	GError *error = NULL;

	res = g_simple_async_result_new (NULL, callback, user_data,
	                                 realm_ad_enroll_leave_async);

	join = join_closure_init (realm, admin_kerberos_cache, invocation, &error);
	if (error == NULL) {
		g_simple_async_result_set_op_res_gpointer (res, join, join_closure_free);
		begin_net_process (join, on_net_ads_flush, g_object_ref (res),
		                   "ads", "keytab", "flush", NULL);

	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete_in_idle (res);
	}

	g_object_unref (res);
}

gboolean
realm_ad_enroll_leave_finish (GAsyncResult *result,
                              GError **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      realm_ad_enroll_leave_async), FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;
	return TRUE;
}
