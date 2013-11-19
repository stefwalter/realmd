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

#include <glib/gstdio.h>

#include "realm-credential.h"
#include "realm-daemon.h"
#include "realm-errors.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>

static gchar *
write_ccache_file (GVariant *ccache,
                   GError **error)
{
	const gchar *directory;
	gchar *filename;
	const guchar *data;
	gsize length;
	gint fd;
	int res;

	data = g_variant_get_fixed_array (ccache, &length, 1);
	g_return_val_if_fail (length > 0, NULL);

	directory = g_get_tmp_dir ();
	filename = g_build_filename (directory, "realm-ad-kerberos-XXXXXX", NULL);

	fd = g_mkstemp_full (filename, O_WRONLY, 0600);
	if (fd < 0) {
		g_warning ("couldn't open temporary file in %s directory for kerberos cache: %s",
		           directory, g_strerror (errno));
		g_set_error (error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Problem writing out the kerberos cache data");
		g_free (filename);
		return NULL;
	}

	while (length > 0) {
		res = write (fd, data, length);
		if (res <= 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			g_warning ("couldn't write kerberos cache to file %s: %s",
			           filename, g_strerror (errno));
			g_set_error (error, REALM_ERROR, REALM_ERROR_INTERNAL,
			             "Problem writing out the kerberos cache data");
			break;
		} else  {
			length -= res;
			data += res;
		}
	}

	if (close (fd) < 0) {
		g_warning ("couldn't write kerberos cache to file %s: %s",
		           filename, g_strerror (errno));
		g_set_error (error, REALM_ERROR, REALM_ERROR_INTERNAL,
		             "Problem writing out the kerberos cache data");
		g_free (filename);
		return NULL;
	}

	if (length != 0) {
		g_free (filename);
		return NULL;
	}

	return filename;
}

static gboolean
parse_ccache (RealmCredential *cred,
              GVariant *contents,
              GError **error)
{
	gsize length;

	if (!g_variant_is_of_type (contents, G_VARIANT_TYPE ("ay"))) {
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		             "Credential cache argument is of wrong DBus type");
		return FALSE;
	}

	g_variant_get_fixed_array (contents, &length, 1);
	if (length == 0) {
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		             "Invalid zero length credential cache argument");
		return FALSE;
	}

	cred->x.ccache.file = write_ccache_file (contents, error);
	if (cred->x.ccache.file == NULL)
		return FALSE;

	cred->type = REALM_CREDENTIAL_CCACHE;
	return TRUE;
}

static gboolean
parse_password (RealmCredential *cred,
                GVariant *contents,
                GError **error)
{
	const gchar *password;

	if (!g_variant_is_of_type (contents, G_VARIANT_TYPE ("(ss)"))) {
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		             "Password credentials are of wrong DBus type");
		return FALSE;
	}

	g_variant_get (contents, "(s&s)", &cred->x.password.name, &password);
	cred->x.password.value = g_bytes_new_with_free_func (password, strlen (password),
	                                                     (GDestroyNotify)g_variant_unref,
	                                                     g_variant_ref (contents));

	cred->type = REALM_CREDENTIAL_PASSWORD;
	return TRUE;
}

static gboolean
parse_secret (RealmCredential *cred,
              GVariant *contents,
              GError **error)
{
	gconstpointer data;
	gsize length;

	if (!g_variant_is_of_type (contents, G_VARIANT_TYPE ("ay"))) {
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		             "Secret credentials are of wrong DBus type");
		return FALSE;
	}

	data = g_variant_get_fixed_array (contents, &length, 1);
	cred->x.secret.value = g_bytes_new_with_free_func (data, length,
	                                                   (GDestroyNotify)g_variant_unref,
	                                                   g_variant_ref (contents));

	cred->type = REALM_CREDENTIAL_SECRET;
	return TRUE;
}

static gboolean
parse_automatic (RealmCredential *cred,
                 GVariant *contents,
                 GError **error)
{
	cred->type = REALM_CREDENTIAL_AUTOMATIC;
	return TRUE;
}

RealmCredential *
realm_credential_parse (GVariant *input,
                        GError **error)
{
	RealmCredential *cred;
	GVariant *outer;
	GVariant *contents;
	const char *owner, *type;
	gboolean ret = TRUE;

	g_return_val_if_fail (input != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	cred = g_new0 (RealmCredential, 1);
	cred->refs = 1;

	g_variant_get (input, "(&s&s@v)", &type, &owner, &outer);

	if (g_str_equal (owner, "administrator")) {
		cred->owner = REALM_CREDENTIAL_OWNER_ADMIN;
	} else if (g_str_equal (owner, "user")) {
		cred->owner = REALM_CREDENTIAL_OWNER_USER;
	} else if (g_str_equal (owner, "computer")) {
		cred->owner = REALM_CREDENTIAL_OWNER_COMPUTER;
	} else if (g_str_equal (owner, "none")) {
		cred->owner = REALM_CREDENTIAL_OWNER_NONE;
	} else {
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		             "Credential cache argument has invalid or unsupported owner");
		ret = FALSE;
	}

	contents = g_variant_get_variant (outer);
	g_variant_unref (outer);

	if (!ret) {
		/* skip */;
	} else if (g_str_equal (type, "ccache")) {
		ret = parse_ccache (cred, contents, error);
	} else if (g_str_equal (type, "password")) {
		ret = parse_password (cred, contents, error);
	} else if (g_str_equal (type, "secret")) {
		ret = parse_secret (cred, contents, error);
	} else if (g_str_equal (type, "automatic")) {
		ret = parse_automatic (cred, contents, error);
	} else {
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
		             "Invalid or unsupported credential type");
		ret = FALSE;
	}

	if (!ret) {
		realm_credential_unref (cred);
		cred = NULL;
	}

	g_variant_unref (contents);
	return cred;
}

RealmCredential *
realm_credential_ref (RealmCredential *cred)
{
	g_return_val_if_fail (cred != NULL, NULL);
	g_return_val_if_fail (cred->refs > 0, NULL);

	cred->refs++;
	return cred;
}

void
realm_credential_unref (RealmCredential *cred)
{
	g_return_if_fail (cred != NULL);
	g_return_if_fail (cred->refs > 0);

	cred->refs--;
	if (cred->refs > 0)
		return;

	switch (cred->type) {
	case REALM_CREDENTIAL_AUTOMATIC:
		break;
	case REALM_CREDENTIAL_CCACHE:
		realm_credential_ccache_delete_and_free (cred->x.ccache.file);
		break;
	case REALM_CREDENTIAL_SECRET:
		g_bytes_unref (cred->x.secret.value);
		break;
	case REALM_CREDENTIAL_PASSWORD:
		g_free (cred->x.password.name);
		g_bytes_unref (cred->x.password.value);
		break;
	}

	g_free (cred);
}

void
realm_credential_ccache_delete_and_free (gchar *ccache_file)
{
	g_return_if_fail (ccache_file != NULL);

	if (!realm_daemon_has_debug_flag () && g_unlink (ccache_file) < 0) {
		g_warning ("couldn't remove kerberos cache file: %s: %s",
		           ccache_file, g_strerror (errno));
	}
	g_free (ccache_file);
}

GVariant *
realm_credential_build_supported (const RealmCredential *creds)
{
	GPtrArray *elements;
	GVariant *tuple[2];
	const gchar *string;
	GVariant *supported;

	elements = g_ptr_array_new ();

	while (creds->type) {
		if (creds->owner == REALM_CREDENTIAL_OWNER_ADMIN)
			string = "administrator";
		else if (creds->owner == REALM_CREDENTIAL_OWNER_USER)
			string = "user";
		else if (creds->owner == REALM_CREDENTIAL_OWNER_COMPUTER)
			string = "computer";
		else if (creds->owner == REALM_CREDENTIAL_OWNER_NONE)
			string = "none";
		else
			g_return_val_if_reached (NULL);

		tuple[1] = g_variant_new_string (string);

		switch (creds->type) {
		case REALM_CREDENTIAL_CCACHE:
			string = "ccache";
			break;
		case REALM_CREDENTIAL_PASSWORD:
			string = "password";
			break;
		case REALM_CREDENTIAL_SECRET:
			string = "secret";
			break;
		case REALM_CREDENTIAL_AUTOMATIC:
			string = "automatic";
			break;
		default:
			g_return_val_if_reached (NULL);
			break;
		}

		tuple[0] = g_variant_new_string (string);

		g_ptr_array_add (elements, g_variant_new_tuple (tuple, 2));
		creds++;
	}

	supported = g_variant_new_array (G_VARIANT_TYPE ("(ss)"),
	                                 (GVariant *const *)elements->pdata,
	                                 elements->len);

	g_ptr_array_free (elements, TRUE);
	g_variant_ref_sink (supported);
	return supported;
}
