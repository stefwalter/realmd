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

#include "config.h"

#include "egg-task.h"

/**
 * EggTask:
 *
 * The opaque object representing a synchronous or asynchronous task
 * and its result.
 */

struct _EggTask {
  GObject parent_instance;

  gpointer source_object;
  gpointer source_tag;

  gpointer task_data;
  GDestroyNotify task_data_destroy;

  GMainContext *context;
  guint64 creation_time;
  gint priority;
  GCancellable *cancellable;
  gboolean check_cancellable;

  GAsyncReadyCallback callback;
  gpointer callback_data;

  EggTaskThreadFunc task_func;
  GMutex lock;
  GCond cond;
  gboolean return_on_cancel;
  gboolean thread_cancelled;
  gboolean synchronous;
  gboolean thread_complete;
  gboolean blocking_other_task;

  GError *error;
  union {
    gpointer pointer;
    gssize   size;
    gboolean boolean;
  } result;
  GDestroyNotify result_destroy;
  gboolean result_set;
};

#define EGG_TASK_IS_THREADED(task) ((task)->task_func != NULL)

struct _EggTaskClass
{
  GObjectClass parent_class;
};

static void egg_task_thread_pool_resort (void);

static void egg_task_async_result_iface_init (GAsyncResultIface *iface);
static void egg_task_thread_pool_init (void);

G_DEFINE_TYPE_WITH_CODE (EggTask, egg_task, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_RESULT,
                                                egg_task_async_result_iface_init);
                         egg_task_thread_pool_init ();)

static GThreadPool *task_pool;
static GMutex task_pool_mutex;
static GPrivate task_private = G_PRIVATE_INIT (NULL);

static void
egg_task_init (EggTask *task)
{
  task->check_cancellable = TRUE;
}

static void
egg_task_finalize (GObject *object)
{
  EggTask *task = EGG_TASK (object);

  g_clear_object (&task->source_object);
  g_clear_object (&task->cancellable);

  if (task->context)
    g_main_context_unref (task->context);

  if (task->task_data_destroy)
    task->task_data_destroy (task->task_data);

  if (task->result_destroy && task->result.pointer)
    task->result_destroy (task->result.pointer);

  if (EGG_TASK_IS_THREADED (task))
    {
      g_mutex_clear (&task->lock);
      g_cond_clear (&task->cond);
    }

  G_OBJECT_CLASS (egg_task_parent_class)->finalize (object);
}

/**
 * egg_task_new:
 * @source_object: (allow-none) (type GObject): the #GObject that owns
 *   this task, or %NULL.
 * @cancellable: (allow-none): optional #GCancellable object, %NULL to ignore.
 * @callback: (scope async): a #GAsyncReadyCallback.
 * @callback_data: (closure): user data passed to @callback.
 *
 * Creates a #EggTask acting on @source_object, which will eventually be
 * used to invoke @callback in the current <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * context</link>.
 *
 * Call this in the "start" method of your asynchronous method, and
 * pass the #EggTask around throughout the asynchronous operation. You
 * can use egg_task_set_task_data() to attach task-specific data to the
 * object, which you can retrieve later via egg_task_get_task_data().
 *
 * By default, if @cancellable is cancelled, then the return value of
 * the task will always be %G_IO_ERROR_CANCELLED, even if the task had
 * already completed before the cancellation. This allows for
 * simplified handling in cases where cancellation may imply that
 * other objects that the task depends on have been destroyed. If you
 * do not want this behavior, you can use
 * egg_task_set_check_cancellable() to change it.
 *
 * Returns: a #EggTask.
 *
 * Since: 2.36
 */
EggTask *
egg_task_new (gpointer              source_object,
              GCancellable         *cancellable,
              GAsyncReadyCallback   callback,
              gpointer              callback_data)
{
  EggTask *task;
  GSource *source;

  task = g_object_new (EGG_TYPE_TASK, NULL);
  task->source_object = source_object ? g_object_ref (source_object) : NULL;
  task->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
  task->callback = callback;
  task->callback_data = callback_data;
  task->context = g_main_context_ref_thread_default ();

  source = g_main_current_source ();
  if (source)
    task->creation_time = g_source_get_time (source);

  return task;
}

/**
 * egg_task_report_error:
 * @source_object: (allow-none) (type GObject): the #GObject that owns
 *   this task, or %NULL.
 * @callback: (scope async): a #GAsyncReadyCallback.
 * @callback_data: (closure): user data passed to @callback.
 * @source_tag: an opaque pointer indicating the source of this task
 * @error: (transfer full): error to report
 *
 * Creates a #EggTask and then immediately calls egg_task_return_error()
 * on it. Use this in the wrapper function of an asynchronous method
 * when you want to avoid even calling the virtual method. You can
 * then use g_async_result_is_tagged() in the finish method wrapper to
 * check if the result there is tagged as having been created by the
 * wrapper method, and deal with it appropriately if so.
 *
 * See also egg_task_report_new_error().
 *
 * Since: 2.36
 */
void
egg_task_report_error (gpointer             source_object,
                       GAsyncReadyCallback  callback,
                       gpointer             callback_data,
                       gpointer             source_tag,
                       GError              *error)
{
  EggTask *task;

  task = egg_task_new (source_object, NULL, callback, callback_data);
  egg_task_set_source_tag (task, source_tag);
  egg_task_return_error (task, error);
  g_object_unref (task);
}

/**
 * egg_task_report_new_error:
 * @source_object: (allow-none) (type GObject): the #GObject that owns
 *   this task, or %NULL.
 * @callback: (scope async): a #GAsyncReadyCallback.
 * @callback_data: (closure): user data passed to @callback.
 * @source_tag: an opaque pointer indicating the source of this task
 * @domain: a #GQuark.
 * @code: an error code.
 * @format: a string with format characters.
 * @...: a list of values to insert into @format.
 *
 * Creates a #EggTask and then immediately calls
 * egg_task_return_new_error() on it. Use this in the wrapper function
 * of an asynchronous method when you want to avoid even calling the
 * virtual method. You can then use g_async_result_is_tagged() in the
 * finish method wrapper to check if the result there is tagged as
 * having been created by the wrapper method, and deal with it
 * appropriately if so.
 *
 * See also egg_task_report_error().
 *
 * Since: 2.36
 */
void
egg_task_report_new_error (gpointer             source_object,
                           GAsyncReadyCallback  callback,
                           gpointer             callback_data,
                           gpointer             source_tag,
                           GQuark               domain,
                           gint                 code,
                           const char          *format,
                           ...)
{
  GError *error;
  va_list ap;

  va_start (ap, format);
  error = g_error_new_valist (domain, code, format, ap);
  va_end (ap);

  egg_task_report_error (source_object, callback, callback_data,
                       source_tag, error);
}

/**
 * egg_task_set_task_data:
 * @task: the #EggTask
 * @task_data: (allow-none): task-specific data
 * @task_data_destroy: (allow-none): #GDestroyNotify for @task_data
 *
 * Sets @task's task data (freeing the existing task data, if any).
 *
 * Since: 2.36
 */
void
egg_task_set_task_data (EggTask          *task,
                        gpointer        task_data,
                        GDestroyNotify  task_data_destroy)
{
  if (task->task_data_destroy)
    task->task_data_destroy (task->task_data);

  task->task_data = task_data;
  task->task_data_destroy = task_data_destroy;
}

/**
 * egg_task_set_priority:
 * @task: the #EggTask
 * @priority: the <link linkend="io-priority">priority</link>
 *   of the request.
 *
 * Sets @task's priority. If you do not call this, it will default to
 * %G_PRIORITY_DEFAULT.
 *
 * This will affect the priority of #GSources created with
 * egg_task_attach_source() and the scheduling of tasks run in threads,
 * and can also be explicitly retrieved later via
 * egg_task_get_priority().
 *
 * Since: 2.36
 */
void
egg_task_set_priority (EggTask *task,
                       gint   priority)
{
  task->priority = priority;
}

/**
 * egg_task_set_check_cancellable:
 * @task: the #EggTask
 * @check_cancellable: whether #EggTask will check the state of
 *   its #GCancellable for you.
 *
 * Sets or clears @task's check-cancellable flag. If this is %TRUE
 * (the default), then egg_task_propagate_pointer(), etc, and
 * egg_task_had_error() will check the task's #GCancellable first, and
 * if it has been cancelled, then they will consider the task to have
 * returned an "Operation was cancelled" error
 * (%G_IO_ERROR_CANCELLED), regardless of any other error or return
 * value the task may have had.
 *
 * If @check_cancellable is %FALSE, then the #EggTask will not check the
 * cancellable itself, and it is up to @task's owner to do this (eg,
 * via egg_task_return_error_if_cancelled()).
 *
 * If you are using egg_task_set_return_on_cancel() as well, then
 * you must leave check-cancellable set %TRUE.
 *
 * Since: 2.36
 */
void
egg_task_set_check_cancellable (EggTask    *task,
                                gboolean  check_cancellable)
{
  g_return_if_fail (check_cancellable || !task->return_on_cancel);

  task->check_cancellable = check_cancellable;
}

static void egg_task_thread_complete (EggTask *task);

/**
 * egg_task_set_return_on_cancel:
 * @task: the #EggTask
 * @return_on_cancel: whether the task returns automatically when
 *   it is cancelled.
 *
 * Sets or clears @task's return-on-cancel flag. This is only
 * meaningful for tasks run via egg_task_run_in_thread() or
 * egg_task_run_in_thread_sync().
 *
 * If @return_on_cancel is %TRUE, then cancelling @task's
 * #GCancellable will immediately cause it to return, as though the
 * task's #EggTaskThreadFunc had called
 * egg_task_return_error_if_cancelled() and then returned.
 *
 * This allows you to create a cancellable wrapper around an
 * uninterruptable function. The #EggTaskThreadFunc just needs to be
 * careful that it does not modify any externally-visible state after
 * it has been cancelled. To do that, the thread should call
 * egg_task_set_return_on_cancel() again to (atomically) set
 * return-on-cancel %FALSE before making externally-visible changes;
 * if the task gets cancelled before the return-on-cancel flag could
 * be changed, egg_task_set_return_on_cancel() will indicate this by
 * returning %FALSE.
 *
 * You can disable and re-enable this flag multiple times if you wish.
 * If the task's #GCancellable is cancelled while return-on-cancel is
 * %FALSE, then calling egg_task_set_return_on_cancel() to set it %TRUE
 * again will cause the task to be cancelled at that point.
 *
 * If the task's #GCancellable is already cancelled before you call
 * egg_task_run_in_thread()/egg_task_run_in_thread_sync(), then the
 * #EggTaskThreadFunc will still be run (for consistency), but the task
 * will also be completed right away.
 *
 * Returns: %TRUE if @task's return-on-cancel flag was changed to
 *   match @return_on_cancel. %FALSE if @task has already been
 *   cancelled.
 *
 * Since: 2.36
 */
gboolean
egg_task_set_return_on_cancel (EggTask    *task,
                               gboolean  return_on_cancel)
{
  g_return_val_if_fail (task->check_cancellable || !return_on_cancel, FALSE);

  if (!EGG_TASK_IS_THREADED (task))
    {
      task->return_on_cancel = return_on_cancel;
      return TRUE;
    }

  g_mutex_lock (&task->lock);
  if (task->thread_cancelled)
    {
      if (return_on_cancel && !task->return_on_cancel)
        {
          g_mutex_unlock (&task->lock);
          egg_task_thread_complete (task);
        }
      else
        g_mutex_unlock (&task->lock);
      return FALSE;
    }
  task->return_on_cancel = return_on_cancel;
  g_mutex_unlock (&task->lock);

  return TRUE;
}

/**
 * egg_task_set_source_tag:
 * @task: the #EggTask
 * @source_tag: an opaque pointer indicating the source of this task
 *
 * Sets @task's source tag. You can use this to tag a task return
 * value with a particular pointer (usually a pointer to the function
 * doing the tagging) and then later check it using
 * egg_task_get_source_tag() (or g_async_result_is_tagged()) in the
 * task's "finish" function, to figure out if the response came from a
 * particular place.
 *
 * Since: 2.36
 */
void
egg_task_set_source_tag (EggTask    *task,
                       gpointer  source_tag)
{
  task->source_tag = source_tag;
}

/**
 * egg_task_get_source_object:
 * @task: a #EggTask
 *
 * Gets the source object from @task. Like
 * xxg_async_result_get_source_object(), but does not ref the object.
 *
 * Returns: (transfer none) (type GObject): @task's source object, or %NULL
 *
 * Since: 2.36
 */
gpointer
egg_task_get_source_object (EggTask *task)
{
  return task->source_object;
}

static GObject *
egg_task_ref_source_object (GAsyncResult *res)
{
  EggTask *task = EGG_TASK (res);

  if (task->source_object)
    return g_object_ref (task->source_object);
  else
    return NULL;
}

/**
 * egg_task_get_task_data:
 * @task: a #EggTask
 *
 * Gets @task's <literal>task_data</literal>.
 *
 * Returns: (transfer none): @task's <literal>task_data</literal>.
 *
 * Since: 2.36
 */
gpointer
egg_task_get_task_data (EggTask *task)
{
  return task->task_data;
}

/**
 * egg_task_get_priority:
 * @task: a #EggTask
 *
 * Gets @task's priority
 *
 * Returns: @task's priority
 *
 * Since: 2.36
 */
gint
egg_task_get_priority (EggTask *task)
{
  return task->priority;
}

/**
 * egg_task_get_context:
 * @task: a #EggTask
 *
 * Gets the #GMainContext that @task will return its result in (that
 * is, the context that was the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * context</link> at the point when @task was created).
 *
 * This will always return a non-%NULL value, even if the task's
 * context is the default #GMainContext.
 *
 * Returns: (transfer none): @task's #GMainContext
 *
 * Since: 2.36
 */
GMainContext *
egg_task_get_context (EggTask *task)
{
  return task->context;
}

/**
 * egg_task_get_cancellable:
 * @task: a #EggTask
 *
 * Gets @task's #GCancellable
 *
 * Returns: (transfer none): @task's #GCancellable
 *
 * Since: 2.36
 */
GCancellable *
egg_task_get_cancellable (EggTask *task)
{
  return task->cancellable;
}

/**
 * egg_task_get_check_cancellable:
 * @task: the #EggTask
 *
 * Gets @task's check-cancellable flag. See
 * egg_task_set_check_cancellable() for more details.
 *
 * Since: 2.36
 */
gboolean
egg_task_get_check_cancellable (EggTask *task)
{
  return task->check_cancellable;
}

/**
 * egg_task_get_return_on_cancel:
 * @task: the #EggTask
 *
 * Gets @task's return-on-cancel flag. See
 * egg_task_set_return_on_cancel() for more details.
 *
 * Since: 2.36
 */
gboolean
egg_task_get_return_on_cancel (EggTask *task)
{
  return task->return_on_cancel;
}

/**
 * egg_task_get_source_tag:
 * @task: a #EggTask
 *
 * Gets @task's source tag. See egg_task_set_source_tag().
 *
 * Return value: (transfer none): @task's source tag
 *
 * Since: 2.36
 */
gpointer
egg_task_get_source_tag (EggTask *task)
{
  return task->source_tag;
}


static void
egg_task_return_now (EggTask *task)
{
  g_main_context_push_thread_default (task->context);
  task->callback (task->source_object,
                  G_ASYNC_RESULT (task),
                  task->callback_data);
  g_main_context_pop_thread_default (task->context);
}

static gboolean
complete_in_idle_cb (gpointer task)
{
  egg_task_return_now (task);
  g_object_unref (task);
  return FALSE;
}

typedef enum {
  EGG_TASK_RETURN_SUCCESS,
  EGG_TASK_RETURN_ERROR,
  EGG_TASK_RETURN_FROM_THREAD
} EggTaskReturnType;

static void
egg_task_return (EggTask           *task,
                 EggTaskReturnType  type)
{
  GSource *source;

  if (type == EGG_TASK_RETURN_SUCCESS)
    task->result_set = TRUE;

  if (task->synchronous || !task->callback)
    return;

  /* Normally we want to invoke the task's callback when its return
   * value is set. But if the task is running in a thread, then we
   * want to wait until after the task_func returns, to simplify
   * locking/refcounting/etc.
   */
  if (EGG_TASK_IS_THREADED (task) && type != EGG_TASK_RETURN_FROM_THREAD)
    return;

  g_object_ref (task);

  /* See if we can complete the task immediately. First, we have to be
   * running inside the task's thread/GMainContext.
   */
  source = g_main_current_source ();
  if (source && g_source_get_context (source) == task->context)
    {
      /* Second, we can only complete immediately if this is not the
       * same iteration of the main loop that the task was created in.
       */
      if (g_source_get_time (source) > task->creation_time)
        {
          egg_task_return_now (task);
          g_object_unref (task);
          return;
        }
    }

  /* Otherwise, complete in the next iteration */
  source = g_idle_source_new ();
  egg_task_attach_source (task, source, complete_in_idle_cb);
  g_source_unref (source);
}


/**
 * EggTaskThreadFunc:
 * @task: the #EggTask
 * @source_object: (type GObject): @task's source object
 * @task_data: @task's task data
 * @cancellable: @task's #GCancellable, or %NULL
 *
 * The prototype for a task function to be run in a thread via
 * egg_task_run_in_thread() or egg_task_run_in_thread_sync().
 *
 * If the return-on-cancel flag is set on @task, and @cancellable gets
 * cancelled, then the #EggTask will be completed immediately (as though
 * egg_task_return_error_if_cancelled() had been called), without
 * waiting for the task function to complete. However, the task
 * function will continue running in its thread in the background. The
 * function therefore needs to be careful about how it uses
 * externally-visible state in this case. See
 * egg_task_set_return_on_cancel() for more details.
 *
 * Other than in that case, @task will be completed when the
 * #EggTaskThreadFunc returns, <emphasis>not</emphasis> when it calls
 * a <literal>egg_task_return_</literal> function.
 *
 * Since: 2.36
 */

static void task_thread_cancelled (GCancellable *cancellable,
                                   gpointer      user_data);

static void
egg_task_thread_complete (EggTask *task)
{
  g_mutex_lock (&task->lock);
  if (task->thread_complete)
    {
      /* The task belatedly completed after having been cancelled
       * (or was cancelled in the midst of being completed).
       */
      g_mutex_unlock (&task->lock);
      return;
    }

  task->thread_complete = TRUE;

  if (task->blocking_other_task)
    {
      g_mutex_lock (&task_pool_mutex);
      g_thread_pool_set_max_threads (task_pool,
                                     g_thread_pool_get_max_threads (task_pool) - 1,
                                     NULL);
      g_mutex_unlock (&task_pool_mutex);
    }
  g_mutex_unlock (&task->lock);

  if (task->cancellable)
    g_signal_handlers_disconnect_by_func (task->cancellable, task_thread_cancelled, task);

  if (task->synchronous)
    g_cond_signal (&task->cond);
  else
    egg_task_return (task, EGG_TASK_RETURN_FROM_THREAD);
}

static void
egg_task_thread_pool_thread (gpointer thread_data,
                             gpointer pool_data)
{
  EggTask *task = thread_data;

  g_private_set (&task_private, task);

  task->task_func (task, task->source_object, task->task_data,
                   task->cancellable);
  egg_task_thread_complete (task);

  g_private_set (&task_private, NULL);
  g_object_unref (task);
}

static void
task_thread_cancelled (GCancellable *cancellable,
                       gpointer      user_data)
{
  EggTask *task = user_data;

  egg_task_thread_pool_resort ();

  g_mutex_lock (&task->lock);
  task->thread_cancelled = TRUE;

  if (!task->return_on_cancel)
    {
      g_mutex_unlock (&task->lock);
      return;
    }

  /* We don't actually set task->error; egg_task_return_error() doesn't
   * use a lock, and egg_task_propagate_error() will call
   * g_cancellable_set_error_if_cancelled() anyway.
   */
  g_mutex_unlock (&task->lock);
  egg_task_thread_complete (task);
}

static void
task_thread_cancelled_disconnect_notify (gpointer  task,
                                         GClosure *closure)
{
  g_object_unref (task);
}

static void
egg_task_start_task_thread (EggTask           *task,
                            EggTaskThreadFunc  task_func)
{
  g_mutex_init (&task->lock);
  g_cond_init (&task->cond);

  g_mutex_lock (&task->lock);

  task->task_func = task_func;

  if (task->cancellable)
    {
      if (task->return_on_cancel &&
          g_cancellable_set_error_if_cancelled (task->cancellable,
                                                &task->error))
        {
          task->thread_cancelled = task->thread_complete = TRUE;
          g_thread_pool_push (task_pool, g_object_ref (task), NULL);
          return;
        }

      g_signal_connect_data (task->cancellable, "cancelled",
                             G_CALLBACK (task_thread_cancelled),
                             g_object_ref (task),
                             task_thread_cancelled_disconnect_notify, 0);
    }

  g_thread_pool_push (task_pool, g_object_ref (task), &task->error);
  if (task->error)
    task->thread_complete = TRUE;
  else if (g_private_get (&task_private))
    {
      /* This thread is being spawned from another EggTask thread, so
       * bump up max-threads so we don't starve.
       */
      g_mutex_lock (&task_pool_mutex);
      if (g_thread_pool_set_max_threads (task_pool,
                                         g_thread_pool_get_max_threads (task_pool) + 1,
                                         NULL))
        task->blocking_other_task = TRUE;
      g_mutex_unlock (&task_pool_mutex);
    }
}

/**
 * egg_task_run_in_thread:
 * @task: a #EggTask
 * @task_func: a #EggTaskThreadFunc
 *
 * Runs @task_func in another thread. When @task_func returns, @task's
 * #GAsyncReadyCallback will be invoked in @task's #GMainContext.
 *
 * This takes a ref on @task until the task completes.
 *
 * See #EggTaskThreadFunc for more details about how @task_func is handled.
 *
 * Since: 2.36
 */
void
egg_task_run_in_thread (EggTask           *task,
                        EggTaskThreadFunc  task_func)
{
  g_return_if_fail (EGG_IS_TASK (task));

  g_object_ref (task);
  egg_task_start_task_thread (task, task_func);

  /* The task may already be cancelled, or g_thread_pool_push() may
   * have failed.
   */
  if (task->thread_complete)
    {
      g_mutex_unlock (&task->lock);
      egg_task_return (task, EGG_TASK_RETURN_FROM_THREAD);
    }
  else
    g_mutex_unlock (&task->lock);

  g_object_unref (task);
}

/**
 * egg_task_run_in_thread_sync:
 * @task: a #EggTask
 * @task_func: a #EggTaskThreadFunc
 *
 * Runs @task_func in another thread, and waits for it to return or be
 * cancelled. You can use egg_task_propagate_pointer(), etc, afterward
 * to get the result of @task_func.
 *
 * See #EggTaskThreadFunc for more details about how @task_func is handled.
 *
 * Normally this is used with tasks created with a %NULL
 * <literal>callback</literal>, but note that even if the task does
 * have a callback, it will not be invoked when @task_func returns.
 *
 * Since: 2.36
 */
void
egg_task_run_in_thread_sync (EggTask           *task,
                             EggTaskThreadFunc  task_func)
{
  g_return_if_fail (EGG_IS_TASK (task));

  g_object_ref (task);

  task->synchronous = TRUE;
  egg_task_start_task_thread (task, task_func);

  while (!task->thread_complete)
    g_cond_wait (&task->cond, &task->lock);

  g_mutex_unlock (&task->lock);
  g_object_unref (task);
}

/**
 * egg_task_attach_source:
 * @task: a #EggTask
 * @source: the source to attach
 * @callback: the callback to invoke when @source triggers
 *
 * A utility function for dealing with async operations where you need
 * to wait for a #GSource to trigger. Attaches @source to @task's
 * #GMainContext with @task's <link
 * linkend="io-priority">priority</link>, and sets @source's callback
 * to @callback, with @task as the callback's
 * <literal>user_data</literal>.
 *
 * This takes a reference on @task until @source is destroyed.
 *
 * Since: 2.36
 */
void
egg_task_attach_source (EggTask       *task,
                        GSource     *source,
                        GSourceFunc  callback)
{
  g_source_set_callback (source, callback,
                         g_object_ref (task), g_object_unref);
  g_source_set_priority (source, task->priority);
  g_source_attach (source, task->context);
}


static gboolean
egg_task_propagate_error (EggTask   *task,
                          GError **error)
{
  if (task->check_cancellable &&
      g_cancellable_set_error_if_cancelled (task->cancellable, error))
    return TRUE;
  else if (task->error)
    {
      g_propagate_error (error, task->error);
      task->error = NULL;
      return TRUE;
    }
  else
    return FALSE;
}

/**
 * egg_task_return_pointer:
 * @task: a #EggTask
 * @result: (allow-none) (transfer full): the pointer result of a task
 *     function
 * @result_destroy: (allow-none): a #GDestroyNotify function.
 *
 * Sets @task's result to @result and completes the task. If @result
 * is not %NULL, then @result_destroy will be used to free @result if
 * the caller does not take ownership of it with
 * egg_task_propagate_pointer().
 *
 * "Completes the task" means that for an ordinary asynchronous task
 * it will either invoke the task's callback, or else queue that
 * callback to be invoked in the proper #GMainContext, or in the next
 * iteration of the current #GMainContext. For a task run via
 * egg_task_run_in_thread() or egg_task_run_in_thread_sync(), calling this
 * method will save @result to be returned to the caller later, but
 * the task will not actually be completed until the #EggTaskThreadFunc
 * exits.
 *
 * Note that since the task may be completed before returning from
 * egg_task_return_pointer(), you cannot assume that @result is still
 * valid after calling this, unless you are still holding another
 * reference on it.
 *
 * Since: 2.36
 */
void
egg_task_return_pointer (EggTask          *task,
                         gpointer        result,
                         GDestroyNotify  result_destroy)
{
  g_return_if_fail (task->result_set == FALSE);

  task->result.pointer = result;
  task->result_destroy = result_destroy;

  egg_task_return (task, EGG_TASK_RETURN_SUCCESS);
}

/**
 * egg_task_propagate_pointer:
 * @task: a #EggTask
 * @error: return location for a #GError
 *
 * Gets the result of @task as a pointer, and transfers ownership
 * of that value to the caller.
 *
 * If the task resulted in an error, or was cancelled, then this will
 * instead return %NULL and set @error.
 *
 * Since this method transfers ownership of the return value (or
 * error) to the caller, you may only call it once.
 *
 * Returns: (transfer full): the task result, or %NULL on error
 *
 * Since: 2.36
 */
gpointer
egg_task_propagate_pointer (EggTask   *task,
                            GError **error)
{
  if (egg_task_propagate_error (task, error))
    return NULL;

  g_return_val_if_fail (task->result_set == TRUE, NULL);

  task->result_destroy = NULL;
  task->result_set = FALSE;
  return task->result.pointer;
}

/**
 * egg_task_return_int:
 * @task: a #EggTask.
 * @result: the integer (#gssize) result of a task function.
 *
 * Sets @task's result to @result and completes the task (see
 * egg_task_return_pointer() for more discussion of exactly what this
 * means).
 *
 * Since: 2.36
 */
void
egg_task_return_int (EggTask  *task,
                     gssize  result)
{
  g_return_if_fail (task->result_set == FALSE);

  task->result.size = result;

  egg_task_return (task, EGG_TASK_RETURN_SUCCESS);
}

/**
 * egg_task_propagate_int:
 * @task: a #EggTask.
 * @error: return location for a #GError
 *
 * Gets the result of @task as an integer (#gssize).
 *
 * If the task resulted in an error, or was cancelled, then this will
 * instead return -1 and set @error.
 *
 * Since this method transfers ownership of the return value (or
 * error) to the caller, you may only call it once.
 *
 * Returns: the task result, or -1 on error
 *
 * Since: 2.36
 */
gssize
egg_task_propagate_int (EggTask   *task,
                        GError **error)
{
  if (egg_task_propagate_error (task, error))
    return -1;

  g_return_val_if_fail (task->result_set == TRUE, -1);

  task->result_set = FALSE;
  return task->result.size;
}

/**
 * egg_task_return_boolean:
 * @task: a #EggTask.
 * @result: the #gboolean result of a task function.
 *
 * Sets @task's result to @result and completes the task (see
 * egg_task_return_pointer() for more discussion of exactly what this
 * means).
 *
 * Since: 2.36
 */
void
egg_task_return_boolean (EggTask    *task,
                         gboolean  result)
{
  g_return_if_fail (task->result_set == FALSE);

  task->result.boolean = result;

  egg_task_return (task, EGG_TASK_RETURN_SUCCESS);
}

/**
 * egg_task_propagate_boolean:
 * @task: a #EggTask.
 * @error: return location for a #GError
 *
 * Gets the result of @task as a #gboolean.
 *
 * If the task resulted in an error, or was cancelled, then this will
 * instead return %FALSE and set @error.
 *
 * Since this method transfers ownership of the return value (or
 * error) to the caller, you may only call it once.
 *
 * Returns: the task result, or %FALSE on error
 *
 * Since: 2.36
 */
gboolean
egg_task_propagate_boolean (EggTask   *task,
                            GError **error)
{
  if (egg_task_propagate_error (task, error))
    return FALSE;

  g_return_val_if_fail (task->result_set == TRUE, FALSE);

  task->result_set = FALSE;
  return task->result.boolean;
}

/**
 * egg_task_return_error:
 * @task: a #EggTask.
 * @error: (transfer full): the #GError result of a task function.
 *
 * Sets @task's result to @error (which @task assumes ownership of)
 * and completes the task (see egg_task_return_pointer() for more
 * discussion of exactly what this means).
 *
 * Note that since the task takes ownership of @error, and since the
 * task may be completed before returning from egg_task_return_error(),
 * you cannot assume that @error is still valid after calling this.
 * Call g_error_copy() on the error if you need to keep a local copy
 * as well.
 *
 * See also egg_task_return_new_error().
 *
 * Since: 2.36
 */
void
egg_task_return_error (EggTask  *task,
                       GError *error)
{
  g_return_if_fail (task->result_set == FALSE);
  g_return_if_fail (error != NULL);

  task->error = error;

  egg_task_return (task, EGG_TASK_RETURN_ERROR);
}

/**
 * egg_task_return_new_error:
 * @task: a #EggTask.
 * @domain: a #GQuark.
 * @code: an error code.
 * @format: a string with format characters.
 * @...: a list of values to insert into @format.
 *
 * Sets @task's result to a new #GError created from @domain, @code,
 * @format, and the remaining arguments, and completes the task (see
 * egg_task_return_pointer() for more discussion of exactly what this
 * means).
 *
 * See also egg_task_return_error().
 *
 * Since: 2.36
 */
void
egg_task_return_new_error (EggTask           *task,
                           GQuark           domain,
                           gint             code,
                           const char      *format,
                           ...)
{
  GError *error;
  va_list args;

  va_start (args, format);
  error = g_error_new_valist (domain, code, format, args);
  va_end (args);

  egg_task_return_error (task, error);
}

/**
 * egg_task_return_error_if_cancelled:
 * @task: a #EggTask
 *
 * Checks if @task's #GCancellable has been cancelled, and if so, sets
 * @task's error accordingly and completes the task (see
 * egg_task_return_pointer() for more discussion of exactly what this
 * means).
 *
 * Return value: %TRUE if @task has been cancelled, %FALSE if not
 *
 * Since: 2.36
 */
gboolean
egg_task_return_error_if_cancelled (EggTask *task)
{
  GError *error = NULL;

  g_return_val_if_fail (task->result_set == FALSE, FALSE);

  if (g_cancellable_set_error_if_cancelled (task->cancellable, &error))
    {
      /* We explicitly set task->error so this works even when
       * check-cancellable is not set.
       */
      g_clear_error (&task->error);
      task->error = error;

      egg_task_return (task, EGG_TASK_RETURN_ERROR);
      return TRUE;
    }
  else
    return FALSE;
}

/**
 * egg_task_had_error:
 * @task: a #EggTask.
 *
 * Tests if @task resulted in an error.
 *
 * Returns: %TRUE if the task resulted in an error, %FALSE otherwise.
 *
 * Since: 2.36
 */
gboolean
egg_task_had_error (EggTask *task)
{
  if (task->error != NULL)
    return TRUE;

  if (task->check_cancellable && g_cancellable_is_cancelled (task->cancellable))
    return TRUE;

  return FALSE;
}

/**
 * egg_task_is_valid:
 * @result: (type Gio.AsyncResult): A #GAsyncResult
 * @source_object: (allow-none) (type GObject): the source object
 *   expected to be associated with the task
 *
 * Checks that @result is a #EggTask, and that @source_object is its
 * source object (or that @source_object is %NULL and @result has no
 * source object). This can be used in g_return_if_fail() checks.
 *
 * Return value: %TRUE if @result and @source_object are valid, %FALSE
 * if not
 *
 * Since: 2.36
 */
gboolean
egg_task_is_valid (gpointer result,
                   gpointer source_object)
{
  if (!EGG_IS_TASK (result))
    return FALSE;

  return EGG_TASK (result)->source_object == source_object;
}

static gint
egg_task_compare_priority (gconstpointer a,
                           gconstpointer b,
                           gpointer      user_data)
{
  const EggTask *ta = a;
  const EggTask *tb = b;
  gboolean a_cancelled, b_cancelled;

  /* Tasks that are causing other tasks to block have higher
   * priority.
   */
  if (ta->blocking_other_task && !tb->blocking_other_task)
    return -1;
  else if (tb->blocking_other_task && !ta->blocking_other_task)
    return 1;

  /* Let already-cancelled tasks finish right away */
  a_cancelled = (ta->check_cancellable &&
                 g_cancellable_is_cancelled (ta->cancellable));
  b_cancelled = (tb->check_cancellable &&
                 g_cancellable_is_cancelled (tb->cancellable));
  if (a_cancelled && !b_cancelled)
    return -1;
  else if (b_cancelled && !a_cancelled)
    return 1;

  /* Lower priority == run sooner == negative return value */
  return ta->priority - tb->priority;
}

static void
egg_task_thread_pool_init (void)
{
  task_pool = g_thread_pool_new (egg_task_thread_pool_thread, NULL,
                                 10, FALSE, NULL);
  g_assert (task_pool != NULL);

  g_thread_pool_set_sort_function (task_pool, egg_task_compare_priority, NULL);
}

static void
egg_task_thread_pool_resort (void)
{
  g_thread_pool_set_sort_function (task_pool, egg_task_compare_priority, NULL);
}

static void
egg_task_class_init (EggTaskClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = egg_task_finalize;
}

static gpointer
egg_task_get_user_data (GAsyncResult *res)
{
  return EGG_TASK (res)->callback_data;
}

static gboolean
egg_task_is_tagged (GAsyncResult *res,
                    gpointer      source_tag)
{
  return EGG_TASK (res)->source_tag == source_tag;
}

static void
egg_task_async_result_iface_init (GAsyncResultIface *iface)
{
  iface->get_user_data = egg_task_get_user_data;
  iface->get_source_object = egg_task_ref_source_object;
  iface->is_tagged = egg_task_is_tagged;
}
