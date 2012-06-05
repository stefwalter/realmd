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

#include "realm-settings.h"
#include "realm-ini-config.h"

#include <string.h>

#define REALM_INI_CONFIG_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), REALM_TYPE_INI_CONFIG, RealmIniConfigClass))
#define REALM_IS_INI_CONFIG_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), REALM_TYPE_INI_CONFIG))
#define REALM_INI_CONFIG_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), REALM_TYPE_INI_CONFIG, RealmIniConfigClass))

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

struct _RealmIniConfig {
	GObject parent;
	gint flags;
	GHashTable *sections;
	ConfigLine *head;
	ConfigLine *tail;

	gchar *filename;
	GFileMonitor *monitor;
	gulong monitor_sig;
	guint reload_scheduled;
};

typedef struct {
	GObjectClass parent_class;
} RealmIniConfigClass;

enum {
	PROP_0,
	PROP_FLAGS
};

enum {
	CHANGED,
	NUM_SIGNALS
};
static guint signals[NUM_SIGNALS] = { 0, };

G_DEFINE_TYPE (RealmIniConfig, realm_ini_config, G_TYPE_OBJECT);

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
realm_ini_config_init (RealmIniConfig *self)
{
	self->sections = g_hash_table_new_full (conf_str_hash, conf_str_equal,
	                                        NULL, config_section_free);
}

static void
realm_ini_config_set_property (GObject *obj,
                               guint prop_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
	RealmIniConfig *self = REALM_INI_CONFIG (obj);

	switch (prop_id) {
	case PROP_FLAGS:
		self->flags = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static gboolean
on_changes_reload_file (gpointer user_data)
{
	RealmIniConfig *self = REALM_INI_CONFIG (user_data);
	realm_ini_config_reload (self);
	return FALSE; /* don't call this timeout again */
}

static void
on_directory_changed (GFileMonitor *monitor,
                      GFile *file,
                      GFile *other_file,
                      GFileMonitorEvent event_type,
                      gpointer user_data)
{
	RealmIniConfig *self = REALM_INI_CONFIG (user_data);
	gchar *event_base;
	gchar *our_base;

	switch (event_type) {
	case G_FILE_MONITOR_EVENT_CHANGED:
	case G_FILE_MONITOR_EVENT_CREATED:
	case G_FILE_MONITOR_EVENT_DELETED:
		break;
	default:
		return;
	}

	if (!self->filename)
		return;

	event_base = g_file_get_basename (file);
	our_base = g_path_get_basename (self->filename);

	/* If it's our file, then schedule a reload */
	if (g_strcmp0 (event_base, our_base) == 0) {
		if (self->reload_scheduled == 0)
			self->reload_scheduled = g_timeout_add (1, on_changes_reload_file, self);
	}

	g_free (event_base);
	g_free (our_base);
}

static void
connect_to_filename (RealmIniConfig *self,
                     const gchar *filename)
{
	GError *error = NULL;
	GFile *directory;
	GFile *file;

	/* Already connected to this filename */
	if (g_strcmp0 (self->filename, filename) == 0)
		return;

	if (self->monitor) {
		g_signal_handler_disconnect (self->monitor, self->monitor_sig);
		g_object_unref (self->monitor);
		self->monitor = NULL;
		self->monitor_sig = 0;
	}

	if (self->reload_scheduled)
		g_source_remove (self->reload_scheduled);
	self->reload_scheduled = 0;

	g_free (self->filename);
	self->filename = NULL;

	if (!filename)
		return;

	self->filename = g_strdup (filename);

	/*
	 * Setup a file monitor. Have to monitor directory, since the file
	 * could theoretically not exist yet.
	 */
	if (!(self->flags & REALM_INI_NO_WATCH)) {
		file = g_file_new_for_path (self->filename);
		directory = g_file_get_parent (file);
		self->monitor = g_file_monitor_directory (directory, G_FILE_MONITOR_NONE,
		                                          NULL, &error);
		if (error == NULL) {
			self->monitor_sig = g_signal_connect (self->monitor, "changed",
			                                      G_CALLBACK (on_directory_changed),
			                                      self);
		} else {
			g_warning ("Couldn't monitor directory: %s", error->message);
			g_error_free (error);
		}
		g_object_unref (directory);
		g_object_unref (file);
	}
}

static void
reset_config_data (RealmIniConfig *self)
{
	ConfigLine *line, *next;

	g_hash_table_remove_all (self->sections);
	for (line = self->head; line != NULL; line = next) {
		next = line->next;
		config_line_free (line);
	}
	self->head = NULL;
	self->tail = NULL;
}

static void
realm_ini_config_finalize (GObject *obj)
{
	RealmIniConfig *self = REALM_INI_CONFIG (obj);

	/* Should free filename and clear up monitors */
	connect_to_filename (self, NULL);
	reset_config_data (self);

	g_hash_table_destroy (self->sections);

	G_OBJECT_CLASS (realm_ini_config_parent_class)->finalize (obj);
}

static void
realm_ini_config_class_init (RealmIniConfigClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = realm_ini_config_set_property;
	object_class->finalize = realm_ini_config_finalize;

	g_object_class_install_property (object_class, PROP_FLAGS,
	               g_param_spec_int ("flags", "Flags", "Ini file flags",
	                                 0, G_MAXINT, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

	signals[CHANGED] = g_signal_new ("changed", REALM_TYPE_INI_CONFIG, G_SIGNAL_RUN_FIRST,
	                                 0, NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 0);
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
remove_new_lines (RealmIniConfig *self,
                  GString *value)
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
		if ((self->flags & REALM_INI_LINE_CONTINUATIONS) &&
		    (at > value->str && *(at - 1) == '\\'))
			g_string_erase (value, (at - 1) - value->str, 2);
		else
			g_string_erase (value, at - value->str, 1);
	}
}

static gchar *
parse_config_line_value (RealmIniConfig *self,
                         GBytes *bytes)
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
	remove_new_lines (self, value);

	return g_strstrip (g_string_free (value, FALSE));
}

static void
append_config_line (RealmIniConfig *self,
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
insert_config_line (RealmIniConfig *self,
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
remove_config_line (RealmIniConfig *self,
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
parse_config_line (RealmIniConfig *self,
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

static void
parse_config_bytes (RealmIniConfig *self,
                    GBytes *bytes)
{
	ConfigSection *current;
	GBytes *line;
	const gchar *beg;
	const gchar *end;
	const gchar *at;
	const gchar *from;
	gsize len;

	/* Clear the current data */
	reset_config_data (self);

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
			if ((self->flags & REALM_INI_LINE_CONTINUATIONS) &&
			    (last != NULL && *last == '\\'))
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

	g_signal_emit (self, signals[CHANGED], 0);
}

void
realm_ini_config_read_string (RealmIniConfig *self,
                                const gchar *data)
{
	GBytes *bytes;

	g_return_if_fail (REALM_IS_INI_CONFIG (self));
	g_return_if_fail (data != NULL);

	bytes = g_bytes_new (data, strlen (data));
	realm_ini_config_read_bytes (self, bytes);
	g_bytes_unref (bytes);
}

void
realm_ini_config_read_bytes (RealmIniConfig *self,
                             GBytes *bytes)
{
	g_return_if_fail (REALM_IS_INI_CONFIG (self));
	g_return_if_fail (bytes != NULL);

	connect_to_filename (self, NULL);
	parse_config_bytes (self, bytes);
}

GBytes *
realm_ini_config_write_bytes (RealmIniConfig *self)
{
	ConfigLine *line;
	GString *result;
	const gchar *data;
	gsize len;

	g_return_val_if_fail (REALM_IS_INI_CONFIG (self), NULL);

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
realm_ini_config_read_file (RealmIniConfig *self,
                            const gchar *filename,
                            GError **error)
{
	GError *err = NULL;
	GBytes *bytes;
	gchar *contents;
	gsize length;

	g_return_val_if_fail (REALM_IS_INI_CONFIG (self), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	g_file_get_contents (filename, &contents, &length, &err);

	/* Ignore errors of the file not existing */
	if (g_error_matches (err, G_FILE_ERROR, G_FILE_ERROR_NOENT))
		g_clear_error (&err);

	if (err != NULL) {
		g_propagate_error (error, err);
		return FALSE;
	}

	bytes = g_bytes_new_take (contents, length);
	parse_config_bytes (self, bytes);
	g_bytes_unref (bytes);

	connect_to_filename (self, filename);
	return TRUE;
}

gboolean
realm_ini_config_write_file (RealmIniConfig *self,
                             const gchar *filename,
                             GError **error)
{
	GBytes *bytes;
	gboolean ret = TRUE;
	const gchar *contents;
	gsize length;

	g_return_val_if_fail (REALM_IS_INI_CONFIG (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (filename == NULL)
		filename = self->filename;
	g_return_val_if_fail (filename != NULL, FALSE);

	bytes = realm_ini_config_write_bytes (self);
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

static void
config_set_value (RealmIniConfig *self,
                  const gchar *section,
                  const gchar *name,
                  const gchar *value)
{
	ConfigSection *sect;
	ConfigLine *line;
	gchar *data;

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

void
realm_ini_config_set (RealmIniConfig *self,
                      const gchar *section,
                      const gchar *name,
                      const gchar *value)
{
	g_return_if_fail (REALM_IS_INI_CONFIG (self));
	g_return_if_fail (section != NULL);

	config_set_value (self, section, name, value);
	g_signal_emit (self, signals[CHANGED], 0);
}

gchar *
realm_ini_config_get (RealmIniConfig *self,
                      const gchar *section,
                      const gchar *name)
{
	ConfigSection *sect;
	ConfigLine *line;

	g_return_val_if_fail (REALM_IS_INI_CONFIG (self), NULL);
	g_return_val_if_fail (section != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	sect = g_hash_table_lookup (self->sections, section);
	if (sect == NULL)
		return NULL;

	line = g_hash_table_lookup (sect->parameters, name);
	if (line == NULL)
		return NULL;

	return parse_config_line_value (self, line->bytes);
}

GHashTable *
realm_ini_config_get_all (RealmIniConfig *self,
                          const gchar *section)
{
	GHashTableIter iter;
	ConfigSection *sect;
	GHashTable *result;
	const gchar *name;
	ConfigLine *line;

	g_return_val_if_fail (REALM_IS_INI_CONFIG (self), NULL);
	g_return_val_if_fail (section != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	sect = g_hash_table_lookup (self->sections, section);
	if (sect == NULL)
		return NULL;

	result = g_hash_table_new_full (conf_str_hash, conf_str_equal, g_free, g_free);

	g_hash_table_iter_init (&iter, sect->parameters);
	while (g_hash_table_iter_next (&iter, (gpointer *)&name, (gpointer *)&line))
		g_hash_table_replace (result, g_strdup (name), parse_config_line_value (self, line->bytes));

	return result;
}

void
realm_ini_config_set_all (RealmIniConfig *self,
                          const gchar *section,
                          GHashTable *parameters)
{
	GHashTableIter iter;
	const gchar *name;
	const gchar *value;

	g_return_if_fail (REALM_IS_INI_CONFIG (self));
	g_return_if_fail (section != NULL);
	g_return_if_fail (parameters != NULL);

	g_hash_table_iter_init (&iter, parameters);
	while (g_hash_table_iter_next (&iter, (gpointer *)&name, (gpointer *)&value))
		config_set_value (self, section, name, value);

	g_signal_emit (self, signals[CHANGED], 0);
}

gchar **
realm_ini_config_get_list (RealmIniConfig *self,
                           const gchar *section,
                           const gchar *name,
                           const gchar *delimiters)
{
	gchar **values;
	gchar *value;
	gint i;

	g_return_val_if_fail (REALM_IS_INI_CONFIG (self), NULL);
	g_return_val_if_fail (section != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (delimiters != NULL, NULL);

	value = realm_ini_config_get (self, section, name);
	if (value == NULL)
		return NULL;

	values = g_strsplit_set (value, delimiters, -1);
	for (i = 0; values[i] != NULL; i++)
		values[i] = g_strstrip (values[i]);
	g_free (value);

	return values;
}

void
realm_ini_config_set_list (RealmIniConfig *self,
                           const gchar *section,
                           const gchar *name,
                           const gchar *delimiter,
                           const gchar **values)
{
	gchar *value;

	g_return_if_fail (REALM_IS_INI_CONFIG (self));
	g_return_if_fail (section != NULL);
	g_return_if_fail (name != NULL);
	g_return_if_fail (delimiter != NULL);

	value = g_strjoinv (delimiter, (gchar **)values);
	realm_ini_config_set (self, section, name, value);
	g_free (value);
}

void
realm_ini_config_reset (RealmIniConfig *self)
{
	g_return_if_fail (REALM_IS_INI_CONFIG (self));

	reset_config_data (self);
	g_signal_emit (self, signals[CHANGED], 0);
}

void
realm_ini_config_reload (RealmIniConfig *self)
{
	GError *error = NULL;

	g_return_if_fail (self->filename != NULL);

	self->reload_scheduled = 0;

	realm_ini_config_read_file (self, self->filename, &error);
	if (error != NULL) {
		g_warning ("Couldn't reload config file: %s: %s",
		           self->filename, error->message);
		g_clear_error (&error);
	}
}

RealmIniConfig *
realm_ini_config_new (RealmIniFlags flags)
{
	return g_object_new (REALM_TYPE_INI_CONFIG,
	                     "flags", flags,
	                     NULL);
}
