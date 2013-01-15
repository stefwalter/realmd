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

#include "realm-ipa-discover.h"
#include "realm-command.h"
#include "realm-dbus-constants.h"
#include "realm-diagnostics.h"
#include "realm-discovery.h"
#include "realm-errors.h"
#include "realm-invocation.h"
#include "realm-network.h"

#include <glib/gi18n.h>

typedef struct {
	GObject parent;
	GDBusMethodInvocation *invocation;
	gboolean completed;
	gboolean found_ipa;
	GError *error;
	GAsyncReadyCallback callback;
	gpointer user_data;

	GSrvTarget *kdc;
	GBytes *http_request;
	GIOStream *current_connection;
	GTlsCertificate *peer_certificate;
	GSocketConnectable *peer_identity;
} RealmIpaDiscover;

typedef struct {
	GObjectClass parent;
} RealmIpaDiscoverClass;

#define REALM_TYPE_IPA_DISCOVER  (realm_ipa_discover_get_type ())
#define REALM_IPA_DISCOVER(inst)  (G_TYPE_CHECK_INSTANCE_CAST ((inst), REALM_TYPE_IPA_DISCOVER, RealmIpaDiscover))
#define REALM_IS_IPA_DISCOVER(inst)  (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REALM_TYPE_IPA_DISCOVER))

GType realm_ipa_discover_get_type (void) G_GNUC_CONST;

void  realm_ipa_discover_async_result_init (GAsyncResultIface *iface);

G_DEFINE_TYPE_WITH_CODE (RealmIpaDiscover, realm_ipa_discover, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_RESULT, realm_ipa_discover_async_result_init);
);

static void
realm_ipa_discover_init (RealmIpaDiscover *self)
{

}

static void
realm_ipa_discover_finalize (GObject *obj)
{
	RealmIpaDiscover *self = REALM_IPA_DISCOVER (obj);

	g_object_unref (self->invocation);
	g_clear_error (&self->error);
	g_srv_target_free (self->kdc);

	if (self->http_request)
		g_bytes_unref (self->http_request);

	if (self->current_connection)
		g_object_unref (self->current_connection);

	if (self->peer_certificate)
		g_object_unref (self->peer_certificate);

	if (self->peer_identity)
		g_object_unref (self->peer_identity);

	g_assert (self->callback == NULL);

	G_OBJECT_CLASS (realm_ipa_discover_parent_class)->finalize (obj);
}

static void
realm_ipa_discover_class_init (RealmIpaDiscoverClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = realm_ipa_discover_finalize;
}

static GObject *
realm_ipa_discover_get_source_object (GAsyncResult *result)
{
	return g_object_ref (result);
}

static gpointer
realm_ipa_discover_get_user_data (GAsyncResult *result)
{
	/* What does this do? */
	g_return_val_if_reached (NULL);
}

void
realm_ipa_discover_async_result_init (GAsyncResultIface *iface)
{
	iface->get_source_object = realm_ipa_discover_get_source_object;
	iface->get_user_data = realm_ipa_discover_get_user_data;
}

/* TODO: This stuff will shortly be part of glib */

typedef struct {
  GCancellable *cancellable;
  guchar *buf;
  gsize count;
  gsize nread;
} ReadAllClosure;

static void
read_all_closure_free (gpointer data)
{
  ReadAllClosure *closure = data;
  if (closure->cancellable)
    g_object_unref (closure->cancellable);
  g_free (closure->buf);
  g_slice_free (ReadAllClosure, closure);
}

static void
read_all_callback (GObject      *stream,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  GSimpleAsyncResult *simple = user_data;
  ReadAllClosure *closure = g_simple_async_result_get_op_res_gpointer (simple);

  GError *error = NULL;
  gssize nread;

  nread = g_input_stream_read_finish (G_INPUT_STREAM (stream),
                                      result, &error);
  if (nread == -1)
    {
      g_simple_async_result_take_error (simple, error);
      g_simple_async_result_complete (simple);
    }
  else if (nread == 0)
    {
      g_simple_async_result_complete (simple);
    }
  else
    {
      closure->nread += nread;
      if (closure->count > closure->nread)
        {
          g_input_stream_read_async (G_INPUT_STREAM (stream),
                                     closure->buf + closure->nread,
                                     closure->count - closure->nread,
                                     G_PRIORITY_DEFAULT, closure->cancellable,
                                     read_all_callback, g_object_ref (simple));
        }
      else
        {
          g_simple_async_result_complete (simple);
        }
    }

  g_object_unref (simple);
}

static void
read_all_bytes_async (GInputStream *stream,
                      gsize count,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
  GSimpleAsyncResult *simple;
  ReadAllClosure *closure;

  simple = g_simple_async_result_new (G_OBJECT (stream),
                                      callback, user_data,
                                      read_all_bytes_async);
  closure = g_slice_new0 (ReadAllClosure);
  closure->buf = g_malloc (count);
  closure->count = count;
  closure->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
  g_simple_async_result_set_op_res_gpointer (simple, closure, read_all_closure_free);

  g_input_stream_read_async (stream, closure->buf, count,
                             G_PRIORITY_DEFAULT, cancellable,
                             read_all_callback, simple);
}

static GBytes *
read_all_bytes_finish (GInputStream *stream,
                       GAsyncResult *result,
                       GError **error)
{
  GSimpleAsyncResult *simple;
  ReadAllClosure *closure;
  GBytes *bytes;

  simple = G_SIMPLE_ASYNC_RESULT (result);
  closure = g_simple_async_result_get_op_res_gpointer (simple);

  if (g_simple_async_result_propagate_error (simple, error))
        return NULL;

  bytes = g_bytes_new_take (closure->buf, closure->nread);
  closure->buf = NULL;
  closure->nread = 0;

  return bytes;
}

typedef struct {
  GCancellable *cancellable;
  GBytes *bytes;
  gsize written;
} WriteAllClosure;

static void
write_all_closure_free (gpointer data)
{
  WriteAllClosure *closure = data;
  if (closure->cancellable)
    g_object_unref (closure->cancellable);
  g_bytes_unref (closure->bytes);
  g_slice_free (WriteAllClosure, closure);
}

static void
write_all_callback (GObject      *stream,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  GSimpleAsyncResult *simple = user_data;
  WriteAllClosure *closure = g_simple_async_result_get_op_res_gpointer (simple);
  GError *error = NULL;
  const guchar *data;
  gsize size;
  gssize nwrote;

  nwrote = g_output_stream_write_finish (G_OUTPUT_STREAM (stream),
                                         result, &error);
  if (nwrote < 0)
    {
      g_simple_async_result_take_error (simple, error);
    }
  else
    {
      closure->written += nwrote;
      data = g_bytes_get_data (closure->bytes, &size);
      if (closure->written < size)
        {
          g_output_stream_write_async (G_OUTPUT_STREAM (stream),
                                       data + closure->written,
                                       size - closure->written,
                                       G_PRIORITY_DEFAULT,
                                       closure->cancellable,
                                       write_all_callback,
                                       g_object_ref (simple));
        }
      else
        {
          g_simple_async_result_complete (simple);
        }
    }

  g_object_unref (simple);
}

static void
write_all_bytes_async (GOutputStream *stream,
                       GBytes *bytes,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
  GSimpleAsyncResult *simple;
  WriteAllClosure *closure;
  gsize size;
  gconstpointer data;

  data = g_bytes_get_data (bytes, &size);
  simple = g_simple_async_result_new (G_OBJECT (stream),
                                      callback, user_data,
                                      write_all_bytes_async);
  closure = g_slice_new0 (WriteAllClosure);
  closure->bytes = g_bytes_ref (bytes);
  closure->cancellable = cancellable ? g_object_ref (cancellable) : cancellable;
  g_simple_async_result_set_op_res_gpointer (simple, closure, write_all_closure_free);

  g_output_stream_write_async (stream,
                               data, size,
                               G_PRIORITY_DEFAULT,
                               cancellable,
                               write_all_callback,
                               simple);
}

static gboolean
write_all_bytes_finish (GOutputStream *stream,
                        GAsyncResult *result,
                        GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
    return FALSE;
  return TRUE;
}

static void
ipa_discover_complete (RealmIpaDiscover *self)
{
	GAsyncReadyCallback call;
	gpointer user_data;

	g_assert (!self->completed);
	self->completed = TRUE;
	call = self->callback;
	user_data = self->user_data;
	self->callback = NULL;
	self->user_data = NULL;

	if (call != NULL)
		(call) (NULL, G_ASYNC_RESULT (self), user_data);

	/* Matches g_object_new in realm_ipa_discover_async */
	g_object_unref (self);
}

static void
ipa_discover_take_error (RealmIpaDiscover *self,
                         const gchar *message,
                         GError *error)
{
	g_assert (!self->completed);

	realm_diagnostics_error (self->invocation, error, "%s", message);

	/* We get the first as our operation error */
	if (self->error)
		g_error_free (error);
	else
		self->error = error;
}

static GBytes *
strip_http_header (GBytes *bytes)
{
	gchar *data;
	const gchar *contents;
	gsize length;
	const gchar *br;

	/* Turn this into a string */
	data = g_bytes_unref_to_data (bytes, &length);
	data = g_realloc (data, length + 1);
	data[length] = 0;

	br = strstr (data, "\r\n\r\n");
	if (br == NULL) {
		br = strstr (data, "\n\n");
		if (br == NULL) {
			contents = NULL;
		} else {
			contents = br + 2;
		}
	} else {
		contents = br + 4;
	}

	if (contents == NULL) {
		return g_bytes_new_static ("", 0);

	} else {
		return g_bytes_new_with_free_func (contents,
		                                   length - (contents - data),
		                                   g_free, data);
	}
}

static void
on_read_http_response (GObject *source,
                       GAsyncResult *result,
                       gpointer user_data)
{
	RealmIpaDiscover *self = REALM_IPA_DISCOVER (user_data);
	GTlsCertificate *certificate;
	GError *error = NULL;
	const gchar *data;
	gsize length;
	GBytes *bytes;

	bytes = read_all_bytes_finish (G_INPUT_STREAM (source), result, &error);

	if (!self->peer_certificate || !self->peer_identity) {
		g_debug ("No peer certificate or peer identity received.");

	} else if (error == NULL) {
		bytes = strip_http_header (bytes);
		data = g_bytes_get_data (bytes, &length);
		certificate = g_tls_certificate_new_from_pem (data, length, &error);
		g_bytes_unref (bytes);

		if (certificate) {

			/*
			 * We're not verifying this certificate for security purposes
			 * but to check that this is a real IPA server. The CA certificate
			 * should be the anchor for the peer certificate.
			 */
			if (g_tls_certificate_verify (self->peer_certificate,
			                              self->peer_identity,
			                              certificate) == 0) {
				realm_diagnostics_info (self->invocation, "Retrieved IPA CA certificate verifies the HTTPS connection");
				self->found_ipa = TRUE;
				ipa_discover_complete (self);

			} else {
				realm_diagnostics_info (self->invocation, "Retrieved IPA CA certificate does not verify the HTTPS connection");
			}

			g_object_unref (certificate);
		}
	}

	if (error != NULL)
		ipa_discover_take_error (self, "Couldn't read certificate via HTTP", error);
	if (!self->completed)
		ipa_discover_complete (self);

	g_object_unref (self);
}


static void
on_write_http_request (GObject *source,
                       GAsyncResult *result,
                       gpointer user_data)
{
	RealmIpaDiscover *self = REALM_IPA_DISCOVER (user_data);
	GCancellable *cancellable;
	GError *error = NULL;
	GInputStream *input;

	/* TODO: Update to new bytes interface */

	write_all_bytes_finish (G_OUTPUT_STREAM (source), result, &error);
	if (error == NULL) {
		input = g_io_stream_get_input_stream (G_IO_STREAM (self->current_connection));
		cancellable = realm_invocation_get_cancellable (self->invocation);
		read_all_bytes_async (input, 100 * 1024, cancellable,
		                      on_read_http_response, g_object_ref (self));
	} else {
		ipa_discover_take_error (self, "Couldn't send HTTP request for certificate", error);
		ipa_discover_complete (self);
	}

	g_object_unref (self);
}

static void
on_connect_to_host (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	RealmIpaDiscover *self = REALM_IPA_DISCOVER (user_data);
	gchar *request;
	GCancellable *cancellable;
	GSocketConnection *connection;
	GOutputStream *output;
	GError *error = NULL;

	g_assert (self->current_connection == NULL);
	g_assert (self->http_request == NULL);

	request = g_strdup_printf ("GET /ipa/config/ca.crt HTTP/1.0\r\n"
	                           "Host: %s\r\n"
	                           "\r\n", g_srv_target_get_hostname (self->kdc));
	self->http_request = g_bytes_new_take (request, strlen (request));

	connection = g_socket_client_connect_to_host_finish (G_SOCKET_CLIENT (source), result, &error);
	if (error == NULL) {
		self->current_connection = G_IO_STREAM (connection);
		output = g_io_stream_get_output_stream (self->current_connection);
		cancellable = realm_invocation_get_cancellable (self->invocation);
		write_all_bytes_async (output, self->http_request, cancellable,
		                       on_write_http_request, g_object_ref (self));

	/* Errors that mean no domain discovered */
	} else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED) ||
	           g_error_matches (error, G_IO_ERROR, G_IO_ERROR_HOST_UNREACHABLE) ||
	           g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NETWORK_UNREACHABLE) ||
	           g_error_matches (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
		g_debug ("Couldn't connect to check for IPA domain: %s", error->message);
		ipa_discover_complete (self);

	} else {
		ipa_discover_take_error (self, "Couldn't connect to check for IPA domain", error);
		ipa_discover_complete (self);
	}

	g_object_unref (self);
}

static void
on_connection_event (GSocketClient      *client,
                     GSocketClientEvent  event,
                     GSocketConnectable *connectable,
                     GIOStream          *connection,
                     gpointer            user_data)
{
	RealmIpaDiscover *self = REALM_IPA_DISCOVER (user_data);

	if (event == G_SOCKET_CLIENT_TLS_HANDSHAKED) {
		g_return_if_fail (self->peer_certificate == NULL);
		self->peer_certificate = g_tls_connection_get_peer_certificate (G_TLS_CONNECTION (connection));
		if (self->peer_certificate)
			g_object_ref (self->peer_certificate);

	} else if (event == G_SOCKET_CLIENT_RESOLVED) {
		if (!self->peer_identity)
			self->peer_identity = g_object_ref (connectable);

	/* Once connected, raise the timeout, so TLS can succeed */
	} else if (event == G_SOCKET_CLIENT_CONNECTED) {
		g_socket_client_set_timeout (client, 30);
	}
}

void
realm_ipa_discover_async (GSrvTarget *kdc,
                          GDBusMethodInvocation *invocation,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	RealmIpaDiscover *self;
	GCancellable *cancellable;
	GSocketClient *client;
	const gchar *hostname;

	g_return_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation));

	cancellable = realm_invocation_get_cancellable (invocation);
	self = g_object_new (REALM_TYPE_IPA_DISCOVER, NULL);
	self->invocation = g_object_ref (invocation);
	self->callback = callback;
	self->user_data = user_data;
	self->kdc = g_srv_target_copy (kdc);

	hostname = g_srv_target_get_hostname (self->kdc);
	client = g_socket_client_new ();

	/* Initial socket connections are limited to a low timeout*/
	g_socket_client_set_timeout (client, 5);

	/*
	 * Note that we accept invalid certificates, we're just comparing them
	 * with what's on the server at this point. Later during the join the
	 * certificate is used correctly.
	 */

	g_socket_client_set_tls (client, TRUE);
	g_socket_client_set_tls_validation_flags (client, 0);

	g_signal_connect_data (client, "event", G_CALLBACK (on_connection_event),
	                       g_object_ref (self), (GClosureNotify)g_object_unref,
	                       G_CONNECT_AFTER);

	realm_diagnostics_info (self->invocation, "Trying to retrieve IPA certificate from %s", hostname);

	g_socket_client_connect_to_host_async (client, hostname, 443, cancellable,
	                                       on_connect_to_host, g_object_ref (self));

	g_object_unref (client);

	/* self is released in ipa_discover_complete */
}

gboolean
realm_ipa_discover_finish (GAsyncResult *result,
                           GError **error)
{
	RealmIpaDiscover *self;

	g_return_val_if_fail (REALM_IS_IPA_DISCOVER (result), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	self = REALM_IPA_DISCOVER (result);

	/* A failure */
	if (self->error) {
		if (error)
			*error = g_error_copy (self->error);
		return FALSE;
	}

	return self->found_ipa;
}
