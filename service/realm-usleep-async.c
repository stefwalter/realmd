/* realmd -- Realm configuration service
 *
 * Copyright 2013 Red Hat Inc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 *
 * Author: Marius Vollmer <marius.vollmer@redhat.com>
 */

#include "config.h"

#include "realm-usleep-async.h"

typedef struct {
	gboolean completed;
	guint timeout_id;
	GCancellable *cancellable;
	guint cancel_id;
} SleepAsyncData;

static void
complete_sleep_async (GSimpleAsyncResult *async)
{
	SleepAsyncData *data = g_simple_async_result_get_op_res_gpointer (async);
	g_object_ref (async);
	if (!data->completed) {
		if (data->timeout_id > 0)
			g_source_remove (data->timeout_id);
		if (data->cancel_id > 0) {
			g_signal_handler_disconnect (data->cancellable, data->cancel_id);
			g_object_unref (data->cancellable);
		}
		data->completed = TRUE;
		g_simple_async_result_complete (async);
	}
	g_object_unref (async);
}

static gboolean
on_sleep_async_done (gpointer user_data)
{
	GSimpleAsyncResult *async = user_data;
	SleepAsyncData *data = g_simple_async_result_get_op_res_gpointer (async);
	data->timeout_id = 0;
	complete_sleep_async (async);
	return FALSE;
}

static void
on_sleep_async_cancelled (GCancellable *cancellable,
                          gpointer user_data)
{
	GSimpleAsyncResult *async = user_data;
	complete_sleep_async (async);
}

void
realm_usleep_async (gulong microseconds,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
	GSimpleAsyncResult *async = g_simple_async_result_new (NULL,
	                                                       callback, user_data,
	                                                       realm_usleep_async);

	SleepAsyncData *data = g_new0 (SleepAsyncData, 1);
	g_simple_async_result_set_op_res_gpointer (async, data, g_free);

	if (cancellable) {
		g_simple_async_result_set_check_cancellable (async, cancellable);
		if (g_cancellable_is_cancelled (cancellable)) {
			g_simple_async_result_complete_in_idle (async);
			g_object_unref (async);
			return;
		}
		data->cancellable = g_object_ref (cancellable);
		data->cancel_id = g_cancellable_connect (cancellable,
		                                         G_CALLBACK (on_sleep_async_cancelled),
		                                         g_object_ref (async), g_object_unref);
	}

	data->timeout_id = g_timeout_add_full (G_PRIORITY_DEFAULT,
	                                       microseconds / 1000,
	                                       on_sleep_async_done,
	                                       g_object_ref (async), g_object_unref);

	g_object_unref (async);
}

gboolean
realm_usleep_finish (GAsyncResult *result,
                     GError **error)
{
	GSimpleAsyncResult *async;

	g_return_val_if_fail (g_simple_async_result_is_valid (result,
	                                                      NULL,
	                                                      realm_usleep_async),
	                      FALSE);

	async = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (async, error))
		return FALSE;

	return TRUE;
}
