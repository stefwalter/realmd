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

#ifndef __REALM_H__
#define __REALM_H__

#include <glib.h>
#include <gio/gio.h>

#include "realm-dbus-generated.h"
#include "realm-client.h"

G_BEGIN_DECLS

#define               realm_operation_id           "realm-enroll"

int                   realm_join                   (int argc,
                                                    char *argv[]);

int                   realm_leave                  (int argc,
                                                    char *argv[]);

int                   realm_discover               (int argc,
                                                    char *argv[]);

int                   realm_list                   (int argc,
                                                    char *argv[]);

int                   realm_permit                 (int argc,
                                                    char *argv[]);

int                   realm_deny                   (int argc,
                                                    char *argv[]);

GVariant *            realm_build_options          (const gchar *first,
                                                    ...) G_GNUC_NULL_TERMINATED;

void                  realm_print_error            (const gchar *format,
                                                    ...) G_GNUC_PRINTF (1, 2);

void                  realm_handle_error           (GError *error,
                                                    const gchar *format,
                                                    ...) G_GNUC_PRINTF (2, 3);

GVariant *            realm_kinit_to_kerberos_cache     (const gchar *name,
                                                         const gchar *realm,
                                                         const gchar *password,
                                                         GError **error);

G_END_DECLS

#endif /* __REALM_H__ */
