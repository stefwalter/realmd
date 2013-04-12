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
 * Marius Vollmer <mvollmer@redhat.com>
 */

#include "config.h"

#include "service/realm-settings.h"

#include <glib-object.h>
#include <glib/gstdio.h>

#include <string.h>

static void
write_config (const char *contents)
{
	GError *error = NULL;
	int ret;

	ret = g_mkdir_with_parents ("/tmp/realmd-etc", 0700);
	g_assert (ret >= 0);
	g_file_set_contents ("/tmp/realmd-etc/realmd.conf", contents, -1, &error);
	g_assert_no_error (error);
}

typedef struct {
	GLogLevelFlags old_fatals;
	int n_criticals;
} Test;

static void
log_counter (const gchar *log_domain,
             GLogLevelFlags log_level,
             const gchar *message,
             gpointer user_data)
{
	Test *test = user_data;
	if ((log_level & G_LOG_LEVEL_MASK) == G_LOG_LEVEL_CRITICAL)
		test->n_criticals += 1;
}

static void
setup (Test *test,
       gconstpointer unused)
{
	test->n_criticals = 0;
	g_log_set_default_handler (log_counter, test);
	test->old_fatals = g_log_set_always_fatal (0);
}

static void
teardown (Test *test,
          gconstpointer unused)
{
	g_log_set_default_handler (g_log_default_handler, NULL);
	g_log_set_always_fatal (test->old_fatals);
}

static void
test_string (Test *test,
             gconstpointer unused)
{
	const gchar *value;

	write_config ("[one]\n"
	              "key = value\n");

	realm_settings_init ();
	value = realm_settings_string ("one", "key");
	g_assert_cmpstr (value, ==, "value");
	realm_settings_uninit ();
}

static void
test_boolean (Test *test,
              gconstpointer unused)
{
	gboolean value;

	write_config ("[one]\n"
	              "true-1 = yes\n"
	              "true-2 = 1\n"
	              "true-3 = true\n"
	              "true-4 = TRUE\n"
	              "true-5 = Yes\n"
	              "false-1 = no\n"
	              "false-2 = 0\n"
	              "false-3 = false\n"
	              "false-4 = nope\n");

#define ASSERT_TRUE(n)					\
	value = realm_settings_boolean ("one", n, FALSE);	\
	g_assert_cmpint (value, ==, TRUE);

#define ASSERT_FALSE(n)					\
	value = realm_settings_boolean ("one", n, TRUE);	\
	g_assert_cmpint (value, ==, FALSE);

	realm_settings_init ();
	ASSERT_TRUE("true-1");
	ASSERT_TRUE("true-2");
	ASSERT_TRUE("true-3");
	ASSERT_TRUE("true-4");
	ASSERT_TRUE("true-5");
	ASSERT_FALSE("false-1");
	ASSERT_FALSE("false-2");
	ASSERT_FALSE("false-3");
	ASSERT_FALSE("false-4");

	value = realm_settings_boolean ("one", "invalid", TRUE);
	g_assert_cmpint (value, ==, TRUE);
	value = realm_settings_boolean ("one", "invalid", FALSE);
	g_assert_cmpint (value, ==, FALSE);

	realm_settings_uninit ();
}

static void
test_double (Test *test,
             gconstpointer unused)
{
	gdouble value;

	write_config ("[one]\n"
	              "key = 1234.0\n"
	              "malformed = abc\n");

	realm_settings_init ();

	value = realm_settings_double ("one", "key", 0.0);
	g_assert_cmpfloat (value, ==, 1234.0);

	value = realm_settings_double ("one", "non-existing", 5678.0);
	g_assert_cmpfloat (value, ==, 5678.0);

	value = realm_settings_double ("one", "malformed", 1212.0);
	g_assert_cmpfloat (value, ==, 1212.0);
	g_assert_cmpint (test->n_criticals, ==, 1);

	realm_settings_uninit ();
}

int
main (int argc,
      char **argv)
{
#if !GLIB_CHECK_VERSION(2, 36, 0)
	g_type_init ();
#endif

	g_test_init (&argc, &argv, NULL);
	g_set_prgname ("test-ini-config");

	g_test_add ("/realmd/settings/string", Test, NULL, setup, test_string, teardown);
	g_test_add ("/realmd/settings/double", Test, NULL, setup, test_double, teardown);
	g_test_add ("/realmd/settings/boolean", Test, NULL, setup, test_boolean, teardown);

	return g_test_run ();
}
