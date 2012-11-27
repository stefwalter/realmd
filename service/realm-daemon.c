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

#include "realm-all-provider.h"
#include "realm-daemon.h"
#include "realm-dbus-constants.h"
#include "realm-dbus-generated.h"
#include "realm-diagnostics.h"
#include "realm-errors.h"
#include "realm-example-provider.h"
#include "realm-kerberos-provider.h"
#include "realm-samba-provider.h"
#include "realm-settings.h"
#include "realm-sssd-provider.h"

#include <glib.h>
#include <glib-unix.h>
#include <glib/gi18n.h>

#include <polkit/polkit.h>

#include <stdio.h>
#include <errno.h>

#define TIMEOUT        60 /* seconds */
#define HOLD_INTERNAL  (GUINT_TO_POINTER (~0))

static GObject *current_invocation = NULL;
static GMainLoop *main_loop = NULL;

static gboolean service_persist = FALSE;
static GHashTable *service_clients = NULL;
static gint64 service_quit_at = 0;
static guint service_timeout_id = 0;
static guint service_bus_name_owner_id = 0;
static gboolean service_bus_name_claimed = FALSE;
static GDBusObjectManagerServer *object_server = NULL;
static gboolean service_debug = FALSE;
static gchar *service_install = NULL;
static gint service_dbus_fd = -1;

typedef struct {
	guint watch;
	gchar *locale;
} RealmClient;

/* We use this for registering the dbus errors */
GQuark realm_error = 0;

/* We use a lock here because it's called from dbus threads */
G_LOCK_DEFINE(polkit_authority);
static PolkitAuthority *polkit_authority = NULL;

static void
on_invocation_gone (gpointer unused,
                    GObject *where_the_object_was)
{
	g_warning ("a GDBusMethodInvocation was released but the invocation was "
	           "registered as part of a realm_daemon_lock_for_action()");
	g_assert (where_the_object_was == current_invocation);
	current_invocation = NULL;
}

gboolean
realm_daemon_lock_for_action (GDBusMethodInvocation *invocation)
{
	g_return_val_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation), FALSE);

	if (current_invocation)
		return FALSE;

	current_invocation = G_OBJECT (invocation);
	g_object_weak_ref (current_invocation, on_invocation_gone, NULL);

	/* Hold the daemon up while action */
	realm_daemon_hold ("current-invocation");

	return TRUE;
}

void
realm_daemon_unlock_for_action (GDBusMethodInvocation *invocation)
{
	g_return_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation));

	if (current_invocation != G_OBJECT (invocation)) {
		g_warning ("trying to realm_daemon_unlock_for_action() with an invocation "
		           "that is not registered as the current locked action.");
		return;
	}

	g_object_weak_unref (current_invocation, on_invocation_gone, NULL);
	current_invocation = NULL;

	/* Matches the hold in realm_daemon_lock_for_action() */
	realm_daemon_release ("current-invocation");
}

gboolean
realm_daemon_is_dbus_peer (void)
{
	return service_dbus_fd != -1;
}

gboolean
realm_daemon_is_install_mode (void)
{
	return service_install != NULL;
}

gboolean
realm_daemon_has_debug_flag (void)
{
	return service_debug;
}

void
realm_daemon_set_locale_until_loop (GDBusMethodInvocation *invocation)
{
	/* TODO: Not yet implemented, need threadsafe implementation */
}

gboolean
realm_daemon_check_dbus_action (const gchar *sender,
                                const gchar *action_id)
{
	PolkitAuthorizationResult *result;
	PolkitAuthority *authority;
	PolkitSubject *subject;
	GError *error = NULL;
	gboolean ret;

	/* If we're a dbus peer, just allow all calls */
	if (realm_daemon_is_dbus_peer ())
		return TRUE;

	g_return_val_if_fail (sender != NULL, FALSE);
	g_return_val_if_fail (action_id != NULL, FALSE);

	G_LOCK (polkit_authority);

	authority = polkit_authority ? g_object_ref (polkit_authority) : NULL;

	G_UNLOCK (polkit_authority);

	if (!authority) {
		authority = polkit_authority_get_sync (NULL, &error);
		if (authority == NULL) {
			g_warning ("failure to get polkit authority: %s", error->message);
			g_error_free (error);
			return FALSE;
		}

		G_LOCK (polkit_authority);

		if (polkit_authority == NULL) {
			polkit_authority = g_object_ref (authority);

		} else {
			g_object_unref (authority);
			authority = g_object_ref (polkit_authority);
		}

		G_UNLOCK (polkit_authority);
	}

	/* do authorization async */
	subject = polkit_system_bus_name_new (sender);
	result = polkit_authority_check_authorization_sync (authority, subject, action_id, NULL,
			POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION, NULL, &error);

	g_object_unref (authority);
	g_object_unref (subject);

	/* failed */
	if (result == NULL) {
		g_warning ("couldn't check polkit authorization%s%s",
		           error ? ": " : "", error ? error->message : "");
		g_error_free (error);
		return FALSE;
	}

	ret = polkit_authorization_result_get_is_authorized (result);
	g_object_unref (result);

	return ret;
}

static void
on_client_vanished (GDBusConnection *connection,
                    const gchar *name,
                    gpointer user_data)
{
	g_debug ("client gone away: %s", name);
	g_hash_table_remove (service_clients, name);
}

static RealmClient *
lookup_or_register_client (const gchar *sender)
{
	RealmClient *client;

	client = g_hash_table_lookup (service_clients, sender);
	if (!client) {
		client = g_slice_new0 (RealmClient);
		client->watch = g_bus_watch_name (G_BUS_TYPE_SYSTEM, sender,
		                                  G_BUS_NAME_WATCHER_FLAGS_NONE,
		                                  NULL, on_client_vanished, NULL, NULL);
		g_debug ("client using service: %s", sender);
		g_hash_table_insert (service_clients, g_strdup (sender), client);
	}

	return client;
}

void
realm_daemon_hold (const gchar *hold)
{
	/*
	 * We register these holds in the same table as the clients
	 * so need to make sure they don't colide with them.
	 */

	g_assert (hold != NULL);
	g_assert (!g_dbus_is_unique_name (hold));


	if (g_hash_table_lookup (service_clients, hold))
		g_critical ("realm_daemon_hold: already have hold: %s", hold);
	g_debug ("holding service: %s", hold);
	g_hash_table_insert (service_clients, g_strdup (hold), g_slice_new0 (RealmClient));
}

void
realm_daemon_release (const gchar *hold)
{
	g_assert (hold != NULL);
	g_assert (!g_dbus_is_unique_name (hold));

	g_debug ("releasing service: %s", hold);
	if (!g_hash_table_remove (service_clients, hold))
		g_critical ("realm_daemon_release: don't have hold: %s", hold);
}

static gboolean
on_service_timeout (gpointer data)
{
	gint seconds;
	gint64 now;

	service_timeout_id = 0;

	if (g_hash_table_size (service_clients) > 0)
		return FALSE;

	now = g_get_monotonic_time ();
	if (now >= service_quit_at) {
		g_debug ("quitting realmd service after timeout");
		g_main_loop_quit (main_loop);

	} else {
		seconds = (service_quit_at - now) / G_TIME_SPAN_SECOND;
		service_timeout_id = g_timeout_add_seconds (seconds + 1, on_service_timeout, NULL);
	}

	return FALSE;
}

void
realm_daemon_poke (void)
{
	if (service_persist)
		return;
	if (g_hash_table_size (service_clients) > 0)
		return;
	service_quit_at = g_get_monotonic_time () + (TIMEOUT * G_TIME_SPAN_SECOND);
	if (service_timeout_id == 0)
		on_service_timeout (NULL);
}

static void
realm_client_unwatch_and_free (gpointer data)
{
	RealmClient *client = data;

	g_assert (data != NULL);
	if (client->watch)
		g_bus_unwatch_name (client->watch);
	g_free (client->locale);
	g_slice_free (RealmClient, client);

	realm_daemon_poke ();
}

static gboolean
on_idle_hold_for_message (gpointer user_data)
{
	GDBusMessage *message = user_data;
	const gchar *sender = g_dbus_message_get_sender (message);
	lookup_or_register_client (sender);
	return FALSE; /* don't call again */
}

static GDBusMessage *
on_connection_filter (GDBusConnection *connection,
                      GDBusMessage *message,
                      gboolean incoming,
                      gpointer user_data)
{
	const gchar *own_name = user_data;
	GDBusMessageType type;

	/* Each time we see an incoming function call, keep the service alive */
	if (incoming) {
		type = g_dbus_message_get_message_type (message);
		if (type == G_DBUS_MESSAGE_TYPE_METHOD_CALL) {

			/* All methods besides 'Release' on the Service interface cause us to watch client */
			if (!g_str_equal (own_name, g_dbus_message_get_sender (message)) &&
			    !(g_str_equal (g_dbus_message_get_path (message), REALM_DBUS_SERVICE_PATH) &&
			      g_str_equal (g_dbus_message_get_member (message), "Release") &&
			      g_str_equal (g_dbus_message_get_interface (message), REALM_DBUS_SERVICE_INTERFACE))) {
				g_idle_add_full (G_PRIORITY_DEFAULT,
				                 on_idle_hold_for_message,
				                 g_object_ref (message),
				                 g_object_unref);
			}
		}
	}

	return message;
}

static gboolean
on_service_release (RealmDbusService *object,
                    GDBusMethodInvocation *invocation)
{
	const char *sender;

	sender = g_dbus_method_invocation_get_sender (invocation);
	g_debug ("explicitly releasing service: %s", sender);
	g_hash_table_remove (service_clients, sender);

	return TRUE;
}

static gboolean
on_service_cancel (RealmDbusService *object,
                   GDBusMethodInvocation *invocation,
                   const gchar *operation_id)
{
	/* TODO: Needs implementation */
	realm_dbus_service_complete_cancel (object, invocation);
	return TRUE;
}

static gboolean
on_service_set_locale (RealmDbusService *object,
                       GDBusMethodInvocation *invocation,
                       const gchar *arg_locale)
{
	RealmClient *client;
	const gchar *sender;

	sender = g_dbus_method_invocation_get_sender (invocation);
	client = lookup_or_register_client (sender);

	g_free (client->locale);
	client->locale = g_strdup (arg_locale);

	realm_dbus_service_complete_set_locale (object, invocation);
	return TRUE;
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar *name,
                  gpointer unused)
{
	service_bus_name_claimed = TRUE;
	g_debug ("claimed name on bus: %s", name);
	realm_daemon_poke ();
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar *name,
              gpointer unused)
{
	if (!service_bus_name_claimed)
		g_message ("couldn't claim service name on DBus bus: %s", name);
	else
		g_warning ("lost service name on DBus bus: %s", name);
}

void
realm_daemon_export_object (GDBusObjectSkeleton *object)
{
	g_return_if_fail (G_IS_DBUS_OBJECT_MANAGER_SERVER (object_server));
	g_return_if_fail (G_IS_DBUS_OBJECT_SKELETON (object));
	g_dbus_object_manager_server_export (object_server, object);
}

static void
initialize_service (GDBusConnection *connection)
{
	RealmProvider *all_provider;
	RealmProvider *provider;

	realm_diagnostics_initialize (connection);

	object_server = g_dbus_object_manager_server_new (REALM_DBUS_SERVICE_PATH);

	all_provider = realm_all_provider_new_and_export (connection);

	provider = realm_sssd_provider_new ();
	g_dbus_object_manager_server_export (object_server, G_DBUS_OBJECT_SKELETON (provider));
	realm_all_provider_register (all_provider, provider);
	g_object_unref (provider);

	provider = realm_samba_provider_new ();
	g_dbus_object_manager_server_export (object_server, G_DBUS_OBJECT_SKELETON (provider));
	realm_all_provider_register (all_provider, provider);
	g_object_unref (provider);

	provider = realm_kerberos_provider_new ();
	g_dbus_object_manager_server_export (object_server, G_DBUS_OBJECT_SKELETON (provider));
	realm_all_provider_register (all_provider, provider);
	g_object_unref (provider);

	if (realm_settings_boolean (REALM_DBUS_IDENTIFIER_EXAMPLE, "enabled")) {
		provider = realm_example_provider_new ();
		g_dbus_object_manager_server_export (object_server, G_DBUS_OBJECT_SKELETON (provider));
		realm_all_provider_register (all_provider, provider);
		g_object_unref (provider);
	}

	g_dbus_object_manager_server_set_connection (object_server, connection);

	/* Use this to control the life time of the providers */
	g_object_set_data_full (G_OBJECT (object_server), "the-provider",
	                        all_provider, g_object_unref);

	/* Matches the hold() in main() */
	realm_daemon_release ("main");

	g_dbus_connection_start_message_processing (connection);
}

static void
on_bus_get_connection (GObject *source,
                       GAsyncResult *result,
                       gpointer unused)
{
	GError *error = NULL;
	GDBusConnection *connection;
	const gchar *self_name;
	guint owner_id;

	connection = g_bus_get_finish (result, &error);
	if (error != NULL) {
		g_warning ("couldn't connect to bus: %s", error->message);
		g_main_loop_quit (main_loop);
		g_error_free (error);

	} else {
		g_debug ("connected to bus");

		/* Add a filter which keeps service alive */
		self_name = g_dbus_connection_get_unique_name (connection);
		g_dbus_connection_add_filter (connection, on_connection_filter,
		                              (gchar *)self_name, NULL);

		initialize_service (connection);

		owner_id = g_bus_own_name_on_connection (connection,
		                                         REALM_DBUS_BUS_NAME,
		                                         G_BUS_NAME_OWNER_FLAGS_NONE,
		                                         on_name_acquired, on_name_lost,
		                                         NULL, NULL);

		service_bus_name_owner_id = owner_id;
		g_object_unref (connection);
	}
}

static void
on_peer_connection_new (GObject *source,
                        GAsyncResult *result,
                        gpointer unused)
{
	GDBusConnection *connection;
	GError *error = NULL;

	connection = g_dbus_connection_new_finish (result, &error);
	if (error != NULL) {
		g_warning ("Couldn't connect to peer: %s", error->message);
		g_main_loop_quit (main_loop);
		g_error_free (error);

	} else {
		g_debug ("connected to peer");
		initialize_service (connection);
		g_object_unref (connection);
	}
}

static gboolean
connect_to_bus_or_peer (void)
{
	GSocketConnection *stream;
	GSocket *socket;
	GError *error = NULL;
	gchar *guid;

	if (service_dbus_fd == -1) {
		g_bus_get (G_BUS_TYPE_SYSTEM, NULL, on_bus_get_connection, NULL);
		return TRUE;
	}

	socket = g_socket_new_from_fd (service_dbus_fd, &error);
	if (error != NULL) {
		g_warning ("Couldn't create socket: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	stream = g_socket_connection_factory_create_connection (socket);
	g_return_val_if_fail (stream != NULL, FALSE);
	g_object_unref (socket);

	guid = g_dbus_generate_guid ();
	g_dbus_connection_new (G_IO_STREAM (stream), guid,
	                       G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_SERVER |
	                       G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_ALLOW_ANONYMOUS |
	                       G_DBUS_CONNECTION_FLAGS_DELAY_MESSAGE_PROCESSING,
	                       NULL, NULL, on_peer_connection_new, NULL);

	g_free (guid);
	g_object_unref (stream);
	return TRUE;
}

static void
on_realm_log_debug (const gchar *log_domain,
                    GLogLevelFlags log_level,
                    const gchar *message,
                    gpointer user_data)
{
	GString *string;
	const gchar *progname;
	int ret;

	string = g_string_new (NULL);

	progname = g_get_prgname ();
	g_string_append_printf (string, "(%s:%lu): %s%sDEBUG: %s\n",
	                        progname ? progname : "process", (gulong)getpid (),
	                        log_domain ? log_domain : "", log_domain ? "-" : "",
	                        message ? message : "(NULL) message");

	ret = write (1, string->str, string->len);

	/* Yes this is dumb, but gets around compiler warning */
	if (ret < 0)
		fprintf (stderr, "couldn't write debug output");

	g_string_free (string, TRUE);
}

static gboolean
on_signal_quit (gpointer data)
{
	g_main_loop_quit (data);
	return FALSE;
}

int
main (int argc,
      char *argv[])
{
	RealmDbusService *service;
	GOptionContext *context;
	GError *error = NULL;
	const gchar *env;
	gchar *path;

	GOptionEntry option_entries[] = {
		{ "debug", 'd', 0, G_OPTION_ARG_NONE, &service_debug,
		  "Turn on debug output, prevent timeout exit", NULL },
		{ "install", 0, 0, G_OPTION_ARG_STRING, &service_install,
		  "Turn on installer mode, install to this prefix", NULL },
		{ "dbus-peer", 0, 0, G_OPTION_ARG_INT, &service_dbus_fd,
		  "Use a peer to peer dbus connection on this fd", NULL },
		{ NULL }
	};

#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif

	g_type_init ();

	/*
	 * Add /sbin to path as a around for problems with authconfig.
	 * See bug:
	 */
	env = g_getenv ("PATH");
	path = g_strdup_printf ("%s:/usr/sbin:/sbin", env ? env : "/usr/bin:/bin");
	g_setenv ("PATH", path, TRUE);
	g_free (path);

	context = g_option_context_new ("realmd");
	g_option_context_add_main_entries (context, option_entries, NULL);
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_message ("%s", error->message);
		g_option_context_free (context);
		g_error_free (error);
		return 2;
	}

	g_option_context_free (context);

	/* Load the platform specific data */
	realm_settings_init ();

	if (service_install) {
		if (chdir (service_install) < 0) {
			g_message ("Couldn't use install prefix: %s: %s",
			           service_install, g_strerror (errno));
			return 1;
		}
		if (chroot (service_install) < 0) {
			g_message ("Couldn't chroot into install prefix: %s: %s",
			           service_install, g_strerror (errno));
			return 1;
		}
	}

	if (g_getenv ("REALM_DEBUG"))
		service_debug = TRUE;
	if (g_getenv ("REALM_PERSIST") || service_debug || service_install)
		service_persist = TRUE;

	if (service_debug) {
		g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, on_realm_log_debug, NULL);
		g_log_set_always_fatal (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);
	}

	realm_error = realm_error_quark ();
	service_clients = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                         g_free, realm_client_unwatch_and_free);
	realm_daemon_hold ("main");

	service = realm_dbus_service_skeleton_new ();
	g_signal_connect (service, "handle-release", G_CALLBACK (on_service_release), NULL);
	g_signal_connect (service, "handle-set-locale", G_CALLBACK (on_service_set_locale), NULL);
	g_signal_connect (service, "handle-cancel", G_CALLBACK (on_service_cancel), NULL);

	g_debug ("starting service");
	connect_to_bus_or_peer ();

	main_loop = g_main_loop_new (NULL, FALSE);

	g_unix_signal_add (SIGINT, on_signal_quit, main_loop);
	g_unix_signal_add (SIGTERM, on_signal_quit, main_loop);

	g_main_loop_run (main_loop);

	if (service_bus_name_owner_id != 0)
		g_bus_unown_name (service_bus_name_owner_id);
	if (object_server != NULL) {
		g_dbus_object_manager_server_set_connection (object_server, NULL);
		g_object_unref (object_server);
	}

	G_LOCK (polkit_authority);
	g_clear_object (&polkit_authority);
	G_UNLOCK (polkit_authority);

	g_debug ("stopping service");
	realm_settings_uninit ();
	g_main_loop_unref (main_loop);

	g_object_unref (service);
	g_hash_table_unref (service_clients);
	g_free (service_install);
	return 0;
}
