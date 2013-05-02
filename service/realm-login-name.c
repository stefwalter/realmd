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

#include "realm-login-name.h"

#include <string.h>

static gboolean
split_login_format (const gchar *format,
                    const gchar **prefix,
                    gsize *prefix_len,
                    const gchar **suffix,
                    gsize *suffix_len)
{
	const gchar *end;
	const gchar *pos;

	g_assert (format != NULL);
	g_assert (prefix != NULL);
	g_assert (prefix_len != NULL);
	g_assert (suffix != NULL);
	g_assert (suffix_len != NULL);

	pos = strstr (format, "%U");
	if (pos == NULL)
		return FALSE;

	end = format + strlen (format);
	*prefix = format;
	*prefix_len = pos - format;
	*suffix = pos + 2;
	*suffix_len = end - (*suffix);
	return TRUE;
}

gchar *
realm_login_name_parse (const gchar *const *formats,
                        gboolean lower,
                        const gchar *login)
{
	const gchar *prefix = NULL;
	const gchar *suffix = NULL;
	gsize prefix_len = 0;
	gsize suffix_len = 0;
	gchar length;
	const gchar *user;
	gsize user_len;
	gint i;

	g_return_val_if_fail (formats != NULL, NULL);
	g_return_val_if_fail (login != NULL, NULL);

	for (i = 0; formats[i]; i++) {
		if (strstr (formats[i], "%D") != NULL) {
			g_warning ("Using a %%D as a domain in a login format is not yet implemented");
			continue;
		}

		split_login_format (formats[i], &prefix, &prefix_len, &suffix, &suffix_len);
		length = strlen (login);

		if (prefix_len + suffix_len >= length)
			continue;
		if (g_ascii_strncasecmp (login, prefix, prefix_len) != 0)
			continue;
		if (g_ascii_strncasecmp (login + (length - suffix_len), suffix, suffix_len) != 0)
			continue;

		user = login + prefix_len;
		user_len = length - (suffix_len + prefix_len);

		if (lower)
			return g_utf8_strdown (user, user_len);
		else
			return g_strndup (user, user_len);
	}

	return NULL;
}

gchar **
realm_login_name_parse_all (const gchar *const *formats,
                            gboolean lower,
                            const gchar **logins,
                            const gchar **failed)
{
	GPtrArray *names;
	gchar *login;
	gint i;

	names = g_ptr_array_new_full (0, g_free);

	for (i = 0; logins != NULL && logins[i] != NULL; i++) {
		login = realm_login_name_parse (formats, lower, logins[i]);
		if (login == NULL) {
			if (failed)
				*failed = logins[i];
			g_ptr_array_free (names, TRUE);
			return NULL;
		}

		g_ptr_array_add (names, login);
	}

	g_ptr_array_add (names, NULL);
	return (gchar **)g_ptr_array_free (names, FALSE);
}

gchar *
realm_login_name_format (const gchar *format,
                         const gchar *user)
{
	const gchar *prefix = NULL;
	const gchar *suffix = NULL;
	gsize prefix_len = 0;
	gsize suffix_len = 0;
	GString *string;

	g_return_val_if_fail (format != NULL, NULL);
	g_return_val_if_fail (user != NULL, NULL);

	string = g_string_sized_new (64);
	split_login_format (format, &prefix, &prefix_len, &suffix, &suffix_len);
	g_string_append_len (string, prefix, prefix_len);
	g_string_append (string, user);
	g_string_append_len (string, suffix, suffix_len);
	return g_string_free (string, FALSE);
}
