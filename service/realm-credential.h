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

#ifndef __REALM_CREDENTIAL_H__
#define __REALM_CREDENTIAL_H__

#include <gio/gio.h>

#include <krb5/krb5.h>

G_BEGIN_DECLS

typedef enum {
	REALM_CREDENTIAL_OWNER_NONE = 0,
	REALM_CREDENTIAL_OWNER_ADMIN = 1,
	REALM_CREDENTIAL_OWNER_USER,
	REALM_CREDENTIAL_OWNER_COMPUTER
} RealmCredentialOwner;

typedef enum {
	REALM_CREDENTIAL_CCACHE = 1,
	REALM_CREDENTIAL_PASSWORD,
	REALM_CREDENTIAL_SECRET,
	REALM_CREDENTIAL_AUTOMATIC
} RealmCredentialType;

typedef struct {
	RealmCredentialType type;
	RealmCredentialOwner owner;

	/*
	 * Sometimes these structures are allocated statically. The following
	 * fields should not be used in that case, nor should the structure
	 * be passed to the realm_credential_ref() or unref() function.
	 */
	int refs;
	union {
		struct {
			gchar *file;
		} ccache;
		struct {
			gchar *name;
			GBytes *value;
		} password;
		struct {
			GBytes *value;
		} secret;
		struct {
			int unused;
		} automatic;
	} x;
} RealmCredential;

RealmCredential *    realm_credential_parse                  (GVariant *variant,
                                                              GError **error);

RealmCredential *    realm_credential_ref                    (RealmCredential *cred);

void                 realm_credential_unref                  (RealmCredential *cred);

void                 realm_credential_ccache_delete_and_free (gchar *ccache_file);

GVariant *           realm_credential_build_supported        (const RealmCredential *creds);

G_END_DECLS

#endif /* __REALM_CREDENTIAL_H__ */
