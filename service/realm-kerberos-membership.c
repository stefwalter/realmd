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

#include "realm-kerberos-membership.h"

typedef RealmKerberosMembershipIface RealmKerberosMembershipInterface;
G_DEFINE_INTERFACE (RealmKerberosMembership, realm_kerberos_membership, 0);

static void
realm_kerberos_membership_default_init (RealmKerberosMembershipIface *iface)
{

}
