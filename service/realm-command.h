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

#ifndef REALM_COMMAND_H
#define REALM_COMMAND_H

#include <gio/gio.h>

G_BEGIN_DECLS

void                realm_command_run_async                    (gchar **environ,
                                                                GDBusMethodInvocation *invocation,
                                                                GCancellable *cancellable,
                                                                GAsyncReadyCallback callback,
                                                                gpointer user_data,
                                                                const gchar *name_or_path,
                                                                ...) G_GNUC_NULL_TERMINATED;

void                realm_command_runv_async                   (gchar **name_or_path_and_arguments,
                                                                gchar **environ,
                                                                GDBusMethodInvocation *invocation,
                                                                GCancellable *cancellable,
                                                                GAsyncReadyCallback callback,
                                                                gpointer user_data);

void                realm_command_run_known_async              (const gchar *known_command,
                                                                gchar **environ,
                                                                GDBusMethodInvocation *invocation,
                                                                GCancellable *cancellable,
                                                                GAsyncReadyCallback callback,
                                                                gpointer user_data);

gint                realm_command_run_finish                   (GAsyncResult *result,
                                                                GString **output,
                                                                GError **error);

G_END_DECLS

#endif /* REALM_COMMAND_H */
