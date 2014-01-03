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

#include "safe-printf.h"

#include <stdarg.h>
#include <string.h>

#ifndef MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#endif

static void
safe_padding (int count,
              int *total,
              void (* callback) (void *, const char *, size_t),
              void *data)
{
	char eight[] = "        ";
	int num;

	while (count > 0) {
		num = MIN (count, 8);
		callback (data, eight, num);
		count -= num;
		*total += num;
	}
}

static void
dummy_callback (void *data,
                const char *piece,
                size_t len)
{

}

int
safe_printf_cb (void (* callback) (void *, const char *, size_t),
                void *data,
                const char *format,
                const char *args[],
                int num_args)
{
	int at_arg = 0;
	const char *cp;
	int precision;
	int width;
	int len;
	const char *value;
	int total;
	int left;

	if (!callback)
		callback = dummy_callback;

	total = 0;
	cp = format;

	while (*cp) {

		/* Piece of raw string */
		if (*cp != '%') {
			len = strcspn (cp, "%");
			callback (data, cp, len);
			total += len;
			cp += len;
			continue;
		}

		cp++;

		/* An literal percent sign? */
		if (*cp == '%') {
			callback (data, "%", 1);
			total++;
			cp++;
			continue;
		}

		value = NULL;
		left = 0;
		precision = -1;
		width = -1;

		/* Test for positional argument.  */
		if (*cp >= '0' && *cp <= '9') {
			/* Look-ahead parsing, otherwise skipped */
			if (cp[strspn (cp, "0123456789")] == '$') {
				unsigned int n = 0;
				for (; *cp >= '0' && *cp <= '9'; cp++)
					n = 10 * n + (*cp - '0');
				/* Positional argument 0 is invalid. */
				if (n == 0)
					return -1;
				/* Positional argument N too high */
				if (n > num_args)
					return -1;
				value = args[n - 1];
				cp++; /* $ */
			}
		}

		/* Read the supported flags. */
		for (; ; cp++) {
			if (*cp == '-')
				left = 1;
			/* Supported but ignored */
			else if (*cp != ' ')
				break;
		}

		/* Parse the width. */
		if (*cp >= '0' && *cp <= '9') {
			width = 0;
			for (; *cp >= '0' && *cp <= '9'; cp++)
				width = 10 * width + (*cp - '0');
		}

		/* Parse the precision. */
		if (*cp == '.') {
			precision = 0;
			for (cp++; *cp >= '0' && *cp <= '9'; cp++)
				precision = 10 * precision + (*cp - '0');
		}

		/* Read the conversion character.  */
		switch (*cp++) {
		case 's':
			/* Non-positional argument */
			if (value == NULL) {
				/* Too many arguments used */
				if (at_arg == num_args)
					return -1;
				value = args[at_arg++];
			}
			break;

		/* No other conversion characters are supported */
		default:
			return -1;
		}

		/* How many characters are we printing? */
		len = strlen (value);
		if (precision >= 0)
			len = MIN (precision, len);

		/* Do we need padding? */
		safe_padding (left ? 0 : width - len, &total, callback, data);

		/* The actual data */;
		callback (data, value, len);
		total += len;

		/* Do we need padding? */
		safe_padding (left ? width - len : 0, &total, callback, data);
	}

	return total;
}

struct sprintf_ctx {
	char *data;
	size_t length;
	size_t alloc;
};

static void
asprintf_callback (void *data,
                   const char *piece,
                   size_t length)
{
	struct sprintf_ctx *cx = data;
	void *mem;

	if (!cx->data)
		return;

	/* Reallocate if necessary */
	if (cx->length + length + 1 > cx->alloc) {
		cx->alloc += MAX (length + 1, 1024);
		mem = realloc (cx->data, cx->alloc);
		if (mem == NULL) {
			free (cx->data);
			cx->data = NULL;
			return;
		}
		cx->data = mem;
	}

	memcpy (cx->data + cx->length, piece, length);
	cx->length += length;
	cx->data[cx->length] = 0;
}

static const char **
valist_to_args (va_list va,
                int *num_args)
{
	int alo_args;
	const char **args;
	const char *arg;
	void *mem;

	*num_args = alo_args = 0;
	args = NULL;

	for (;;) {
		arg = va_arg (va, const char *);
		if (arg == NULL)
			break;
		if (*num_args == alo_args) {
			alo_args += 8;
			mem = realloc (args, sizeof (const char *) * alo_args);
			if (!mem) {
				free (args);
				return NULL;
			}
			args = mem;
		}
		args[(*num_args)++] = arg;
	}

	return args;
}

int
safe_asprintf (char **strp,
               const char *format,
               ...)
{
	struct sprintf_ctx cx;
	const char **args;
	int num_args;
	va_list va;
	int ret;
	int i;

	va_start (va, format);
	args = valist_to_args (va, &num_args);
	va_end (va);

	if (args == NULL)
		return -1;

	/* Preallocate a pretty good guess */
	cx.alloc = strlen (format) + 1;
	for (i = 0; i < num_args; i++)
		cx.alloc += strlen (args[i]);

	cx.data = malloc (cx.alloc);
	if (!cx.data) {
		free (args);
		return -1;
	}

	cx.data[0] = '\0';
	cx.length = 0;

	ret = safe_printf_cb (asprintf_callback, &cx, format, args, num_args);
	if (cx.data == NULL)
		ret = -1;
	if (ret < 0)
		free (cx.data);
	else
		*strp = cx.data;

	free (args);
	return ret;
}

static void
snprintf_callback (void *data,
                   const char *piece,
                   size_t length)
{
	struct sprintf_ctx *cx = data;

	/* Don't copy if too much data */
	if (cx->length > cx->alloc)
		length = 0;
	else if (cx->length + length > cx->alloc)
		length = cx->alloc - cx->length;
	else
		length = length;

	if (length > 0)
		memcpy (cx->data + cx->length, piece, length);

	/* Null termination happens later */
	cx->length += length;
}

int
safe_snprintf (char *str,
               size_t len,
               const char *format,
               ...)
{
	struct sprintf_ctx cx;
	int num_args;
	va_list va;
	const char **args;
	int ret;

	cx.data = str;
	cx.length = 0;
	cx.alloc = len;

	va_start (va, format);
	args = valist_to_args (va, &num_args);
	va_end (va);

	if (args == NULL)
		return -1;

	cx.data[0] = '\0';

	ret = safe_printf_cb (snprintf_callback, &cx, format, args, num_args);
	if (ret < 0)
		return ret;

	/* Null terminate appropriately */
	if (len > 0)
		cx.data[MIN(cx.length, len - 1)] = '\0';

	free (args);
	return ret;
}
