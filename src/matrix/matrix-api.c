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

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <olm/olm.h>
#include <sys/random.h>

#include "chatty-chat.h"
#include "chatty-ma-buddy.h"
#include "matrix-enums.h"
#include "matrix-utils.h"
#include "matrix-api.h"
#include "matrix-net.h"
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

  MatrixEnc      *matrix_enc;
  MatrixNet      *matrix_net;

  /* Executed for every request response */
  MatrixCallback  callback;
  gpointer        cb_object;
  GCancellable   *cancellable;
  char           *next_batch;
  MatrixAction    action;

  /* for sending events, incremented for each event */
  int             event_id;

  guint           full_state_loaded : 1;
  guint           is_sync : 1;
  /* Set when error occurs with sync enabled */
  guint           sync_failed : 1;
  guint           homeserver_verified : 1;
  guint           login_success : 1;
  guint           room_list_loaded : 1;

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
  g_autoptr(GError) error = NULL;

  g_assert (MATRIX_IS_API (self));

  root = matrix_utils_read_uri_finish (result, &error);

  if (!error)
    error = matrix_utils_json_node_get_error (root);

  /* Since GTask can't have timeout, We cancel the cancellable to fake timeout */
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
    g_clear_object (&self->cancellable);
    self->cancellable = g_cancellable_new ();
  }

  if (handle_common_errors (self, error))
    return;

  if (!root) {
    CHATTY_TRACE_MSG ("Error verifying home server: %s", error->message);
    self->callback (self->cb_object, self, self->action, NULL, error);
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
    error = g_error_new (MATRIX_ERROR, M_BAD_HOME_SERVER,
                         "Couldn't Verify Client-Server API to be "
                         "‘r0.5.0’ or ‘r0.6.0’ for %s", self->homeserver);
    self->callback (self->cb_object, self, self->action, NULL, error);
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
  g_autoptr(GError) error = NULL;
  char *homeserver;

  g_assert (MATRIX_IS_API (self));

  homeserver = matrix_utils_get_homeserver_finish (result, &error);

  CHATTY_TRACE_MSG ("Get home server, has-error: %d, home server: %s",
                    !error, homeserver);

  if (!homeserver) {
    self->sync_failed = TRUE;
    self->callback (self->cb_object, self, self->action, NULL, error);

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
api_get_file_cb (GObject      *object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  MatrixApi *self;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;
  gboolean status;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (MATRIX_IS_API (self));

  status = matrix_net_get_file_finish (self->matrix_net, result, &error);

  if (error) {
    g_task_return_error (task, error);

    return;
  }

  g_task_return_boolean (task, status);
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
api_set_read_marker_cb (GObject      *obj,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(JsonObject) object = NULL;
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

  CHATTY_TRACE_MSG ("Error: %s", error->message);

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
  g_autoptr(MatrixApi) self = user_data;
  g_autoptr(JsonObject) root = NULL;
  g_autoptr(GError) error = NULL;
  JsonObject *object = NULL;
  const char *value;

  g_assert (MATRIX_IS_API (self));
  g_assert (G_IS_TASK (result));

  root = g_task_propagate_pointer (G_TASK (result), &error);

  CHATTY_TRACE_MSG ("login complete. success: %d", !error);
  if (error) {
    self->sync_failed = TRUE;
    /* use a better code to inform invalid password */
    if (error->code == M_FORBIDDEN)
      error->code = M_BAD_PASSWORD;
    self->callback (self->cb_object, self, MATRIX_PASSWORD_LOGIN, NULL, error);
    g_debug ("Error logging in: %s", error->message);
    return;
  }

  self->login_success = TRUE;

  /* https://matrix.org/docs/spec/client_server/r0.6.1#post-matrix-client-r0-login */
  value = matrix_utils_json_object_get_string (root, "user_id");
  api_set_string_value (&self->username, value);

  value = matrix_utils_json_object_get_string (root, "access_token");
  matrix_utils_free_buffer (self->access_token);
  self->access_token = g_strdup (value);
  matrix_net_set_access_token (self->matrix_net, self->access_token);

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
  g_autoptr(MatrixApi) self = user_data;
  g_autoptr(JsonObject) root = NULL;
  g_autoptr(GError) error = NULL;
  JsonObject *object = NULL;

  g_assert (MATRIX_IS_API (self));
  g_assert (G_IS_TASK (result));

  root = g_task_propagate_pointer (G_TASK (result), &error);

  if (error) {
    self->sync_failed = TRUE;
    self->callback (self->cb_object, self, MATRIX_UPLOAD_KEY, NULL, error);
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
  g_autoptr(MatrixApi) self = user_data;
  g_autoptr(JsonObject) root = NULL;
  g_autoptr(GError) error = NULL;
  JsonObject *object = NULL;

  g_assert (MATRIX_IS_API (self));
  g_assert (G_IS_TASK (result));

  root = g_task_propagate_pointer (G_TASK (result), &error);

  if (!self->next_batch || error || !self->full_state_loaded)
    CHATTY_TRACE_MSG ("sync success: %d, full-state: %d, next-batch: %s",
                      !error, !self->full_state_loaded, self->next_batch);

  if (handle_common_errors (self, error))
    return;

  if (error) {
    self->sync_failed = TRUE;
    self->callback (self->cb_object, self, self->action, NULL, error);
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
  JsonObject *object, *child;

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

  matrix_net_send_json_async (self->matrix_net, 2, object,
                              "/_matrix/client/r0/login", SOUP_METHOD_POST,
                              NULL, NULL, matrix_login_cb,
                              g_object_ref (self));
}

static void
matrix_upload_key (MatrixApi *self)
{
  char *key;

  g_assert (MATRIX_IS_API (self));
  g_assert (self->key);

  key = g_steal_pointer (&self->key);

  matrix_net_send_data_async (self->matrix_net, 2, key, strlen (key),
                              "/_matrix/client/r0/keys/upload", SOUP_METHOD_POST,
                              NULL, NULL, matrix_upload_key_cb,
                              g_object_ref (self));
}

static void
get_joined_rooms_cb (GObject      *obj,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  g_autoptr(MatrixApi) self = user_data;
  g_autoptr(JsonObject) root = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (MATRIX_IS_API (self));
  g_assert (G_IS_TASK (result));

  CHATTY_TRACE_MSG ("Getting joined rooms, success: %d", !error);

  if (handle_common_errors (self, error))
    return;

  root = g_task_propagate_pointer (G_TASK (result), &error);

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

  CHATTY_TRACE_MSG ("Getting joined rooms");
  matrix_net_send_json_async (self->matrix_net, 0, NULL,
                              "/_matrix/client/r0/joined_rooms", SOUP_METHOD_GET,
                              NULL, NULL, get_joined_rooms_cb,
                              g_object_ref (self));
}

static void
matrix_start_sync (MatrixApi *self)
{
  g_assert (MATRIX_IS_API (self));

  self->is_sync = TRUE;
  self->sync_failed = FALSE;
  g_clear_handle_id (&self->resync_id, g_source_remove);

  if (!self->homeserver) {
    self->action = MATRIX_GET_HOMESERVER;
    if (!matrix_utils_username_is_complete (self->username)) {
      g_autoptr(GError) error = NULL;

      g_debug ("Error: No Homeserver provided");
      self->sync_failed = TRUE;
      error = g_error_new (MATRIX_ERROR, M_NO_HOME_SERVER, "No Homeserver provided");
      self->callback (self->cb_object, self, self->action, NULL, error);
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

  matrix_net_send_json_async (self->matrix_net, 2, NULL,
                              "/_matrix/client/r0/sync", SOUP_METHOD_GET,
                              query, self->cancellable, matrix_take_red_pill_cb,
                              g_object_ref (self));
}

static void
matrix_api_finalize (GObject *object)
{
  MatrixApi *self = (MatrixApi *)object;

  if (self->cancellable)
    g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  g_clear_object (&self->matrix_enc);
  g_clear_object (&self->matrix_net);

  g_clear_handle_id (&self->resync_id, g_source_remove);

  g_free (self->username);
  g_free (self->homeserver);
  g_free (self->device_id);
  matrix_utils_free_buffer (self->password);
  matrix_utils_free_buffer (self->access_token);

  g_free (self->next_batch);

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
  self->cancellable = g_cancellable_new ();
  self->matrix_net = matrix_net_new ();
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

  matrix_net_set_homeserver (self->matrix_net, self->homeserver);

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

  matrix_utils_free_buffer (self->access_token);
  g_free (self->device_id);

  self->access_token = g_strdup (access_token);
  self->device_id = g_strdup (device_id);
  matrix_net_set_access_token (self->matrix_net, self->access_token);

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
  g_autofree char *uri = NULL;
  JsonObject *object;

  g_return_if_fail (MATRIX_IS_API (self));

  CHATTY_TRACE_MSG ("Update typing: %d", !!is_typing);
  /* https://matrix.org/docs/spec/client_server/r0.6.1#put-matrix-client-r0-rooms-roomid-typing-userid */
  object = json_object_new ();
  json_object_set_boolean_member (object, "typing", !!is_typing);
  if (is_typing)
    json_object_set_int_member (object, "timeout", TYPING_TIMEOUT);

  uri = g_strconcat (self->homeserver, "/_matrix/client/r0/rooms/",
                     room_id, "/typing/", self->username, NULL);

  matrix_net_send_json_async (self->matrix_net, 0, object, uri, SOUP_METHOD_PUT,
                              NULL, self->cancellable, matrix_send_typing_cb, NULL);
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
  matrix_net_send_json_async (self->matrix_net, -1, NULL, uri, SOUP_METHOD_GET,
                              NULL, self->cancellable, matrix_get_room_state_cb, task);
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
  matrix_net_send_json_async (self->matrix_net, -1, NULL, uri, SOUP_METHOD_GET,
                              NULL, self->cancellable, matrix_get_room_name_cb, task);
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
  g_autofree char *uri = NULL;
  GHashTable *query;
  GTask *task;

  g_return_if_fail (MATRIX_IS_API (self));

  task = g_task_new (self, self->cancellable, callback, user_data);

  query = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                 (GDestroyNotify)matrix_utils_free_buffer);
  g_hash_table_insert (query, g_strdup ("membership"), g_strdup ("join"));

  if (self->next_batch)
    g_hash_table_insert (query, g_strdup ("since"), g_strdup (self->next_batch));

  /* https://matrix.org/docs/spec/client_server/r0.6.1#get-matrix-client-r0-rooms-roomid-members */
  uri = g_strconcat ("/_matrix/client/r0/rooms/", room_id, "/members", NULL);
  matrix_net_send_json_async (self->matrix_net, -1, NULL, uri, SOUP_METHOD_GET,
                              query, self->cancellable, matrix_get_members_cb, task);
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
  query = g_hash_table_new_full (g_str_hash, g_str_equal, free,
                                 (GDestroyNotify)matrix_utils_free_buffer);
  g_hash_table_insert (query, g_strdup ("from"), g_strdup (prev_batch));
  g_hash_table_insert (query, g_strdup ("dir"), g_strdup ("b"));
  g_hash_table_insert (query, g_strdup ("limit"), g_strdup ("30"));
  if (last_batch)
    g_hash_table_insert (query, g_strdup ("to"), g_strdup (last_batch));

  CHATTY_TRACE_MSG ("Load prev-batch");
  /* https://matrix.org/docs/spec/client_server/r0.6.1#get-matrix-client-r0-rooms-roomid-messages */
  uri = g_strconcat ("/_matrix/client/r0/rooms/", room_id, "/messages", NULL);
  matrix_net_send_json_async (self->matrix_net, 0, NULL, uri, SOUP_METHOD_GET,
                              query, self->cancellable, matrix_get_messages_cb, task);
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

  matrix_net_send_json_async (self->matrix_net, 0, object,
                              "/_matrix/client/r0/keys/query", SOUP_METHOD_POST,
                              NULL, self->cancellable, matrix_keys_query_cb, task);
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

  matrix_net_send_json_async (self->matrix_net, 0, object,
                              "/_matrix/client/r0/keys/claim", SOUP_METHOD_POST,
                              NULL, self->cancellable, matrix_keys_claim_cb, task);
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
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (!message || CHATTY_IS_MESSAGE (message));

  if (message)
    g_object_ref (message);

  task = g_task_new (self, cancellable, callback, user_data);

  g_object_set_data (G_OBJECT (task), "file", file);
  g_object_set_data_full (G_OBJECT (task), "message", message, g_object_unref);

  if (file->status != CHATTY_FILE_UNKNOWN) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Download not required");
    return;
  }

  matrix_net_get_file_async (self->matrix_net, message,
                             file, cancellable,
                             progress_callback,
                             api_get_file_cb,
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
  json_object_set_object_member (root, "content", content);

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

  matrix_net_send_json_async (self->matrix_net, 0, root, uri, SOUP_METHOD_PUT,
                              NULL, self->cancellable, api_send_message_cb, task);
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
  matrix_net_send_json_async (self->matrix_net, 0, content, uri, SOUP_METHOD_PUT,
                              NULL, self->cancellable, api_send_message_cb, task);
}

void
matrix_api_send_message_async (MatrixApi           *self,
                               ChattyChat          *chat,
                               const char          *room_id,
                               ChattyMessage       *message,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
  JsonObject *object;
  GTask *task;

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
  matrix_net_send_json_async (self->matrix_net, 0, root, uri, SOUP_METHOD_POST,
                              NULL, self->cancellable, api_set_read_marker_cb, task);
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
  matrix_net_send_json_async (self->matrix_net, 0, root, uri, SOUP_METHOD_PUT,
                              NULL, self->cancellable, api_upload_group_keys_cb, task);
}

gboolean
matrix_api_upload_group_keys_finish (MatrixApi     *self,
                                     GAsyncResult  *result,
                                     GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_API (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
api_leave_room_cb (GObject      *obj,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  MatrixApi *self;
  g_autoptr(GTask) task = user_data;
  g_autoptr(JsonObject) object = NULL;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (MATRIX_IS_API (self));

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
  matrix_net_send_json_async (self->matrix_net, 1, NULL, uri, SOUP_METHOD_POST,
                              NULL, self->cancellable, api_leave_room_cb, task);
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

static void
api_get_user_info_cb (GObject      *obj,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  MatrixApi *self;
  g_autoptr(GTask) task = user_data;
  const char *name, *avatar_url;
  GError *error = NULL;
  g_autoptr(JsonObject) object = NULL;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (MATRIX_IS_API (self));

  object = g_task_propagate_pointer (G_TASK (result), &error);

  CHATTY_TRACE_MSG ("Getting user info. success: %d", !error);

  if (error) {
    g_task_return_error (task, error);
    return;
  }

  name = matrix_utils_json_object_get_string (object, "displayname");
  avatar_url = matrix_utils_json_object_get_string (object, "avatar_url");

  g_object_set_data_full (G_OBJECT (task), "name", g_strdup (name), g_free);
  g_object_set_data_full (G_OBJECT (task), "avatar-url", g_strdup (avatar_url), g_free);

  g_task_return_boolean (task, TRUE);
}

void
matrix_api_get_user_info_async (MatrixApi           *self,
                                const char          *user_id,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  g_autofree char *uri = NULL;
  GTask *task;

  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  if (!user_id)
    user_id = self->username;

  CHATTY_TRACE_MSG ("Getting user info: %s", user_id);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_task_data (task, g_strdup (user_id), g_free);
  uri = g_strdup_printf ("/_matrix/client/r0/profile/%s", user_id);
  matrix_net_send_json_async (self->matrix_net, 1, NULL, uri, SOUP_METHOD_GET,
                              NULL, self->cancellable, api_get_user_info_cb, task);
}

gboolean
matrix_api_get_user_info_finish (MatrixApi     *self,
                                 char         **name,
                                 char         **avatar_url,
                                 GAsyncResult  *result,
                                 GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_API (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  *name = g_strdup (g_object_get_data (G_OBJECT (result), "name"));
  *avatar_url = g_strdup (g_object_get_data (G_OBJECT (result), "avatar-url"));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
api_set_name_cb (GObject      *obj,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  MatrixApi *self;
  g_autoptr(GTask) task = user_data;
  g_autoptr(JsonObject) object = NULL;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (MATRIX_IS_API (self));

  object = g_task_propagate_pointer (G_TASK (result), &error);

  CHATTY_TRACE_MSG ("Setting name success: %d", !error);

  if (error)
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, TRUE);
}

void
matrix_api_set_name_async (MatrixApi           *self,
                           const char          *name,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  g_autofree char *uri = NULL;
  JsonObject *root = NULL;
  GTask *task;

  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  CHATTY_TRACE_MSG ("Setting name to '%s' for user '%s'",
                    name, self->username);

  if (name && *name) {
    root = json_object_new ();
    json_object_set_string_member (root, "displayname", name);
  }

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_task_data (task, g_strdup (name), g_free);

  uri = g_strdup_printf ("/_matrix/client/r0/profile/%s/displayname", self->username);
  matrix_net_send_json_async (self->matrix_net, 1, root, uri, SOUP_METHOD_PUT,
                              NULL, self->cancellable, api_set_name_cb, task);
}

gboolean
matrix_api_set_name_finish (MatrixApi     *self,
                            GAsyncResult  *result,
                            GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_API (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
api_get_3pid_cb (GObject      *obj,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  MatrixApi *self;
  g_autoptr(GTask) task = user_data;
  GPtrArray *emails, *phones;
  GError *error = NULL;
  g_autoptr(JsonObject) object = NULL;
  JsonArray *array;
  guint length;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (MATRIX_IS_API (self));

  object = g_task_propagate_pointer (G_TASK (result), &error);
  array = matrix_utils_json_object_get_array (object, "threepids");

  CHATTY_TRACE_MSG ("Getting 3pid. success: %d", !error);

  if (!array) {
    if (error)
      g_task_return_error (task, error);
    else
      g_task_return_boolean (task, FALSE);

    return;
  }

  emails = g_ptr_array_new_full (1, g_free);
  phones = g_ptr_array_new_full (1, g_free);

  length = json_array_get_length (array);

  for (guint i = 0; i < length; i++) {
    const char *type, *value;

    object = json_array_get_object_element (array, i);
    value = matrix_utils_json_object_get_string (object, "address");
    type = matrix_utils_json_object_get_string (object, "medium");

    if (g_strcmp0 (type, "email") == 0)
      g_ptr_array_add (emails, g_strdup (value));
    else if (g_strcmp0 (type, "msisdn") == 0)
      g_ptr_array_add (phones, g_strdup (value));
  }

  g_object_set_data_full (G_OBJECT (task), "email", emails,
                          (GDestroyNotify)g_ptr_array_unref);
  g_object_set_data_full (G_OBJECT (task), "phone", phones,
                          (GDestroyNotify)g_ptr_array_unref);

  g_task_return_boolean (task, TRUE);
}

void
matrix_api_get_3pid_async (MatrixApi           *self,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  CHATTY_TRACE_MSG ("Getting user 3pid: %s", self->username);

  task = g_task_new (self, cancellable, callback, user_data);
  matrix_net_send_json_async (self->matrix_net, 1, NULL,
                              "/_matrix/client/r0/account/3pid", SOUP_METHOD_GET,
                              NULL, self->cancellable, api_get_3pid_cb, task);
}

gboolean
matrix_api_get_3pid_finish (MatrixApi     *self,
                            GPtrArray    **emails,
                            GPtrArray    **phones,
                            GAsyncResult  *result,
                            GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_API (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  if (emails)
    *emails = g_object_steal_data (G_OBJECT (result), "email");
  if (phones)
    *phones = g_object_steal_data (G_OBJECT (result), "phone");

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
api_delete_3pid_cb (GObject      *obj,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  MatrixApi *self;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;
  g_autoptr(JsonObject) object = NULL;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (MATRIX_IS_API (self));

  object = g_task_propagate_pointer (G_TASK (result), &error);

  CHATTY_TRACE_MSG ("Deleting 3pid. success: %d", !error);

  if (error) {
    g_task_return_error (task, error);
    return;
  }

  g_warning ("%s", matrix_utils_json_object_to_string (object, TRUE));
  g_task_return_boolean (task, TRUE);
}

void
matrix_api_delete_3pid_async (MatrixApi           *self,
                              const char          *value,
                              ChattyIdType         type,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  JsonObject *root;
  GTask *task;

  g_return_if_fail (MATRIX_IS_API (self));
  g_return_if_fail (value && *value);
  g_return_if_fail (type == CHATTY_ID_EMAIL || type == CHATTY_ID_PHONE);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  CHATTY_TRACE_MSG ("Deleting 3pid: %s", value);

  root = json_object_new ();
  json_object_set_string_member (root, "address", value);
  if (type == CHATTY_ID_PHONE)
    json_object_set_string_member (root, "medium", "msisdn");
  else
    json_object_set_string_member (root, "medium", "email");

  task = g_task_new (self, cancellable, callback, user_data);
  g_object_set_data (G_OBJECT (task), "type", GINT_TO_POINTER (type));
  g_object_set_data_full (G_OBJECT (task), "value",
                          g_strdup (value), g_free);

  matrix_net_send_json_async (self->matrix_net, 2, root,
                              "/_matrix/client/r0/account/3pid/delete", SOUP_METHOD_POST,
                              NULL, cancellable, api_delete_3pid_cb, task);
}

gboolean
matrix_api_delete_3pid_finish (MatrixApi    *self,
                               GAsyncResult *result,
                               GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_API (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
