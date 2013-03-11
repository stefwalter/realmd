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
	GSimpleAsyncResult *async;
	guint timeout_id;
	GCancellable *cancellable;
	guint cancel_id;
} SleepAsyncData;

static void
free_sleep_async_data (gpointer user_data)
{
	SleepAsyncData *data = user_data;
	if (data->cancellable) {
		/* g_cancellable_disconnect would dead-lock here */
		g_signal_handler_disconnect (data->cancellable, data->cancel_id);
		g_object_unref (data->cancellable);
	}
	g_object_unref (data->async);
	g_free (data);
}

static gboolean
on_sleep_async_done (gpointer user_data)
{
	SleepAsyncData *data = user_data;
	g_simple_async_result_complete (data->async);
	return FALSE;
}

static void
on_sleep_async_cancelled (GCancellable *cancellable,
                          gpointer user_data)
{
	SleepAsyncData *data = user_data;
	g_simple_async_result_complete (data->async);
	g_source_remove (data->timeout_id);
}

void
realm_usleep_async (gulong microseconds,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
	SleepAsyncData *data = g_new0 (SleepAsyncData, 1);
	data->async = g_simple_async_result_new (NULL,
	                                         callback, user_data,
	                                         realm_usleep_async);

	if (cancellable) {
		data->cancellable = g_object_ref (cancellable);
		data->cancel_id = g_cancellable_connect (cancellable,
		                                         G_CALLBACK (on_sleep_async_cancelled),
		                                         data, NULL);
		g_simple_async_result_set_check_cancellable (data->async, cancellable);
	}

	data->timeout_id = g_timeout_add_full (G_PRIORITY_DEFAULT,
	                                       microseconds / 1000,
	                                       on_sleep_async_done,
	                                       data, (GDestroyNotify)free_sleep_async_data);
}

gboolean
realm_usleep_finish (GAsyncResult *result,
                     GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (g_simple_async_result_is_valid (result,
	                                                      NULL,
	                                                      realm_usleep_async),
	                      FALSE);

	simple = (GSimpleAsyncResult *) result;
	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return TRUE;
}
