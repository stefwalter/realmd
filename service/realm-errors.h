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

#ifndef __REALM_ERRORS_H__
#define __REALM_ERRORS_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define       REALM_ERROR               (realm_error_quark ())

GQuark        realm_error_quark         (void) G_GNUC_CONST;

typedef enum {
	REALM_ERROR_INTERNAL,
	REALM_ERROR_FAILED,
	REALM_ERROR_BUSY,
	REALM_ERROR_ALREADY_CONFIGURED,
	REALM_ERROR_NOT_CONFIGURED,
	REALM_ERROR_AUTH_FAILED,
	REALM_ERROR_BAD_HOSTNAME,
	REALM_ERROR_CANCELLED,
	_NUM_REALM_ERRORS
} RealmErrorCodes;

#define       REALM_KRB5_ERROR          (realm_krb5_error_quark ())

GQuark        realm_krb5_error_quark    (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __REALM_ERRORS_H__ */
