/* realmd -- Realm configuration service
 *
 * Copyright 2012 Red Hat Inc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 *
 * Author: Stef Walter <stefw@gnome.org>
 */

#include "config.h"

#ifndef __REALM_DIAGNOSTICS_H__
#define __REALM_DIAGNOSTICS_H__

#include <gio/gio.h>

G_BEGIN_DECLS

void          realm_diagnostics_initialize            (GDBusConnection *connection);

void          realm_diagnostics_info                  (GDBusMethodInvocation *invocation,
                                                       const gchar *format,
                                                       ...) G_GNUC_PRINTF (2, 3);

void          realm_diagnostics_info_data             (GDBusMethodInvocation *invocation,
                                                       const gchar *data,
                                                       gssize n_data);

void          realm_diagnostics_error                 (GDBusMethodInvocation *invocation,
                                                       GError *error,
                                                       const gchar *format,
                                                       ...) G_GNUC_PRINTF (3, 4);

void          realm_diagnostics_signal                (GDBusMethodInvocation *invocation,
                                                       const gchar *data);

G_END_DECLS

#endif /* __REALM_DIAGNOSTICS_H__ */
