/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* matrix-utils.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-matrix-utils"
#define BUFFER_SIZE 256

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define __STDC_WANT_LIB_EXT1__ 1
#include <string.h>
#include <glib/gi18n.h>

#include "matrix-enums.h"
#include "chatty-log.h"
#include "chatty-utils.h"
#include "matrix-utils.h"

static const char *error_codes[] = {
  "", /* Index 0 is reserved for no error */
  "M_FORBIDDEN",
  "M_UNKNOWN_TOKEN",
  "M_MISSING_TOKEN",
  "M_BAD_JSON",
  "M_NOT_JSON",
  "M_NOT_FOUND",
  "M_LIMIT_EXCEEDED",
  "M_UNKNOWN",
  "M_UNRECOGNIZED",
  "M_UNAUTHORIZED",
  "M_USER_DEACTIVATED",
  "M_USER_IN_USE",
  "M_INVALID_USERNAME",
  "M_ROOM_IN_USE",
  "M_INVALID_ROOM_STATE",
  "M_THREEPID_IN_USE",
  "M_THREEPID_NOT_FOUND",
  "M_THREEPID_AUTH_FAILED",
  "M_THREEPID_DENIED",
  "M_SERVER_NOT_TRUSTED",
  "M_UNSUPPORTED_ROOM_VERSION",
  "M_INCOMPATIBLE_ROOM_VERSION",
  "M_BAD_STATE",
  "M_GUEST_ACCESS_FORBIDDEN",
  "M_CAPTCHA_NEEDED",
  "M_CAPTCHA_INVALID",
  "M_MISSING_PARAM",
  "M_INVALID_PARAM",
  "M_TOO_LARGE",
  "M_EXCLUSIVE",
  "M_RESOURCE_LIMIT_EXCEEDED",
  "M_CANNOT_LEAVE_SERVER_NOTICE_ROOM",
};

/**
 * matrix_error_quark:
 *
 * Get the Matrix Error Quark.
 *
 * Returns: a #GQuark.
 **/
G_DEFINE_QUARK (matrix-error-quark, matrix_error)

GError *
matrix_utils_json_node_get_error (JsonNode *node)
{
  JsonObject *object = NULL;
  const char *error, *err_code;

  if (!node || (!JSON_NODE_HOLDS_OBJECT (node) && !JSON_NODE_HOLDS_ARRAY (node)))
    return g_error_new (MATRIX_ERROR, M_NOT_JSON,
                        "Not JSON Object");

  /* Returned by /_matrix/client/r0/rooms/{roomId}/state */
  if (JSON_NODE_HOLDS_ARRAY (node))
    return NULL;

  object = json_node_get_object (node);
  err_code = matrix_utils_json_object_get_string (object, "errcode");

  if (!err_code)
    return NULL;

  error = matrix_utils_json_object_get_string (object, "error");

  if (!error)
    error = "Unknown Error";

  if (!g_str_has_prefix (err_code, "M_"))
    return g_error_new (MATRIX_ERROR, M_UNKNOWN,
                        "Invalid Error code");

  for (guint i = 0; i < G_N_ELEMENTS (error_codes); i++)
    if (g_str_equal (error_codes[i], err_code))
      return g_error_new (MATRIX_ERROR, i, "%s", error);

  return g_error_new (MATRIX_ERROR, M_UNKNOWN,
                      "Unknown Error");
}

void
matrix_utils_clear (char   *buffer,
                    size_t  length)
{
  if (!buffer || length == 0)
    return;

  /* Brushing up your C: Note: we are not comparing with -1 */
  if (length == -1)
    length = strlen (buffer);

#ifdef __STDC_LIB_EXT1__
  memset_s (buffer, length, 0, length);
#elif HAVE_EXPLICIT_BZERO
  explicit_bzero (buffer, length);
#else
  volatile char *end = buffer + length;

  while (buffer != end)
    *(buffer++) = 0;
#endif
}

void
matrix_utils_free_buffer (char *buffer)
{
  matrix_utils_clear (buffer, -1);
  g_free (buffer);
}

gboolean
matrix_utils_username_is_complete (const char *username)
{
  if (!username || *username != '@')
    return FALSE;

  if (strchr (username, ':'))
    return TRUE;

  return FALSE;
}

const char *
matrix_utils_get_url_from_username (const char *username)
{
  if (!chatty_utils_username_is_valid (username, CHATTY_PROTOCOL_MATRIX))
    return NULL;

  /* Return the string after ‘:’ */
  return strchr (username, ':') + 1;
}

char *
matrix_utils_json_object_to_string (JsonObject *object,
                                    gboolean    prettify)
{
  g_autoptr(JsonNode) node = NULL;

  g_return_val_if_fail (object, NULL);

  node = json_node_new (JSON_NODE_OBJECT);
  json_node_init_object (node, object);

  return json_to_string (node, !!prettify);
}

static void utils_json_canonical_array (JsonArray *array,
                                        GString   *out);
static void
utils_handle_node (JsonNode *node,
                   GString  *out)
{
  GType type;

  g_assert (node);
  g_assert (out);

  type = json_node_get_value_type (node);

  if (type == JSON_TYPE_OBJECT)
    matrix_utils_json_get_canonical (json_node_get_object (node), out);
  else if (type == JSON_TYPE_ARRAY)
    utils_json_canonical_array (json_node_get_array (node), out);
  else if (type == G_TYPE_INVALID)
    g_string_append (out, "null");
  else if (type == G_TYPE_STRING)
    g_string_append_printf (out, "\"%s\"", json_node_get_string (node));
  else if (type == G_TYPE_INT64)
    g_string_append_printf (out, "%" G_GINT64_FORMAT, json_node_get_int (node));
  else if (type == G_TYPE_DOUBLE)
    g_string_append_printf (out, "%f", json_node_get_double (node));
  else if (type == G_TYPE_BOOLEAN)
    g_string_append (out, json_node_get_boolean (node) ? "true" : "false");
  else
    g_return_if_reached ();
}

static void
utils_json_canonical_array (JsonArray *array,
                            GString   *out)
{
  g_autoptr(GList) elements = NULL;

  g_assert (array);
  g_assert (out);

  g_string_append_c (out, '[');
  elements = json_array_get_elements (array);

  /* The order of array members shouldn’t be changed */
  for (GList *item = elements; item; item = item->next) {
    utils_handle_node (item->data, out);

    if (item->next)
      g_string_append_c (out, ',');
  }

  g_string_append_c (out, ']');
}

GString *
matrix_utils_json_get_canonical (JsonObject *object,
                                 GString    *out)
{
  g_autoptr(GList) members = NULL;

  g_return_val_if_fail (object, NULL);

  if (!out)
    out = g_string_sized_new (BUFFER_SIZE);

  g_string_append_c (out, '{');

  members = json_object_get_members (object);
  members = g_list_sort (members, (GCompareFunc)g_strcmp0);

  for (GList *item = members; item; item = item->next) {
    JsonNode *node;

    g_string_append_printf (out, "\"%s\":", (char *)item->data);

    node = json_object_get_member (object, item->data);
    utils_handle_node (node, out);

    if (item->next)
      g_string_append_c (out, ',');
  }

  g_string_append_c (out, '}');

  return out;
}

JsonObject *
matrix_utils_string_to_json_object (const char *json_str)
{
  g_autoptr(JsonParser) parser = NULL;
  JsonNode *node;

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, json_str, -1, NULL))
    return NULL;

  node = json_parser_get_root (parser);

  if (!JSON_NODE_HOLDS_OBJECT (node))
    return NULL;

  return json_node_dup_object (node);
}

gint64
matrix_utils_json_object_get_int (JsonObject *object,
                                  const char *member)
{
  JsonNode *node;

  if (!object || !member || !*member)
    return 0;

  node = json_object_get_member (object, member);

  if (node && JSON_NODE_HOLDS_VALUE (node))
    return json_node_get_int (node);

  return 0;
}

gboolean
matrix_utils_json_object_get_bool (JsonObject *object,
                                   const char *member)
{
  JsonNode *node;

  if (!object || !member || !*member)
    return FALSE;

  node = json_object_get_member (object, member);

  if (node && JSON_NODE_HOLDS_VALUE (node))
    return json_node_get_boolean (node);

  return FALSE;
}

const char *
matrix_utils_json_object_get_string (JsonObject *object,
                                     const char *member)
{
  JsonNode *node;

  if (!object || !member || !*member)
    return NULL;

  node = json_object_get_member (object, member);

  if (node && JSON_NODE_HOLDS_VALUE (node))
    return json_node_get_string (node);

  return NULL;
}

JsonObject *
matrix_utils_json_object_get_object (JsonObject *object,
                                     const char *member)
{
  JsonNode *node;

  if (!object || !member || !*member)
    return NULL;

  node = json_object_get_member (object, member);

  if (node && JSON_NODE_HOLDS_OBJECT (node))
    return json_node_get_object (node);

  return NULL;
}

JsonArray *
matrix_utils_json_object_get_array (JsonObject *object,
                                    const char *member)
{
  JsonNode *node;

  if (!object || !member || !*member)
    return NULL;

  node = json_object_get_member (object, member);

  if (node && JSON_NODE_HOLDS_ARRAY (node))
    return json_node_get_array (node);

  return NULL;
}

JsonObject *
matrix_utils_get_message_json_object (SoupMessage *message,
                                      const char  *member)
{
  g_autoptr(JsonParser) parser = NULL;
  g_autoptr(SoupBuffer) buffer = NULL;
  JsonObject *object = NULL;
  gboolean is_json;

  if (!message || !message->response_body)
    return NULL;

  buffer = soup_message_body_flatten (message->response_body);
  parser = json_parser_new ();
  is_json = json_parser_load_from_data (parser, buffer->data, buffer->length, NULL);

  if (is_json) {
    JsonNode *root;

    root = json_parser_get_root (parser);

    if (root && JSON_NODE_HOLDS_OBJECT (root))
      object = json_node_get_object (root);

    if (member && object)
      object = json_object_get_object_member (object, member);
  }

  return object ? json_object_ref (object) : NULL;
}

static gboolean
cancel_read_uri (gpointer user_data)
{
  g_autoptr(GTask) task = user_data;

  g_assert (G_IS_TASK (task));

  g_object_set_data (G_OBJECT (task), "timeout-id", 0);

  /* XXX: Not thread safe? */
  if (g_task_get_completed (task) || g_task_had_error (task))
    return G_SOURCE_REMOVE;

  g_task_set_task_data (task, GINT_TO_POINTER (TRUE), NULL);
  g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                           "Request timeout");
  g_cancellable_cancel (g_task_get_cancellable (task));

  return G_SOURCE_REMOVE;
}

static void
load_from_stream_cb (JsonParser   *parser,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;
  gboolean timeout;

  g_assert (JSON_IS_PARSER (parser));
  g_assert (G_IS_TASK (task));

  timeout = GPOINTER_TO_INT (g_task_get_task_data (task));

  /* Task return is handled somewhere else */
  if (timeout)
    return;

  if (json_parser_load_from_stream_finish (parser, result, &error))
    g_task_return_pointer (task, json_parser_steal_root (parser),
                           (GDestroyNotify)json_node_unref);
  else
    g_task_return_error (task, error);
}

static gboolean
matrix_utils_handle_ssl_error (SoupMessage *message)
{
  GTlsCertificate *cert = NULL;
  GApplication *app;
  GtkWidget *dialog;
  GtkWindow *window = NULL;
  SoupURI *uri;
  g_autofree char *msg = NULL;
  const char *host;
  GTlsCertificateFlags err_flags;
  gboolean cancelled = FALSE;

  if (!SOUP_IS_MESSAGE (message) ||
      !soup_message_get_https_status (message, &cert, &err_flags) ||
      !err_flags)
    return cancelled;

  app = g_application_get_default ();
  if (app)
    window = gtk_application_get_active_window (GTK_APPLICATION (app));

  if (!window)
    return cancelled;

  uri = soup_message_get_uri (message);
  host = soup_uri_get_host (uri);

  switch (err_flags) {
  case G_TLS_CERTIFICATE_UNKNOWN_CA:
    if (g_tls_certificate_get_issuer (cert))
      msg = g_strdup_printf (_("The certificate for ‘%s’ has unknown CA"), host);
    else
      msg = g_strdup_printf (_("The certificate for ‘%s’ is self-signed"), host);
    break;

  case G_TLS_CERTIFICATE_EXPIRED:
    msg = g_strdup_printf (_("The certificate for ‘%s’ has expired"), host);
    break;

  case G_TLS_CERTIFICATE_REVOKED:
    msg = g_strdup_printf (_("The certificate for ‘%s’ has been revoked"), host);
    break;

  case G_TLS_CERTIFICATE_BAD_IDENTITY:
  case G_TLS_CERTIFICATE_NOT_ACTIVATED:
  case G_TLS_CERTIFICATE_INSECURE:
  case G_TLS_CERTIFICATE_GENERIC_ERROR:
  case G_TLS_CERTIFICATE_VALIDATE_ALL:
  default:
    msg = g_strdup_printf (_("Error validating certificate for ‘%s’"), host);
  }

  dialog = gtk_message_dialog_new (window,
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_NONE,
                                   "%s", msg);

  /* XXX: This may not work, see https://gitlab.gnome.org/GNOME/glib-networking/-/issues/32 */
  if (err_flags == G_TLS_CERTIFICATE_REVOKED)
    gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                            _("Close"), GTK_RESPONSE_CLOSE,
                            NULL);
  else
    gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                            _("Reject"), GTK_RESPONSE_REJECT,
                            _("Accept"), GTK_RESPONSE_ACCEPT,
                            NULL);

  if (gtk_dialog_run (GTK_DIALOG (dialog)) != GTK_RESPONSE_ACCEPT)
    cancelled = TRUE;

  gtk_widget_destroy (dialog);

  return cancelled;
}

static void
uri_file_read_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  SoupSession *session = (SoupSession *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GInputStream) stream = NULL;
  g_autoptr(JsonParser) parser = NULL;
  GCancellable *cancellable;
  SoupMessage *message;
  GError *error = NULL;
  gboolean has_timeout;
  GTlsCertificateFlags err_flags;

  g_assert (G_IS_TASK (task));
  g_assert (SOUP_IS_SESSION (session));

  stream = soup_session_send_finish (session, result, &error);
  message = g_object_get_data (G_OBJECT (task), "message");
  has_timeout = GPOINTER_TO_INT (g_task_get_task_data (task));

  /* Task return is handled somewhere else */
  if (has_timeout)
    return;

  if (error) {
    g_task_return_error (task, error);
    return;
  }

  soup_message_get_https_status (message, NULL, &err_flags);

  if (message &&
      soup_message_get_https_status (message, NULL, &err_flags) &&
      err_flags) {
    guint timeout_id, timeout;

    timeout = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (task), "timeout"));
    timeout_id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (task), "timeout-id"));
    g_clear_handle_id (&timeout_id, g_source_remove);
    g_object_unref (task);

    if (matrix_utils_handle_ssl_error (message)) {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                               "Cancelled");
      return;
    }

    timeout_id = g_timeout_add_seconds (timeout, cancel_read_uri, g_object_ref (task));
    g_object_set_data (G_OBJECT (task), "timeout-id", GUINT_TO_POINTER (timeout_id));
  }

  cancellable = g_task_get_cancellable (task);
  parser = json_parser_new ();
  json_parser_load_from_stream_async (parser, stream, cancellable,
                                      (GAsyncReadyCallback)load_from_stream_cb,
                                      g_steal_pointer (&task));
}

static void
message_network_event_cb (SoupMessage        *msg,
                          GSocketClientEvent  event,
                          GIOStream          *connection,
                          gpointer            user_data)
{
  GSocketAddress *address;

  /* We shall have a non %NULL @connection by %G_SOCKET_CLIENT_CONNECTING event */
  if (event != G_SOCKET_CLIENT_CONNECTING)
    return;

  /* @connection is a #GSocketConnection */
  address = g_socket_connection_get_remote_address (G_SOCKET_CONNECTION (connection), NULL);
  g_object_set_data_full (user_data, "address", address, g_object_unref);
}

void
matrix_utils_read_uri_async (const char          *uri,
                             guint                timeout,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  g_autoptr(SoupSession) session = NULL;
  g_autoptr(SoupMessage) message = NULL;
  g_autoptr(GCancellable) cancel = NULL;
  g_autoptr(GTask) task = NULL;
  guint timeout_id;

  g_return_if_fail (uri && *uri);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  if (cancellable)
    cancel = g_object_ref (cancellable);
  else
    cancel = g_cancellable_new ();

  task = g_task_new (NULL, cancel, callback, user_data);
  /* if this changes to TRUE, we consider it has been timeout */
  g_task_set_task_data (task, GINT_TO_POINTER (FALSE), NULL);
  g_task_set_source_tag (task, matrix_utils_read_uri_async);

  timeout = CLAMP (timeout, 5, 60);
  timeout_id = g_timeout_add_seconds (timeout, cancel_read_uri, g_object_ref (task));
  g_object_set_data (G_OBJECT (task), "timeout", GUINT_TO_POINTER (timeout));
  g_object_set_data (G_OBJECT (task), "timeout-id", GUINT_TO_POINTER (timeout_id));

  message = soup_message_new (SOUP_METHOD_GET, uri);
  if (!message) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
                             "%s is not a valid uri", uri);
    return;
  }

  soup_message_set_flags (message, SOUP_MESSAGE_NO_REDIRECT);
  g_object_set_data_full (G_OBJECT (task), "message", g_object_ref (message), g_object_unref);

  /* XXX: Switch to  */
  g_signal_connect_object (message, "network-event",
                           G_CALLBACK (message_network_event_cb), task,
                           G_CONNECT_AFTER);
  session = soup_session_new ();
  g_object_set (G_OBJECT (session), SOUP_SESSION_SSL_STRICT, FALSE, NULL);

  soup_session_send_async (session, message, cancel,
                           uri_file_read_cb,
                           g_steal_pointer (&task));
}

JsonNode *
matrix_utils_read_uri_finish (GAsyncResult  *result,
                              GError       **error)
{
  GTask *task = (GTask *)result;

  g_return_val_if_fail (G_IS_TASK (result), NULL);
  g_return_val_if_fail (g_task_get_source_tag (task) == matrix_utils_read_uri_async, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static void
get_homeserver_cb (GObject      *obj,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(JsonNode) root = NULL;
  JsonObject *object = NULL;
  const char *homeserver = NULL;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  root = matrix_utils_read_uri_finish (result, &error);

  if (!root) {
    g_task_return_error (task, error);
    return;
  }

  g_object_set_data_full (G_OBJECT (task), "address",
                          g_object_steal_data (G_OBJECT (result), "address"),
                          g_object_unref);

  if (JSON_NODE_HOLDS_OBJECT (root))
    object = json_node_get_object (root);

  if (object)
    object = matrix_utils_json_object_get_object (object, "m.homeserver");

  if (object)
    homeserver = matrix_utils_json_object_get_string (object, "base_url");

  if (!homeserver)
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Got invalid response from server");
  else
    g_task_return_pointer (task, g_strdup (homeserver), g_free);
}

/**
 * matrix_utils_get_homeserver_async:
 * @username: A complete matrix username
 * @timeout: timeout in seconds
 * @cancellable: (nullable): A #GCancellable
 * @callback: The callback to run
 * @user_data: (nullable): The data passed to @callback
 *
 * Get homeserver from the given @username.  @userename
 * should be in complete form (eg: @user:example.org)
 *
 * @timeout is clamped between 5 and 60 seconds.
 *
 * This is a network operation and shall connect to the
 * network to fetch homeserver details.
 *
 * See https://matrix.org/docs/spec/client_server/r0.6.1#server-discovery
 */
void
matrix_utils_get_homeserver_async (const char          *username,
                                   guint                timeout,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autofree char *uri = NULL;
  const char *url;

  g_return_if_fail (username);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_return_if_fail (callback);

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, matrix_utils_get_homeserver_async);

  if (!chatty_utils_username_is_valid (username, CHATTY_PROTOCOL_MATRIX)) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
                             "Username '%s' is not a complete matrix id", username);
    return;
  }

  url = matrix_utils_get_url_from_username (username);
  uri = g_strconcat ("https://", url, "/.well-known/matrix/client", NULL);

  matrix_utils_read_uri_async (uri, timeout, cancellable,
                               get_homeserver_cb, g_steal_pointer (&task));
}

/**
 * matrix_utils_get_homeserver_finish:
 * @result: A #GAsyncResult
 * @error: (optional): A #GError
 *
 * Finish call to matrix_utils_get_homeserver_async().
 *
 * Returns: (nullable) : The homeserver string or %NULL
 * on error.  Free with g_free().
 */
char *
matrix_utils_get_homeserver_finish (GAsyncResult  *result,
                                    GError       **error)
{
  GTask *task = (GTask *)result;

  g_return_val_if_fail (G_IS_TASK (result), NULL);
  g_return_val_if_fail (g_task_get_source_tag (task) == matrix_utils_get_homeserver_async, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static void
api_get_version_cb (GObject      *obj,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(JsonNode) root = NULL;
  JsonObject *object = NULL;
  JsonArray *array = NULL;
  GError *error = NULL;
  const char *server;
  gboolean valid;

  g_assert (G_IS_TASK (task));

  server = g_task_get_task_data (task);
  root = matrix_utils_read_uri_finish (result, &error);

  if (!error && root)
    error = matrix_utils_json_node_get_error (root);

  if (!root ||
      g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
      g_error_matches (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
    if (error)
      g_task_return_error (task, error);
    else
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Failed to get version for server '%s'", server);
    return;
  }

  g_object_set_data_full (G_OBJECT (task), "address",
                          g_object_steal_data (G_OBJECT (result), "address"),
                          g_object_unref);

  object = json_node_get_object (root);
  array = matrix_utils_json_object_get_array (object, "versions");
  valid = FALSE;

  if (array) {
    g_autoptr(GString) versions = NULL;
    guint length;

    versions = g_string_new ("");
    length = json_array_get_length (array);

    for (guint i = 0; i < length; i++) {
      const char *version;

      version = json_array_get_string_element (array, i);
      g_string_append_printf (versions, " %s", version);

      /* We have tested only with r0.6.x and r0.5.0 */
      if (g_str_has_prefix (version, "r0.5.") ||
          g_str_has_prefix (version, "r0.6."))
        valid = TRUE;
    }

    g_log (G_LOG_DOMAIN, CHATTY_LOG_LEVEL_TRACE,
           "'%s' has versions:%s, valid: %s",
           server, versions->str, CHATTY_LOG_BOOL (valid));
  }

  g_task_return_boolean (task, valid);
}

void
matrix_utils_verify_homeserver_async (const char          *server,
                                      guint                timeout,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autofree char *uri = NULL;

  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_return_if_fail (callback);

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_task_data (task, g_strdup (server), g_free);
  g_task_set_source_tag (task, matrix_utils_verify_homeserver_async);

  if (!server || !*server ||
      !g_str_has_prefix (server, "http")) {
    g_task_return_new_error (task, G_IO_ERROR,
                             G_IO_ERROR_INVALID_DATA,
                             "URI '%s' is invalid", server);
    return;
  }

  uri = g_strconcat (server, "/_matrix/client/versions", NULL);
  matrix_utils_read_uri_async (uri, timeout, cancellable,
                               api_get_version_cb,
                               g_steal_pointer (&task));
}

gboolean
matrix_utils_verify_homeserver_finish (GAsyncResult *result,
                                       GError       **error)
{
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
pixbuf_load_stream_cb (GObject      *object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;
  GdkPixbuf *pixbuf;

  g_assert (G_IS_TASK (task));

  pixbuf = gdk_pixbuf_new_from_stream_finish (result, &error);

  if (error)
    g_task_return_error (task, error);
  else
    g_task_return_pointer (task, pixbuf, g_object_unref);
}

static void
avatar_file_read_cb (GObject      *object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(GFileInputStream) stream = NULL;
  GCancellable *cancellable;
  GError *error = NULL;
  int width;

  g_assert (G_IS_TASK (task));

  stream = g_file_read_finish (G_FILE (object), result, &error);

  cancellable = g_task_get_cancellable (task);
  width = GPOINTER_TO_INT (g_object_get_data (object, "width"));

  if (error)
    g_task_return_error (task, error);
  else
    gdk_pixbuf_new_from_stream_at_scale_async (G_INPUT_STREAM (stream),
                                               MIN (width, 512), -1, TRUE,
                                               cancellable,
                                               pixbuf_load_stream_cb,
                                               g_steal_pointer (&task));
}

static void
avatar_file_info_read_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;
  int height, width;

  g_assert (G_IS_TASK (task));

  gdk_pixbuf_get_file_info_finish (result, &width, &height, &error);

  if (error) {
    g_task_return_error (task, error);
  } else {
    g_autoptr(GFile) file = NULL;
    GCancellable *cancellable;
    char *path;

    cancellable = g_task_get_cancellable (task);
    path = g_task_get_task_data (task);
    file = g_file_new_for_path (path);

    g_object_set_data (G_OBJECT (file), "height", GINT_TO_POINTER (height));
    g_object_set_data (G_OBJECT (file), "width", GINT_TO_POINTER (width));

    g_file_read_async (file, G_PRIORITY_DEFAULT, cancellable,
                       avatar_file_read_cb,
                       g_steal_pointer (&task));
  }
}

void
matrix_utils_get_pixbuf_async (const char          *file,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (file && *file);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_return_if_fail (callback);

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_task_data (task, g_strdup (file), g_free);

  gdk_pixbuf_get_file_info_async (file, cancellable,
                                  avatar_file_info_read_cb,
                                  task);

}

GdkPixbuf *
matrix_utils_get_pixbuf_finish (GAsyncResult  *result,
                                GError       **error)
{
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}
