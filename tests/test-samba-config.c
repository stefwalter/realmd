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

#include "service/realm-platform.h"
#include "service/realm-samba-config.h"

#include <string.h>

#define assert_cmpmem(a, na, cmp, b, nb) \
	do { gconstpointer __p1 = (a), __p2 = (b); gsize __n1 = (na), __n2 = (nb); \
	     if (__n1 cmp __n2 && memcmp (__p1, __p2, __n1) cmp 0) ; else \
	       assertion_message_cmpmem (G_LOG_DOMAIN, __FILE__, __LINE__, \
	            G_STRFUNC, #a "[" #na"] " #cmp " " #b "[" #nb "]", \
	            __p1, __n1, #cmp, __p2, __n2); } while (0)

static const char HEXC[] = "0123456789ABCDEF";

static gchar *
escape_data (const guchar *data,
                  gsize n_data)
{
	GString *result;
	gchar c;
	gsize i;
	guchar j;

	g_assert (data != NULL);

	result = g_string_sized_new (n_data * 2 + 1);
	for (i = 0; i < n_data; ++i) {
		c = data[i];
		if (c == '\n') {
			g_string_append (result, "\\n");
		} else if (c == '\r') {
			g_string_append (result, "\\r");
		} else if (c == '\v') {
			g_string_append (result, "\\v");
		} else if (g_ascii_isprint (c)) {
			g_string_append_c (result, c);
		} else {
			g_string_append (result, "\\x");
			j = c >> 4 & 0xf;
			g_string_append_c (result, HEXC[j]);
			j = c & 0xf;
			g_string_append_c (result, HEXC[j]);
		}
	}

	return g_string_free (result, FALSE);
}

static void
assertion_message_cmpmem (const char *domain,
                              const char *file,
                              int line,
                              const char *func,
                              const char *expr,
                              gconstpointer arg1,
                              gsize n_arg1,
                              const char *cmp,
                              gconstpointer arg2,
                              gsize n_arg2)
{
	char *a1, *a2, *s;
	a1 = arg1 ? escape_data (arg1, n_arg1) : g_strdup ("NULL");
	a2 = arg2 ? escape_data (arg2, n_arg2) : g_strdup ("NULL");
	s = g_strdup_printf ("assertion failed (%s): (%s %s %s)", expr, a1, cmp, a2);
	g_free (a1);
	g_free (a2);
	g_assertion_message (domain, file, line, func, s);
	g_free (s);
}

typedef struct {
	RealmSambaConfig *config;
} Test;

static void
setup (Test *test,
       gconstpointer unused)
{
	test->config = realm_samba_config_new ();
}

static void
teardown (Test *test,
          gconstpointer unused)
{
	g_object_unref (test->config);
}

static void
test_read_one (Test *test,
               gconstpointer unused)
{
	GError *error = NULL;
	gchar *value;
	gboolean ret;

	ret = realm_samba_config_read_file (test->config, TESTFILE_DIR "/smb-one.conf", &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	value = realm_samba_config_get (test->config, "section", "one");
	g_assert_cmpstr (value, ==, "uno");
	g_free (value);
	value = realm_samba_config_get (test->config, "section", "two");
	g_assert_cmpstr (value, ==, "dos");
	g_free (value);
	value = realm_samba_config_get (test->config, "section", "three");
	g_assert_cmpstr (value, ==, "three \tThree \tTHREE");
	g_free (value);
	value = realm_samba_config_get (test->config, "section", "four");
	g_assert_cmpstr (value, ==, "cuatro");
	g_free (value);
	value = realm_samba_config_get (test->config, "section", "five");
	g_assert_cmpstr (value, ==, "cinco");
	g_free (value);

	/* Not present */
	value = realm_samba_config_get (test->config, "section", "zero");
	g_assert (value == NULL);

	/* Section header is broken */
	value = realm_samba_config_get (test->config, "broken", "five");
	g_assert (value == NULL);

	value = realm_samba_config_get (test->config, "another section", "ended here");
	g_assert_cmpstr (value, ==, "last");
	g_free (value);
}

static void
test_read_all (Test *test,
               gconstpointer unused)
{
	GError *error = NULL;
	GHashTable *parameters;
	gboolean ret;

	ret = realm_samba_config_read_file (test->config, TESTFILE_DIR "/smb-one.conf", &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	parameters = realm_samba_config_get_all (test->config, "section");
	g_assert_cmpstr (g_hash_table_lookup (parameters, "one"), ==, "uno");
	g_assert_cmpstr (g_hash_table_lookup (parameters, "two"), ==, "dos");
	g_assert_cmpstr (g_hash_table_lookup (parameters, "three"), ==, "three \tThree \tTHREE");
	g_assert_cmpstr (g_hash_table_lookup (parameters, "four"), ==, "cuatro");
	g_assert_cmpstr (g_hash_table_lookup (parameters, "five"), ==, "cinco");

	/* Section header is broken */
	parameters = realm_samba_config_get_all (test->config, "broken");
	g_assert (parameters == NULL);
}

static void
test_read_carriage_return (Test *test,
                           gconstpointer unused)
{
	const gchar *data = "[section]\n1=one\r\n2=two";
	GBytes *bytes;
	gchar *value;

	bytes = g_bytes_new_static (data, strlen (data));
	realm_samba_config_read_bytes (test->config, bytes);
	g_bytes_unref (bytes);

	value = realm_samba_config_get (test->config, "section", "1");
	g_assert_cmpstr (value, ==, "one");
	g_free (value);
	value = realm_samba_config_get (test->config, "section", "2");
	g_assert_cmpstr (value, ==, "two");
}

static void
test_read_system_error (Test *test,
                        gconstpointer unused)
{
	GError *error = NULL;
	gboolean ret;

	/* Test doesn't work as root */
	if (geteuid () == 0)
		return;

	/* Set the system samba config path to /dev/mem, can't read that */
	realm_platform_add ("paths", "smb.conf", "/dev/mem");

	ret = realm_samba_config_read_system (test->config, &error);
	g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_ACCES);
	g_assert (ret == FALSE);
	g_error_free (error);
}

static void
test_write_exact (Test *test,
                  gconstpointer unused)
{
	GError *error = NULL;
	gchar *contents;
	gsize length;
	const gchar *output;
	gsize written;
	GBytes *bytes;

	g_file_get_contents (TESTFILE_DIR "/smb-one.conf", &contents, &length, &error);
	g_assert_no_error (error);

	bytes = g_bytes_new (contents, length);
	realm_samba_config_read_bytes (test->config, bytes);
	g_bytes_unref (bytes);

	bytes = realm_samba_config_write_bytes (test->config);
	output = g_bytes_get_data (bytes, &written);
	assert_cmpmem (contents, length, ==, output, written);
	g_bytes_unref (bytes);

	g_free (contents);
}

static void
test_write_file (Test *test,
                  gconstpointer unused)
{
	GError *error = NULL;
	gchar *contents;
	gsize length;
	gchar *output;
	gsize written;
	GBytes *bytes;
	gboolean ret;

	g_file_get_contents (TESTFILE_DIR "/smb-one.conf", &contents, &length, &error);
	g_assert_no_error (error);

	bytes = g_bytes_new (contents, length);
	realm_samba_config_read_bytes (test->config, bytes);
	g_bytes_unref (bytes);

	/* Set the system samba config path to: */
	realm_platform_add ("paths", "smb.conf", "/tmp/test-samba-config.conf");

	ret = realm_samba_config_write_system (test->config, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_file_get_contents (TESTFILE_DIR "/smb-one.conf", &output, &written, &error);
	g_assert_no_error (error);

	assert_cmpmem (contents, length, ==, output, written);

	g_free (contents);
	g_free (output);
}

static void
test_write_system (Test *test,
                   gconstpointer unused)
{
	GError *error = NULL;
	gchar *contents;
	gsize length;
	gchar *output;
	gsize written;
	GBytes *bytes;
	gboolean ret;

	g_file_get_contents (TESTFILE_DIR "/smb-one.conf", &contents, &length, &error);
	g_assert_no_error (error);

	bytes = g_bytes_new (contents, length);
	realm_samba_config_read_bytes (test->config, bytes);
	g_bytes_unref (bytes);

	ret = realm_samba_config_write_file (test->config, "/tmp/test-samba-config.conf", &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_file_get_contents (TESTFILE_DIR "/smb-one.conf", &output, &written, &error);
	g_assert_no_error (error);

	assert_cmpmem (contents, length, ==, output, written);

	g_free (contents);
	g_free (output);
}

static void
test_write_empty_no_create (Test *test,
                            gconstpointer unused)
{
	GError *error = NULL;
	gboolean ret;

	ret = realm_samba_config_write_file (test->config, "/non-existant", &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_assert (!g_file_test ("/non-existant", G_FILE_TEST_EXISTS));
}


static void
test_file_not_exist (Test *test,
                     gconstpointer unused)
{
	GError *error = NULL;
	gboolean ret;

	ret = realm_samba_config_read_file (test->config, "/non-existant", &error);
	g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
	g_assert (ret == FALSE);
	g_error_free (error);
}

static void
test_system_not_exist (Test *test,
                     gconstpointer unused)
{
	GError *error = NULL;
	gboolean ret;

	realm_platform_add ("paths", "smb.conf", "/non-existant");

	ret = realm_samba_config_read_system (test->config, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
}

static void
test_set (Test *test,
          gconstpointer unused)
{
	const gchar *data = "[section]\n\t1= one\r\n2=two";
	const gchar *check = "[section]\n1 = the number one\n2=two\n3 = three\n";
	const gchar *output;
	gsize n_check;
	gsize n_output;
	GBytes *bytes;

	bytes = g_bytes_new_static (data, strlen (data));
	realm_samba_config_read_bytes (test->config, bytes);
	g_bytes_unref (bytes);

	realm_samba_config_set (test->config, "section", "1", "the number one");
	realm_samba_config_set (test->config, "section", "3", "three");

	bytes = realm_samba_config_write_bytes (test->config);
	output = g_bytes_get_data (bytes, &n_output);
	n_check = strlen (check);
	assert_cmpmem (check, n_check, ==, output, n_output);
	g_bytes_unref (bytes);
}

static void
test_set_middle (Test *test,
                 gconstpointer unused)
{
	const gchar *data = "[section]\n1=one\n2=two\n\n[another]\n4=four";
	const gchar *check = "[section]\n1=one\n2=two\n3 = three\n\n[another]\n4=four";
	const gchar *output;
	gsize n_check;
	gsize n_output;
	GBytes *bytes;

	bytes = g_bytes_new_static (data, strlen (data));
	realm_samba_config_read_bytes (test->config, bytes);
	g_bytes_unref (bytes);

	realm_samba_config_set (test->config, "section", "3", "three");

	bytes = realm_samba_config_write_bytes (test->config);
	output = g_bytes_get_data (bytes, &n_output);
	n_check = strlen (check);
	assert_cmpmem (check, n_check, ==, output, n_output);
	g_bytes_unref (bytes);
}

static void
test_set_section (Test *test,
                  gconstpointer unused)
{
	const gchar *data = "[section]\n1=one\n2=two";
	const gchar *check = "[section]\n1=one\n2=two\n\n[happy]\n4 = four\n";
	const gchar *output;
	gsize n_check;
	gsize n_output;
	GBytes *bytes;

	bytes = g_bytes_new_static (data, strlen (data));
	realm_samba_config_read_bytes (test->config, bytes);
	g_bytes_unref (bytes);

	realm_samba_config_set (test->config, "happy", "4", "four");

	bytes = realm_samba_config_write_bytes (test->config);
	output = g_bytes_get_data (bytes, &n_output);
	n_check = strlen (check);
	assert_cmpmem (check, n_check, ==, output, n_output);
	g_bytes_unref (bytes);
}

static void
test_set_all (Test *test,
              gconstpointer unused)
{
	const gchar *data = "[section]\n\t1= one\r\n2=two";
	const gchar *check = "[section]\n1 = the number one\n2=two\n3 = three\n";
	const gchar *output;
	GHashTable *parameters;
	gsize n_check;
	gsize n_output;
	GBytes *bytes;

	bytes = g_bytes_new_static (data, strlen (data));
	realm_samba_config_read_bytes (test->config, bytes);
	g_bytes_unref (bytes);

	parameters = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (parameters, "1", "the number one");
	g_hash_table_insert (parameters, "3", "three");
	realm_samba_config_set_all (test->config, "section", parameters);
	g_hash_table_unref (parameters);

	bytes = realm_samba_config_write_bytes (test->config);
	output = g_bytes_get_data (bytes, &n_output);
	n_check = strlen (check);
	assert_cmpmem (check, n_check, ==, output, n_output);
	g_bytes_unref (bytes);
}

static void
test_change (Test *test,
             gconstpointer unused)
{
	const gchar *data = "[section]\n\t1= one\r\n2=two";
	const gchar *check = "[section]\n1 = the number one\n2=two\n3 = three\n";
	GError *error = NULL;
	gchar *output;

	/* Setup this file as the system smb.conf */
	realm_platform_add ("paths", "smb.conf", "/tmp/test-samba-config.conf");
	g_file_set_contents ("/tmp/test-samba-config.conf", data, -1, &error);
	g_assert_no_error (error);

	realm_samba_config_change ("section", &error,
	                           "1", "the number one",
	                           "3", "three",
	                           NULL);

	g_file_get_contents ("/tmp/test-samba-config.conf", &output, NULL, &error);
	g_assert_no_error (error);

	g_assert_cmpstr (output, ==, check);
	g_free (output);
}

int
main (int argc,
      char **argv)
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_set_prgname ("test-samba-config");

	realm_platform_init ();

	g_test_add ("/realmd/samba-config/read-one", Test, NULL, setup, test_read_one, teardown);
	g_test_add ("/realmd/samba-config/read-all", Test, NULL, setup, test_read_all, teardown);
	g_test_add ("/realmd/samba-config/read-carriage-return", Test, NULL, setup, test_read_carriage_return, teardown);
	g_test_add ("/realmd/samba-config/read-system-error", Test, NULL, setup, test_read_system_error, teardown);

	g_test_add ("/realmd/samba-config/write-exact", Test, NULL, setup, test_write_exact, teardown);
	g_test_add ("/realmd/samba-config/write-file", Test, NULL, setup, test_write_file, teardown);
	g_test_add ("/realmd/samba-config/write-system", Test, NULL, setup, test_write_system, teardown);
	g_test_add ("/realmd/samba-config/write-empty-no-create", Test, NULL, setup, test_write_empty_no_create, teardown);

	g_test_add ("/realmd/samba-config/set", Test, NULL, setup, test_set, teardown);
	g_test_add ("/realmd/samba-config/set-middle", Test, NULL, setup, test_set_middle, teardown);
	g_test_add ("/realmd/samba-config/set-section", Test, NULL, setup, test_set_section, teardown);
	g_test_add ("/realmd/samba-config/set-all", Test, NULL, setup, test_set_all, teardown);

	g_test_add ("/realmd/samba-config/change", Test, NULL, setup, test_change, teardown);

	g_test_add ("/realmd/samba-config/file-not-exist", Test, NULL, setup, test_file_not_exist, teardown);
	g_test_add ("/realmd/samba-config/system-not-exist", Test, NULL, setup, test_system_not_exist, teardown);

	return g_test_run ();
}
