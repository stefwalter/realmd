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
	                                  GDBusMethodInvocation *invocation,
	                                  GAsyncReadyCallback callback,
	                                  gpointer user_data);

	gint         (* discover_finish) (RealmProvider *provider,
	                                  GAsyncResult *result,
	                                  GVariant **realm,
	                                  GVariant **discovery,
	                                  GError **error);
};

GType               realm_provider_get_type                 (void) G_GNUC_CONST;

GVariant *          realm_provider_new_realm_info           (const gchar *bus_name,
                                                             const gchar *object_path,
                                                             const gchar *interface);

G_END_DECLS

#endif /* __REALM_KERBEROS_PROVIDER_H__ */
