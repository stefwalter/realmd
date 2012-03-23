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

#ifndef __IC_AD_PROVIDER_H__
#define __IC_AD_PROVIDER_H__

#include <gio/gio.h>

#include "ic-kerberos-provider.h"

G_BEGIN_DECLS

#define IC_TYPE_AD_PROVIDER            (ic_ad_provider_get_type ())
#define IC_AD_PROVIDER(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), IC_TYPE_AD_PROVIDER, IcAdProvider))
#define IC_IS_AD_PROVIDER(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), IC_TYPE_AD_PROVIDER))

typedef struct _IcAdProvider IcAdProvider;

GType               ic_ad_provider_get_type                 (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __IC_AD_PROVIDER_H__ */
