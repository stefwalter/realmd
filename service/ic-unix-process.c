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
#include "ic-unix-process.h"

#include <glib/gi18n-lib.h>

#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

enum {
	PROP_0,
	PROP_DIRECTORY,
	PROP_EXECUTABLE,
	PROP_INPUT_STREAM,
	PROP_OUTPUT_STREAM,
	PROP_ERROR_STREAM
};

enum {
	FD_INPUT,
	FD_OUTPUT,
	FD_ERROR,
	NUM_FDS
};

typedef struct _ProcessSource {
	GSource source;
	GPollFD polls[NUM_FDS];         /* The various fd's we're listening to */

	IcUnixProcess *process;       /* Pointer back to the process object */

	GByteArray *input_buf;

	GPid child_pid;
	guint child_sig;

	GCancellable *cancellable;
	guint cancel_sig;
} ProcessSource;

struct _IcUnixProcessPrivate {
	gchar *directory;
	gchar *executable;

	GInputStream *input_stream;
	GOutputStream *output_stream;
	GOutputStream *error_stream;

	gboolean running;
	gboolean complete;
	GError *error;
	guint source_sig;

	GAsyncReadyCallback async_callback;
	gpointer user_data;
};

/* Forward declarations */
static void ic_unix_process_init_async (GAsyncResultIface *iface);

G_DEFINE_TYPE_WITH_CODE (IcUnixProcess, ic_unix_process, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_RESULT, ic_unix_process_init_async));

static void
ic_unix_process_init (IcUnixProcess *self)
{
	self->pv = G_TYPE_INSTANCE_GET_PRIVATE (self, IC_TYPE_UNIX_PROCESS,
	                                        IcUnixProcessPrivate);
}

static void
ic_unix_process_constructed (GObject *obj)
{
	IcUnixProcess *self = IC_UNIX_PROCESS (obj);

	if (G_OBJECT_CLASS (ic_unix_process_parent_class)->constructed)
		G_OBJECT_CLASS (ic_unix_process_parent_class)->constructed (obj);

	if (!self->pv->executable)
		g_warning ("the IcUnixProcess::executable property must be specified");
}

static void
ic_unix_process_get_property (GObject *obj,
                              guint prop_id,
                              GValue *value,
                              GParamSpec *pspec)
{
	IcUnixProcess *self = IC_UNIX_PROCESS (obj);

	switch (prop_id) {
	case PROP_DIRECTORY:
		g_value_set_string (value, self->pv->directory);
		break;
	case PROP_EXECUTABLE:
		g_value_set_string (value, self->pv->executable);
		break;
	case PROP_INPUT_STREAM:
		g_value_set_object (value, ic_unix_process_get_input_stream (self));
		break;
	case PROP_OUTPUT_STREAM:
		g_value_set_object (value, ic_unix_process_get_output_stream (self));
		break;
	case PROP_ERROR_STREAM:
		g_value_set_object (value, ic_unix_process_get_error_stream (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
ic_unix_process_set_property (GObject *obj,
                              guint prop_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
	IcUnixProcess *self = IC_UNIX_PROCESS (obj);

	switch (prop_id) {
	case PROP_DIRECTORY:
		g_return_if_fail (!self->pv->directory);
		self->pv->directory = g_value_dup_string (value);
		break;
	case PROP_EXECUTABLE:
		g_return_if_fail (!self->pv->executable);
		self->pv->executable = g_value_dup_string (value);
		break;
	case PROP_INPUT_STREAM:
		ic_unix_process_set_input_stream (self, g_value_get_object (value));
		break;
	case PROP_OUTPUT_STREAM:
		ic_unix_process_set_output_stream (self, g_value_get_object (value));
		break;
	case PROP_ERROR_STREAM:
		ic_unix_process_set_error_stream (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
ic_unix_process_dispose (GObject *obj)
{
	IcUnixProcess *self = IC_UNIX_PROCESS (obj);

	g_clear_object (&self->pv->input_stream);
	g_clear_object (&self->pv->output_stream);
	g_clear_object (&self->pv->error_stream);

	G_OBJECT_CLASS (ic_unix_process_parent_class)->dispose (obj);
}

static void
ic_unix_process_finalize (GObject *obj)
{
	IcUnixProcess *self = IC_UNIX_PROCESS (obj);

	g_assert (self->pv->source_sig == 0);
	g_assert (!self->pv->running);
	g_free (self->pv->directory);
	g_free (self->pv->executable);
	g_clear_error (&self->pv->error);

	G_OBJECT_CLASS (ic_unix_process_parent_class)->finalize (obj);
}

static void
ic_unix_process_class_init (IcUnixProcessClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->constructed = ic_unix_process_constructed;
	gobject_class->get_property = ic_unix_process_get_property;
	gobject_class->set_property = ic_unix_process_set_property;
	gobject_class->dispose = ic_unix_process_dispose;
	gobject_class->finalize = ic_unix_process_finalize;

	g_object_class_install_property (gobject_class, PROP_DIRECTORY,
	           g_param_spec_string ("directory", "Directory", "Unix Directory",
	                                NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (gobject_class, PROP_EXECUTABLE,
	           g_param_spec_string ("executable", "Executable", "Unix Executable",
	                                NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (gobject_class, PROP_INPUT_STREAM,
	           g_param_spec_object ("input-stream", "Input Stream", "Input Stream",
	                                G_TYPE_INPUT_STREAM, G_PARAM_READWRITE));

	g_object_class_install_property (gobject_class, PROP_OUTPUT_STREAM,
	           g_param_spec_object ("output-stream", "Output Stream", "Output Stream",
	                                G_TYPE_OUTPUT_STREAM, G_PARAM_READWRITE));

	g_object_class_install_property (gobject_class, PROP_ERROR_STREAM,
	            g_param_spec_object ("error-stream", "Error Stream", "Error Stream",
	                                G_TYPE_OUTPUT_STREAM, G_PARAM_READWRITE));

	g_type_class_add_private (gobject_class, sizeof (IcUnixProcessPrivate));
}

static gpointer
ic_unix_process_get_user_data (GAsyncResult *result)
{
	g_return_val_if_fail (IC_IS_UNIX_PROCESS (result), NULL);
	return IC_UNIX_PROCESS (result)->pv->user_data;
}

static GObject*
ic_unix_process_get_source_object (GAsyncResult *result)
{
	g_return_val_if_fail (IC_IS_UNIX_PROCESS (result), NULL);
	return g_object_ref (result);
}

static void
ic_unix_process_init_async (GAsyncResultIface *iface)
{
	iface->get_source_object = ic_unix_process_get_source_object;
	iface->get_user_data = ic_unix_process_get_user_data;
}

IcUnixProcess*
ic_unix_process_new (const gchar *directory,
                     const gchar *executable)
{
	return g_object_new (IC_TYPE_UNIX_PROCESS,
	                     "directory", directory,
	                     "executable", executable,
	                     NULL);
}

const gchar *
ic_unix_process_get_directory (IcUnixProcess *self)
{
	g_return_val_if_fail (IC_UNIX_PROCESS (self), NULL);
	return self->pv->directory;
}

GInputStream *
ic_unix_process_get_input_stream (IcUnixProcess *self)
{
	g_return_val_if_fail (IC_UNIX_PROCESS (self), NULL);
	return self->pv->input_stream;
}

void
ic_unix_process_set_input_stream (IcUnixProcess *self,
                                     GInputStream *input)
{
	g_return_if_fail (IC_UNIX_PROCESS (self));
	g_return_if_fail (input == NULL || G_INPUT_STREAM (input));

	if (input)
		g_object_ref (input);
	if (self->pv->input_stream)
		g_object_unref (self->pv->input_stream);
	self->pv->input_stream = input;
	g_object_notify (G_OBJECT (self), "input-stream");
}

GOutputStream *
ic_unix_process_get_output_stream (IcUnixProcess *self)
{
	g_return_val_if_fail (IC_UNIX_PROCESS (self), NULL);
	return self->pv->output_stream;
}

void
ic_unix_process_set_output_stream (IcUnixProcess *self,
                                      GOutputStream *output)
{
	g_return_if_fail (IC_UNIX_PROCESS (self));
	g_return_if_fail (output == NULL || G_OUTPUT_STREAM (output));

	if (output)
		g_object_ref (output);
	if (self->pv->output_stream)
		g_object_unref (self->pv->output_stream);
	self->pv->output_stream = output;
	g_object_notify (G_OBJECT (self), "output-stream");
}

GOutputStream *
ic_unix_process_get_error_stream (IcUnixProcess *self)
{
	g_return_val_if_fail (IC_UNIX_PROCESS (self), NULL);
	return self->pv->error_stream;
}

void
ic_unix_process_set_error_stream (IcUnixProcess *self,
                                  GOutputStream *output)
{
	g_return_if_fail (IC_UNIX_PROCESS (self));
	g_return_if_fail (output == NULL || G_OUTPUT_STREAM (output));

	if (output)
		g_object_ref (output);
	if (self->pv->error_stream)
		g_object_unref (self->pv->error_stream);
	self->pv->error_stream = output;
	g_object_notify (G_OBJECT (self), "error-stream");
}

static void
run_async_ready_callback (IcUnixProcess *self)
{
	GAsyncReadyCallback callback;
	gpointer user_data;

	ic_debug ("running async callback");

	/* Remove these before completing */
	callback = self->pv->async_callback;
	user_data = self->pv->user_data;
	self->pv->async_callback = NULL;
	self->pv->user_data = NULL;

	if (callback != NULL)
		(callback) (G_OBJECT (self), G_ASYNC_RESULT (self), user_data);
}

static gboolean
on_run_async_ready_callback_later (gpointer user_data)
{
	run_async_ready_callback (IC_UNIX_PROCESS (user_data));
	return FALSE; /* Don't run this callback again */
}

static void
run_async_ready_callback_later (IcUnixProcess *self)
{
	ic_debug ("running async callback later");
	g_idle_add_full (G_PRIORITY_DEFAULT, on_run_async_ready_callback_later,
	                 g_object_ref (self), g_object_unref);
}

static void
complete_run_process (IcUnixProcess *self)
{
	g_return_if_fail (self->pv->running);
	g_return_if_fail (!self->pv->complete);

	self->pv->running = FALSE;
	self->pv->complete = TRUE;

	if (self->pv->error == NULL) {
		ic_debug ("completed process");
	} else {
		ic_debug ("completed process with error: %s",
		            self->pv->error->message);
	}
}

static void
complete_source_is_done (ProcessSource *process_source)
{
	IcUnixProcess *self = process_source->process;

	ic_debug ("all fds closed and process exited, completing");

	g_assert (process_source->child_sig == 0);

	if (process_source->cancel_sig) {
		g_signal_handler_disconnect (process_source->cancellable, process_source->cancel_sig);
		process_source->cancel_sig = 0;
	}

	g_clear_object (&process_source->cancellable);

	complete_run_process (self);
	run_async_ready_callback (self);

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

	g_object_unref (process_source->process);
	if (process_source->input_buf)
		g_byte_array_free (process_source->input_buf, TRUE);

	g_assert (!process_source->child_pid);
	g_assert (!process_source->child_sig);
}

static gboolean
read_output (int fd, GByteArray *buffer)
{
	guchar block[1024];
	gssize result;

	g_return_val_if_fail (fd >= 0, FALSE);

	do {
		result = read (fd, block, sizeof (block));
		if (result < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return FALSE;
		} else {
			g_byte_array_append (buffer, block, result);
		}
	} while (result == sizeof (block));

	return TRUE;
}

static gboolean
write_input (int fd, GByteArray *buffer)
{
	gssize result;

	g_return_val_if_fail (fd >= 0, FALSE);

	for (;;) {
		result = write (fd, buffer->data, buffer->len);
		if (result < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return FALSE;
		} else {
			g_byte_array_remove_range (buffer, 0, result);
			return TRUE;
		}
	}
}

static gboolean
on_process_source_input (IcUnixProcess *self,
                         ProcessSource *process_source,
                         gint fd)
{
	gssize read;

	if (process_source->input_buf == NULL ||
	    process_source->input_buf->len == 0) {
		if (self->pv->input_stream == NULL)
			return FALSE;
		if (!process_source->input_buf)
			process_source->input_buf = g_byte_array_new ();
		g_byte_array_set_size (process_source->input_buf, 4096);
		read = g_input_stream_read (self->pv->input_stream,
		                            process_source->input_buf->data,
		                            process_source->input_buf->len,
		                            process_source->cancellable, NULL);
		g_byte_array_set_size (process_source->input_buf, read < 0 ? 0 : read);
		if (read < 0)
			return FALSE;
		if (read == 0)
			return FALSE;
	}

	if (!write_input (fd, process_source->input_buf)) {
		g_warning ("couldn't write output data to %s process", self->pv->executable);
		return FALSE;
	}

	return TRUE;
}

static gboolean
on_process_source_output (IcUnixProcess *self,
                        ProcessSource *process_source,
                        gint fd)
{
	GByteArray *buffer = g_byte_array_new ();
	gboolean result = TRUE;

	if (!read_output (fd, buffer)) {
		g_warning ("couldn't read output data from %s process", self->pv->executable);
		result = FALSE;
	} else if (buffer->len > 0) {
		ic_debug ("received %d bytes of output data", (gint)buffer->len);
		if (self->pv->output_stream != NULL)
			g_output_stream_write_all (self->pv->output_stream,
			                           buffer->data, buffer->len,
			                           NULL, process_source->cancellable, NULL);
	}

	g_byte_array_unref (buffer);
	return result;
}

static gboolean
on_process_source_error (IcUnixProcess *self,
                         ProcessSource *process_source,
                         gint fd)
{
	GByteArray *buffer = g_byte_array_new ();
	gboolean result = TRUE;

	if (!read_output (fd, buffer)) {
		g_warning ("couldn't read error data from %s process", self->pv->executable);
		result = FALSE;
	} else {
		ic_debug ("received %d bytes of error data", (gint)buffer->len);
		if (self->pv->output_stream != NULL)
			g_output_stream_write_all (self->pv->output_stream,
			                           buffer->data, buffer->len,
			                           NULL, process_source->cancellable, NULL);
	}

	g_byte_array_unref (buffer);
	return result;
}

static gboolean
on_process_source_dispatch (GSource *source,
                            GSourceFunc unused,
                            gpointer user_data)
{
	ProcessSource *process_source = (ProcessSource*)source;
	IcUnixProcess *self = process_source->process;
	GPollFD *poll;
	guint i;

	/* Standard input, no support yet */
	poll = &process_source->polls[FD_INPUT];
	if (poll->fd >= 0) {
		if (poll->revents & G_IO_OUT)
			if (!on_process_source_input (self, process_source, poll->fd))
				poll->revents |= G_IO_HUP;
		if (poll->revents & G_IO_HUP)
			close_poll (source, poll);
		poll->revents = 0;
	}

	/* Standard output */
	poll = &process_source->polls[FD_OUTPUT];
	if (poll->fd >= 0) {
		if (poll->revents & G_IO_IN)
			if (!on_process_source_output (self, process_source, poll->fd))
				poll->revents |= G_IO_HUP;
		if (poll->revents & G_IO_HUP)
			close_poll (source, poll);
		poll->revents = 0;
	}

	/* Standard error */
	poll = &process_source->polls[FD_ERROR];
	if (poll->fd >= 0) {
		if (poll->revents & G_IO_IN)
			if (!on_process_source_error (self, process_source, poll->fd))
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
	self->pv->source_sig = 0;

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
	IcUnixProcess *self = process_source->process;
	GError *error = NULL;
	gint code;
	guint i;

	ic_debug ("process exited: %d", (int)pid);

	g_spawn_close_pid (process_source->child_pid);
	process_source->child_pid = 0;
	process_source->child_sig = 0;

	if (WIFEXITED (status)) {
		code = WEXITSTATUS (status);
		if (code != 0) {
			error = g_error_new (G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
			                     _("%s process exited with code: %d"),
			                     self->pv->executable, code);
		}
	} else if (WIFSIGNALED (status)) {
		code = WTERMSIG (status);
		/* Ignore cases where we've signaled the process because we were cancelled */
		if (!g_error_matches (self->pv->error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			error = g_error_new (G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
			                     _("%s process was terminated with signal: %d"),
			                     self->pv->executable, code);
	}

	/* Take this as the async result error */
	if (error && !self->pv->error) {
		ic_debug ("%s", error->message);
		self->pv->error = error;

	/* Already have an error, just print out message */
	} else if (error) {
		g_warning ("%s", error->message);
		g_error_free (error);
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

	g_assert (process_source->process);

	ic_debug ("process cancelled");

	/* Set an error, which is respected when this actually completes. */
	if (process_source->process->pv->error == NULL)
		process_source->process->pv->error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
		                                                        _("The operation was cancelled"));

	/* Try and kill the child process */
	if (process_source->child_pid) {
		ic_debug ("sending term signal to process: %d",
		            (int)process_source->child_pid);
		kill (process_source->child_pid, SIGTERM);
	}
}

void
ic_unix_process_run_async (IcUnixProcess *self,
                           const gchar **argv,
                           const gchar **envp,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	GError *error = NULL;
	GPtrArray *args;
	GPtrArray *envs;
	int child_fds[NUM_FDS];
	int output_fd = -1;
	int error_fd = -1;
	int input_fd = -1;
	ProcessSource *process_source;
	GSource *source;
	GPid pid;
	guint i;

	g_return_if_fail (IC_IS_UNIX_PROCESS (self));
	g_return_if_fail (argv);
	g_return_if_fail (callback);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	g_return_if_fail (self->pv->running == FALSE);
	g_return_if_fail (self->pv->complete == FALSE);
	g_return_if_fail (self->pv->executable);

	self->pv->async_callback = callback;
	self->pv->user_data = user_data;

	for (i = 0; i < NUM_FDS; i++)
		child_fds[i] = -1;

	/* The command needs to be updated with these status and attribute fds */
	args = g_ptr_array_new_with_free_func (g_free);
	g_ptr_array_add (args, g_strdup (self->pv->executable));

	/* Spawn/child will close all other attributes, besides thesthose in child_fds */
	child_fds[FD_INPUT] = 0;
	child_fds[FD_OUTPUT] = 1;
	child_fds[FD_ERROR] = 2;

	/* All the remaining arguments */
	for (i = 0; argv[i] != NULL; i++)
		g_ptr_array_add (args, g_strdup (argv[i]));
	g_ptr_array_add (args, NULL);

	envs = g_ptr_array_new ();
	for (i = 0; envp && envp[i] != NULL; i++)
		g_ptr_array_add (envs, (gpointer)envp[i]);
	g_ptr_array_add (envs, NULL);

	if (ic_debugging) {
		gchar *command = g_strjoinv (" ", (gchar**)args->pdata);
		gchar *environ = g_strjoinv (", ", (gchar**)envs->pdata);
		ic_debug ("running command: %s", command);
		ic_debug ("process environment: %s", environ);
		g_free (command);
		g_free (environ);
	}

	g_spawn_async_with_pipes (self->pv->directory, (gchar**)args->pdata,
	                          (gchar**)envs->pdata, G_SPAWN_DO_NOT_REAP_CHILD,
	                          on_unix_process_child_setup, child_fds,
	                          &pid, &input_fd, &output_fd, &error_fd, &error);

	g_ptr_array_free (args, TRUE);
	g_ptr_array_free (envs, TRUE);

	self->pv->complete = FALSE;
	self->pv->running = TRUE;

	if (error) {
		g_assert (!self->pv->error);
		self->pv->error = error;
		complete_run_process (self);
		run_async_ready_callback_later (self);
		return;
	}

	ic_debug ("process started: %d", (int)pid);

	source = g_source_new (&process_source_funcs, sizeof (ProcessSource));

	/* Initialize the source */
	process_source = (ProcessSource *)source;
	for (i = 0; i < NUM_FDS; i++)
		process_source->polls[i].fd = -1;
	process_source->process = g_object_ref (self);
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

	g_assert (self->pv->source_sig == 0);
	g_source_set_callback (source, unused_callback, NULL, NULL);
	self->pv->source_sig = g_source_attach (source, g_main_context_default ());

	/* This assumes the outstanding reference to source */
	g_assert (process_source->child_sig == 0);
	process_source->child_sig = g_child_watch_add_full (G_PRIORITY_DEFAULT, pid,
	                                                    on_unix_process_child_exited,
	                                                    g_source_ref (source),
	                                                    (GDestroyNotify)g_source_unref);

	/* source is unreffed in complete_if_source_is_done() */
}

gboolean
ic_unix_process_run_finish (IcUnixProcess *self, GAsyncResult *result,
                               GError **error)
{
	g_return_val_if_fail (IC_IS_UNIX_PROCESS (self), FALSE);
	g_return_val_if_fail (!error || !*error, FALSE);
	g_return_val_if_fail (G_ASYNC_RESULT (self) == result, FALSE);
	g_return_val_if_fail (self->pv->complete, FALSE);

	/* This allows the process to run again... */
	self->pv->complete = FALSE;

	g_assert (!self->pv->running);
	g_assert (!self->pv->async_callback);
	g_assert (!self->pv->user_data);

	if (self->pv->error) {
		g_propagate_error (error, self->pv->error);
		self->pv->error = NULL;
		return FALSE;
	}

	return TRUE;
}
