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

#ifndef __IC_ADS_PROVIDER_H__
#define __IC_ADS_PROVIDER_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define IC_TYPE_ADS_PROVIDER            (ic_ads_provider_get_type ())
#define IC_ADS_PROVIDER(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), IC_TYPE_ADS_PROVIDER, IcAdsProvider))
#define IC_IS_ADS_PROVIDER(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), IC_TYPE_ADS_PROVIDER))

typedef struct _IcAdsProvider IcAdsProvider;

GType               ic_ads_provider_get_type                 (void) G_GNUC_CONST;

void                ic_ads_provider_start                    (void);

void                ic_ads_provider_stop                     (void);

G_END_DECLS

#endif /* __IC_ADS_ENROLL_H__ */
