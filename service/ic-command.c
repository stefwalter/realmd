/*
 * Copyright (C) 2011 Collabora Ltd.
 * Copyright (C) 2012 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Author: Stef Walter <stefw@collabora.co.uk>
 */

#include "config.h"

#define DEBUG_FLAG IC_DEBUG_PROCESS
#include "ic-debug.h"
#include "ic-command.h"

#include <glib/gi18n-lib.h>

#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

enum {
	FD_INPUT,
	FD_OUTPUT,
	FD_ERROR,
	NUM_FDS
};


typedef struct {
	GString *input;
	GString *output;
	guint source_sig;
	gint exit_code;
	gboolean cancelled;
} CommandClosure;

typedef struct {
	GSource source;
	GPollFD polls[NUM_FDS];         /* The various fd's we're listening to */

	GPid child_pid;
	guint child_sig;

	GSimpleAsyncResult *res;
	CommandClosure *command;

	GCancellable *cancellable;
	guint cancel_sig;
} ProcessSource;

static void
command_closure_free (gpointer data)
{
	CommandClosure *command = data;
	g_string_free (command->output, TRUE);
	g_assert (command->source_sig == 0);
	g_slice_free (CommandClosure, command);
}

static void
complete_source_is_done (ProcessSource *process_source)
{
	ic_debug ("all fds closed and process exited, completing");

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
		ic_debug ("closing fd: %d", *fd);
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

static gboolean
write_input (int fd,
             GString *buffer)
{
	gssize result;

	g_return_val_if_fail (fd >= 0, FALSE);

	for (;;) {
		result = write (fd, buffer->str, buffer->len);
		if (result < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return FALSE;
		} else {
			g_string_erase (buffer, 0, result);
			return TRUE;
		}
	}
}

static gboolean
on_process_source_input (CommandClosure *command,
                         ProcessSource *process_source,
                         gint fd)
{
	if (command->input == NULL)
		return FALSE;
	if (!write_input (fd, command->input)) {
		g_warning ("couldn't write output data to process");
		return FALSE;
	}

	return TRUE;
}

static gboolean
on_process_source_output (CommandClosure *command,
                          ProcessSource *process_source,
                          gint fd)
{
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

	ic_debug ("process exited: %d", (int)pid);

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
	ProcessSource *process_source = user_data;

	ic_debug ("process cancelled");

	/* Set an error, which is respected when this actually completes. */
	g_simple_async_result_set_error (process_source->res, G_IO_ERROR, G_IO_ERROR_CANCELLED,
	                                 _("The operation was cancelled"));
	process_source->command->cancelled = TRUE;

	/* Try and kill the child process */
	if (process_source->child_pid) {
		ic_debug ("sending term signal to process: %d",
		            (int)process_source->child_pid);
		kill (process_source->child_pid, SIGTERM);
	}
}

void
ic_command_run_async (gchar **environ,
                      GDBusMethodInvocation *invocation,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data,
                      const gchar *name_or_path,
                      ...)
{
	GPtrArray *array;
	va_list va;
	gchar *arg;

	g_return_if_fail (name_or_path != NULL);
	g_return_if_fail (invocation == NULL || G_IS_DBUS_METHOD_INVOCATION (invocation));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	array = g_ptr_array_new ();
	g_ptr_array_add (array, (gchar *)name_or_path);

	va_start (va, name_or_path);
	do {
		arg = va_arg (va, gchar *);
		g_ptr_array_add (array, arg);
	} while (arg != NULL);
	va_end (va);

	ic_command_runv_async ((gchar **)array->pdata, environ, invocation,
	                       cancellable, callback, user_data);
}

void
ic_command_runv_async (gchar **name_or_path_and_arguments,
                       gchar **environ,
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
	GPid pid;
	guint i;

	g_return_if_fail (name_or_path_and_arguments != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (invocation == NULL || G_IS_DBUS_METHOD_INVOCATION (invocation));

	for (i = 0; i < NUM_FDS; i++)
		child_fds[i] = -1;

	/* TODO: Figure out if it's a name, and lookup the path */

	/* Spawn/child will close all other attributes, besides thesthose in child_fds */
	child_fds[FD_INPUT] = 0;
	child_fds[FD_OUTPUT] = 1;
	child_fds[FD_ERROR] = 2;

	if (ic_debugging) {
		gchar *command = g_strjoinv (" ", name_or_path_and_arguments);
		gchar *environment = g_strjoinv (", ", (gchar**)environ);
		ic_debug ("running command: %s", command);
		ic_debug ("process environment: %s", environment);
		g_free (command);
		g_free (environment);
	}

	g_spawn_async_with_pipes (NULL, name_or_path_and_arguments, environ,
	                          G_SPAWN_DO_NOT_REAP_CHILD,
	                          on_unix_process_child_setup, child_fds,
	                          &pid, &input_fd, &output_fd, &error_fd, &error);

	res = g_simple_async_result_new (NULL, callback, user_data, ic_command_runv_async);
	command = g_slice_new0 (CommandClosure);
	command->input = NULL;
	command->output = g_string_sized_new (128);
	g_simple_async_result_set_op_res_gpointer (res, command, command_closure_free);

	if (error) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete_in_idle (res);
		g_object_unref (res);
		return;
	}

	ic_debug ("process started: %d", (int)pid);

	source = g_source_new (&process_source_funcs, sizeof (ProcessSource));

	/* Initialize the source */
	process_source = (ProcessSource *)source;
	for (i = 0; i < NUM_FDS; i++)
		process_source->polls[i].fd = -1;
	process_source->res = g_object_ref (res);
	process_source->command = command;
	process_source->child_pid = pid;

	process_source->polls[FD_INPUT].fd = input_fd;
	if (input_fd >= 0) {
		process_source->polls[FD_INPUT].events = G_IO_HUP | G_IO_OUT;
		g_source_add_poll (source, &process_source->polls[FD_INPUT]);
	}
	process_source->polls[FD_OUTPUT].fd = output_fd;
	if (output_fd >= 0) {
		process_source->polls[FD_OUTPUT].events = G_IO_HUP | G_IO_IN;
		g_source_add_poll (source, &process_source->polls[FD_OUTPUT]);
	}
	process_source->polls[FD_ERROR].fd = error_fd;
	if (error_fd >= 0) {
		process_source->polls[FD_ERROR].events = G_IO_HUP | G_IO_IN;
		g_source_add_poll (source, &process_source->polls[FD_ERROR]);
	}

	if (cancellable) {
		process_source->cancellable = g_object_ref (cancellable);
		process_source->cancel_sig = g_cancellable_connect (cancellable,
		                                                    G_CALLBACK (on_cancellable_cancelled),
		                                                    g_source_ref (source),
		                                                    (GDestroyNotify)g_source_unref);
	}

	g_assert (command->source_sig == 0);
	g_source_set_callback (source, unused_callback, NULL, NULL);
	command->source_sig = g_source_attach (source, g_main_context_default ());

	/* This assumes the outstanding reference to source */
	g_assert (process_source->child_sig == 0);
	process_source->child_sig = g_child_watch_add_full (G_PRIORITY_DEFAULT, pid,
	                                                    on_unix_process_child_exited,
	                                                    g_source_ref (source),
	                                                    (GDestroyNotify)g_source_unref);

	/* source is unreffed in complete_if_source_is_done() */
}

gint
ic_command_run_finish (GAsyncResult *result,
                       GString **output,
                       GError **error)
{
	GSimpleAsyncResult *res;
	CommandClosure *command;

	g_return_val_if_fail (error == NULL || *error == NULL, -1);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      ic_command_runv_async), -1);

	res = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (res, error))
		return -1;

	command = g_simple_async_result_get_op_res_gpointer (res);
	if (output) {
		*output = command->output;
		command->output = NULL;
	}

	return command->exit_code;
}
