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

#include "realm-ipa-discover.h"
#include "realm-command.h"
#include "realm-daemon.h"
#include "realm-dbus-constants.h"
#include "realm-diagnostics.h"
#include "realm-discovery.h"
#include "realm-errors.h"
#include "realm-packages.h"
#include "realm-provider.h"
#include "realm-service.h"
#include "realm-sssd.h"
#include "realm-sssd-ipa.h"
#include "realm-sssd-config.h"

#include <glib/gstdio.h>

#include <errno.h>
#include <string.h>

struct _RealmSssdIpa {
	RealmSssd parent;
};

typedef struct {
	RealmSssdClass parent_class;
} RealmSssdIpaClass;

G_DEFINE_TYPE (RealmSssdIpa, realm_sssd_ipa, REALM_TYPE_SSSD);

static void
realm_sssd_ipa_init (RealmSssdIpa *self)
{

}

static void
realm_sssd_ipa_constructed (GObject *obj)
{
	RealmKerberos *kerberos = REALM_KERBEROS (obj);

	G_OBJECT_CLASS (realm_sssd_ipa_parent_class)->constructed (obj);

	realm_kerberos_set_details (kerberos,
	                            REALM_DBUS_OPTION_SERVER_SOFTWARE, REALM_DBUS_IDENTIFIER_FREEIPA,
	                            REALM_DBUS_OPTION_CLIENT_SOFTWARE, REALM_DBUS_IDENTIFIER_SSSD,
	                            NULL);
}

void
realm_sssd_ipa_class_init (RealmSssdIpaClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = realm_sssd_ipa_constructed;
}
