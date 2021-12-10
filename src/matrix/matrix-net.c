/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* matrix-api.c
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-matrix-net"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define GCRYPT_NO_DEPRECATED
#include <gcrypt.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include "chatty-utils.h"
#include "matrix-utils.h"
#include "matrix-enums.h"
#include "matrix-enc.h"
#include "matrix-net.h"
#include "chatty-log.h"

/**
 * SECTION: matrix-net
 * @title: MatrixNet
 * @short_description: Matrix Network related methods
 * @include: "matrix-net.h"
 */

#define MAX_CONNECTIONS     4

struct _MatrixNet
{
  GObject         parent_instance;

  SoupSession    *soup_session;
  SoupSession    *file_session;
  GCancellable   *cancellable;
  char           *homeserver;
  char           *access_token;
};


G_DEFINE_TYPE (MatrixNet, matrix_net, G_TYPE_OBJECT)

static void
net_download_stream_cb (GObject      *obj,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  GCancellable *cancellable;
  GOutputStream *out_stream = NULL;
  GError *error = NULL;
  char *buffer, *secret;
  gsize n_written;
  gssize n_read;

  g_assert (G_IS_TASK (task));

  n_read = g_input_stream_read_finish (G_INPUT_STREAM (obj), result, &error);

  if (error) {
    g_task_return_error (task, error);

    return;
  }

  cancellable = g_task_get_cancellable (task);
  buffer = g_task_get_task_data (task);
  secret = g_object_get_data (user_data, "secret");
  out_stream = g_object_get_data (user_data, "out-stream");
  g_assert (out_stream);

  if (secret) {
    gcry_cipher_hd_t cipher_hd;
    gcry_error_t err;

    cipher_hd = g_object_get_data (user_data, "cipher");
    g_assert (cipher_hd);

    err = gcry_cipher_decrypt (cipher_hd, secret, n_read, buffer, n_read);
    if (!err)
      buffer = secret;
  }

  g_output_stream_write_all (out_stream, buffer, n_read, &n_written, NULL, NULL);
  if (n_read == 0 || n_read == -1) {
    g_output_stream_close (out_stream, cancellable, NULL);

    if (n_read == 0) {
      g_autoptr(GFile) parent = NULL;
      GFile *out_file;
      ChattyFileInfo *file;

      file = g_object_get_data (user_data, "file");
      out_file = g_object_get_data (user_data, "out-file");

      /* We don't use absolute directory so that the path is user agnostic */
      parent = g_file_new_build_filename (g_get_user_cache_dir (), "chatty", NULL);
      file->path = g_file_get_relative_path (parent, out_file);
    }

    g_task_return_boolean (task, n_read == 0);

    return;
  }

  buffer = g_task_get_task_data (task);
  g_input_stream_read_async (G_INPUT_STREAM (obj), buffer, 1024 * 8, G_PRIORITY_DEFAULT, cancellable,
                             net_download_stream_cb, g_steal_pointer (&task));
}

static void
net_get_file_stream_cb  (GObject      *obj,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  MatrixNet *self;
  g_autoptr(GTask) task = user_data;
  GCancellable *cancellable;
  ChattyMessage *message;
  ChattyFileInfo *file;
  GInputStream *stream;
  GError *error = NULL;
  char *buffer = NULL;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (MATRIX_IS_NET (self));

  stream = soup_session_send_finish (SOUP_SESSION (obj), result, &error);
  file = g_object_get_data (user_data, "file");
  message = g_object_get_data (user_data, "message");
  cancellable = g_task_get_cancellable (task);

  if (!error) {
    GFileOutputStream *out_stream;
    GFile *out_file;
    gboolean is_thumbnail = FALSE;
    g_autofree char *file_name = NULL;

    if (message &&
        chatty_message_get_preview (message) == file)
      is_thumbnail = TRUE;

    file_name = g_path_get_basename (file->url);

    /* If @message is NULL, @file is an avatar image */
    out_file = g_file_new_build_filename (g_get_user_cache_dir (), "chatty", "matrix",
                                          message ? "files" : "avatars",
                                          is_thumbnail ? "thumbnail" : "", file_name,
                                          NULL);
    out_stream = g_file_append_to (out_file, 0, cancellable, &error);
    g_object_set_data_full (user_data, "out-file", out_file, g_object_unref);
    g_object_set_data_full (user_data, "out-stream", out_stream, g_object_unref);
  }

  if (error) {
    g_task_return_error (task, error);
    return;
  }

  buffer = g_malloc (1024 * 8);
  g_task_set_task_data (task, buffer, g_free);

  if (message &&
      chatty_message_get_encrypted (message) && file->user_data) {
    MatrixFileEncInfo *key;
    gcry_cipher_hd_t cipher_hd;
    gcry_error_t err;

    key = file->user_data;
    err = gcry_cipher_open (&cipher_hd, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_CTR, 0);

    if (!err)
      err = gcry_cipher_setkey (cipher_hd, key->aes_key, key->aes_key_len);

    if (!err)
      err = gcry_cipher_setctr (cipher_hd, key->aes_iv, key->aes_iv_len);

    if (!err) {
      char *secret = g_malloc (1024 * 8);
      g_object_set_data_full (user_data, "secret", secret, g_free);
      g_object_set_data_full (user_data, "cipher", cipher_hd,
                              (GDestroyNotify)gcry_cipher_close);
    }
  }

  g_input_stream_read_async (stream, buffer, 1024 * 8, G_PRIORITY_DEFAULT, cancellable,
                             net_download_stream_cb, g_steal_pointer (&task));
}

static void
net_load_from_stream_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  MatrixNet *self;
  JsonParser *parser = JSON_PARSER (object);
  g_autoptr(GTask) task = user_data;
  JsonNode *root = NULL;
  GError *error = NULL;

  g_assert (JSON_IS_PARSER (parser));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (MATRIX_IS_NET (self));

  json_parser_load_from_stream_finish (parser, result, &error);

  if (!error) {
    root = json_parser_get_root (parser);
    error = matrix_utils_json_node_get_error (root);
  }

  if (error) {
    if (g_error_matches (error, MATRIX_ERROR, M_LIMIT_EXCEEDED) &&
        root &&
        JSON_NODE_HOLDS_OBJECT (root)) {
      JsonObject *obj;
      guint retry;

      obj = json_node_get_object (root);
      retry = matrix_utils_json_object_get_int (obj, "retry_after_ms");
      g_object_set_data (G_OBJECT (task), "retry-after", GINT_TO_POINTER (retry));
    } else {
      CHATTY_DEBUG_MSG ("Error loading from stream: %s", error->message);
    }

    g_task_return_error (task, error);
    return;
  }

  if (JSON_NODE_HOLDS_OBJECT (root))
    g_task_return_pointer (task, json_node_dup_object (root),
                           (GDestroyNotify)json_object_unref);
  else if (JSON_NODE_HOLDS_ARRAY (root))
    g_task_return_pointer (task, json_node_dup_array (root),
                           (GDestroyNotify)json_array_unref);
  else
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Received invalid data");
}

static void
session_send_cb (GObject      *object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  MatrixNet *self;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GInputStream) stream = NULL;
  g_autoptr(JsonParser) parser = NULL;
  GCancellable *cancellable;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (MATRIX_IS_NET (self));

  stream = soup_session_send_finish (self->soup_session, result, &error);

  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      CHATTY_TRACE_MSG ("Error session send: %s", error->message);
    g_task_return_error (task, error);
    return;
  }

  cancellable = g_task_get_cancellable (task);
  parser = json_parser_new ();
  json_parser_load_from_stream_async (parser, stream, cancellable,
                                      net_load_from_stream_cb,
                                      g_steal_pointer (&task));
}

/*
 * queue_data:
 * @data: (transfer full)
 * @size: non-zero if @data is not %NULL
 * @task: (transfer full)
 */
static void
queue_data (MatrixNet  *self,
            char       *data,
            gsize       size,
            const char *uri_path,
            const char *method, /* interned */
            GHashTable *query,
            GTask      *task)
{
  g_autoptr(SoupMessage) message = NULL;
  g_autoptr(SoupURI) uri = NULL;
  GCancellable *cancellable;
  SoupMessagePriority msg_priority;
  int priority = 0;

  g_assert (MATRIX_IS_NET (self));
  g_assert (uri_path && *uri_path);
  g_assert (method && *method);
  g_return_if_fail (self->homeserver && *self->homeserver);

  g_assert (method == SOUP_METHOD_GET ||
            method == SOUP_METHOD_POST ||
            method == SOUP_METHOD_PUT);

  uri = soup_uri_new (self->homeserver);
  soup_uri_set_path (uri, uri_path);

  if (self->access_token) {
    if (!query)
      query = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                     (GDestroyNotify)matrix_utils_free_buffer);

    g_hash_table_replace (query, g_strdup ("access_token"), g_strdup (self->access_token));
    soup_uri_set_query_from_form (uri, query);
    g_hash_table_unref (query);
  }

  message = soup_message_new_from_uri (method, uri);
  soup_message_headers_append (message->request_headers, "Accept-Encoding", "gzip");

  priority = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "priority"));

  if (priority <= -2)
    msg_priority = SOUP_MESSAGE_PRIORITY_VERY_LOW;
  else if (priority == -1)
    msg_priority = SOUP_MESSAGE_PRIORITY_LOW;
  else if (priority == 1)
    msg_priority = SOUP_MESSAGE_PRIORITY_HIGH;
  else if (priority >= 2)
    msg_priority = SOUP_MESSAGE_PRIORITY_VERY_HIGH;
  else
    msg_priority = SOUP_MESSAGE_PRIORITY_NORMAL;

  soup_message_set_priority (message, msg_priority);

  if (data)
    soup_message_set_request (message, "application/json", SOUP_MEMORY_TAKE, data, size);

  cancellable = g_task_get_cancellable (task);
  g_task_set_task_data (task, g_object_ref (message), g_object_unref);
  soup_session_send_async (self->soup_session, message, cancellable,
                           session_send_cb, task);
}

static void
matrix_net_finalize (GObject *object)
{
  MatrixNet *self = (MatrixNet *)object;

  if (self->cancellable)
    g_cancellable_cancel (self->cancellable);

  soup_session_abort (self->soup_session);
  soup_session_abort (self->file_session);

  g_clear_object (&self->cancellable);
  g_clear_object (&self->soup_session);
  g_clear_object (&self->file_session);

  g_free (self->homeserver);

  matrix_utils_free_buffer (self->access_token);
  G_OBJECT_CLASS (matrix_net_parent_class)->finalize (object);
}

static void
matrix_net_class_init (MatrixNetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = matrix_net_finalize;
}

static void
matrix_net_init (MatrixNet *self)
{
  self->soup_session = g_object_new (SOUP_TYPE_SESSION,
                                     "max-conns-per-host", MAX_CONNECTIONS,
                                     NULL);
  self->file_session = g_object_new (SOUP_TYPE_SESSION,
                                     "max-conns-per-host", MAX_CONNECTIONS,
                                     NULL);
  self->cancellable = g_cancellable_new ();
}

MatrixNet *
matrix_net_new (void)
{
  return g_object_new (MATRIX_TYPE_NET, NULL);
}

void
matrix_net_set_homeserver (MatrixNet  *self,
                           const char *homeserver)
{
  g_return_if_fail (MATRIX_IS_NET (self));
  g_return_if_fail (homeserver && *homeserver);

  g_free (self->homeserver);
  self->homeserver = g_strdup (homeserver);
}

void
matrix_net_set_access_token (MatrixNet  *self,
                             const char *access_token)
{
  g_return_if_fail (MATRIX_IS_NET (self));

  matrix_utils_free_buffer (self->access_token);
  self->access_token = g_strdup (access_token);
}

/**
 * matrix_net_send_data_async:
 * @self: A #MatrixNet
 * @priority: The priority of request, 0 for default
 * @data: (nullable) (transfer full): The data to send
 * @size: The @data size in bytes
 * @uri_path: A string of the matrix uri path
 * @method: An interned string for GET, PUT, POST, etc.
 * @query: (nullable): A query to pass to internal #SoupURI
 * @cancellable: (nullable): A #GCancellable
 * @callback: The callback to run when completed
 * @user_data: user data for @callback
 *
 * Send a JSON data @object to the @uri_path endpoint.
 * @method should be one of %SOUP_METHOD_GET, %SOUP_METHOD_PUT
 * or %SOUP_METHOD_POST.
 * If @cancellable is %NULL, the internal cancellable
 * shall be used
 */
void
matrix_net_send_data_async (MatrixNet           *self,
                            int                  priority,
                            char                *data,
                            gsize                size,
                            const char          *uri_path,
                            const char          *method, /* interned */
                            GHashTable          *query,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (MATRIX_IS_NET (self));
  g_return_if_fail (uri_path && *uri_path);
  g_return_if_fail (method && *method);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_return_if_fail (callback);
  g_return_if_fail (self->homeserver && *self->homeserver);

  if (data && *data)
    g_return_if_fail (size);

  if (!cancellable)
    cancellable = self->cancellable;

  task = g_task_new (self, cancellable, callback, user_data);
  g_object_set_data (G_OBJECT (task), "priority", GINT_TO_POINTER (priority));

  queue_data (self, data, size, uri_path, method, query, task);
}

/**
 * matrix_net_send_json_async:
 * @self: A #MatrixNet
 * @priority: The priority of request, 0 for default
 * @object: (nullable) (transfer full): The data to send
 * @uri_path: A string of the matrix uri path
 * @method: An interned string for GET, PUT, POST, etc.
 * @query: (nullable): A query to pass to internal #SoupURI
 * @cancellable: (nullable): A #GCancellable
 * @callback: The callback to run when completed
 * @user_data: user data for @callback
 *
 * Send a JSON data @object to the @uri_path endpoint.
 * @method should be one of %SOUP_METHOD_GET, %SOUP_METHOD_PUT
 * or %SOUP_METHOD_POST.
 * If @cancellable is %NULL, the internal cancellable
 * shall be used
 */
void
matrix_net_send_json_async (MatrixNet           *self,
                            int                  priority,
                            JsonObject          *object,
                            const char          *uri_path,
                            const char          *method, /* interned */
                            GHashTable          *query,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
  GTask *task;
  char *data = NULL;
  gsize size = 0;

  g_return_if_fail (MATRIX_IS_NET (self));
  g_return_if_fail (uri_path && *uri_path);
  g_return_if_fail (method && *method);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_return_if_fail (callback);
  g_return_if_fail (self->homeserver && *self->homeserver);

  if (object) {
    data = matrix_utils_json_object_to_string (object, FALSE);
    json_object_unref (object);
  }

  if (data && *data)
    size = strlen (data);

  if (!cancellable)
    cancellable = self->cancellable;

  task = g_task_new (self, cancellable, callback, user_data);
  g_object_set_data (G_OBJECT (task), "priority", GINT_TO_POINTER (priority));

  queue_data (self, data, size, uri_path, method, query, task);
}

/**
 * matrix_net_get_file_async:
 * @self: A #MatrixNet
 * @message: (nullable) (transfer full): A #ChattyMessage
 * @file: A #ChattyFileInfo
 * @cancellable: (nullable): A #GCancellable
 * @progress_callback: (nullable): A #GFileProgressCallback
 * @callback: The callback to run when completed
 * @user_data: user data for @callback
 *
 * Download the file @file.  @file path shall be updated
 * after download is completed, and if @file is encrypted
 * and has keys to decrypt the file, the file shall be
 * stored decrypted.
 */
void
matrix_net_get_file_async (MatrixNet             *self,
                           ChattyMessage         *message,
                           ChattyFileInfo        *file,
                           GCancellable          *cancellable,
                           GFileProgressCallback  progress_callback,
                           GAsyncReadyCallback    callback,
                           gpointer               user_data)
{
  g_autofree char *url = NULL;
  SoupMessage *msg;
  GTask *task;

  g_return_if_fail (MATRIX_IS_NET (self));
  g_return_if_fail (!message || CHATTY_IS_MESSAGE (message));
  g_return_if_fail (file && file->url);

  if (message)
    g_object_ref (message);

  if (!cancellable)
    cancellable = self->cancellable;

  if (g_str_has_prefix (file->url, "mxc://")) {
    const char *file_url;

    file_url = file->url + strlen ("mxc://");
    url = g_strconcat (self->homeserver,
                       "/_matrix/media/r0/download/", file_url, NULL);
  }

  if (!url)
    url = g_strdup (file->url);

  msg = soup_message_new (SOUP_METHOD_GET, url);

  task = g_task_new (self, cancellable, callback, user_data);
  g_object_set_data (G_OBJECT (task), "progress", progress_callback);
  g_object_set_data (G_OBJECT (task), "file", file);
  g_object_set_data_full (G_OBJECT (task), "msg", msg, g_object_unref);
  g_object_set_data_full (G_OBJECT (task), "message", message, g_object_unref);

  file->status = CHATTY_FILE_DOWNLOADING;
  if (message)
    chatty_message_emit_updated (message);

  soup_session_send_async (self->file_session, msg, cancellable,
                           net_get_file_stream_cb, task);
}

gboolean
matrix_net_get_file_finish (MatrixNet     *self,
                            GAsyncResult  *result,
                            GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_NET (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
