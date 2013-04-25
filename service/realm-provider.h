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

#ifndef __REALM_PROVIDER_H__
#define __REALM_PROVIDER_H__

#include <gio/gio.h>

#include "realm-dbus-generated.h"
#include "realm-disco.h"
#include "realm-kerberos.h"

G_BEGIN_DECLS

#define REALM_TYPE_PROVIDER            (realm_provider_get_type ())
#define REALM_PROVIDER(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_PROVIDER, RealmProvider))
#define REALM_IS_PROVIDER(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_PROVIDER))
#define REALM_PROVIDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), REALM_TYPE_PROVIDER, RealmProviderClass))
#define REALM_IS_PROVIDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), REALM_TYPE_PROVIDER))
#define REALM_PROVIDER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), REALM_TYPE_PROVIDER, RealmProviderClass))

typedef struct _RealmProvider RealmProvider;
typedef struct _RealmProviderClass RealmProviderClass;
typedef struct _RealmProviderPrivate RealmProviderPrivate;

struct _RealmProvider {
	RealmDbusProviderSkeleton parent;
	RealmProviderPrivate *pv;
};

struct _RealmProviderClass {
	RealmDbusProviderSkeletonClass parent_class;

	void         (* discover_async)  (RealmProvider *provider,
	                                  const gchar *string,
	                                  GVariant *options,
	                                  GDBusMethodInvocation *invocation,
	                                  GAsyncReadyCallback callback,
	                                  gpointer user_data);

	GList *      (* discover_finish) (RealmProvider *provider,
	                                  GAsyncResult *result,
	                                  gint *relevance,
	                                  GError **error);

	GList *      (* get_realms)      (RealmProvider *provider);
};

GType                    realm_provider_get_type                 (void) G_GNUC_CONST;

RealmProvider *          realm_provider_start                    (GDBusConnection *connection,
                                                                  GType type);

void                     realm_provider_stop_all                 (void);

RealmKerberos *          realm_provider_lookup_or_register_realm (RealmProvider *self,
                                                                  GType realm_type,
                                                                  const gchar *realm_name,
                                                                  RealmDisco *disco);

gboolean                 realm_provider_is_default               (const gchar *type,
                                                                  const gchar *name);

void                     realm_provider_discover                 (RealmProvider *self,
                                                                  const gchar *string,
                                                                  GVariant *options,
                                                                  GDBusMethodInvocation *invocation,
                                                                  GAsyncReadyCallback callback,
                                                                  gpointer user_data);

GList *                  realm_provider_discover_finish          (RealmProvider *self,
                                                                  GAsyncResult *result,
                                                                  gint *relevance,
                                                                  GError **error);

void                     realm_provider_set_name                 (RealmProvider *self,
                                                                  const gchar *value);

GList *                  realm_provider_get_realms               (RealmProvider *self);

void                     realm_provider_set_realm_paths          (RealmProvider *self,
                                                                  const gchar **value);

gboolean                 realm_provider_match_software           (GVariant *options,
                                                                  const gchar *server_software,
                                                                  const gchar *client_software,
                                                                  const gchar *membership_software);

G_END_DECLS

#endif /* __REALM_KERBEROS_PROVIDER_H__ */
