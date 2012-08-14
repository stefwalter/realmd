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

#include "dbus/realm-dbus-constants.h"
#include "service/realm-login-name.h"

#include <glib/gstdio.h>

#include <string.h>

typedef struct {
	gconstpointer unused;
} Test;

typedef struct {
	const gchar *const *formats;
	const gchar *user;
	const gchar *login;
} Fixture;

static void
setup (Test *test,
       gconstpointer unused)
{

}

static void
teardown (Test *test,
          gconstpointer unused)
{

}

static void
test_format_login (Test *test,
                   gconstpointer data)
{
	const Fixture *fixture = data;
	gchar *login;

	login = realm_login_name_format (fixture->formats[0], fixture->user);
	g_assert_cmpstr (login, ==, fixture->login);
	g_free (login);
}

static void
test_parse_login (Test *test,
                  gconstpointer data)
{
	const Fixture *fixture = data;
	gchar *user;

	user = realm_login_name_parse (fixture->formats, FALSE, fixture->login);
	if (fixture->user == NULL)
		g_assert (user == NULL);
	else
		g_assert_cmpstr (user, ==, fixture->user);
	g_free (user);
}

static void
test_parse_all (Test *test,
                gconstpointer unused)
{
	const gchar *failed = NULL;
	const gchar *original[] = {
		"Domain\\User",
		"Domain\\Two",
		"Domain\\Three",
		NULL,
	};
	const gchar *const formats[] = {
		"Domain\\%U",
		NULL
	};

	gchar **changed;

	changed = realm_login_name_parse_all (formats, FALSE, original, &failed);
	g_assert (changed != NULL);
	g_assert_cmpstr (changed[0], ==, "User");
	g_assert_cmpstr (changed[1], ==, "Two");
	g_assert_cmpstr (changed[2], ==, "Three");
	g_assert (changed[3] == NULL);
	g_assert (failed == NULL);

	g_strfreev (changed);
}

static void
test_parse_all_failed (Test *test,
                       gconstpointer unused)
{
	const gchar *failed = NULL;
	const gchar *original[] = {
		"Domain\\User",
		"Wheeee",
		NULL,
	};
	const gchar *const formats[] = {
		"Domain\\%U",
		NULL
	};

	gchar **changed;

	changed = realm_login_name_parse_all (formats, FALSE, original, &failed);
	g_assert (changed == NULL);
	g_assert_cmpstr (failed, ==, "Wheeee");

	g_strfreev (changed);
}


int
main (int argc,
      char **argv)
{
	static const gchar *const domain_formats[] = {
		"Domain\\%U",
		NULL
	};

	static const gchar *const prefix_suffix_formats[] = {
		"prefix|%U|suffix",
		NULL
	};

	static const gchar *const email_formats[] = {
		"%U@domain",
		NULL
	};


	static const Fixture format_fixtures[] = {
		{ domain_formats, "User", "Domain\\User" },
		{ prefix_suffix_formats, "User", "prefix|User|suffix" },
		{ email_formats, "user", "user@domain" },
	};

	static const Fixture parse_fixtures[] = {
		{ domain_formats, "User", "Domain\\User" },
		{ prefix_suffix_formats, "User", "prefix|User|suffix" },
		{ email_formats, "user", "user@domain" },
		{ domain_formats, NULL, "Another\\User" },
		{ prefix_suffix_formats, NULL, "different|User|suffix" },
		{ email_formats, NULL, "user@another" },
	};

	gchar *name;
	gint i;

	g_test_init (&argc, &argv, NULL);
	g_set_prgname ("test-login-name");

	for (i = 0; i < G_N_ELEMENTS (format_fixtures); i++) {
		name = g_strdup_printf ("/realmd/login-name/format_%s", format_fixtures[i].login);
		g_strcanon (name, REALM_DBUS_NAME_CHARS "/-", '_');
		g_test_add (name, Test, &format_fixtures[i], setup, test_format_login, teardown);
		g_free (name);
	}

	for (i = 0; i < G_N_ELEMENTS (parse_fixtures); i++) {
		name = g_strdup_printf ("/realmd/login-name/parse_%s", parse_fixtures[i].login);
		g_strcanon (name, REALM_DBUS_NAME_CHARS "/-", '_');
		g_test_add (name, Test, &parse_fixtures[i], setup, test_parse_login, teardown);
		g_free (name);
	}

	g_test_add ("/realmd/login-name/parse-all", Test, NULL, setup, test_parse_all, teardown);
	g_test_add ("/realmd/login-name/parse-all-failed", Test, NULL, setup, test_parse_all_failed, teardown);

	return g_test_run ();
}
