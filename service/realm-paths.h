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

#ifndef __REALM_PATHS_H__
#define __REALM_PATHS_H__

G_BEGIN_DECLS

#ifndef REALM_NET_PATH
#define REALM_NET_PATH             "/usr/bin/net"
#endif

#ifndef REALM_WINBINDD_PATH
#define REALM_WINBINDD_PATH         "/usr/sbin/winbindd"
#endif

G_END_DECLS

#endif /* __REALM_PATHS_H__ */
