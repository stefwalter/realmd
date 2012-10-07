/*
 * Copyright (C) 2011 Collabora Ltd.
 * Copyright (C) 2012 Red Hat Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 *
 * Author: Stef Walter <stefw@collabora.co.uk>
 */

#include "config.h"

#include "realm-daemon.h"
#include "realm-command.h"
#include "realm-diagnostics.h"
#include "realm-settings.h"

#include <glib/gi18n-lib.h>

#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#if 0
enum {
	FD_INPUT,
	FD_OUTPUT,
	FD_ERROR,
	NUM_FDS
};
#endif

#define DEBUG_VERBOSE 0

typedef struct {
	GBytes *input;
	gsize input_offset;
	GString *output;
	gint exit_code;
	gboolean cancelled;
	GDBusMethodInvocation *invocation;
	GSubprocess *process;
	gint cancel_sig;
} CommandClosure;

static void
command_closure_free (gpointer data)
{
	CommandClosure *command = data;
	if (command->input)
		g_bytes_unref (command->input);
	if (command->invocation)
		g_object_unref (command->invocation);
	if (command->process)
		g_object_unref (command->process);
	g_string_free (command->output, TRUE);
	g_assert (command->cancel_sig == 0);
	g_slice_free (CommandClosure, command);
}

#if 0
static void
complete_source_is_done (ProcessSource *process_source)
{
#if DEBUG_VERBOSE
	g_debug ("all fds closed and process exited, completing");
#endif

	g_assert (process_source->child_sig == 0);

	if (process_source->cancel_sig) {
		g_signal_handler_disconnect (process_source->cancellable, process_source->cancel_sig);
		process_source->cancel_sig = 0;
	}

	g_clear_object (&process_source->cancellable);
	g_simple_async_result_complete (process_source->res);

	/* All done, the source can go away now */
	g_source_unref ((GSource*)process_source);
}

static void
close_fd (int *fd)
{
	g_assert (fd);
	if (*fd >= 0) {
#if DEBUG_VERBOSE
		g_debug ("closing fd: %d", *fd);
#endif
		close (*fd);
	}
	*fd = -1;
}

static void
close_poll (GSource *source, GPollFD *poll)
{
	g_source_remove_poll (source, poll);
	close_fd (&poll->fd);
	poll->revents = 0;
}

static gboolean
unused_callback (gpointer data)
{
	/* Never called */
	g_assert_not_reached ();
	return FALSE;
}

static gboolean
on_process_source_prepare (GSource *source, gint *timeout_)
{
	ProcessSource *process_source = (ProcessSource*)source;
	gint i;

	for (i = 0; i < NUM_FDS; ++i) {
		if (process_source->polls[i].fd >= 0)
			return FALSE;
	}

	/* If none of the FDs are valid, then process immediately */
	return TRUE;
}

static gboolean
on_process_source_check (GSource *source)
{
	ProcessSource *process_source = (ProcessSource*)source;
	gint i;

	for (i = 0; i < NUM_FDS; ++i) {
		if (process_source->polls[i].fd >= 0 && process_source->polls[i].revents != 0)
			return TRUE;
	}
	return FALSE;
}

static void
on_process_source_finalize (GSource *source)
{
	ProcessSource *process_source = (ProcessSource*)source;
	gint i;

	g_assert (process_source->cancellable == NULL);
	g_assert (process_source->cancel_sig == 0);

	for (i = 0; i < NUM_FDS; ++i)
		close_fd (&process_source->polls[i].fd);

	g_assert (!process_source->child_pid);
	g_assert (!process_source->child_sig);
}

static gboolean
read_output (int fd,
             GString *buffer)
{
	gchar block[1024];
	gssize result;

	g_return_val_if_fail (fd >= 0, FALSE);

	do {
		result = read (fd, block, sizeof (block));
		if (result < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return FALSE;
		} else {
			g_string_append_len (buffer, block, result);
		}
	} while (result == sizeof (block));

	return TRUE;
}
#endif

static void
on_process_input (GObject *source,
                  GAsyncResult *result,
                  gpointer user_data)
{
	GSimpleAsyncResult *async = G_SIMPLE_ASYNC_RESULT (user_data);
	CommandClosure *command = g_simple_async_result_get_op_res_gpointer (async);
	GError *error = NULL;
	gssize written;

	written = g_output_stream_write_finish (G_OUTPUT_STREAM (source),
	                                        result, &error);

	if (written < 0) {
		g_warning ("couldn't write output data to process: %s", error->message);

	} else {
		command->input_offset += result;
		if (command->input_offset < length) {
			data = g_bytes_get_data (command->input, &length);
			g_output_stream_write_async (G_OUTPUT_STREAM (source),
			                             data + command->input_offset,
			                             length - command->input_offset,
			                             G_PRIORITY_DEFAULT, NULL,
			                             on_process_input, g_object_ref (async));
		}
	}

	g_object_unref (async);
}

static void
on_process_output (GObject *source,
                   GAsyncResult *result,
                   gpointer user_data)
{
	GAsyncResult *result =
	g_object_unref (async);
}

static gboolean
on_process_source_output (CommandClosure *command,
                          ProcessSource *process_source,
                          gint fd)
{
	static gboolean
	read_output (int fd,
	             GString *buffer)
	{
		gchar block[1024];
		gssize result;

		g_return_val_if_fail (fd >= 0, FALSE);

		do {
			result = read (fd, block, sizeof (block));
			if (result < 0) {
				if (errno == EINTR || errno == EAGAIN)
					continue;
				return FALSE;
			} else {
				g_string_append_len (buffer, block, result);
			}
		} while (result == sizeof (block));

		return TRUE;
	}
	#endif



	if (!read_output (fd, command->output)) {
		g_warning ("couldn't read output data from process");
		return FALSE;
	}

	return TRUE;
}

static gboolean
on_process_source_error (CommandClosure *command,
                         ProcessSource *process_source,
                         gint fd)
{
	if (!read_output (fd, command->output)) {
		g_warning ("couldn't read error data from process");
		return FALSE;
	}

	return TRUE;
}

static gboolean
on_process_source_dispatch (GSource *source,
                            GSourceFunc unused,
                            gpointer user_data)
{
	ProcessSource *process_source = (ProcessSource*)source;
	CommandClosure *command = process_source->command;
	GPollFD *poll;
	guint i;

	/* Standard input, no support yet */
	poll = &process_source->polls[FD_INPUT];
	if (poll->fd >= 0) {
		if (poll->revents & G_IO_OUT)
			if (!on_process_source_input (command, process_source, poll->fd))
				poll->revents |= G_IO_HUP;
		if (poll->revents & G_IO_HUP)
			close_poll (source, poll);
		poll->revents = 0;
	}

	/* Standard output */
	poll = &process_source->polls[FD_OUTPUT];
	if (poll->fd >= 0) {
		if (poll->revents & G_IO_IN)
			if (!on_process_source_output (command, process_source, poll->fd))
				poll->revents |= G_IO_HUP;
		if (poll->revents & G_IO_HUP)
			close_poll (source, poll);
		poll->revents = 0;
	}

	/* Standard error */
	poll = &process_source->polls[FD_ERROR];
	if (poll->fd >= 0) {
		if (poll->revents & G_IO_IN)
			if (!on_process_source_error (command, process_source, poll->fd))
				poll->revents |= G_IO_HUP;
		if (poll->revents & G_IO_HUP)
			close_poll (source, poll);
		poll->revents = 0;
	}

	for (i = 0; i < NUM_FDS; ++i) {
		if (process_source->polls[i].fd >= 0)
			return TRUE;
	}

	/* Because we return below */
	command->source_sig = 0;

	if (!process_source->child_pid)
		complete_source_is_done (process_source);

	return FALSE; /* Disconnect this source */
}

static GSourceFuncs process_source_funcs = {
	on_process_source_prepare,
	on_process_source_check,
	on_process_source_dispatch,
	on_process_source_finalize,
};

static void
on_unix_process_child_exited (GPid pid,
                              gint status,
                              gpointer user_data)
{
	ProcessSource *process_source = user_data;
	CommandClosure *command = process_source->command;
	gint code;
	guint i;

	g_debug ("process exited: %d", (int)pid);

	g_spawn_close_pid (process_source->child_pid);
	process_source->child_pid = 0;
	process_source->child_sig = 0;

	if (WIFEXITED (status)) {
		command->exit_code = WEXITSTATUS (status);

	} else if (WIFSIGNALED (status)) {
		code = WTERMSIG (status);
		/* Ignore cases where we've signaled the process because we were cancelled */
		if (!command->cancelled)
			g_simple_async_result_set_error (process_source->res, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
			                                 _("Process was terminated with signal: %d"), code);
	}

	for (i = 0; i < NUM_FDS; ++i) {
		if (process_source->polls[i].fd >= 0)
			return;
	}

	complete_source_is_done (process_source);
}

static void
on_unix_process_child_setup (gpointer user_data)
{
	int *child_fds = user_data;
	long val;
	guint i;

	/*
	 * Become a process leader in order to close the controlling terminal.
	 * This allows us to avoid the sub-processes blocking on reading from
	 * the terminal. We can also pipe passwords and such into stdin since
	 * getpass() will fall back to that.
	 */
	setsid ();

	/*
	 * Clear close-on-exec flag for these file descriptors, so that
	 * gnupg can write to them
	 */

	for (i = 0; i < NUM_FDS; i++) {
		if (child_fds[i] >= 0) {
			val = fcntl (child_fds[i], F_GETFD);
			fcntl (child_fds[i], F_SETFD, val & ~FD_CLOEXEC);
		}
	}
}

static void
on_cancellable_cancelled (GCancellable *cancellable,
                          gpointer user_data)
{
	CommandClosure *command = user_data;

	g_debug ("process cancelled: %d", (int)g_subprocess_get_pid (command->process));

	/* Set an error, which is respected when this actually completes. */
	g_simple_async_result_set_error (process_source->res, G_IO_ERROR, G_IO_ERROR_CANCELLED,
	                                 _("The operation was cancelled"));
	process_source->command->cancelled = TRUE;

	g_subprocess_request_exit (command->process);
}

void
realm_command_runv_async (gchar **argv,
                          gchar **environ,
                          GBytes *input,
                          GDBusMethodInvocation *invocation,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	GSimpleAsyncResult *res;
	CommandClosure *command;
	GError *error = NULL;
	int child_fds[NUM_FDS];
	int output_fd = -1;
	int error_fd = -1;
	int input_fd = -1;
	ProcessSource *process_source;
	GSource *source;
	gchar *cmd_string;
	gchar *env_string;
	gchar **parts;
	gchar **env;
	GPid pid;
	guint i;

	g_return_if_fail (argv != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (invocation == NULL || G_IS_DBUS_METHOD_INVOCATION (invocation));


	env = g_get_environ ();
	env_string = NULL;
	if (environ) {
		env_string = g_strjoinv (" ", environ);
		for (i = 0; environ != NULL && environ[i] != NULL; i++) {
			parts = g_strsplit (environ[i], "=", 2);
			if (!parts[0] || !parts[1])
				g_warning ("invalid environment variable: %s", environ[i]);
			else
				env = g_environ_setenv (env, parts[0], parts[1], TRUE);
			g_strfreev (parts);
		}
	}

	cmd_string = g_strjoinv (" ", argv);
	realm_diagnostics_info (invocation, "%s%s%s",
	                        env_string ? env_string : "",
	                        env_string ? " " : "",
	                        cmd_string);
	g_free (env_string);
	g_free (cmd_string);

	process = g_subprocess_new (NULL, argv, env,
	                            G_SUBPROCESS_FLAGS_NEW_SESSION |
	                            G_SUBPROCESS_FLAGS_STDIN_PIPE |
	                            G_SUBPROCESS_FLAGS_STDOUT_PIPE |
	                            G_SUBPROCESS_FLAGS_STDERR_PIPE,
	                            &error);

	g_strfreev (env);

	res = g_simple_async_result_new (NULL, callback, user_data, realm_command_runv_async);
	command = g_slice_new0 (CommandClosure);
	command->input = input ? g_bytes_ref (input) : NULL;
	command->output = g_string_sized_new (128);
	command->invocation = invocation ? g_object_ref (invocation) : NULL;
	command->process = process;
	g_simple_async_result_set_op_res_gpointer (res, command, command_closure_free);

	if (error) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete_in_idle (res);
		g_object_unref (res);
		return;
	}

	g_debug ("process started: %d", (int)g_subprocess_get_pid (process));

	g_subprocess_wait (process, NULL,
	                   on_unix_process_child_exited, g_object_ref (res));

	if (cancellable) {
		command->cancel_sig = g_cancellable_connect (cancellable,
		                                             G_CALLBACK (on_cancellable_cancelled),
		                                             command, NULL);
	}

	g_object_unref (res);
}

static gboolean
is_only_whitespace (const gchar *string)
{
	while (*string != '\0') {
		if (!g_ascii_isspace (*string))
			return FALSE;
		string++;
	}

	return TRUE;
}

void
realm_command_run_known_async (const gchar *known_command,
                               gchar **environ,
                               GDBusMethodInvocation *invocation,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	const gchar *command_line;
	GError *error = NULL;
	gchar **argv;
	gint unused;

	const gchar *empty_argv[] = {
		"/bin/true",
		"empty-configured-command",
		known_command,
		NULL,
	};

	const gchar *invalid_argv[] = {
		"/bin/false",
		"invalid-configured-command",
		known_command,
		NULL
	};

	g_return_if_fail (known_command != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (invocation == NULL || G_IS_DBUS_METHOD_INVOCATION (invocation));

	command_line = realm_settings_value ("commands", known_command);
	if (command_line == NULL) {
		g_warning ("Couldn't find the configured string commands/%s", known_command);
		argv = g_strdupv ((gchar **)invalid_argv);

	} else if (is_only_whitespace (command_line)) {
		argv = g_strdupv ((gchar **)empty_argv);

	} else if (!g_shell_parse_argv (command_line, &unused, &argv, &error)) {
		g_warning ("Couldn't parse the command line: %s: %s", command_line, error->message);
		g_error_free (error);
		argv = g_strdupv ((gchar **)invalid_argv);
	}

	realm_command_runv_async (argv, environ, NULL, invocation, cancellable, callback, user_data);
	g_strfreev (argv);
}

gint
realm_command_run_finish (GAsyncResult *result,
                          GString **output,
                          GError **error)
{
	GSimpleAsyncResult *res;
	CommandClosure *command;

	g_return_val_if_fail (error == NULL || *error == NULL, -1);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      realm_command_runv_async), -1);

	res = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (res, error))
		return -1;

	command = g_simple_async_result_get_op_res_gpointer (res);
	if (command->output->len)
		realm_diagnostics_info_data (command->invocation,
		                             command->output->str,
		                             command->output->len);
	if (output) {
		*output = command->output;
		command->output = NULL;
	}

	return command->exit_code;
}
