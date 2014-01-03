/* realmd -- Realm configuration service
 *
 * Copyright 2013 Red Hat Inc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 *
 * Author: Stef Walter <stefw@redhat.com>
 */

#include "config.h"

#ifndef __SAFE_PRINTF_H__
#define __SAFE_PRINTF_H__

#include <stdlib.h>

#ifndef GNUC_NULL_TERMINATED
#if __GNUC__ >= 4
#define GNUC_NULL_TERMINATED __attribute__((__sentinel__))
#else
#define GNUC_NULL_TERMINATED
#endif
#endif

int        safe_asprintf     (char **strp,
                              const char *format,
                              ...) GNUC_NULL_TERMINATED;

int        safe_snprintf     (char *str,
                              size_t len,
                              const char *format,
                              ...) GNUC_NULL_TERMINATED;

int        safe_printf_cb    (void (* callback) (void *data, const char *piece, size_t len),
                              void *data,
                              const char *format,
                              const char * const args[],
                              int num_args);

#endif /* __SAFE_PRINTF_H__ */
