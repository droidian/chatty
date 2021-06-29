/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* matrix-api.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-matrix-api"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define GCRYPT_NO_DEPRECATED
#include <gcrypt.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <olm/olm.h>
#include <sys/random.h>

#include "chatty-chat.h"
#include "chatty-ma-buddy.h"
#include "matrix-enums.h"
#include "matrix-utils.h"
#include "matrix-api.h"
#include "chatty-log.h"

/**
 * SECTION: chatty-api
 * @title: MatrixApi
 * @short_description: The Matrix HTTP API.
 * @include: "chatty-api.h"
 *
 * This class handles all communications with Matrix server
 * user REST APIs.
 */

#define MAX_CONNECTIONS     6
#define URI_REQUEST_TIMEOUT 60    /* seconds */
#define SYNC_TIMEOUT        30000 /* milliseconds */
#define TYPING_TIMEOUT      10000 /* milliseconds */
#define KEY_TIMEOUT         10000 /* milliseconds */

struct _MatrixApi
{
  GObject         parent_instance;


  char           *username;
  char           *password;
  char           *homeserver;
  char           *device_id;
  char           *access_token;
  char           *key;
  SoupSession    *soup_session;

  MatrixEnc      *matrix_enc;

  /* Executed for every request response */
  MatrixCallback  callback;
  gpointer        cb_object;
  GCancellable   *cancellable;
  char           *next_batch;
  GError         *error; /* Current error, if any. */
  MatrixAction    action;

  /* for sending events, incremented for each event */
  int             event_id;

  gboolean        full_state_loaded;
  gboolean        is_sync;
  /* Set when error occurs with sync enabled */
  gboolean        sync_failed;
  gboolean        homeserver_verified;
  gboolean        login_success;
  gboolean        room_list_loaded;

  guint           resync_id;
};

G_DEFINE_TYPE (MatrixApi, matrix_api, G_TYPE_OBJECT)

static void matrix_verify_homeserver (MatrixApi *self);
static void matrix_login             (MatrixApi *self);
static void matrix_upload_key        (MatrixApi *self);
static void matrix_start_sync        (MatrixApi *self);
static void matrix_take_red_pill     (MatrixApi *self);
static gboolean handle_common_errors (MatrixApi *self,
                                      GError    *error);

static void
api_set_string_value (char       **strp,
                      const char  *value)
{
  g_assert (strp);

  if (value) {
    g_free (*strp);
    *strp = g_strdup (value);
  }
}

static void
api_get_version_cb (GObject      *obj,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  MatrixApi *self = user_data;
  g_autoptr(JsonNode) root = NULL;
  JsonObject *object = NULL;
  JsonArray *array = NULL;
  GError *error = NULL;

  g_assert (MATRIX_IS_API (self));

  root = matrix_utils_read_uri_finish (result, &error);
  g_clear_error (&self->error);

  if (!error)
    error = matrix_utils_json_node_get_error (root);

  if (handle_common_errors (self, error))
    return;

  if (!root) {
    CHATTY_TRACE_MSG ("Error verifying home server: %s", error->message);
    self->error = error;
    self->callback (self->cb_object, self, self->action, NULL, self->error);
    return;
  }

  object = json_node_get_object (root);
  array = matrix_utils_json_object_get_array (object, "versions");

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
        self->homeserver_verified = TRUE;
    }

    CHATTY_TRACE_MSG ("%s has versions:%s", self->homeserver, versions->str);

    if (!self->homeserver_verified)
      g_warning ("Chatty requires Client-Server API to be ‘r0.5.x’ or ‘r0.6.x’");
  }

  if (!self->homeserver_verified) {
    self->error = g_error_new (MATRIX_ERROR, M_BAD_HOME_SERVER,
                               "Couldn't Verify Client-Server API to be "
                               "‘r0.5.0’ or ‘r0.6.0’ for %s", self->homeserver);
    self->callback (self->cb_object, self, self->action, NULL, self->error);
  } else {
    matrix_start_sync (self);
  }
}

static void
api_get_homeserver_cb (gpointer      object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  MatrixApi *self = user_data;
  GError *error = NULL;
  char *homeserver;

  g_assert (MATRIX_IS_API (self));

  homeserver = matrix_utils_get_homeserver_finish (result, &error);
  g_clear_error (&self->error);

  CHATTY_TRACE_MSG ("Get home server, has-error: %d, home server: %s",
                    !error, homeserver);

  if (!homeserver) {
    self->sync_failed = TRUE;
    self->error = error;
    self->callback (self->cb_object, self, self->action, NULL, self->error);

    return;
  }

  matrix_api_set_homeserver (self, homeserver);
  matrix_verify_homeserver (self);
}

static void
api_send_message_cb (GObject      *obj,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(JsonObject) object = NULL;
  ChattyMessage *message;
  GError *error = NULL;
  char *event_id;
  int retry_after;

  g_assert (G_IS_TASK (task));

  object = g_task_propagate_pointer (G_TASK (result), &error);
  retry_after = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (result), "retry-after"));
  g_object_set_data (G_OBJECT (task), "retry-after", GINT_TO_POINTER (retry_after));

  message = g_object_get_data (G_OBJECT (task), "message");
  event_id = g_object_get_data (G_OBJECT (task), "event-id");

  CHATTY_TRACE_MSG ("Sent message. event-id: %s, success: %d, retry-after: %d",
                    event_id, !error, retry_after);

  if (error) {
    g_debug ("Error sending message: %s", error->message);
    chatty_message_set_status (message, CHATTY_STATUS_SENDING_FAILED, 0);
    g_task_return_error (task, error);
  } else {
    chatty_message_set_status (message, CHATTY_STATUS_SENT, 0);
    g_task_return_boolean (task, !!object);
  }
}

static void
api_download_stream_cb (GObject      *obj,
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

    err = gcry_cipher_decrypt(cipher_hd, secret, n_read, buffer, n_read);
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
                             api_download_stream_cb, g_steal_pointer (&task));
}

static void
api_get_file_stream_cb  (GObject      *obj,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  GCancellable *cancellable;
  ChattyMessage *message;
  ChattyFileInfo *file;
  GInputStream *stream;
  GError *error = NULL;
  char *buffer = NULL;
  MatrixApi *self;

  g_assert (G_IS_TASK (task));

  CHATTY_ENTRY;

  stream = soup_session_send_finish (SOUP_SESSION (obj), result, &error);
  file = g_object_get_data (user_data, "file");
  message = g_object_get_data (user_data, "message");
  cancellable = g_task_get_cancellable (task);

  if (!error) {
    GFileOutputStream *out_stream;
    GFile *out_file;
    gboolean is_thumbnail = FALSE;
    char *name, *file_name;

    if (message &&
        chatty_message_get_preview (message) == file)
      is_thumbnail = TRUE;

    name = g_path_get_basename (file->url);
    file_name = g_strdup (name);

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
    CHATTY_EXIT;
  }

  self = g_task_get_source_object (task);
  g_assert (MATRIX_IS_API (self));

  buffer = g_malloc (1024 * 8);
  g_task_set_task_data (task, buffer, g_free);

  if (message &&
      chatty_message_get_encrypted (message) && file->user_data) {
    gcry_cipher_hd_t cipher_hd;
    MatrixFileEncInfo *key;
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
                             api_download_stream_cb, g_steal_pointer (&task));
  CHATTY_EXIT;
}

static void
matrix_send_typing_cb (GObject      *obj,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  g_autoptr(JsonObject) object = NULL;
  g_autoptr(GError) error = NULL;

  object = g_task_propagate_pointer (G_TASK (result), &error);

  if (error)
    g_warning ("Error set typing: %s", error->message);
}


static void
api_set_read_marker_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;

  object = g_task_propagate_pointer (G_TASK (result), &error);

  CHATTY_TRACE_MSG ("Mark as read. success: %d", !error);

  if (error) {
    g_debug ("Error setting read marker: %s", error->message);
    g_task_return_error (task, error);
  } else {
    g_task_return_boolean (task, TRUE);
  }
}

static void
api_upload_group_keys_cb (GObject      *obj,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(JsonObject) object = NULL;
  GError *error = NULL;

  CHATTY_ENTRY;

  object = g_task_propagate_pointer (G_TASK (result), &error);

  if (error) {
    g_debug ("Error uploading group keys: %s", error->message);
    g_task_return_error (task, error);
  } else {
    g_task_return_boolean (task, TRUE);
  }

  CHATTY_EXIT;
}

static void
matrix_get_room_state_cb (GObject      *obj,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  JsonArray *array;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  array = g_task_propagate_pointer (G_TASK (result), &error);

  if (error) {
    g_debug ("Error getting room state: %s", error->message);
    g_task_return_error (task, error);
  } else {
    g_task_return_pointer (task, array, (GDestroyNotify)json_array_unref);
  }
}

static void
matrix_get_room_name_cb (GObject      *obj,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  JsonObject *object = NULL;
  GError *error = NULL;

  object = g_task_propagate_pointer (G_TASK (result), &error);

  if (error) {
    g_debug ("Error getting room state: %s", error->message);
    g_task_return_error (task, error);
  } else {
    g_task_return_pointer (task, object, (GDestroyNotify)json_object_unref);
  }
}

static void
matrix_get_members_cb (GObject      *obj,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  JsonObject *object = NULL;
  GError *error = NULL;

  CHATTY_ENTRY;

  object = g_task_propagate_pointer (G_TASK (result), &error);

  if (error) {
    g_debug ("Error getting members: %s", error->message);
    g_task_return_error (task, error);
  } else {
    g_task_return_pointer (task, object, (GDestroyNotify)json_object_unref);
  }

  CHATTY_EXIT;
}

static void
matrix_get_messages_cb (GObject      *obj,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  JsonObject *object = NULL;
  GError *error = NULL;

  object = g_task_propagate_pointer (G_TASK (result), &error);

  if (error) {
    g_warning ("Error getting members: %s", error->message);
    g_task_return_error (task, error);
  } else {
    g_task_return_pointer (task, object, (GDestroyNotify)json_object_unref);
  }
}

static void
matrix_keys_query_cb (GObject      *obj,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  JsonObject *object = NULL;
  GError *error = NULL;

  object = g_task_propagate_pointer (G_TASK (result), &error);

  CHATTY_TRACE_MSG ("Query key complete. success: %d", !error);

  if (error) {
    g_debug ("Error key query: %s", error->message);
    g_task_return_error (task, error);
  } else {
    g_task_return_pointer (task, object, (GDestroyNotify)json_object_unref);
  }
}

static void
matrix_keys_claim_cb (GObject      *obj,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  JsonObject *object = NULL;
  GError *error = NULL;

  object = g_task_propagate_pointer (G_TASK (result), &error);

  if (error) {
    g_debug ("Error key query: %s", error->message);
    g_task_return_error (task, error);
  } else {
    g_task_return_pointer (task, object, (GDestroyNotify)json_object_unref);
  }
}

static gboolean
schedule_resync (gpointer user_data)
{
  MatrixApi *self = user_data;
  GNetworkMonitor *network_monitor;
  GNetworkConnectivity connectivity;

  g_assert (MATRIX_IS_API (self));
  self->resync_id = 0;

  network_monitor = g_network_monitor_get_default ();
  connectivity = g_network_monitor_get_connectivity (network_monitor);

  CHATTY_TRACE_MSG ("Schedule sync for user %s, sync now: %d", self->username,
                    connectivity == G_NETWORK_CONNECTIVITY_FULL);
  if (connectivity == G_NETWORK_CONNECTIVITY_FULL)
    matrix_start_sync (self);

  return G_SOURCE_REMOVE;
}

/*
 * Handle Self fixable errors.
 *
 * Returns: %TRUE if @error was handled.
 * %FALSE otherwise
 */
static gboolean
handle_common_errors (MatrixApi *self,
                      GError    *error)
{
  if (!error)
    return FALSE;

  if (g_error_matches (error, MATRIX_ERROR, M_UNKNOWN_TOKEN)
      && self->password) {
    CHATTY_TRACE_MSG ("Re-logging in %s", self->username);
    self->login_success = FALSE;
    self->room_list_loaded = FALSE;
    g_clear_pointer (&self->access_token, matrix_utils_free_buffer);
    matrix_enc_set_details (self->matrix_enc, NULL, NULL);
    matrix_start_sync (self);

    return TRUE;
  }

  /*
   * The G_RESOLVER_ERROR may be suggesting that the hostname is wrong, but we don't
   * know if it's network/DNS/Proxy error. So keep retrying.
   */
  if ((error->domain == SOUP_HTTP_ERROR &&
       error->code <= SOUP_STATUS_TLS_FAILED &&
       error->code > SOUP_STATUS_CANCELLED) ||
      g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NETWORK_UNREACHABLE) ||
      g_error_matches (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT) ||
      error->domain == G_RESOLVER_ERROR ||
      error->domain == JSON_PARSER_ERROR) {
    GNetworkMonitor *network_monitor;

    network_monitor = g_network_monitor_get_default ();

    /* Distributions may advertise to have full network support
     * even when connected only to local network */
    if (g_network_monitor_get_connectivity (network_monitor) == G_NETWORK_CONNECTIVITY_FULL) {
      g_clear_handle_id (&self->resync_id, g_source_remove);

      CHATTY_TRACE_MSG ("Schedule sync for user %s", self->username);
      self->resync_id = g_timeout_add_seconds (URI_REQUEST_TIMEOUT,
                                               schedule_resync, self);
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
handle_one_time_keys (MatrixApi  *self,
                      JsonObject *object)
{
  size_t count, limit;

  g_assert (MATRIX_IS_API (self));

  if (!object)
    return FALSE;

  count = matrix_utils_json_object_get_int (object, "signed_curve25519");
  limit = matrix_enc_max_one_time_keys (self->matrix_enc) / 2;

  /* If we don't have enough onetime keys add some */
  if (count < limit) {
    CHATTY_TRACE_MSG ("generating %lu onetime keys", limit - count);
    matrix_enc_create_one_time_keys (self->matrix_enc, limit - count);

    g_free (self->key);
    self->key = matrix_enc_get_one_time_keys_json (self->matrix_enc);
    matrix_upload_key (self);

    return TRUE;
  }

  return FALSE;
}

static void
matrix_login_cb (GObject      *obj,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  MatrixApi *self;
  g_autoptr(JsonObject) root = NULL;
  JsonObject *object = NULL;
  GError *error = NULL;
  const char *value;

  g_assert (G_IS_TASK (result));

  self = g_task_get_source_object (G_TASK (result));
  g_assert (MATRIX_IS_API (self));

  root = g_task_propagate_pointer (G_TASK (result), &error);
  g_clear_error (&self->error);

  CHATTY_TRACE_MSG ("login complete. success: %d", !error);
  if (error) {
    self->sync_failed = TRUE;
    /* use a better code to inform invalid password */
    if (error->code == M_FORBIDDEN)
      error->code = M_BAD_PASSWORD;
    self->error = error;
    self->callback (self->cb_object, self, MATRIX_PASSWORD_LOGIN, NULL, self->error);
    g_debug ("Error logging in: %s", error->message);
    return;
  }

  self->login_success = TRUE;

  /* https://matrix.org/docs/spec/client_server/r0.6.1#post-matrix-client-r0-login */
  value = matrix_utils_json_object_get_string (root, "user_id");
  api_set_string_value (&self->username, value);

  value = matrix_utils_json_object_get_string (root, "access_token");
  api_set_string_value (&self->access_token, value);

  value = matrix_utils_json_object_get_string (root, "device_id");
  api_set_string_value (&self->device_id, value);

  object = matrix_utils_json_object_get_object (root, "well_known");
  object = matrix_utils_json_object_get_object (object, "m.homeserver");
  value = matrix_utils_json_object_get_string (object, "base_url");
  matrix_api_set_homeserver (self, value);

  matrix_enc_set_details (self->matrix_enc, self->username, self->device_id);
  g_free (self->key);
  self->key = matrix_enc_get_device_keys_json (self->matrix_enc);

  self->callback (self->cb_object, self, MATRIX_PASSWORD_LOGIN, NULL, NULL);
  matrix_upload_key (self);
}

static void
matrix_upload_key_cb (GObject      *obj,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  MatrixApi *self;
  g_autoptr(JsonObject) root = NULL;
  JsonObject *object = NULL;
  GError *error = NULL;

  g_assert (G_IS_TASK (result));

  self = g_task_get_source_object (G_TASK (result));
  g_assert (MATRIX_IS_API (self));

  root = g_task_propagate_pointer (G_TASK (result), &error);
  g_clear_error (&self->error);

  if (error) {
    self->sync_failed = TRUE;
    self->error = error;
    self->callback (self->cb_object, self, MATRIX_UPLOAD_KEY, NULL, self->error);
    g_debug ("Error uploading key: %s", error->message);
    CHATTY_EXIT;
  }

  self->callback (self->cb_object, self, MATRIX_UPLOAD_KEY, root, NULL);

  object = matrix_utils_json_object_get_object (root, "one_time_key_counts");
  CHATTY_TRACE_MSG ("Uploaded %d keys",
                    matrix_utils_json_object_get_int (object, "signed_curve25519"));

  if (!handle_one_time_keys (self, object) &&
       self->action != MATRIX_RED_PILL)
    matrix_take_red_pill (self);
}

/* sync callback */
static void
matrix_take_red_pill_cb (GObject      *obj,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  MatrixApi *self;
  g_autoptr(JsonObject) root = NULL;
  JsonObject *object = NULL;
  GError *error = NULL;

  g_assert (G_IS_TASK (result));

  self = g_task_get_source_object (G_TASK (result));
  g_assert (MATRIX_IS_API (self));

  root = g_task_propagate_pointer (G_TASK (result), &error);
  g_clear_error (&self->error);

  if (!self->next_batch || error || !self->full_state_loaded)
    CHATTY_TRACE_MSG ("sync success: %d, full-state: %d, next-batch: %s",
                      !error, !self->full_state_loaded, self->next_batch);

  if (handle_common_errors (self, error))
    return;

  if (error) {
    self->sync_failed = TRUE;
    self->error = error;
    self->callback (self->cb_object, self, self->action, NULL, self->error);
    g_debug ("Error syncing with time %s: %s", self->next_batch, error->message);
    return;
  }

  self->login_success = TRUE;

  object = matrix_utils_json_object_get_object (root, "device_one_time_keys_count");
  handle_one_time_keys (self, object);

  /* XXX: For some reason full state isn't loaded unless we have passed “next_batch”.
   * So, if we haven’t, don’t mark so.
   */
  if (self->next_batch)
    self->full_state_loaded = TRUE;

  g_free (self->next_batch);
  self->next_batch = g_strdup (matrix_utils_json_object_get_string (root, "next_batch"));

  self->callback (self->cb_object, self, self->action, root, NULL);

  /* Repeat */
  matrix_take_red_pill (self);
}

static void
api_load_from_stream_cb (JsonParser   *parser,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  MatrixApi *self;
  g_autoptr(GTask) task = user_data;
  JsonNode *root = NULL;
  GError *error = NULL;

  g_assert (JSON_IS_PARSER (parser));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (MATRIX_IS_API (self));

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
      CHATTY_TRACE_MSG ("Error loading from stream: %s", error->message);
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
session_send_cb (SoupSession  *session,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(GInputStream) stream = NULL;
  g_autoptr(JsonParser) parser = NULL;
  MatrixApi *self;
  GError *error = NULL;

  g_assert (SOUP_IS_SESSION (session));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (MATRIX_IS_API (self));

  stream = soup_session_send_finish (session, result, &error);

  if (error) {
    CHATTY_TRACE_MSG ("Error session send: %s", error->message);
    g_task_return_error (task, error);
    return;
  }

  parser = json_parser_new ();
  json_parser_load_from_stream_async (parser, stream, self->cancellable,
                                      (GAsyncReadyCallback)api_load_from_stream_cb,
                                      g_steal_pointer (&task));
}

static void
queue_data (MatrixApi           *self,
            char                *data,
            gsize                size,
            const char          *uri_path,
            const char          *method, /* interned */
            GHashTable          *query,
            GAsyncReadyCallback  callback,
            gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(SoupURI) uri = NULL;
  SoupMessage *message;

  g_assert (MATRIX_IS_API (self));
  g_assert (uri_path && *uri_path);
  g_assert (method == SOUP_METHOD_GET ||
            method == SOUP_METHOD_POST ||
            method == SOUP_METHOD_PUT);
  g_assert (callback);

  g_return_if_fail (self->homeserver && *self->homeserver);

  uri = soup_uri_new (self->homeserver);
  soup_uri_set_path (uri, uri_path);

  if (self->access_token) {
    if (!query)
      query = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    g_hash_table_replace (query, g_strdup ("access_token"), self->access_token);
    soup_uri_set_query_from_form (uri, query);
  }

  message = soup_message_new_from_uri (method, uri);
  soup_message_headers_append (message->request_headers, "Accept-Encoding", "gzip");

  if (callback == matrix_take_red_pill_cb)
    soup_message_set_priority (message, SOUP_MESSAGE_PRIORITY_VERY_HIGH);

  if (data && size == -1)
    size = strlen (data);

  if (data)
    soup_message_set_request (message, "application/json", SOUP_MEMORY_TAKE, data, size);

  task = g_task_new (self, self->cancellable, callback, user_data);
  g_task_set_task_data (task, g_object_ref (message), g_object_unref);
  soup_session_send_async (self->soup_session, message, self->cancellable,
                           (GAsyncReadyCallback)session_send_cb,
                           g_steal_pointer (&task));
}

static void
queue_json_object (MatrixApi           *self,
                   JsonObject          *object,
                   const char          *uri_path,
                   const char          *method, /* interned */
                   GHashTable          *query,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
  char *body = NULL;

  g_assert (MATRIX_IS_API (self));
  g_assert (object);
  g_return_if_fail (self->homeserver && *self->homeserver);

  body = matrix_utils_json_object_to_string (object, FALSE);
  queue_data (self, body, -1, uri_path, method, query, callback, user_data);
}

static void
matrix_verify_homeserver (MatrixApi *self)
{
  g_autofree char *uri = NULL;

  g_assert (MATRIX_IS_API (self));
  CHATTY_TRACE_MSG ("verifying homeserver %s", self->homeserver);

  self->action = MATRIX_VERIFY_HOMESERVER;
  uri = g_strconcat (self->homeserver, "/_matrix/client/versions", NULL);
  matrix_utils_read_uri_async (uri, URI_REQUEST_TIMEOUT,
                               self->cancellable,
                               api_get_version_cb, self);
}

static void
matrix_login (MatrixApi *self)
{
  g_autoptr(JsonObject) object = NULL;
  JsonObject *child;

  g_assert (MATRIX_IS_API (self));
  g_assert (self->username);
  g_assert (self->homeserver);
  g_assert (!self->access_token);
  g_assert (self->password && *self->password);

  CHATTY_TRACE_MSG ("logging in to account '%s' on server '%s'",
                    self->username, self->homeserver);

  /* https://matrix.org/docs/spec/client_server/r0.6.1#post-matrix-client-r0-login */
  object = json_object_new ();
  json_object_set_string_member (object, "type", "m.login.password");
  json_object_set_string_member (object, "password", self->password);
  json_object_set_string_member (object, "initial_device_display_name", "Chatty");

  child = json_object_new ();
  json_object_set_string_member (child, "type", "m.id.user");
  json_object_set_string_member (child, "user", self->username);
  json_object_set_object_member (object, "identifier", child);

  queue_json_object (self, object, "/_matrix/client/r0/login",
                     SOUP_METHOD_POST, NULL, matrix_login_cb, NULL);
}

static void
matrix_upload_key (MatrixApi *self)
{
  char *key;

  g_assert (MATRIX_IS_API (self));
  g_assert (self->key);

  key = g_steal_pointer (&self->key);

  queue_data (self, key, strlen (key), "/_matrix/client/r0/keys/upload",
              SOUP_METHOD_POST, NULL, matrix_upload_key_cb, NULL);
}

static void
get_joined_rooms_cb (GObject      *obj,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  MatrixApi *self;
  g_autoptr(JsonObject) root = NULL;
  GError *error = NULL;

  g_assert (G_IS_TASK (result));

  self = g_task_get_source_object (G_TASK (result));
  g_assert (MATRIX_IS_API (self));

  if (handle_common_errors (self, error))
    return;

  root = g_task_propagate_pointer (G_TASK (result), &error);
  g_clear_error (&self->error);
  self->error = error;

  self->callback (self->cb_object, self, MATRIX_GET_JOINED_ROOMS, root, error);

  if (!error) {
    self->room_list_loaded = TRUE;
    matrix_start_sync (self);
  }
}

static void
matrix_get_joined_rooms (MatrixApi *self)
{
  g_assert (MATRIX_IS_API (self));
  g_assert (!self->room_list_loaded);

  queue_data (self, NULL, 0, "/_matrix/client/r0/joined_rooms",
              SOUP_METHOD_GET, NULL, get_joined_rooms_cb, NULL);
}

static void
matrix_start_sync (MatrixApi *self)
{
  g_assert (MATRIX_IS_API (self));

  self->is_sync = TRUE;
  self->sync_failed = FALSE;
  g_clear_error (&self->error);
  g_clear_handle_id (&self->resync_id, g_source_remove);

  if (!self->homeserver) {
    self->action = MATRIX_GET_HOMESERVER;
    if (!matrix_utils_username_is_complete (self->username)) {
      g_debug ("Error: No Homeserver provided");
      self->sync_failed = TRUE;
      self->error = g_error_new (MATRIX_ERROR, M_NO_HOME_SERVER, "No Homeserver provided");
      self->callback (self->cb_object, self, self->action, NULL, self->error);
    } else {
      g_debug ("Fetching home server details from username");
      matrix_utils_get_homeserver_async (self->username, URI_REQUEST_TIMEOUT, self->cancellable,
                                         (GAsyncReadyCallback)api_get_homeserver_cb,
                                         self);
    }
  } else if (!self->homeserver_verified) {
    matrix_verify_homeserver (self);
  } else if (!self->access_token) {
    matrix_login (self);
  } else if (!self->room_list_loaded) {
    matrix_get_joined_rooms (self);
  } else {
    matrix_take_red_pill (self);
  }
}

static void
matrix_take_red_pill (MatrixApi *self)
{
  GHashTable *query;

  g_assert (MATRIX_IS_API (self));

  self->action = MATRIX_RED_PILL;
  query = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  if (self->login_success)
    g_hash_table_insert (query, g_strdup ("timeout"), g_strdup_printf ("%u", SYNC_TIMEOUT));
  else
    g_hash_table_insert (query, g_strdup ("timeout"), g_strdup_printf ("%u", SYNC_TIMEOUT / 1000));

  if (self->next_batch)
    g_hash_table_insert (query, g_strdup ("since"), g_strdup (self->next_batch));
  if (!self->full_state_loaded)
    g_hash_table_insert (query, g_strdup ("full_state"), g_strdup ("true"));

  queue_data (self, NULL, 0, "/_matrix/client/r0/sync",
              SOUP_METHOD_GET, query, matrix_take_red_pill_cb, NULL);
}

static void
matrix_api_finalize (GObject *object)
{
  MatrixApi *self = (MatrixApi *)object;

  if (self->cancellable)
    g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  g_clear_handle_id (&self->resync_id, g_source_remove);
  soup_session_abort (self->soup_session);
  g_object_unref (self->soup_session);

  g_free (self->username);
  g_free (self->homeserver);
  g_free (self->device_id);
  matrix_utils_free_buffer (self->password);
  matrix_utils_free_buffer (self->access_token);

  G_OBJECT_CLASS (matrix_api_parent_class)->finalize (object);
}

static void
matrix_api_class_init (MatrixApiClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = matrix_api_finalize;
}


static void
matrix_api_init (MatrixApi *self)
{
  self->soup_session = g_object_new (SOUP_TYPE_SESSION,
                                     "max-conns-per-host", MAX_CONNECTIONS,
                                     NULL);
  self->cancellable = g_cancellable_new ();
}

/**
 * matrix_api_new:
 * @username: (nullable): A valid matrix user id
 *
 * Create a new #MatrixApi for @username.  For the
 * #MatrixApi to be usable password/access token
 * and sync_callback should be set.
 *
 * If @username is not in full form (ie,
 * @user:example.com), homeserver should be set
 * with matrix_api_set_homeserver()
 *
 * Returns: (transfer full): A new #MatrixApi.
 * Free with g_object_unref().
 */
MatrixApi *
matrix_api_new (const char *username)
{
  MatrixApi *self;

  self = g_object_new (MATRIX_TYPE_API, NULL);
  self->username = g_strdup (username);

  return self;
}

void
matrix_api_set_enc (MatrixApi *self,
                    MatrixEnc *enc)
{
  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (MATRIX_IS_ENC (enc));
  g_return_if_fail (!self->matrix_enc);

  g_set_object (&self->matrix_enc, enc);

  if (self->username && self->device_id)
    matrix_enc_set_details (self->matrix_enc, self->username, self->device_id);
}

/**
 * matrix_api_get_username:
 * @self: A #MatrixApi
 *
 * Get the username of @self.  This will be a fully
 * qualified Matrix ID (eg: @user:example.com) if
 * @self has succeeded in synchronizing with the
 * server.  Otherwise, the username set for @self
 * shall be returned.
 *
 * Returns: The matrix username.
 */
const char *
matrix_api_get_username (MatrixApi *self)
{
  g_return_val_if_fail (MATRIX_IS_API (self), NULL);

  return self->username;
}

void
matrix_api_set_username (MatrixApi  *self,
                         const char *username)
{
  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (!self->username);

  self->username = g_strdup (username);
}

/**
 * matrix_api_get_password:
 * @self: A #MatrixApi
 *
 * Get the password of @self.
 *
 * Returns: (nullable): The matrix username.
 */
const char *
matrix_api_get_password (MatrixApi *self)
{
  g_return_val_if_fail (MATRIX_IS_API (self), NULL);

  return self->password;
}

/**
 * matrix_api_set_password:
 * @self: A #MatrixApi
 * @password: A valid password string
 *
 * Set the password for @self.
 */
void
matrix_api_set_password (MatrixApi  *self,
                         const char *password)
{
  g_return_if_fail (MATRIX_IS_API (self));

  if (!password || !*password)
    return;

  matrix_utils_free_buffer (self->password);
  self->password = g_strdup (password);
}

/**
 * matrix_api_set_sync_callback:
 * @self: A #MatrixApi
 * @callback: A #MatriCallback
 * @object: (nullable) (transfer full): A #GObject
 *
 * Set sync callback. It’s allowed to set callback
 * only once.
 *
 * @object should be a #GObject (derived) object
 * or %NULL.
 *
 * callback shall run as `callback(@object, ...)`
 *
 * @callback shall be run for every event that’s worth
 * informing (Say, the callback won’t be run if the
 * sync response is empty).
 *
 * The @callback may run with a %NULL #GAsyncResult
 * argument.  Check the sync state before handling
 * the #GAsyncResult. See matrix_api_get_sync_state().
 */
void
matrix_api_set_sync_callback (MatrixApi      *self,
                              MatrixCallback  callback,
                              gpointer        object)
{
  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (callback);
  g_return_if_fail (!object || G_IS_OBJECT (object));
  g_return_if_fail (!self->callback);

  self->callback = callback;
  g_clear_object (&self->cb_object);
  self->cb_object = g_object_ref (object);
}

const char *
matrix_api_get_homeserver (MatrixApi *self)
{
  g_return_val_if_fail (MATRIX_IS_API (self), NULL);

  return self->homeserver;
}

void
matrix_api_set_homeserver (MatrixApi  *self,
                           const char *homeserver)
{
  g_autoptr(SoupURI) uri = NULL;
  GString *host;

  g_return_if_fail (MATRIX_IS_API (self));

  uri = soup_uri_new (homeserver);
  if (!homeserver || !uri ||
      !SOUP_URI_VALID_FOR_HTTP (uri))
    return;

  host = g_string_new (NULL);
  g_string_append (host, soup_uri_get_scheme (uri));
  g_string_append (host, "://");
  g_string_append (host, uri->host);
  if (!soup_uri_uses_default_port (uri))
    g_string_append_printf (host, ":%d", soup_uri_get_port (uri));

  g_free (self->homeserver);
  self->homeserver = g_string_free (host, FALSE);

  if (self->is_sync &&
      self->sync_failed &&
      self->action == MATRIX_GET_HOMESERVER) {
    self->sync_failed = FALSE;
    matrix_verify_homeserver (self);
  }
}

/**
 * matrix_api_get_device_id:
 * @self: A #MatrixApi
 *
 * Get the device ID of @self.  If the
 * account login succeeded, the device
 * ID provided by the server is returned.
 * Otherwise, the one set with @self is
 * returned.
 *
 * Please not that the user login is done
 * only if @self has no access-token set,
 * or if the acces-token is invalid.
 *
 * Returns: (nullable): The Device ID.
 */
const char *
matrix_api_get_device_id (MatrixApi *self)
{
  g_return_val_if_fail (MATRIX_IS_API (self), NULL);

  return self->device_id;
}

const char *
matrix_api_get_access_token (MatrixApi *self)
{
  g_return_val_if_fail (MATRIX_IS_API (self), NULL);

  return self->access_token;
}

void
matrix_api_set_access_token (MatrixApi  *self,
                             const char *access_token,
                             const char *device_id)
{
  g_return_if_fail (MATRIX_IS_API (self));

  g_free (self->access_token);
  g_free (self->device_id);

  self->access_token = g_strdup (access_token);
  self->device_id = g_strdup (device_id);
  if (self->matrix_enc)
    matrix_enc_set_details (self->matrix_enc, self->username, self->device_id);
}

const char *
matrix_api_get_next_batch (MatrixApi *self)
{
  g_return_val_if_fail (MATRIX_IS_API (self), NULL);

  return self->next_batch;
}

void
matrix_api_set_next_batch (MatrixApi  *self,
                           const char *next_batch)
{
  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (!self->next_batch);

  if (next_batch)
    self->full_state_loaded = TRUE;

  self->next_batch = g_strdup (next_batch);
}

/**
 * matrix_api_start_sync:
 * @self: A #MatrixApi
 *
 * Start synchronizing with the matrix server.
 *
 * If a sync process is already in progress
 * this function simply returns.
 *
 * The process is:
 *   1. Get home server (if required)
 *   2. Verify homeserver Server-Client API
 *   3. If access token set, start sync
 *   4. Else login with password
 */
void
matrix_api_start_sync (MatrixApi *self)
{
  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (self->callback);
  g_return_if_fail (self->username);
  g_return_if_fail (self->password || self->access_token);

  if (self->is_sync && !self->sync_failed)
    return;

  matrix_start_sync (self);
}

void
matrix_api_stop_sync (MatrixApi *self)
{
  g_return_if_fail (MATRIX_IS_API (self));

  g_cancellable_cancel (self->cancellable);
  self->is_sync = FALSE;
  self->sync_failed = FALSE;

  /* Free the cancellable and create a new
     one for further use */
  g_object_unref (self->cancellable);
  self->cancellable = g_cancellable_new ();
}

void
matrix_api_set_upload_key (MatrixApi *self,
                           char      *key)
{
  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (key && *key);

  g_free (self->key);
  self->key = key;

  if (self->is_sync && self->action == MATRIX_RED_PILL)
    matrix_upload_key (self);
}

void
matrix_api_set_typing (MatrixApi  *self,
                       const char *room_id,
                       gboolean    is_typing)
{
  g_autoptr(JsonObject) object = NULL;
  g_autofree char *uri = NULL;

  g_return_if_fail (MATRIX_IS_API (self));

  CHATTY_TRACE_MSG ("Update typing: %d", !!is_typing);
  /* https://matrix.org/docs/spec/client_server/r0.6.1#put-matrix-client-r0-rooms-roomid-typing-userid */
  object = json_object_new ();
  json_object_set_boolean_member (object, "typing", !!is_typing);
  if (is_typing)
    json_object_set_int_member (object, "timeout", TYPING_TIMEOUT);

  uri = g_strconcat (self->homeserver, "/_matrix/client/r0/rooms/",
                     room_id, "/typing/", self->username, NULL);

  queue_json_object (self, object, uri, SOUP_METHOD_PUT,
                     NULL, matrix_send_typing_cb, NULL);
}

void
matrix_api_get_room_state_async (MatrixApi           *self,
                                 const char          *room_id,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
  g_autofree char *uri = NULL;
  GTask *task;

  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (room_id && *room_id);

  task = g_task_new (self, self->cancellable, callback, user_data);

  uri = g_strconcat ("/_matrix/client/r0/rooms/", room_id, "/state", NULL);
  queue_data (self, NULL, 0, uri, SOUP_METHOD_GET,
              NULL, matrix_get_room_state_cb, task);
}

JsonArray *
matrix_api_get_room_state_finish (MatrixApi     *self,
                                  GAsyncResult  *result,
                                  GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_API (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);
  g_return_val_if_fail (!error || !*error, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

void
matrix_api_get_room_name_async (MatrixApi           *self,
                                const char          *room_id,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  g_autofree char *uri = NULL;
  GTask *task;

  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (room_id && *room_id);

  task = g_task_new (self, self->cancellable, callback, user_data);
  uri = g_strconcat ("/_matrix/client/r0/rooms/", room_id, "/state/m.room.name", NULL);
  queue_data (self, NULL, 0, uri, SOUP_METHOD_GET,
              NULL, matrix_get_room_name_cb, task);
}

JsonObject *
matrix_api_get_room_name_finish (MatrixApi     *self,
                                 GAsyncResult  *result,
                                 GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_API (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);
  g_return_val_if_fail (!error || !*error, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

void
matrix_api_get_members_async (MatrixApi           *self,
                              const char          *room_id,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autofree char *uri = NULL;
  GHashTable *query;

  g_return_if_fail (MATRIX_IS_API (self));

  task = g_task_new (self, self->cancellable, callback, user_data);

  query = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
  g_hash_table_insert (query, "membership", "join");

  if (self->next_batch)
    g_hash_table_insert (query, g_strdup ("since"), g_strdup (self->next_batch));

  /* https://matrix.org/docs/spec/client_server/r0.6.1#get-matrix-client-r0-rooms-roomid-members */
  uri = g_strconcat ("/_matrix/client/r0/rooms/", room_id, "/members", NULL);
  queue_data (self, NULL, 0, uri, SOUP_METHOD_GET,
              query, matrix_get_members_cb, g_steal_pointer (&task));
}

JsonObject *
matrix_api_get_members_finish (MatrixApi     *self,
                               GAsyncResult  *result,
                               GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_API (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);
  g_return_val_if_fail (!error || !*error, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

void
matrix_api_load_prev_batch_async (MatrixApi           *self,
                                  const char          *room_id,
                                  char                *prev_batch,
                                  char                *last_batch,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  g_autofree char *uri = NULL;
  GHashTable *query;
  GTask *task;

  if (!prev_batch)
    return;

  g_return_if_fail (MATRIX_IS_API (self));

  task = g_task_new (self, self->cancellable, callback, user_data);

  /* Create a query to get past 30 messages */
  query = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
  g_hash_table_insert (query, g_strdup ("from"), g_strdup (prev_batch));
  g_hash_table_insert (query, g_strdup ("dir"), g_strdup ("b"));
  g_hash_table_insert (query, g_strdup ("limit"), g_strdup ("30"));
  if (last_batch)
    g_hash_table_insert (query, g_strdup ("to"), g_strdup (last_batch));

  CHATTY_TRACE_MSG ("Load prev-batch");
  /* https://matrix.org/docs/spec/client_server/r0.6.1#get-matrix-client-r0-rooms-roomid-messages */
  uri = g_strconcat ("/_matrix/client/r0/rooms/", room_id, "/messages", NULL);
  queue_data (self, NULL, 0, uri, SOUP_METHOD_GET,
              query, matrix_get_messages_cb, task);
}

JsonObject *
matrix_api_load_prev_batch_finish (MatrixApi     *self,
                                   GAsyncResult  *result,
                                   GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_API (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);
  g_return_val_if_fail (!error || !*error, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

/**
 * matrix_api_query_keys_async:
 * @self: A #MatrixApi
 * @member_list: A #GListModel of #ChattyMaBuddy
 * @token: (nullable): A 'since' token string
 * @callback: A #GAsyncReadyCallback
 * @user_data: user data passed to @callback
 *
 * Get identity keys of all devices in @member_list.
 * Pass in @token (obtained via the "since" in /sync)
 * if only the device changes since the corresponding
 * /sync is needed.
 *
 * Finish the call with matrix_api_query_keys_finish()
 * to get the result.
 */
void
matrix_api_query_keys_async (MatrixApi           *self,
                             GListModel          *member_list,
                             const char          *token,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  JsonObject *object, *child;
  GTask *task;
  guint n_items;

  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (G_IS_LIST_MODEL (member_list));
  g_return_if_fail (g_list_model_get_item_type (member_list) == CHATTY_TYPE_MA_BUDDY);
  g_return_if_fail (g_list_model_get_n_items (member_list) > 0);

  /* https://matrix.org/docs/spec/client_server/r0.6.1#post-matrix-client-r0-keys-query */
  object = json_object_new ();
  json_object_set_int_member (object, "timeout", KEY_TIMEOUT);
  if (token)
    json_object_set_string_member (object, "token", token);

  n_items = g_list_model_get_n_items (member_list);
  child = json_object_new ();

  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyMaBuddy) buddy = NULL;

    buddy = g_list_model_get_item (member_list, i);
    json_object_set_array_member (child,
                                  chatty_ma_buddy_get_id (buddy),
                                  json_array_new ());
  }

  json_object_set_object_member (object, "device_keys", child);
  CHATTY_TRACE_MSG ("Query keys of %u members", n_items);

  task = g_task_new (self, self->cancellable, callback, user_data);

  queue_json_object (self, object, "/_matrix/client/r0/keys/query",
                     SOUP_METHOD_POST, NULL, matrix_keys_query_cb, task);
}

JsonObject *
matrix_api_query_keys_finish (MatrixApi    *self,
                              GAsyncResult *result,
                              GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_API (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);
  g_return_val_if_fail (!error || !*error, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

/**
 * matrix_api_claim_keys_async:
 * @self: A #MatrixApi
 * @member_list: A #GListModel of #ChattyMaBuddy
 * @callback: A #GAsyncReadyCallback
 * @user_data: user data passed to @callback
 *
 * Claim a key for all devices of @members_list
 */
void
matrix_api_claim_keys_async (MatrixApi           *self,
                             GListModel          *member_list,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  JsonObject *object, *child;
  GTask *task;

  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (G_IS_LIST_MODEL (member_list));
  g_return_if_fail (g_list_model_get_item_type (member_list) == CHATTY_TYPE_MA_BUDDY);
  g_return_if_fail (g_list_model_get_n_items (member_list) > 0);

  /* https://matrix.org/docs/spec/client_server/r0.6.1#post-matrix-client-r0-keys-claim */
  object = json_object_new ();
  json_object_set_int_member (object, "timeout", KEY_TIMEOUT);

  child = json_object_new ();

  for (guint i = 0; i < g_list_model_get_n_items (member_list); i++) {
    g_autoptr(ChattyMaBuddy) buddy = NULL;
    JsonObject *key_json;

    buddy = g_list_model_get_item (member_list, i);
    key_json = chatty_ma_buddy_device_key_json (buddy);

    if (key_json)
      json_object_set_object_member (child,
                                     chatty_ma_buddy_get_id (buddy),
                                     key_json);
  }

  json_object_set_object_member (object, "one_time_keys", child);
  CHATTY_TRACE_MSG ("Claiming keys");

  task = g_task_new (self, self->cancellable, callback, user_data);

  queue_json_object (self, object, "/_matrix/client/r0/keys/claim",
                     SOUP_METHOD_POST, NULL, matrix_keys_claim_cb, task);
}

JsonObject *
matrix_api_claim_keys_finish (MatrixApi     *self,
                              GAsyncResult  *result,
                              GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_API (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);
  g_return_val_if_fail (!error || !*error, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

void
matrix_api_get_file_async (MatrixApi             *self,
                           ChattyMessage         *message,
                           ChattyFileInfo        *file,
                           GCancellable          *cancellable,
                           GFileProgressCallback  progress_callback,
                           GAsyncReadyCallback    callback,
                           gpointer               user_data)
{
  g_autoptr(SoupMessage) msg = NULL;
  GTask *task;

  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (!message || CHATTY_IS_MESSAGE (message));

  CHATTY_TRACE_MSG ("Downloading file");

  if (message)
    g_object_ref (message);

  task = g_task_new (self, self->cancellable, callback, user_data);
  g_object_set_data (G_OBJECT (task), "progress", progress_callback);
  g_object_set_data (G_OBJECT (task), "file", file);
  g_object_set_data_full (G_OBJECT (task), "msg", msg, g_object_unref);
  g_object_set_data_full (G_OBJECT (task), "message", message, g_object_unref);

  if (file->status != CHATTY_FILE_UNKNOWN) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Download not required");
    return;
  }

  file->status = CHATTY_FILE_DOWNLOADING;
  if (message)
    chatty_message_emit_updated (message);

  msg = soup_message_new (SOUP_METHOD_GET, file->url);
  soup_session_send_async (self->soup_session, msg, self->cancellable,
                           (GAsyncReadyCallback)api_get_file_stream_cb,
                           g_steal_pointer (&task));
}

gboolean
matrix_api_get_file_finish (MatrixApi     *self,
                            GAsyncResult  *result,
                            GError       **error)
{
  CHATTY_ENTRY;

  g_return_val_if_fail (MATRIX_IS_API (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  CHATTY_RETURN (g_task_propagate_boolean (G_TASK (result), error));
}

static void
api_send_message_encrypted (MatrixApi     *self,
                            const char    *room_id,
                            ChattyMessage *message,
                            JsonObject    *content,
                            GTask         *task)
{
  g_autofree char *text = NULL;
  g_autofree char *uri = NULL;
  JsonObject *root;
  char *id;

  g_assert (MATRIX_IS_API (self));
  g_assert (content);

  root = json_object_new ();
  json_object_set_string_member (root, "type", "m.room.message");
  json_object_set_string_member (root, "room_id", room_id);
  json_object_set_object_member (root, "content", json_object_ref (content));

  text = matrix_utils_json_object_to_string (root, FALSE);
  json_object_unref (root);
  root = matrix_enc_encrypt_for_chat (self->matrix_enc, room_id, text);

  self->event_id++;
  id = g_strdup_printf ("m%"G_GINT64_FORMAT".%d",
                        g_get_real_time () / G_TIME_SPAN_MILLISECOND,
                        self->event_id);
  g_object_set_data_full (G_OBJECT (message), "event-id", id, g_free);

  g_object_set_data_full (G_OBJECT (task), "message", g_object_ref (message),
                          g_object_unref);
  CHATTY_TRACE_MSG ("Sending encrypted message. room: %s, event id: %s", room_id, id);

  uri = g_strdup_printf ("/_matrix/client/r0/rooms/%s/send/m.room.encrypted/%s", room_id, id);

  queue_json_object (self, root, uri, SOUP_METHOD_PUT,
                     NULL, api_send_message_cb, g_object_ref (task));
  json_object_unref (root);
}

static void
api_send_message (MatrixApi     *self,
                  const char    *room_id,
                  ChattyMessage *message,
                  JsonObject    *content,
                  GTask         *task)
{
  g_autofree char *uri = NULL;
  char *id;

  g_assert (MATRIX_IS_API (self));
  g_assert (content);

  self->event_id++;
  id = g_strdup_printf ("m%"G_GINT64_FORMAT".%d",
                        g_get_real_time () / G_TIME_SPAN_MILLISECOND,
                        self->event_id);
  g_object_set_data_full (G_OBJECT (message), "event-id", id, g_free);

  g_object_set_data_full (G_OBJECT (task), "message", g_object_ref (message),
                          g_object_unref);

  CHATTY_TRACE_MSG ("Sending message. room: %s, event id: %s", room_id, id);

  /* https://matrix.org/docs/spec/client_server/r0.6.1#put-matrix-client-r0-rooms-roomid-send-eventtype-txnid */
  uri = g_strdup_printf ("/_matrix/client/r0/rooms/%s/send/m.room.message/%s", room_id, id);
  queue_json_object (self, content, uri, SOUP_METHOD_PUT,
                     NULL, api_send_message_cb, g_object_ref (task));
}

void
matrix_api_send_message_async (MatrixApi           *self,
                               ChattyChat          *chat,
                               const char          *room_id,
                               ChattyMessage       *message,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
  g_autoptr(JsonObject) object = NULL;
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (CHATTY_IS_MESSAGE (message));

  task = g_task_new (self, self->cancellable, callback, user_data);
  object = json_object_new ();
  json_object_set_string_member (object, "msgtype", "m.text");
  json_object_set_string_member (object, "body", chatty_message_get_text (message));

  if (chatty_chat_get_encryption (chat) == CHATTY_ENCRYPTION_ENABLED)
    api_send_message_encrypted (self, room_id, message, object, task);
  else
    api_send_message (self, room_id, message, object, task);
}

gboolean
matrix_api_send_message_finish (MatrixApi     *self,
                                GAsyncResult  *result,
                                GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_API (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

void
matrix_api_set_read_marker_async (MatrixApi           *self,
                                  const char          *room_id,
                                  ChattyMessage       *message,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  g_autofree char *uri = NULL;
  JsonObject *root;
  const char *id;
  GTask *task;

  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (CHATTY_IS_MESSAGE (message));

  id = chatty_message_get_uid (message);
  root = json_object_new ();
  json_object_set_string_member (root, "m.fully_read", id);
  json_object_set_string_member (root, "m.read", id);

  CHATTY_TRACE_MSG ("Marking is read, message id: %s", id);

  task = g_task_new (self, self->cancellable, callback, user_data);

  uri = g_strdup_printf ("/_matrix/client/r0/rooms/%s/read_markers", room_id);
  queue_json_object (self, root, uri, SOUP_METHOD_POST,
                     NULL, api_set_read_marker_cb, task);
}

gboolean
matrix_api_set_read_marker_finish (MatrixApi     *self,
                                   GAsyncResult  *result,
                                   GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_API (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

void
matrix_api_upload_group_keys_async (MatrixApi           *self,
                                    const char          *room_id,
                                    GListModel          *member_list,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  g_autofree char *uri = NULL;
  JsonObject *root, *object;
  GTask *task;

  CHATTY_ENTRY;

  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (G_IS_LIST_MODEL (member_list));
  g_return_if_fail (g_list_model_get_item_type (member_list) == CHATTY_TYPE_MA_BUDDY);
  g_return_if_fail (g_list_model_get_n_items (member_list) > 0);

  root = json_object_new ();
  object = matrix_enc_create_out_group_keys (self->matrix_enc, room_id, member_list);
  json_object_set_object_member (root, "messages", object);

  task = g_task_new (self, self->cancellable, callback, user_data);

  self->event_id++;
  uri = g_strdup_printf ("/_matrix/client/r0/sendToDevice/m.room.encrypted/m%"G_GINT64_FORMAT".%d",
                         g_get_real_time () / G_TIME_SPAN_MILLISECOND,
                         self->event_id);
  queue_json_object (self, root, uri, SOUP_METHOD_PUT,
                     NULL, api_upload_group_keys_cb, task);
  CHATTY_EXIT;
}

gboolean
matrix_api_upload_group_keys_finish (MatrixApi     *self,
                                     GAsyncResult  *result,
                                     GError       **error)
{
  CHATTY_ENTRY;

  g_return_val_if_fail (MATRIX_IS_API (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  CHATTY_RETURN (g_task_propagate_boolean (G_TASK (result), error));
}

static void
api_leave_room_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  MatrixApi *self = (MatrixApi *)object;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;

  g_assert (MATRIX_IS_API (self));
  g_assert (G_IS_TASK (task));

  object = g_task_propagate_pointer (G_TASK (result), &error);

  CHATTY_TRACE_MSG ("Leave room. success: %d", !error);

  if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    g_debug ("Error leaving room: %s", error->message);

  if (error)
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, TRUE);
}

void
matrix_api_leave_chat_async (MatrixApi           *self,
                             const char          *room_id,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  GTask *task;
  g_autofree char *uri = NULL;

  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (room_id && *room_id == '!');

  CHATTY_TRACE_MSG ("Leaving room id: %s", room_id);

  task = g_task_new (self, self->cancellable, callback, user_data);
  g_task_set_task_data (task, g_strdup (room_id), g_free);
  uri = g_strdup_printf ("/_matrix/client/r0/rooms/%s/leave", room_id);
  queue_data (self, NULL, 0, uri, SOUP_METHOD_POST,
              NULL, api_leave_room_cb, task);
}

gboolean
matrix_api_leave_chat_finish (MatrixApi     *self,
                              GAsyncResult  *result,
                              GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_API (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
