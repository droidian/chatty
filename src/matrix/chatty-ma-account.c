/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-ma-account.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-ma-account"

#include <json-glib/json-glib.h>
#include <libsecret/secret.h>
#include <libsoup/soup.h>
#include <glib/gi18n.h>

#include "chatty-secret-store.h"
#include "chatty-history.h"
#include "matrix-api.h"
#include "matrix-enc.h"
#include "matrix-db.h"
#include "matrix-utils.h"
#include "chatty-utils.h"
#include "chatty-ma-chat.h"
#include "chatty-ma-account.h"
#include "chatty-log.h"

/**
 * SECTION: chatty-mat-account
 * @title: ChattyMaAccount
 * @short_description: An abstraction for Matrix accounts
 * @include: "chatty-mat-account.h"
 */

#define SYNC_TIMEOUT 30000 /* milliseconds */

struct _ChattyMaAccount
{
  ChattyAccount   parent_instance;

  char           *name;

  MatrixApi      *matrix_api;
  MatrixEnc      *matrix_enc;
  MatrixDb       *matrix_db;
  HdyValueObject *device_fp;

  ChattyHistory  *history_db;

  char           *pickle_key;
  char           *next_batch;

  GListStore     *chat_list;
  /* this will be moved to chat_list after login succeeds */
  GPtrArray      *db_chat_list;
  GdkPixbuf      *avatar;

  ChattyStatus   status;
  gboolean       homeserver_valid;
  gboolean       account_enabled;

  gboolean       is_loading;
  gboolean       save_account_pending;
  gboolean       save_password_pending;

  /* for sending events, incremented for each event */
  int            event_id;
  guint          connect_id;
};

G_DEFINE_TYPE (ChattyMaAccount, chatty_ma_account, CHATTY_TYPE_ACCOUNT)


static ChattyMaChat *
matrix_find_chat_with_id (ChattyMaAccount *self,
                          const char       *room_id,
                          guint            *index)
{
  guint n_items;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  if (!room_id || !*room_id)
    return NULL;

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->chat_list));
  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyMaChat) chat = NULL;

    chat = g_list_model_get_item (G_LIST_MODEL (self->chat_list), i);
    if (chatty_ma_chat_matches_id (chat, room_id)) {
      if (index)
        *index = i;

      return chat;
    }
  }

  return NULL;
}

static void
matrix_parse_device_data (ChattyMaAccount *self,
                          JsonObject      *to_device)
{
  JsonObject *object;
  JsonArray *array;
  guint length = 0;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));
  g_assert (to_device);

  array = matrix_utils_json_object_get_array (to_device, "events");
  if (array)
    length = json_array_get_length (array);

  if (length)
    CHATTY_TRACE_MSG ("Got %d to-device events", length);

  for (guint i = 0; i < length; i++) {
    const char *type;

    object = json_array_get_object_element (array, i);
    type = matrix_utils_json_object_get_string (object, "type");

    CHATTY_TRACE_MSG ("parsing to-device event, type: %s", type);

    if (g_strcmp0 (type, "m.room.encrypted") == 0)
      matrix_enc_handle_room_encrypted (self->matrix_enc, object);
  }
}

static void
matrix_parse_room_data (ChattyMaAccount *self,
                        JsonObject       *rooms)
{
  JsonObject *joined_rooms, *left_rooms;
  ChattyMaChat *chat;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));
  g_assert (rooms);

  joined_rooms = matrix_utils_json_object_get_object (rooms, "join");

  if (joined_rooms) {
    g_autoptr(GList) joined_room_ids = NULL;
    JsonObject *room_data;

    joined_room_ids = json_object_get_members (joined_rooms);

    for (GList *room_id = joined_room_ids; room_id; room_id = room_id->next) {
      guint index = 0;

      chat = matrix_find_chat_with_id (self, room_id->data, &index);
      room_data = matrix_utils_json_object_get_object (joined_rooms, room_id->data);

      CHATTY_TRACE_MSG ("joined room: %s, new: %d", room_id->data, !!chat);

      if (!chat) {
        chat = g_object_new (CHATTY_TYPE_MA_CHAT, "room-id", room_id->data, NULL);
        chatty_ma_chat_set_matrix_db (chat, self->matrix_db);
        chatty_ma_chat_set_history_db (chat, self->history_db);
        /* TODO */
        /* chatty_ma_chat_set_last_batch (chat, self->next_batch); */
        chatty_ma_chat_set_data (chat, CHATTY_ACCOUNT (self), self->matrix_api, self->matrix_enc);
        g_object_set (chat, "json-data", room_data, NULL);
        g_list_store_append (self->chat_list, chat);
        g_object_unref (chat);
      } else if (room_data) {
        g_object_set (chat, "json-data", room_data, NULL);
        g_list_model_items_changed (G_LIST_MODEL (self->chat_list), index, 1, 1);
      }
    }
  }

  left_rooms = matrix_utils_json_object_get_object (rooms, "leave");

  if (left_rooms) {
    g_autoptr(GList) left_room_ids = NULL;

    left_room_ids = json_object_get_members (left_rooms);

    for (GList *room_id = left_room_ids; room_id; room_id = room_id->next) {
      chat = matrix_find_chat_with_id (self, room_id->data, NULL);

      if (chat) {
        chatty_item_set_state (CHATTY_ITEM (chat), CHATTY_ITEM_HIDDEN);
        chatty_history_update_chat (self->history_db, CHATTY_CHAT (chat));
        chatty_utils_remove_list_item (self->chat_list, chat);
      }
    }
  }
}

static void
handle_get_homeserver (ChattyMaAccount *self,
                       JsonObject      *object,
                       GError          *error)
{
  CHATTY_ENTRY;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  if (error) {
    self->status = CHATTY_DISCONNECTED;
    g_object_notify (G_OBJECT (self), "status");
  }

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
    g_warning ("Couldn't connect to ‘/.well-known/matrix/client’ ");
    matrix_api_set_homeserver (self->matrix_api, "https://chat.librem.one");
  }

  CHATTY_EXIT;
}

static void
handle_verify_homeserver (ChattyMaAccount *self,
                          JsonObject      *object,
                          GError          *error)
{
  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  if (error) {
    self->status = CHATTY_DISCONNECTED;
    g_object_notify (G_OBJECT (self), "status");
  }
}

static void
handle_password_login (ChattyMaAccount *self,
                       JsonObject      *object,
                       GError          *error)
{
  CHATTY_ENTRY;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  /* If no error, Api is informing us that logging in succeeded.
   * Let’s update matrix_enc & set device keys to upload */
  if (g_error_matches (error, MATRIX_ERROR, M_BAD_PASSWORD)) {
    GtkWidget *dialog, *content, *header_bar;
    GtkWidget *cancel_btn, *entry;
    g_autofree char *label = NULL;
    const char *password;
    int response;

    dialog = gtk_dialog_new_with_buttons (_("Incorrect password"),
                                          gtk_application_get_active_window (GTK_APPLICATION (g_application_get_default ())),
                                          GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_USE_HEADER_BAR,
                                          _("_OK"), GTK_RESPONSE_ACCEPT,
                                          _("_Cancel"), GTK_RESPONSE_REJECT,
                                          NULL);

    content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    gtk_container_set_border_width (GTK_CONTAINER (content), 18);
    gtk_box_set_spacing (GTK_BOX (content), 12);
    label = g_strdup_printf (_("Please enter password for “%s”"),
                             matrix_api_get_username (self->matrix_api));
    gtk_container_add (GTK_CONTAINER (content), gtk_label_new (label));
    entry = gtk_entry_new ();
    gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);
    gtk_container_add (GTK_CONTAINER (content), entry);
    gtk_widget_show_all (content);

    header_bar = gtk_dialog_get_header_bar (GTK_DIALOG (dialog));
    gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (header_bar), FALSE);

    cancel_btn = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog),
                                                     GTK_RESPONSE_REJECT);
    g_object_ref (cancel_btn);
    gtk_container_remove (GTK_CONTAINER (header_bar), cancel_btn);
    gtk_header_bar_pack_start (GTK_HEADER_BAR (header_bar), cancel_btn);
    g_object_unref (cancel_btn);

    response = gtk_dialog_run (GTK_DIALOG (dialog));
    password = gtk_entry_get_text (GTK_ENTRY (entry));


    if (response != GTK_RESPONSE_ACCEPT || !password || !*password) {
      chatty_account_set_enabled (CHATTY_ACCOUNT (self), FALSE);
    } else {
      matrix_api_set_password (self->matrix_api, password);
      self->is_loading = TRUE;
      chatty_account_set_enabled (CHATTY_ACCOUNT (self), FALSE);
      self->is_loading = FALSE;
      chatty_account_set_enabled (CHATTY_ACCOUNT (self), TRUE);
    }

    gtk_widget_destroy (dialog);
  }

  if (!error) {
    self->save_password_pending = TRUE;
    chatty_account_save (CHATTY_ACCOUNT (self));

    self->status = CHATTY_CONNECTED;
    g_object_notify (G_OBJECT (self), "status");
  }

  CHATTY_EXIT;
}

static void
handle_upload_key (ChattyMaAccount *self,
                   JsonObject      *object,
                   GError          *error)
{
  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  if (object) {
    /* XXX: check later */
    matrix_enc_publish_one_time_keys (self->matrix_enc);

    self->save_account_pending = TRUE;
    chatty_account_save (CHATTY_ACCOUNT (self));
  }
}

static ChattyMaChat *
ma_account_find_chat (ChattyMaAccount *self,
                      const char      *room_id)
{
  GPtrArray *chats = self->db_chat_list;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  if (!room_id || !*room_id || !chats)
    return NULL;

  for (guint i = 0; i < chats->len; i++) {
    const char *chat_name;

    chat_name = chatty_chat_get_chat_name (chats->pdata[i]);
    if (g_strcmp0 (chat_name, room_id) == 0)
      return g_object_ref (chats->pdata[i]);
  }

  return NULL;
}

static void
handle_get_joined_rooms (ChattyMaAccount *self,
                         JsonObject      *object,
                         GError          *error)
{
  JsonArray *array;
  guint length = 0;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  array = matrix_utils_json_object_get_array (object, "joined_rooms");

  if (array)
    length = json_array_get_length (array);

  for (guint i = 0; i < length; i++) {
    g_autoptr(ChattyMaChat) chat = NULL;
    const char *room_id;

    room_id = json_array_get_string_element (array, i);
    chat = ma_account_find_chat (self, room_id);
    if (!chat)
      chat = g_object_new (CHATTY_TYPE_MA_CHAT, "room-id", room_id, NULL);
    chatty_ma_chat_set_matrix_db (chat, self->matrix_db);
    chatty_ma_chat_set_history_db (chat, self->history_db);
    chatty_ma_chat_set_data (chat, CHATTY_ACCOUNT (self), self->matrix_api, self->matrix_enc);
    g_list_store_append (self->chat_list, chat);
  }

  g_clear_pointer (&self->db_chat_list, g_ptr_array_unref);
}

static void
handle_red_pill (ChattyMaAccount *self,
                 JsonObject      *root,
                 GError          *error)
{
  JsonObject *object;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  if (error)
    return;

  if (self->status != CHATTY_CONNECTED) {
    self->status = CHATTY_CONNECTED;
    g_object_notify (G_OBJECT (self), "status");
  }

  object = matrix_utils_json_object_get_object (root, "to_device");
  if (object)
    matrix_parse_device_data (self, object);

  object = matrix_utils_json_object_get_object (root, "rooms");
  if (object)
    matrix_parse_room_data (self, object);

  self->save_account_pending = TRUE;
  chatty_account_save (CHATTY_ACCOUNT (self));
}

static void
matrix_account_sync_cb (ChattyMaAccount *self,
                        MatrixApi       *api,
                        MatrixAction     action,
                        JsonObject      *object,
                        GError          *error)
{
  g_assert (CHATTY_IS_MA_ACCOUNT (self));
  g_assert (MATRIX_IS_API (api));
  g_assert (self->matrix_api == api);

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return;

  if (error)
    g_debug ("%s Error %d: %s", g_quark_to_string (error->domain),
             error->code, error->message);

  if (error &&
      ((error->domain == SOUP_HTTP_ERROR &&
        error->code <= SOUP_STATUS_TLS_FAILED &&
        error->code > SOUP_STATUS_CANCELLED) ||
       g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NETWORK_UNREACHABLE) ||
       g_error_matches (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT) ||
       error->domain == G_RESOLVER_ERROR ||
       error->domain == JSON_PARSER_ERROR)) {
    self->status = CHATTY_DISCONNECTED;
    g_object_notify (G_OBJECT (self), "status");
    return;
  }

  switch (action) {
  case MATRIX_BLUE_PILL:
    return;

  case MATRIX_GET_HOMESERVER:
    handle_get_homeserver (self, object, error);
    return;

  case MATRIX_VERIFY_HOMESERVER:
    handle_verify_homeserver (self, object, error);
    return;

  case MATRIX_PASSWORD_LOGIN:
    handle_password_login (self, object, error);
    return;

  case MATRIX_UPLOAD_KEY:
    handle_upload_key (self, object, error);
    return;

  case MATRIX_GET_JOINED_ROOMS:
    handle_get_joined_rooms (self, object, error);
    return;

  case MATRIX_RED_PILL:
    handle_red_pill (self, object, error);
    return;

  case MATRIX_ACCESS_TOKEN_LOGIN:
  case MATRIX_SET_TYPING:
  case MATRIX_SEND_MESSAGE:
  case MATRIX_SEND_IMAGE:
  case MATRIX_SEND_VIDEO:
  case MATRIX_SEND_FILE:
  default:
    break;
  }
}

static const char *
chatty_ma_account_get_protocol_name (ChattyAccount *account)
{
  return "Matrix";
}

static ChattyStatus
chatty_ma_account_get_status (ChattyAccount *account)
{
  ChattyMaAccount *self = (ChattyMaAccount *)account;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  return self->status;
}

static const char *
chatty_ma_account_get_username (ChattyAccount *account)
{
  ChattyMaAccount *self = (ChattyMaAccount *)account;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  if (matrix_api_get_username (self->matrix_api))
    return matrix_api_get_username (self->matrix_api);

  return "";
}

static void
chatty_ma_account_set_username (ChattyAccount *account,
                                const char    *username)
{
  ChattyMaAccount *self = (ChattyMaAccount *)account;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  matrix_api_set_username (self->matrix_api, username);
}

static gboolean
chatty_ma_account_get_enabled (ChattyAccount *account)
{
  ChattyMaAccount *self = (ChattyMaAccount *)account;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  return self->account_enabled;
}

static void
chatty_ma_account_set_enabled (ChattyAccount *account,
                               gboolean       enable)
{
  ChattyMaAccount *self = (ChattyMaAccount *)account;
  GNetworkMonitor *network_monitor;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  if (self->account_enabled == enable)
    return;

  g_clear_handle_id (&self->connect_id, g_source_remove);

  if (!self->matrix_enc && enable) {
    CHATTY_TRACE_MSG ("Create new enc. user: %s has pickle: %d, has key: %d",
                      chatty_account_get_username (account), FALSE, FALSE);
    self->matrix_enc = matrix_enc_new (self->matrix_db, NULL, NULL);
    matrix_api_set_enc (self->matrix_api, self->matrix_enc);
  }

  self->account_enabled = enable;
  network_monitor = g_network_monitor_get_default ();
  CHATTY_TRACE_MSG ("Enable account %s: %d, is loading: %d",
                    chatty_account_get_username (account),
                    enable, self->is_loading);

  if (self->account_enabled &&
      g_network_monitor_get_connectivity (network_monitor) == G_NETWORK_CONNECTIVITY_FULL) {
    self->status = CHATTY_CONNECTING;
    matrix_api_start_sync (self->matrix_api);
  } else if (!self->account_enabled){
    self->status = CHATTY_DISCONNECTED;
    matrix_api_stop_sync (self->matrix_api);
  }

  g_object_notify (G_OBJECT (self), "enabled");
  g_object_notify (G_OBJECT (self), "status");

  if (!self->is_loading) {
    self->save_account_pending = TRUE;
    chatty_account_save (account);
  }
}

static const char *
chatty_ma_account_get_password (ChattyAccount *account)
{
  const char *password;
  ChattyMaAccount *self = (ChattyMaAccount *)account;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  password = matrix_api_get_password (self->matrix_api);

  if (password)
    return password;

  return "";
}

static void
chatty_ma_account_set_password (ChattyAccount *account,
                                const char    *password)
{
  ChattyMaAccount *self = (ChattyMaAccount *)account;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  if (g_strcmp0 (password, matrix_api_get_password (self->matrix_api)) == 0)
    return;

  matrix_api_set_password (self->matrix_api, password);

  if (matrix_api_get_homeserver (self->matrix_api)) {
    self->save_password_pending = TRUE;
    chatty_account_save (account);
  }
}

static gboolean
account_connect (gpointer user_data)
{
  g_autoptr(ChattyMaAccount) self = user_data;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  self->connect_id = 0;
  self->status = CHATTY_CONNECTING;
  matrix_api_start_sync (self->matrix_api);
  g_object_notify (G_OBJECT (self), "status");

  return G_SOURCE_REMOVE;
}

/* XXX: We always delay regardless of the value of @delay */
static void
chatty_ma_account_connect (ChattyAccount *account,
                           gboolean       delay)
{
  ChattyMaAccount *self = (ChattyMaAccount *)account;

  CHATTY_ENTRY;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  if (!chatty_account_get_enabled (account))
    CHATTY_EXIT;

  g_clear_handle_id (&self->connect_id, g_source_remove);
  self->connect_id = g_timeout_add (300, account_connect, g_object_ref (account));
  CHATTY_EXIT;
}

static void
chatty_ma_account_disconnect (ChattyAccount *account)
{
  ChattyMaAccount *self = (ChattyMaAccount *)account;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  self->status = CHATTY_DISCONNECTED;
  matrix_api_stop_sync (self->matrix_api);
  g_object_notify (G_OBJECT (self), "status");
}

static gboolean
chatty_ma_account_get_remember_password (ChattyAccount *self)
{
  /* password is always remembered */
  return TRUE;
}

static void
chatty_ma_account_save (ChattyAccount *account)
{
  ChattyMaAccount *self = (ChattyMaAccount *)account;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));
  g_return_if_fail (matrix_api_get_username (self->matrix_api));

  chatty_ma_account_save_async (self, FALSE, NULL, NULL, NULL);
}

static void
chatty_ma_account_delete (ChattyAccount *account)
{
  ChattyMaAccount *self = (ChattyMaAccount *)account;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));
}

static HdyValueObject *
chatty_ma_account_get_device_fp (ChattyAccount *account)
{
  ChattyMaAccount *self = (ChattyMaAccount *)account;
  const char *device_id;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  device_id = matrix_api_get_device_id (self->matrix_api);
  g_clear_object (&self->device_fp);

  if (!self->device_fp && device_id) {
    g_autoptr(GString) fp = NULL;
    const char *str;

    fp = g_string_new (NULL);
    str = matrix_enc_get_ed25519_key (self->matrix_enc);

    while (str && *str) {
      g_autofree char *chunk = g_strndup (str, 4);

      g_string_append_printf (fp, "%s ", chunk);
      str = str + strlen (chunk);
    }

    self->device_fp = hdy_value_object_new_string (fp->str);
    g_object_set_data_full (G_OBJECT (self->device_fp), "device-id",
                            g_strdup (device_id), g_free);
  }

  return self->device_fp;
}

static void
ma_account_leave_chat_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  ChattyMaAccount *self;
  g_autoptr(GTask) task = user_data;
  ChattyChat *chat;
  GError *error = NULL;
  gboolean success;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  chat = g_task_get_task_data (task);
  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  success = matrix_api_leave_chat_finish (self->matrix_api, result, &error);
  CHATTY_TRACE_MSG ("Leaving chat: %s(%s), success: %d",
                    chatty_item_get_name (CHATTY_ITEM (chat)),
                    chatty_chat_get_chat_name (chat),
                    success);

  if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    g_warning ("Error deleting chat: %s", error->message);

  /* Failed deleting from server, re-add in local chat list */
  if (!success) {
    ChattyItemState old_state;

    g_list_store_append (self->chat_list, chat);
    chatty_item_set_state (CHATTY_ITEM (chat), CHATTY_ITEM_HIDDEN);

    old_state = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "state"));
    chatty_item_set_state (CHATTY_ITEM (chat), old_state);
    chatty_history_update_chat (self->history_db, chat);
  }

  if (error)
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, success);
}

static void
chatty_ma_account_leave_chat_async (ChattyAccount       *account,
                                    ChattyChat          *chat,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  ChattyMaAccount *self = (ChattyMaAccount *)account;
  g_autoptr(GTask) task = NULL;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_task_data (task, g_object_ref (chat), g_object_unref);

  /* Remove the item so that it’s no longer listed in chat list */
  /* TODO: Handle edge case where the item was deleted from two
   * different sessions the same time */
  if (!chatty_utils_remove_list_item (self->chat_list, chat))
    g_return_if_reached ();

  CHATTY_TRACE_MSG ("Leaving chat: %s(%s)",
                    chatty_item_get_name (CHATTY_ITEM (chat)),
                    chatty_chat_get_chat_name (chat));

  g_object_set_data (G_OBJECT (task), "state",
                     GINT_TO_POINTER (chatty_item_get_state (CHATTY_ITEM (chat))));
  chatty_item_set_state (CHATTY_ITEM (chat), CHATTY_ITEM_HIDDEN);
  chatty_history_update_chat (self->history_db, chat);
  matrix_api_leave_chat_async (self->matrix_api,
                               chatty_chat_get_chat_name (chat),
                               ma_account_leave_chat_cb,
                               g_steal_pointer (&task));
}

static ChattyProtocol
chatty_ma_account_get_protocols (ChattyItem *item)
{
  return CHATTY_PROTOCOL_MATRIX;
}

static const char *
chatty_ma_account_get_name (ChattyItem *item)
{
  ChattyMaAccount *self = (ChattyMaAccount *)item;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  if (self->name)
    return self->name;

  return "";
}

static void
chatty_ma_account_set_name (ChattyItem *item,
                            const char *name)
{
  ChattyMaAccount *self = (ChattyMaAccount *)item;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  g_free (self->name);
  self->name = g_strdup (name);
}

static void
chatty_ma_account_finalize (GObject *object)
{
  ChattyMaAccount *self = (ChattyMaAccount *)object;

  g_clear_handle_id (&self->connect_id, g_source_remove);
  g_list_store_remove_all (self->chat_list);

  g_clear_object (&self->matrix_api);
  g_clear_object (&self->matrix_enc);
  g_clear_object (&self->device_fp);
  g_clear_object (&self->chat_list);
  g_clear_object (&self->avatar);
  g_clear_object (&self->matrix_db);
  g_clear_object (&self->history_db);
  g_clear_pointer (&self->db_chat_list, g_ptr_array_unref);

  g_free (self->name);
  g_free (self->pickle_key);
  g_free (self->next_batch);

  G_OBJECT_CLASS (chatty_ma_account_parent_class)->finalize (object);
}

static void
chatty_ma_account_class_init (ChattyMaAccountClass *klass)
{
  GObjectClass *object_class  = G_OBJECT_CLASS (klass);
  ChattyItemClass *item_class = CHATTY_ITEM_CLASS (klass);
  ChattyAccountClass *account_class = CHATTY_ACCOUNT_CLASS (klass);

  object_class->finalize = chatty_ma_account_finalize;

  item_class->get_protocols = chatty_ma_account_get_protocols;
  item_class->get_name = chatty_ma_account_get_name;
  item_class->set_name = chatty_ma_account_set_name;

  account_class->get_protocol_name = chatty_ma_account_get_protocol_name;
  account_class->get_status   = chatty_ma_account_get_status;
  account_class->get_username = chatty_ma_account_get_username;
  account_class->set_username = chatty_ma_account_set_username;
  account_class->get_enabled  = chatty_ma_account_get_enabled;
  account_class->set_enabled  = chatty_ma_account_set_enabled;
  account_class->get_password = chatty_ma_account_get_password;
  account_class->set_password = chatty_ma_account_set_password;
  account_class->connect      = chatty_ma_account_connect;
  account_class->disconnect   = chatty_ma_account_disconnect;
  account_class->get_remember_password = chatty_ma_account_get_remember_password;
  account_class->save = chatty_ma_account_save;
  account_class->delete = chatty_ma_account_delete;
  account_class->get_device_fp = chatty_ma_account_get_device_fp;
  account_class->leave_chat_async = chatty_ma_account_leave_chat_async;
}

static void
chatty_ma_account_init (ChattyMaAccount *self)
{
  self->chat_list = g_list_store_new (CHATTY_TYPE_MA_CHAT);

  self->matrix_api = matrix_api_new (NULL);
  matrix_api_set_sync_callback (self->matrix_api,
                                (MatrixCallback)matrix_account_sync_cb, self);
}

ChattyMaAccount *
chatty_ma_account_new (const char *username,
                       const char *password)
{
  ChattyMaAccount *self;

  g_return_val_if_fail (username, NULL);

  self = g_object_new (CHATTY_TYPE_MA_ACCOUNT, NULL);

  chatty_account_set_username (CHATTY_ACCOUNT (self), username);
  chatty_account_set_password (CHATTY_ACCOUNT (self), password);

  return self;
}

static char *
ma_account_get_value (const char *str,
                      const char *key)
{
  const char *start, *end;

  if (!str || !*str)
    return NULL;

  g_assert (key && *key);

  start = strstr (str, key);
  if (start) {
    start = start + strlen (key);
    while (*start && *start++ != '"')
      ;

    end = start - 1;
    do {
      end++;
      end = strchr (end, '"');
    } while (end && *(end - 1) == '\\' && *(end - 2) != '\\');

    if (end && end > start)
      return g_strndup (start, end - start);
  }

  return NULL;
}

ChattyMaAccount *
chatty_ma_account_new_secret (gpointer secret_item)
{
  ChattyMaAccount *self = NULL;
  g_autoptr(GHashTable) attributes = NULL;
  SecretItem *item = secret_item;
  SecretValue *value;
  const char *username, *homeserver, *credentials = NULL;
  char *password, *token, *device_id;
  char *password_str, *token_str = NULL;

  g_return_val_if_fail (SECRET_IS_ITEM (item), NULL);

  value = secret_item_get_secret (item);

  if (value)
    credentials = secret_value_get_text (value);

  if (!credentials)
    return NULL;

  attributes = secret_item_get_attributes (item);
  username = g_hash_table_lookup (attributes, CHATTY_USERNAME_ATTRIBUTE);
  homeserver = g_hash_table_lookup (attributes, CHATTY_SERVER_ATTRIBUTE);

  password = ma_account_get_value (credentials, "\"password\"");
  g_return_val_if_fail (password, NULL);
  password_str = g_strcompress (password);

  self = chatty_ma_account_new (username, password_str);
  token = ma_account_get_value (credentials, "\"access-token\"");
  device_id = ma_account_get_value (credentials, "\"device-id\"");
  chatty_ma_account_set_homeserver (self, homeserver);

  if (token)
    token_str = g_strcompress (token);

  if (token && device_id) {
    self->pickle_key = ma_account_get_value (credentials, "\"pickle-key\"");
    matrix_api_set_access_token (self->matrix_api, token_str, device_id);
  }

  matrix_utils_free_buffer (device_id);
  matrix_utils_free_buffer (password);
  matrix_utils_free_buffer (password_str);
  matrix_utils_free_buffer (token);
  matrix_utils_free_buffer (token_str);

  return self;
}

void
chatty_ma_account_set_history_db (ChattyMaAccount *self,
                                  gpointer         history_db)
{
  g_return_if_fail (CHATTY_IS_MA_ACCOUNT (self));
  g_return_if_fail (CHATTY_IS_HISTORY (history_db));
  g_return_if_fail (!self->history_db);

  self->history_db = g_object_ref (history_db);
}

static void
db_load_account_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  ChattyMaAccount *self = user_data;
  GTask *task = (GTask *)result;
  g_autoptr(GError) error = NULL;
  gboolean enabled;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));
  g_assert (G_IS_TASK (task));

  if (!self->matrix_enc) {
    const char *pickle;

    pickle = g_object_get_data (G_OBJECT (task), "pickle");
    CHATTY_TRACE_MSG ("Create new enc. user: %s has pickle: %d, has key: %d",
                      chatty_account_get_username (CHATTY_ACCOUNT (self)),
                      !!pickle, !!self->pickle_key);
    self->matrix_enc = matrix_enc_new (self->matrix_db, pickle, self->pickle_key);
    matrix_api_set_enc (self->matrix_api, self->matrix_enc);
    if (!pickle)
      matrix_api_set_access_token (self->matrix_api, NULL, NULL);
    g_clear_pointer (&self->pickle_key, matrix_utils_free_buffer);
  }

  if (!matrix_db_load_account_finish (self->matrix_db, result, &error)) {
    if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Error loading account %s: %s",
                 chatty_account_get_username (CHATTY_ACCOUNT (self)),
                 error->message);
    return;
  }

  enabled = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "enabled"));
  self->next_batch = g_strdup (g_object_get_data (G_OBJECT (task), "batch"));
  CHATTY_TRACE_MSG ("Loaded %s from db. enabled: %d, has next-batch: %d",
                    chatty_account_get_username (CHATTY_ACCOUNT (self)),
                    !!enabled, !!self->next_batch);

  self->is_loading = TRUE;
  matrix_api_set_next_batch (self->matrix_api, self->next_batch);
  chatty_account_set_enabled (CHATTY_ACCOUNT (self), enabled);
  self->is_loading = FALSE;
}

static void
db_load_chats_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  ChattyMaAccount *self = user_data;
  GTask *task = (GTask *)result;
  GPtrArray *chats = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (CHATTY_IS_MA_ACCOUNT (self));
  g_assert (G_IS_TASK (task));

  chats = chatty_history_get_chats_finish (self->history_db, result, &error);
  self->db_chat_list = chats;
  CHATTY_TRACE_MSG ("%s Loaded %u chats from db",
                    chatty_account_get_username (CHATTY_ACCOUNT (self)),
                    !chats ? 0 : chats->len);

  if (error)
    g_warning ("Error getting chats: %s", error->message);


  matrix_db_load_account_async (self->matrix_db, CHATTY_ACCOUNT (self),
                                matrix_api_get_device_id (self->matrix_api),
                                db_load_account_cb, self);
}

void
chatty_ma_account_set_db (ChattyMaAccount *self,
                          gpointer         matrix_db)
{
  g_return_if_fail (CHATTY_IS_MA_ACCOUNT (self));
  g_return_if_fail (MATRIX_IS_DB (matrix_db));
  g_return_if_fail (!self->matrix_db);
  g_return_if_fail (self->history_db);

  self->matrix_db = g_object_ref (matrix_db);
  chatty_history_get_chats_async (self->history_db, CHATTY_ACCOUNT (self),
                                  db_load_chats_cb, self);
}

static void
ma_account_db_save_cb (GObject      *object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  ChattyMaAccount *self;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;
  gboolean status;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  status = matrix_db_save_account_finish (self->matrix_db, result, &error);
  if (error || !status)
    CHATTY_TRACE_MSG ("Saving %s failed",
                      chatty_account_get_username (CHATTY_ACCOUNT (self)));

  if (error || !status)
    self->save_account_pending = TRUE;

  if (error)
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, status);
}

static void
ma_account_save_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  ChattyMaAccount *self;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;
  gboolean status;

  CHATTY_ENTRY;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  status = chatty_secret_store_save_finish (result, &error);

  if (error || !status)
    self->save_password_pending = TRUE;

  if (error) {
    g_task_return_error (task, error);
  } else if (self->save_account_pending) {
    char *pickle = NULL;

    if (matrix_api_get_access_token (self->matrix_api))
      pickle = matrix_enc_get_account_pickle (self->matrix_enc);

    self->save_account_pending = FALSE;
    matrix_db_save_account_async (self->matrix_db, CHATTY_ACCOUNT (self),
                                  chatty_account_get_enabled (CHATTY_ACCOUNT (self)),
                                  pickle,
                                  matrix_api_get_device_id (self->matrix_api),
                                  matrix_api_get_next_batch (self->matrix_api),
                                  ma_account_db_save_cb, g_steal_pointer (&task));
  } else {
    g_task_return_boolean (task, status);
  }

  CHATTY_EXIT;
}

void
chatty_ma_account_save_async (ChattyMaAccount     *self,
                              gboolean             force,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (CHATTY_IS_MA_ACCOUNT (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_return_if_fail (*chatty_account_get_username (CHATTY_ACCOUNT (self)));
  g_return_if_fail (*chatty_account_get_password (CHATTY_ACCOUNT (self)));
  g_return_if_fail (*chatty_ma_account_get_homeserver (self));

  task = g_task_new (self, cancellable, callback, user_data);
  if (self->save_password_pending || force) {
    char *key = NULL;

    if (self->matrix_enc && matrix_api_get_access_token (self->matrix_api))
      key = matrix_enc_get_pickle_key (self->matrix_enc);

    self->save_password_pending = FALSE;
    chatty_secret_store_save_async (CHATTY_ACCOUNT (self),
                                    g_strdup (matrix_api_get_access_token (self->matrix_api)),
                                    matrix_api_get_device_id (self->matrix_api),
                                    key, cancellable,
                                    ma_account_save_cb, task);
  } else if (self->save_account_pending) {
    char *pickle = NULL;

    if (matrix_api_get_access_token (self->matrix_api))
      pickle = matrix_enc_get_account_pickle (self->matrix_enc);

    self->save_account_pending = FALSE;
    matrix_db_save_account_async (self->matrix_db, CHATTY_ACCOUNT (self),
                                  chatty_account_get_enabled (CHATTY_ACCOUNT (self)),
                                  pickle,
                                  matrix_api_get_device_id (self->matrix_api),
                                  matrix_api_get_next_batch (self->matrix_api),
                                  ma_account_db_save_cb, task);
  }
}

gboolean
chatty_ma_account_save_finish (ChattyMaAccount  *self,
                               GAsyncResult     *result,
                               GError          **error)
{
  g_return_val_if_fail (CHATTY_IS_MA_ACCOUNT (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

const char *
chatty_ma_account_get_homeserver (ChattyMaAccount *self)
{
  const char *homeserver;

  g_return_val_if_fail (CHATTY_IS_MA_ACCOUNT (self), "");

  homeserver = matrix_api_get_homeserver (self->matrix_api);

  if (homeserver)
    return homeserver;

  return "";
}

void
chatty_ma_account_set_homeserver (ChattyMaAccount *self,
                                  const char      *server_url)
{
  g_return_if_fail (CHATTY_IS_MA_ACCOUNT (self));

  matrix_api_set_homeserver (self->matrix_api, server_url);
}

const char *
chatty_ma_account_get_device_id (ChattyMaAccount *self)
{
  const char *device_id;
  g_return_val_if_fail (CHATTY_IS_MA_ACCOUNT (self), "");

  device_id = matrix_api_get_device_id (self->matrix_api);

  if (device_id)
    return device_id;

  return "";
}

GListModel *
chatty_ma_account_get_chat_list (ChattyMaAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_MA_ACCOUNT (self), NULL);

  return G_LIST_MODEL (self->chat_list);
}

void
chatty_ma_account_send_file (ChattyMaAccount *self,
                             ChattyChat      *chat,
                             const char      *file_name)
{
  /* TODO */
}

static void
ma_get_details_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  ChattyMaAccount *self;
  g_autoptr(GTask) task = user_data;
  char *name, *avatar_url;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  matrix_api_get_user_info_finish (self->matrix_api,
                                   &name, &avatar_url,
                                   result, &error);

  if (error)
    g_task_return_error (task, error);
  else {
    g_free (self->name);
    self->name = name;

    g_object_notify (G_OBJECT (self), "name");
    g_task_return_boolean (task, TRUE);
  }
}

void
chatty_ma_account_get_details_async (ChattyMaAccount     *self,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (CHATTY_IS_MA_ACCOUNT (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);

  if (self->name)
    g_task_return_boolean (task, TRUE);
  else
    matrix_api_get_user_info_async (self->matrix_api, NULL, cancellable,
                                    ma_get_details_cb,
                                    g_steal_pointer (&task));
}

gboolean
chatty_ma_account_get_details_finish (ChattyMaAccount  *self,
                                      GAsyncResult     *result,
                                      GError          **error)
{
  g_return_val_if_fail (CHATTY_IS_MA_ACCOUNT (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
ma_set_name_cb (GObject      *object,
                GAsyncResult *result,
                gpointer      user_data)
{
  ChattyMaAccount *self;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  matrix_api_set_name_finish (self->matrix_api, result, &error);

  if (error)
    g_task_return_error (task, error);
  else {
    char *name;

    name = g_task_get_task_data (task);
    g_free (self->name);
    self->name = g_strdup (name);

    g_object_notify (G_OBJECT (self), "name");
    g_task_return_boolean (task, TRUE);
  }
}

void
chatty_ma_account_set_name_async (ChattyMaAccount     *self,
                                  const char          *name,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  GTask *task = NULL;

  g_return_if_fail (CHATTY_IS_MA_ACCOUNT (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_task_data (task, g_strdup (name), g_free);

  matrix_api_set_name_async (self->matrix_api, name, cancellable,
                             ma_set_name_cb, task);
}

gboolean
chatty_ma_account_set_name_finish (ChattyMaAccount  *self,
                                   GAsyncResult     *result,
                                   GError          **error)
{
  g_return_val_if_fail (CHATTY_IS_MA_ACCOUNT (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
ma_get_3pid_cb (GObject      *object,
                GAsyncResult *result,
                gpointer      user_data)
{
  ChattyMaAccount *self;
  g_autoptr(GTask) task = user_data;
  GPtrArray *emails, *phones;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  matrix_api_get_3pid_finish (self->matrix_api,
                              &emails, &phones,
                              result, &error);

  if (error)
    g_task_return_error (task, error);
  else {
    g_object_set_data_full (G_OBJECT (task), "email", emails,
                            (GDestroyNotify)g_ptr_array_unref);
    g_object_set_data_full (G_OBJECT (task), "phone", phones,
                            (GDestroyNotify)g_ptr_array_unref);

    g_task_return_boolean (task, TRUE);
  }
}

void
chatty_ma_account_get_3pid_async (ChattyMaAccount     *self,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (CHATTY_IS_MA_ACCOUNT (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);

  matrix_api_get_3pid_async (self->matrix_api, cancellable,
                             ma_get_3pid_cb,
                             g_steal_pointer (&task));
}

gboolean
chatty_ma_account_get_3pid_finish (ChattyMaAccount  *self,
                                   GPtrArray       **emails,
                                   GPtrArray       **phones,
                                   GAsyncResult     *result,
                                   GError          **error)
{
  g_return_val_if_fail (CHATTY_IS_MA_ACCOUNT (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  if (emails)
    *emails = g_object_steal_data (G_OBJECT (result), "email");
  if (phones)
    *phones = g_object_steal_data (G_OBJECT (result), "phone");

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
ma_delete_3pid_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  ChattyMaAccount *self;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (CHATTY_IS_MA_ACCOUNT (self));

  matrix_api_delete_3pid_finish (self->matrix_api, result, &error);

  if (error)
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, TRUE);
}

void
chatty_ma_account_delete_3pid_async (ChattyMaAccount     *self,
                                     const char          *value,
                                     ChattyIdType         type,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  GTask *task = NULL;

  g_return_if_fail (CHATTY_IS_MA_ACCOUNT (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);

  matrix_api_delete_3pid_async (self->matrix_api,
                                value, type, cancellable,
                                ma_delete_3pid_cb, task);
}

gboolean
chatty_ma_account_delete_3pid_finish (ChattyMaAccount  *self,
                                      GAsyncResult     *result,
                                      GError          **error)
{
  g_return_val_if_fail (CHATTY_IS_MA_ACCOUNT (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

void
chatty_ma_account_add_chat (ChattyMaAccount *self,
                            ChattyChat      *chat)
{
  g_return_if_fail (CHATTY_IS_MA_ACCOUNT (self));
  g_return_if_fail (CHATTY_IS_MA_CHAT (chat));

  chatty_ma_chat_set_data (CHATTY_MA_CHAT (chat), CHATTY_ACCOUNT (self),
                           self->matrix_api, self->matrix_enc);
  g_list_store_append (self->chat_list, chat);
}
