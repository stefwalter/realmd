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

#ifndef REALM_COMMAND_H
#define REALM_COMMAND_H

#include <gio/gio.h>

G_BEGIN_DECLS

void                realm_command_runv_async                   (gchar **name_or_path_and_arguments,
                                                                gchar **environ,
                                                                GBytes *input,
                                                                GDBusMethodInvocation *invocation,
                                                                GAsyncReadyCallback callback,
                                                                gpointer user_data);

void                realm_command_run_known_async              (const gchar *known_command,
                                                                gchar **environ,
                                                                GDBusMethodInvocation *invocation,
                                                                GAsyncReadyCallback callback,
                                                                gpointer user_data);

gint                realm_command_run_finish                   (GAsyncResult *result,
                                                                GString **output,
                                                                GError **error);

GBytes *            realm_command_build_password_line          (GBytes *password);

G_END_DECLS

#endif /* REALM_COMMAND_H */
