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

#ifndef IC_UNIX_PROCESS_H
#define IC_UNIX_PROCESS_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define IC_TYPE_UNIX_PROCESS               (ic_unix_process_get_type ())
#define IC_UNIX_PROCESS(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), IC_TYPE_UNIX_PROCESS, IcUnixProcess))
#define IC_UNIX_PROCESS_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), IC_TYPE_UNIX_PROCESS, IcUnixProcessClass))
#define IC_IS_UNIX_PROCESS(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), IC_TYPE_UNIX_PROCESS))
#define IC_IS_UNIX_PROCESS_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), IC_TYPE_UNIX_PROCESS))
#define IC_UNIX_PROCESS_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), IC_TYPE_UNIX_PROCESS, IcUnixProcessClass))

typedef struct _IcUnixProcess IcUnixProcess;
typedef struct _IcUnixProcessClass IcUnixProcessClass;
typedef struct _IcUnixProcessPrivate IcUnixProcessPrivate;

struct _IcUnixProcess {
	GObject parent;
	IcUnixProcessPrivate *pv;
};

struct _IcUnixProcessClass {
	GObjectClass parent_class;
};

GType               ic_unix_process_get_type                (void) G_GNUC_CONST;

IcUnixProcess *     ic_unix_process_new                     (const gchar *directory,
                                                             const gchar *executable);

const gchar *       ic_unix_process_get_directory           (IcUnixProcess *self);

GInputStream *      ic_unix_process_get_input_stream        (IcUnixProcess *self);

void                ic_unix_process_set_input_stream        (IcUnixProcess *self,
                                                             GInputStream *input);

GOutputStream *     ic_unix_process_get_output_stream       (IcUnixProcess *self);

void                ic_unix_process_set_output_stream       (IcUnixProcess *self,
                                                             GOutputStream *output);

GOutputStream *     ic_unix_process_get_error_stream        (IcUnixProcess *self);

void                ic_unix_process_set_error_stream        (IcUnixProcess *self,
                                                             GOutputStream *output);

void                ic_unix_process_run_async               (IcUnixProcess *self,
                                                             const gchar **argv,
                                                             const gchar **envp,
                                                             GCancellable *cancellable,
                                                             GAsyncReadyCallback callback,
                                                             gpointer user_data);

gint                ic_unix_process_run_finish              (IcUnixProcess *self,
                                                             GAsyncResult *result,
                                                             GError **error);

G_END_DECLS

#endif /* IC_UNIX_PROCESS_H */
