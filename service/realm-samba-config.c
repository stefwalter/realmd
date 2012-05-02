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

#include "realm-platform.h"
#include "realm-samba-config.h"

#include <string.h>

#define REALM_SAMBA_CONFIG_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), REALM_TYPE_SAMBA_CONFIG, RealmSambaConfigClass))
#define REALM_IS_SAMBA_CONFIG_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), REALM_TYPE_SAMBA_CONFIG))
#define REALM_SAMBA_CONFIG_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), REALM_TYPE_SAMBA_CONFIG, RealmSambaConfigClass))

typedef struct _ConfigLine {
	gchar *name;
	GBytes *bytes;
	struct _ConfigLine *prev;
	struct _ConfigLine *next;
} ConfigLine;

typedef struct {
	GHashTable *parameters;
	ConfigLine *tail;
} ConfigSection;

struct _RealmSambaConfig {
	GObject parent;
	GHashTable *sections;
	ConfigLine *head;
	ConfigLine *tail;
};

typedef struct {
	GObjectClass parent_class;
} RealmSambaConfigClass;

G_DEFINE_TYPE (RealmSambaConfig, realm_samba_config, G_TYPE_OBJECT);

static guint
conf_str_hash (gconstpointer v)
{
	const signed char *p;
	guint32 h = 5381;

	/* Case insensitive for ascii */
	for (p = v; *p != '\0'; p++)
		h = (h << 5) + h + g_ascii_tolower (*p);

	return h;
}

static gboolean
conf_str_equal (gconstpointer v1,
                gconstpointer v2)
{
	const gchar *string1 = v1;
	const gchar *string2 = v2;

	/* Case insensitive for ascii */
	return g_ascii_strcasecmp (string1, string2) == 0;
}

static void
config_section_free (gpointer data)
{
	ConfigSection *sect = data;
	g_hash_table_destroy (sect->parameters);
	g_slice_free (ConfigSection, sect);
}

static void
config_line_free (gpointer data)
{
	ConfigLine *line = data;
	g_free (line->name);
	g_bytes_unref (line->bytes);
	g_slice_free (ConfigLine, line);
}

static void
realm_samba_config_init (RealmSambaConfig *self)
{
	self->sections = g_hash_table_new_full (conf_str_hash, conf_str_equal,
	                                        NULL, config_section_free);
}

static void
realm_samba_config_class_finalize (GObject *obj)
{
	RealmSambaConfig *self = REALM_SAMBA_CONFIG (obj);
	ConfigLine *line, *next;

	g_hash_table_destroy (self->sections);
	for (line = self->head; line != NULL; line = next) {
		next = line->next;
		config_line_free (line);
	}

	G_OBJECT_CLASS (realm_samba_config_parent_class)->finalize (obj);
}

static void
realm_samba_config_class_init (RealmSambaConfigClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = realm_samba_config_class_finalize;
}

enum {
	NONE,
	COMMENT,
	SECTION,
	PARAMETER,
	INVALID
};

static gint
parse_config_line_type_and_name (GBytes *bytes,
                                 gchar **name)
{
	const gchar *from;
	const gchar *end;
	const gchar *at;
	gsize len;

	*name = NULL;

	at = g_bytes_get_data (bytes, &len);
	end = at + len;

	/* Skip initial spaces */
	while (at < end && g_ascii_isspace (*at))
		at++;

	if (at == end)
		return NONE;

	/* A comment? */
	if (*at == '#' || *at == ';')
		return COMMENT;

	/* A section? */
	if (*at == '[') {
		at++;
		from = at;

		while (at < end && *at != ']' && *at != '\n')
			at++;
		if (at < end && *at == ']' && at > from) {
			*name = g_strndup (from, at - from);
			return SECTION;
		}

		return NONE;
	}

	/* A parameter? */
	from = at;
	at = memchr (from, '=', end - from);
	if (at != NULL && at > from) {
		while (at - 1 > from && g_ascii_isspace (*(at - 1)))
			at--;
		if (at > from) {
			*name = g_strndup (from, at - from);
			return PARAMETER;
		}
	}

	return INVALID;
}

static void
remove_new_lines (GString *value)
{
	gsize offset = 0;

	/* Remove all \r charaters from DOS style endings */
	for (;;) {
		gchar *at = strchr (value->str + offset, '\r');
		if (at == NULL)
			break;
		g_string_erase (value, at - value->str, 1);
	}

	offset = 0;

	/* Remove all \n characters, including escaped newlines */
	for (;;) {
		gchar *at = strchr (value->str + offset, '\n');
		if (at == NULL)
			break;
		if (at > value->str && *(at - 1) == '\\')
			g_string_erase (value, (at - 1) - value->str, 2);
		else
			g_string_erase (value, at - value->str, 1);
	}
}

static gchar *
parse_config_line_value (GBytes *bytes)
{
	GString *value;
	const gchar *end;
	const gchar *at;
	gsize len;

	at = g_bytes_get_data (bytes, &len);
	end = at + len;

	/* Should always have an = when parsed */
	at = memchr (at, '=', end - at);
	g_return_val_if_fail (at != NULL, NULL);

	at++;

	/* Skip spaces after equal */
	while (at < end && g_ascii_isspace (*at))
		at++;

	value = g_string_new_len (at, end - at);

	/* Remove any continuations and line endings */
	remove_new_lines (value);

	return g_string_free (value, FALSE);
}

static void
append_config_line (RealmSambaConfig *self,
                    ConfigLine *line)
{
	if (self->tail == NULL) {
		self->head = line;
		self->tail = line;
	} else {
		self->tail->next = line;
		line->prev = self->tail;
		self->tail = line;
	}
}

static void
insert_config_line (RealmSambaConfig *self,
                    ConfigLine *after,
                    ConfigLine *line)
{
	g_assert (after != NULL);
	g_assert (line != NULL);

	line->next = after->next;
	line->prev = after;
	if (after->next)
		after->next->prev = line;
	after->next = line;
}

static void
remove_config_line (RealmSambaConfig *self,
                    ConfigLine *line)
{
	g_assert (line != NULL);

	/* We only get called for parameters in sections */
	g_assert (line->prev != NULL);
	g_assert (self->head != line);

	if (line->next)
		line->next->prev = line->prev;
	line->prev->next = line->next;
}

static void
parse_config_line (RealmSambaConfig *self,
                   GBytes *bytes,
                   ConfigSection **current)
{
	ConfigSection *sect;
	ConfigLine *line;
	gchar *name = NULL;
	gint type;

	line = g_slice_new0 (ConfigLine);
	line->bytes = g_bytes_ref (bytes);

	/* What kind of line is this? */
	type = parse_config_line_type_and_name (bytes, &name);
	switch (type) {
	case SECTION:
		sect = g_hash_table_lookup (self->sections, name);
		if (sect == NULL) {
			sect = g_slice_new0 (ConfigSection);
			sect->parameters = g_hash_table_new (conf_str_hash, conf_str_equal);
			g_hash_table_replace (self->sections, name, sect);
			sect->tail = line;
		}
		*current = sect;
		break;
	case PARAMETER:
		if (*current != NULL)
			g_hash_table_insert ((*current)->parameters, name, line);
		break;
	}

	line->name = name;
	append_config_line (self, line);

	/* Add this line as the end of the current section */
	if (type != NONE && *current != NULL)
		(*current)->tail = line;
}

void
realm_samba_config_read_string (RealmSambaConfig *self,
                                const gchar *data)
{
	GBytes *bytes;

	g_return_if_fail (REALM_IS_SAMBA_CONFIG (self));
	g_return_if_fail (data != NULL);

	bytes = g_bytes_new (data, strlen (data));
	realm_samba_config_read_bytes (self, bytes);
	g_bytes_unref (bytes);
}

void
realm_samba_config_read_bytes (RealmSambaConfig *self,
                               GBytes *bytes)
{
	ConfigSection *current;
	GBytes *line;
	const gchar *beg;
	const gchar *end;
	const gchar *at;
	const gchar *from;
	gsize len;

	g_return_if_fail (REALM_IS_SAMBA_CONFIG (self));
	g_return_if_fail (bytes != NULL);

	current = NULL;

	beg = from = at = g_bytes_get_data (bytes, &len);
	end = at + len;

	for (;;) {
		const gchar *search = at;
		at = memchr (search, '\n', end - search);
		if (at == NULL) {
			line = g_bytes_new_from_bytes (bytes,
			                               from - beg,
			                               end - from);

		} else {
			const gchar *last = at > search ? at - 1 : NULL;
			at++;

			/* Line continuation */
			if (last != NULL && *last == '\\')
				continue;

			line = g_bytes_new_from_bytes (bytes,
			                               from - beg,
			                               at - from);
		}

		parse_config_line (self, line, &current);
		g_bytes_unref (line);

		if (at == NULL)
			break;
		from = at;
	}
}

GBytes *
realm_samba_config_write_bytes (RealmSambaConfig *self)
{
	ConfigLine *line;
	GString *result;
	const gchar *data;
	gsize len;

	g_return_val_if_fail (REALM_IS_SAMBA_CONFIG (self), NULL);

	result = g_string_sized_new (4096);
	for (line = self->head; line != NULL; line = line->next) {
		/*
		 * Add \n between lines if not already present. This happens
		 * if the file was parsed without a trailing \n, and then
		 * stuff was added to the end.
		 */
		if (result->len > 0 && result->str[result->len - 1] != '\n')
			g_string_append_c (result, '\n');

		data = g_bytes_get_data (line->bytes, &len);
		g_string_append_len (result, data, len);
	}

	len = result->len;
	return g_bytes_new_take (g_string_free (result, FALSE), len);
}

gboolean
realm_samba_config_read_file (RealmSambaConfig *self,
                              const gchar *filename,
                              GError **error)
{
	GBytes *bytes;
	gchar *contents;
	gsize length;

	g_return_val_if_fail (REALM_IS_SAMBA_CONFIG (self), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!g_file_get_contents (filename, &contents, &length, error))
		return FALSE;

	bytes = g_bytes_new_take (contents, length);
	realm_samba_config_read_bytes (self, bytes);
	g_bytes_unref (bytes);

	return TRUE;
}

gboolean
realm_samba_config_write_file (RealmSambaConfig *self,
                               const gchar *filename,
                               GError **error)
{
	GBytes *bytes;
	gboolean ret = TRUE;
	const gchar *contents;
	gsize length;

	g_return_val_if_fail (REALM_IS_SAMBA_CONFIG (self), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	bytes = realm_samba_config_write_bytes (self);
	g_return_val_if_fail (bytes != NULL, FALSE);

	contents = g_bytes_get_data (bytes, &length);

	/*
	 * If not writing any data, and the no file is present, don't
	 * write an empty file.
	 */
	if (length > 0 || g_file_test (filename, G_FILE_TEST_EXISTS))
		ret = g_file_set_contents (filename, contents, length, error);

	g_bytes_unref (bytes);
	return ret;
}

gboolean
realm_samba_config_read_system (RealmSambaConfig *self,
                                GError **error)
{
	const gchar *filename;
	GError *err = NULL;
	gboolean ret;

	g_return_val_if_fail (REALM_IS_SAMBA_CONFIG (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	filename = realm_platform_path ("smb.conf");
	ret = realm_samba_config_read_file (self, filename, &err);

	/* Ignore errors of the file not existing */
	if (g_error_matches (err, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
		g_clear_error (&err);
		ret = TRUE;
	}

	if (err != NULL)
		g_propagate_error (error, err);

	return ret;
}

gboolean
realm_samba_config_write_system (RealmSambaConfig *self,
                                 GError **error)
{
	const gchar *filename;

	g_return_val_if_fail (REALM_IS_SAMBA_CONFIG (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	filename = realm_platform_path ("smb.conf");
	return realm_samba_config_write_file (self, filename, error);
}

void
realm_samba_config_set (RealmSambaConfig *self,
                        const gchar *section,
                        const gchar *name,
                        const gchar *value)
{
	ConfigSection *sect;
	ConfigLine *line;
	gchar *data;

	g_return_if_fail (REALM_IS_SAMBA_CONFIG (self));
	g_return_if_fail (section != NULL);
	g_return_if_fail (strchr (section, ']') == NULL);
	g_return_if_fail (strchr (section, '[') == NULL);
	g_return_if_fail (name != NULL);
	g_return_if_fail (strchr (name, '=') == NULL);
	g_return_if_fail (strchr (name, '\n') == NULL);
	g_return_if_fail (value == NULL || strchr (value ? value : NULL, '\n') == NULL);

	sect = g_hash_table_lookup (self->sections, section);
	if (sect == NULL) {
		/* No such section, and removing */
		if (value == NULL)
			return;

		/* A blank line */
		line = g_slice_new0 (ConfigLine);
		line->bytes = g_bytes_new ("\n", 1);
		line->name = NULL;
		append_config_line (self, line);

		/* The actual section header */
		data = g_strdup_printf ("[%s]\n", section);
		line = g_slice_new0 (ConfigLine);
		line->bytes = g_bytes_new_take (data, strlen (data));
		line->name = g_strdup (section);
		append_config_line (self, line);

		/* Register it */
		sect = g_slice_new0 (ConfigSection);
		sect->parameters = g_hash_table_new (conf_str_hash, conf_str_equal);
		sect->tail = line;
		g_hash_table_replace (self->sections, line->name, sect);
	}

	line = g_hash_table_lookup (sect->parameters, name);

	/* Removing this parameter? */
	if (value == NULL) {
		if (line != NULL) {
			if (sect->tail == line)
				sect->tail = line->prev;
			remove_config_line (self, line);
			config_line_free (line);
		}
		return;
	}

	data = g_strdup_printf ("%s = %s\n", name, value);

	/* Don't have this line, add to section */
	if (line == NULL) {
		line = g_slice_new0 (ConfigLine);
		line->bytes = g_bytes_new_take (data, strlen (data));
		line->name = g_strdup (name);
		insert_config_line (self, sect->tail, line);

	/* Already have this line, replace the data */
	} else {
		g_bytes_unref (line->bytes);
		line->bytes = g_bytes_new_take (data, strlen (data));
	}
}

gchar *
realm_samba_config_get (RealmSambaConfig *self,
                        const gchar *section,
                        const gchar *name)
{
	ConfigSection *sect;
	ConfigLine *line;

	g_return_val_if_fail (REALM_IS_SAMBA_CONFIG (self), NULL);
	g_return_val_if_fail (section != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	sect = g_hash_table_lookup (self->sections, section);
	if (sect == NULL)
		return NULL;

	line = g_hash_table_lookup (sect->parameters, name);
	if (line == NULL)
		return NULL;

	return parse_config_line_value (line->bytes);
}

GHashTable *
realm_samba_config_get_all (RealmSambaConfig *self,
                            const gchar *section)
{
	GHashTableIter iter;
	ConfigSection *sect;
	GHashTable *result;
	const gchar *name;
	ConfigLine *line;

	g_return_val_if_fail (REALM_IS_SAMBA_CONFIG (self), NULL);
	g_return_val_if_fail (section != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	sect = g_hash_table_lookup (self->sections, section);
	if (sect == NULL)
		return NULL;

	result = g_hash_table_new_full (conf_str_hash, conf_str_equal, g_free, g_free);

	g_hash_table_iter_init (&iter, sect->parameters);
	while (g_hash_table_iter_next (&iter, (gpointer *)&name, (gpointer *)&line))
		g_hash_table_replace (result, g_strdup (name), parse_config_line_value (line->bytes));

	return result;
}

void
realm_samba_config_set_all (RealmSambaConfig *self,
                            const gchar *section,
                            GHashTable *parameters)
{
	GHashTableIter iter;
	const gchar *name;
	const gchar *value;

	g_return_if_fail (REALM_IS_SAMBA_CONFIG (self));
	g_return_if_fail (section != NULL);
	g_return_if_fail (parameters != NULL);

	g_hash_table_iter_init (&iter, parameters);
	while (g_hash_table_iter_next (&iter, (gpointer *)&name, (gpointer *)&value))
		realm_samba_config_set (self, section, name, value);
}

gboolean
realm_samba_config_change (const gchar *section,
                           GError **error,
                           ...)
{
	GHashTable *parameters;
	const gchar *name;
	const gchar *value;
	gboolean ret;
	va_list va;

	g_return_val_if_fail (section != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	parameters = g_hash_table_new (conf_str_hash, conf_str_equal);
	va_start (va, error);
	while ((name = va_arg (va, const gchar *)) != NULL) {
		value = va_arg (va, const gchar *);
		g_hash_table_insert (parameters, (gpointer)name, (gpointer)value);
	}
	va_end (va);

	ret = realm_samba_config_changev (section, parameters, error);
	g_hash_table_unref (parameters);
	return ret;
}

gboolean
realm_samba_config_changev (const gchar *section,
                            GHashTable *parameters,
                            GError **error)
{
	RealmSambaConfig *config;
	gboolean ret = FALSE;

	g_return_val_if_fail (section != NULL, FALSE);
	g_return_val_if_fail (parameters != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	config = realm_samba_config_new ();

	if (realm_samba_config_read_system (config, error)) {
		realm_samba_config_set_all (config, section, parameters);
		ret = realm_samba_config_write_system (config, error);
	}

	g_object_unref (config);
	return ret;
}

RealmSambaConfig *
realm_samba_config_new (void)
{
	return g_object_new (REALM_TYPE_SAMBA_CONFIG, NULL);
}
