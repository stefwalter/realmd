/* identity-config - Identity configuration service
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

#ifndef __IC_ERRORS_H__
#define __IC_ERRORS_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define       IC_ERROR               (ic_error_quark ())

GQuark        ic_error_quark         (void) G_GNUC_CONST;

typedef enum {
	IC_ERROR_DISCOVERY_FAILED,
	_NUM_IC_ERRORS
} IcErrorCodes;

G_END_DECLS

#endif /* __IC_ADS_ENROLL_H__ */
