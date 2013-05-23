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
 * Author: Stef Walter <stefw@redhat.com>
 */

#ifndef __REALM_DISCO_MSCLDAP_H__
#define __REALM_DISCO_MSCLDAP_H__

#include "realm-disco.h"

#include <gio/gio.h>

#include <ldap.h>

void           realm_disco_mscldap_async      (GSocketAddress *address,
                                               GSocketProtocol protocol,
                                               const gchar *explicit_server,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data);

RealmDisco *   realm_disco_mscldap_finish     (GAsyncResult *result,
                                               GError **error);

gboolean       realm_disco_mscldap_result     (LDAP *ldap,
                                               LDAPMessage *message,
                                               RealmDisco *disco,
                                               GError **error);

gboolean       realm_disco_mscldap_request    (LDAP *ldap,
                                               int *msgidp,
                                               GError **error);

#endif /* __REALM_DISCO_MSCLDAP_H__ */
