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

#ifndef __REALM_LOGIN_NAME_H__
#define __REALM_LOGIN_NAME_H__

#include <glib.h>

G_BEGIN_DECLS

gchar *        realm_login_name_parse     (const gchar *const *formats,
                                           gboolean lower,
                                           const gchar *login);

gchar **       realm_login_name_parse_all (const gchar *const *formats,
                                           gboolean lower,
                                           const gchar **logins,
                                           const gchar **failed);

gchar *        realm_login_name_format    (const gchar *format,
                                           const gchar *user);

G_END_DECLS

#endif /* __REALM_LOGIN_NAME_H__ */
