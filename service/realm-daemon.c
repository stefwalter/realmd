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
#define DEBUG_FLAG REALM_DEBUG_SERVICE
#include "realm-debug.h"
#include "realm-diagnostics.h"
#include "realm-errors.h"
#include "realm-samba-provider.h"
#include "realm-settings.h"
#include "realm-sssd-ad-provider.h"
#include "realm-sssd-ipa-provider.h"

#include <glib.h>

#include <polkit/polkit.h>

#define TIMEOUT        60 /* seconds */
#define HOLD_INTERNAL  (GUINT_TO_POINTER (~0))

static GObject *current_invocation = NULL;
static GMainLoop *main_loop = NULL;

static gboolean service_persist = FALSE;
static GHashTable *service_holds = NULL;
static gint64 service_quit_at = 0;
static guint service_timeout_id = 0;

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
realm_daemon_check_dbus_action (const gchar *sender,
                                 const gchar *action_id)
{
	PolkitAuthorizationResult *result;
	PolkitAuthority *authority;
	PolkitSubject *subject;
	GError *error = NULL;
	gboolean ret;

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
		g_warning ("couldn't check polkit authorization: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	ret = polkit_authorization_result_get_is_authorized (result);
	g_object_unref (result);

	return ret;
}

void
realm_daemon_hold (const gchar *hold)
{
	g_assert (hold != NULL);

	if (g_hash_table_lookup (service_holds, hold))
		g_critical ("realm_daemon_hold: already have hold: %s", hold);
	g_hash_table_insert (service_holds, g_strdup (hold), HOLD_INTERNAL);
}

void
realm_daemon_release (const gchar *hold)
{
	g_assert (hold != NULL);

	if (!g_hash_table_remove (service_holds, hold))
		g_critical ("realm_daemon_release: don't have hold: %s", hold);
}

static gboolean
on_service_timeout (gpointer data)
{
	gint seconds;
	gint64 now;

	service_timeout_id = 0;

	if (g_hash_table_size (service_holds) > 0)
		return FALSE;

	now = g_get_monotonic_time ();
	if (now >= service_quit_at) {
		realm_debug ("quitting realmd service after timeout");
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
	if (g_hash_table_size (service_holds) > 0)
		return;
	service_quit_at = g_get_monotonic_time () + (TIMEOUT * G_TIME_SPAN_SECOND);
	if (service_timeout_id == 0)
		on_service_timeout (NULL);
}

static void
on_client_vanished (GDBusConnection *connection,
                    const gchar *name,
                    gpointer user_data)
{
	g_hash_table_remove (service_holds, name);
}

static void
unwatch_if_watched (gpointer data)
{
	if (data != HOLD_INTERNAL)
		g_bus_unwatch_name (GPOINTER_TO_UINT (data));
	realm_daemon_poke ();
}

static gboolean
on_idle_hold_for_message (gpointer user_data)
{
	GDBusMessage *message = user_data;
	const gchar *sender = g_dbus_message_get_sender (message);
	guint watch;

	if (!g_hash_table_lookup (service_holds, sender)) {
		watch = g_bus_watch_name (G_BUS_TYPE_SYSTEM, sender,
		                          G_BUS_NAME_WATCHER_FLAGS_NONE,
		                          NULL, on_client_vanished, NULL, NULL);
		g_hash_table_insert (service_holds, g_strdup (sender),
		                     GUINT_TO_POINTER (watch));
	}

	return FALSE; /* don't call again */
}

static gboolean
on_idle_release_for_message (gpointer user_data)
{
	GDBusMessage *message = user_data;
	const gchar *sender = g_dbus_message_get_sender (message);

	g_hash_table_remove (service_holds, sender);
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
	GDBusMessage *reply;
	GError *error = NULL;

	/* Each time we see an incoming function call, keep the service alive */
	if (incoming) {
		type = g_dbus_message_get_message_type (message);
		if (type == G_DBUS_MESSAGE_TYPE_METHOD_CALL) {

			/* Is a client calling to release from the daemon? */
			if (g_strcmp0 (g_dbus_message_get_path (message), "/org/freedesktop/realmd") == 0 &&
			    g_strcmp0 (g_dbus_message_get_member (message), "ReleaseDaemon") == 0 &&
			    g_strcmp0 (g_dbus_message_get_interface (message), "org.freedesktop.realmd.Daemon") == 0) {
				g_idle_add_full (G_PRIORITY_DEFAULT,
				                 on_idle_release_for_message,
				                 g_object_ref (message),
				                 g_object_unref);

				/* Reply to this message */
				reply = g_dbus_message_new_method_reply (message);
				g_dbus_message_set_body (reply, g_variant_new ("()"));
				g_dbus_connection_send_message (connection, reply,
				                                G_DBUS_SEND_MESSAGE_FLAGS_NONE,
				                                NULL, &error);
				if (error != NULL) {
					g_critical ("Couldn't send ReleaseDaemon reply message: %s", error->message);
					g_error_free (error);
				}
				g_object_unref (message);
				return NULL;

			/* Another method call, watch the client and don't go away until they do, or release */
			} else if (g_strcmp0 (own_name, g_dbus_message_get_sender (message)) != 0) {
				g_idle_add_full (G_PRIORITY_DEFAULT,
				                 on_idle_hold_for_message,
				                 g_object_ref (message),
				                 g_object_unref);
			}
		}
	}

	return message;
}

static void
on_bus_get_connection (GObject *source,
                       GAsyncResult *result,
                       gpointer user_data)
{
	GError *error = NULL;
	GDBusConnection **connection = (GDBusConnection **)user_data;
	const gchar *self_name;

	*connection = g_bus_get_finish (result, &error);
	if (error != NULL) {
		g_warning ("couldn't connect to bus: %s", error->message);
		g_main_loop_quit (main_loop);
		g_error_free (error);
	} else {
		realm_debug ("connected to bus");

		/* Add a filter which keeps service alive */
		self_name = g_dbus_connection_get_unique_name (*connection);
		g_dbus_connection_add_filter (*connection, on_connection_filter,
		                              (gchar *)self_name, NULL);

		realm_diagnostics_initialize (*connection);
		realm_provider_start (*connection, REALM_TYPE_SSSD_AD_PROVIDER);
		realm_provider_start (*connection, REALM_TYPE_SSSD_IPA_PROVIDER);
		realm_provider_start (*connection, REALM_TYPE_SAMBA_PROVIDER);
		realm_provider_start (*connection, REALM_TYPE_ALL_PROVIDER);
	}

	/* Matches the hold() in main() */
	realm_daemon_release ("main");
}

static GOptionEntry option_entries[] = {
	{ NULL }
};

int
main (int argc,
      char *argv[])
{
	GDBusConnection *connection = NULL;
	GOptionContext *context;
	GError *error = NULL;
	g_type_init ();

	context = g_option_context_new ("realmd");
	g_option_context_add_main_entries (context, option_entries, NULL);
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("%s", error->message);
		g_option_context_free (context);
		g_error_free (error);
		return 2;
	}

	if (g_getenv ("REALM_PERSIST"))
		service_persist = 1;

	realm_debug_init ();
	realm_error = realm_error_quark ();
	service_holds = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                       g_free, unwatch_if_watched);
	realm_daemon_hold ("main");

	/* Load the platform specific data */
	realm_settings_init ();

	realm_debug ("starting service");
	g_bus_get (G_BUS_TYPE_SYSTEM, NULL, on_bus_get_connection, &connection);

	main_loop = g_main_loop_new (NULL, FALSE);

	g_main_loop_run (main_loop);

	if (connection != NULL) {
		realm_provider_stop_all ();
		g_object_unref (connection);
	}

	G_LOCK (polkit_authority);
	g_clear_object (&polkit_authority);
	G_UNLOCK (polkit_authority);

	realm_debug ("stopping service");
	realm_settings_uninit ();
	g_main_loop_unref (main_loop);
	g_option_context_free (context);

	g_hash_table_unref (service_holds);
	return 0;
}
