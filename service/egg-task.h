/* GIO - GLib Input, Output and Streaming Library
 *
 * Copyright 2011 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __EGG_TASK_H__
#define __EGG_TASK_H__

#include <gio/gio.h>

G_BEGIN_DECLS

/*
 * Copied from glib until 2.36 is stable and included in the various distros
 */

#define EGG_TYPE_TASK         (egg_task_get_type ())
#define EGG_TASK(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EGG_TYPE_TASK, EggTask))
#define EGG_TASK_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EGG_TYPE_TASK, EggTaskClass))
#define EGG_IS_TASK(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EGG_TYPE_TASK))
#define EGG_IS_TASK_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EGG_TYPE_TASK))
#define EGG_TASK_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EGG_TYPE_TASK, EggTaskClass))

typedef struct _EggTask        EggTask;
typedef struct _EggTaskClass   EggTaskClass;

GType         egg_task_get_type              (void) G_GNUC_CONST;

EggTask      *egg_task_new                   (gpointer             source_object,
                                              GCancellable        *cancellable,
                                              GAsyncReadyCallback  callback,
                                              gpointer             callback_data);

void          egg_task_report_error          (gpointer             source_object,
                                              GAsyncReadyCallback  callback,
                                              gpointer             callback_data,
                                              gpointer             source_tag,
                                              GError              *error);
void          egg_task_report_new_error      (gpointer             source_object,
                                              GAsyncReadyCallback  callback,
                                              gpointer             callback_data,
                                              gpointer             source_tag,
                                              GQuark               domain,
                                              gint                 code,
                                              const char          *format,
                                              ...) G_GNUC_PRINTF(7, 8);

void          egg_task_set_task_data         (EggTask               *task,
                                              gpointer             task_data,
                                              GDestroyNotify       task_data_destroy);
void          egg_task_set_priority          (EggTask               *task,
                                              gint                 priority);
void          egg_task_set_check_cancellable (EggTask               *task,
                                              gboolean             check_cancellable);
void          egg_task_set_source_tag        (EggTask               *task,
                                              gpointer             source_tag);

gpointer      egg_task_get_source_object     (EggTask               *task);
gpointer      egg_task_get_task_data         (EggTask               *task);
gint          egg_task_get_priority          (EggTask               *task);
GMainContext *egg_task_get_context           (EggTask               *task);
GCancellable *egg_task_get_cancellable       (EggTask               *task);
gboolean      egg_task_get_check_cancellable (EggTask               *task);
gpointer      egg_task_get_source_tag        (EggTask               *task);

gboolean      egg_task_is_valid              (gpointer             result,
                                              gpointer             source_object);


typedef void (*EggTaskThreadFunc)           (EggTask           *task,
                                             gpointer         source_object,
                                             gpointer         task_data,
                                             GCancellable    *cancellable);
void          egg_task_run_in_thread        (EggTask           *task,
                                             EggTaskThreadFunc  task_func);
void          egg_task_run_in_thread_sync   (EggTask           *task,
                                             EggTaskThreadFunc  task_func);
gboolean      egg_task_set_return_on_cancel (EggTask           *task,
                                             gboolean         return_on_cancel);
gboolean      egg_task_get_return_on_cancel (EggTask           *task);

void          egg_task_attach_source        (EggTask           *task,
                                             GSource         *source,
                                             GSourceFunc      callback);


void          egg_task_return_pointer            (EggTask           *task,
                                                  gpointer         result,
                                                  GDestroyNotify   result_destroy);
void          egg_task_return_boolean            (EggTask           *task,
                                                  gboolean         result);
void          egg_task_return_int                (EggTask           *task,
                                                  gssize           result);

void          egg_task_return_error              (EggTask           *task,
                                                  GError          *error);
void          egg_task_return_new_error          (EggTask           *task,
                                                  GQuark           domain,
                                                  gint             code,
                                                  const char      *format,
                                                  ...) G_GNUC_PRINTF (4, 5);

gboolean      egg_task_return_error_if_cancelled (EggTask           *task);

gpointer      egg_task_propagate_pointer         (EggTask           *task,
                                                  GError         **error);
gboolean      egg_task_propagate_boolean         (EggTask           *task,
                                                  GError         **error);
gssize        egg_task_propagate_int             (EggTask           *task,
                                                  GError         **error);
gboolean      egg_task_had_error                 (EggTask           *task);

G_END_DECLS

#endif /* __EGG_TASK_H__ */
