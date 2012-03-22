/* identity-config - Identity configuration service
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

#ifndef __IC_DIAGNOSTICS_H__
#define __IC_DIAGNOSTICS_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#if 0
void                ic_diagnostics_push_invocation     (GDBusMethodInvocation *invocation);

void                ic_diagnostics_pop_invocation      (GDBusMethodInvocation *invocation);
#endif

void          ic_diagnostics_initialize            (GDBusConnection *connection);

void          ic_diagnostics_info                  (GDBusMethodInvocation *invocation,
                                                    const gchar *format,
                                                    ...) G_GNUC_PRINTF (2, 3);

void          ic_diagnostics_info_data             (GDBusMethodInvocation *invocation,
                                                    const gchar *data,
                                                    gssize n_data);

void          ic_diagnostics_error                 (GDBusMethodInvocation *invocation,
                                                    GError *error,
                                                    const gchar *format,
                                                    ...) G_GNUC_PRINTF (3, 4);

G_END_DECLS

#endif /* __IC_DIAGNOSTICS_H__ */
