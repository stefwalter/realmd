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

#ifndef __REALM_SSSD_H__
#define __REALM_SSSD_H__

#include <gio/gio.h>

#include "realm-kerberos.h"
#include "realm-ini-config.h"

G_BEGIN_DECLS

#define REALM_TYPE_SSSD            (realm_sssd_get_type ())
#define REALM_SSSD(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_SSSD, RealmSssd))
#define REALM_IS_SSSD(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_SSSD))
#define REALM_SSSD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), REALM_TYPE_SSSD, RealmSssdClass))
#define REALM_IS_SSSD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), REALM_TYPE_SSSD))
#define REALM_SSSD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), REALM_TYPE_SSSD, RealmSssdClass))

typedef struct _RealmSssd RealmSssd;
typedef struct _RealmSssdClass RealmSssdClass;
typedef struct _RealmSssdPrivate RealmSssdPrivate;

struct _RealmSssd {
	RealmKerberos parent;
	RealmSssdPrivate *pv;
};

struct _RealmSssdClass {
	RealmKerberosClass parent_class;

	/*
	 * This is set by derived classes and is a value for the sssd.conf
	 * provider relevant to this realm, surch as "ipa" or "ad"
	 */
	const char *sssd_conf_provider_name;
};

typedef struct _RealmSssd RealmSssd;

GType               realm_sssd_get_type            (void) G_GNUC_CONST;

RealmIniConfig *    realm_sssd_get_config          (RealmSssd *self);

const gchar *       realm_sssd_get_config_section  (RealmSssd *self);

const gchar *       realm_sssd_get_config_domain   (RealmSssd *self);

gchar *             realm_sssd_build_default_home  (const gchar *value);

void                realm_sssd_deconfigure_domain_tail  (RealmSssd *self,
                                                         GTask *task,
                                                         GDBusMethodInvocation *invocation);

gboolean            realm_sssd_set_login_policy         (RealmIniConfig *config,
                                                         const gchar *section,
                                                         const gchar *access_provider,
                                                         const gchar **add_names,
                                                         const gchar **remove_names,
                                                         gboolean names_are_groups,
                                                         GError **error);

void                realm_sssd_update_properties        (RealmSssd *self);

G_END_DECLS

#endif /* __REALM_SSSD_H__ */
