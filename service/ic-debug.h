/*
 * Copyright (C) 2007 Nokia Corporation
 * Copyright (C) 2007-2011 Collabora Ltd.
 * copyright (C) 2012 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef IC_DEBUG_H
#define IC_DEBUG_H

#include "config.h"

#include <glib.h>

G_BEGIN_DECLS

/* Please keep this enum in sync with #keys in gcr-debug.c */
typedef enum {
	IC_DEBUG_PROCESS = 1 << 1,
} IcDebugFlags;

gboolean           ic_debug_flag_is_set              (IcDebugFlags flag);

void               ic_debug_set_flags                (const gchar *flags_string);

void               ic_debug_message                  (IcDebugFlags flag,
                                                      const gchar *format,
                                                      ...) G_GNUC_PRINTF (2, 3);

G_END_DECLS

#endif /* IC_DEBUG_H */

/* -----------------------------------------------------------------------------
 * Below this point is outside the IC_DEBUG_H guard - so it can take effect
 * more than once. So you can do:
 *
 * #define DEBUG_FLAG IC_DEBUG_ONE_THING
 * #include "gcr-debug.h"
 * ...
 * DEBUG ("if we're debugging one thing");
 * ...
 * #undef DEBUG_FLAG
 * #define DEBUG_FLAG IC_DEBUG_OTHER_THING
 * #include "gcr-debug.h"
 * ...
 * DEBUG ("if we're debugging the other thing");
 * ...
 */

#ifdef DEBUG_FLAG
#ifdef WITH_DEBUG

#undef ic_debug
#define ic_debug(format, ...) \
	ic_debug_message (DEBUG_FLAG, "%s: " format, G_STRFUNC, ##__VA_ARGS__)

#undef ic_debugging
#define ic_debugging \
	ic_debug_flag_is_set (DEBUG_FLAG)

#else /* !defined (WITH_DEBUG) */

#undef ic_debug
#define ic_debug(format, ...) \
	do {} while (0)

#undef ic_debugging
#define ic_debugging 0

#endif /* !defined (WITH_DEBUG) */

#endif /* defined (DEBUG_FLAG) */
