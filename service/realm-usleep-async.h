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

#ifndef __REALM_USLEEP_ASYNC_H__
#define __REALM_USLEEP_ASYNC_H__

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

/* A cancellable delay
 */

void      realm_usleep_async       (gulong microseconds,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data);

gboolean  realm_usleep_finish      (GAsyncResult *result,
                                    GError **error);

G_END_DECLS

#endif /* __REALM_USLEEP_ASYNC_H__ */
