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

G_BEGIN_DECLS

int      realm_join         (int argc,
                             char *argv[]);

int      realm_leave        (int argc,
                             char *argv[]);

int      realm_discover     (int argc,
                             char *argv[]);

int      realm_list         (int argc,
                             char *argv[]);

G_END_DECLS

#endif /* __REALM_H__ */
