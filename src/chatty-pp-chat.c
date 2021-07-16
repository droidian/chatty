/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-chat.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-chat"

#include <purple.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "contrib/gtk.h"
#include "chatty-history.h"
#include "chatty-settings.h"
#include "chatty-utils.h"
#include "users/chatty-pp-buddy.h"
#include "users/chatty-pp-account.h"
#include "chatty-manager.h"
#include "chatty-pp-chat.h"
#include "chatty-log.h"

#define CHATTY_COLOR_BLUE "4A8FD9"

enum {
  LURCH_STATUS_DISABLED = 0,  /* manually disabled */
  LURCH_STATUS_NOT_SUPPORTED, /* no OMEMO support, i.e. there is no devicelist node */
  LURCH_STATUS_NO_SESSION,    /* OMEMO is supported, but there is no libsignal session yet */
  LURCH_STATUS_OK             /* OMEMO is supported and session exists */
};

/**
 * SECTION: chatty-chat
 * @title: ChattyChat
 * @short_description: An abstraction over #PurpleConversation
 * @include: "chatty-chat.h"
 *
 * libpurple doesn’t have a nice OOP interface for managing anything.
 * This class hides all the complexities surrounding it.
 */

struct _ChattyPpChat
{
  ChattyChat          parent_instance;

  ChattyPpAccount    *pp_account;
  ChattyHistory      *history;

  PurpleAccount      *account;
  PurpleBuddy        *buddy;
  char               *username;

  PurpleChat         *pp_chat;
  PurpleConversation *conv;
  GListStore         *chat_users;
  GtkSortListModel   *sorted_chat_users;
  GListStore         *message_store;
  GListStore         *fp_list;

  char               *last_message;
  char               *chat_name;
  guint               unread_count;
  guint               last_msg_time;
  ChattyEncryption    encrypt;
  gboolean            buddy_typing;
  gboolean            initial_history_loaded;
  gboolean            history_is_loading;
  gboolean            supports_encryption;

  gboolean            file_upload_checked;
  void (*upload_file_callback)(gpointer, gpointer);
  gpointer            callback_userdata;
};

G_DEFINE_TYPE (ChattyPpChat, chatty_pp_chat, CHATTY_TYPE_CHAT)

static char *
jabber_id_strip_resource (const char *name)
{
  g_auto(GStrv) split = NULL;
  char *stripped;

  split = g_strsplit (name, "/", -1);
  stripped = g_strdup (split[0]);

  return stripped;
}

static void
emit_avatar_changed (ChattyPpChat *self)
{
  g_assert (CHATTY_IS_PP_CHAT (self));

  g_signal_emit_by_name (self, "avatar-changed");
}

static GdkPixbuf *
chatty_pp_chat_get_buddy_icon (PurpleBlistNode *node)
{
  PurpleStoredImage *custom_img;
  const guchar *data = NULL;
  gsize len;

  custom_img = purple_buddy_icons_node_find_custom_icon (node);

  if (custom_img) {
    data = purple_imgstore_get_data (custom_img);
    len = purple_imgstore_get_size (custom_img);
  }

  purple_imgstore_unref (custom_img);

  if (data != NULL)
    return chatty_utils_get_pixbuf_from_data (data, len);

  return NULL;
}

static void
pp_chat_add_history_since_component (ChattyPpChat *self,
                                     GHashTable   *components,
                                     const char   *account,
                                     const char   *room)
{
  time_t mtime;
  struct tm * timeinfo;
  char *iso_timestamp = g_malloc0 (MAX_GMT_ISO_SIZE * sizeof (char));

  mtime = chatty_history_get_last_message_time (self->history, account, room);
  mtime += 1; // Use the next epoch to exclude the last stored message(s)
  timeinfo = gmtime (&mtime);
  g_return_if_fail (strftime (iso_timestamp, MAX_GMT_ISO_SIZE * sizeof(char),
                              "%Y-%m-%dT%H:%M:%SZ", timeinfo));

  g_hash_table_steal (components, "history_since");
  g_hash_table_insert (components, "history_since", iso_timestamp);
}

static void
pp_chat_setup_file_upload (ChattyPpChat *self)
{
  PurplePluginProtocolInfo *prpl_info = NULL;
  PurpleConnection *gc;
  PurpleBlistNode *node = NULL;
  g_autoptr(GList) list = NULL;

  g_assert (CHATTY_IS_PP_CHAT (self));

  gc = purple_conversation_get_gc (self->conv);
  if (gc)
    node = chatty_utils_get_conv_blist_node (self->conv);
  if (node)
    prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO (gc->prpl);

  if (!prpl_info)
    return;

  self->file_upload_checked = TRUE;
  if (prpl_info->blist_node_menu)
    list = prpl_info->blist_node_menu (node);

  for (GList *l = list; l; l = l->next) {
    PurpleMenuAction *act = l->data;

    if (g_strcmp0 (act->label, "HTTP File Upload") == 0) {
      self->upload_file_callback = (gpointer)act->callback;
      self->callback_userdata = act->data;
      purple_menu_action_free (act);
      break;
    }

    purple_menu_action_free (act);
  }
}

static void
list_fingerprints_cb (int         error,
                      GHashTable *fp_table,
                      gpointer    user_data)
{
  g_autoptr(GTask)task = user_data;
  g_autoptr(GList) key_list = NULL;
  ChattyPpChat *self;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (CHATTY_IS_PP_CHAT (self));

  if (error || !fp_table) {
    CHATTY_DEBUG_MSG ("No fingerprints. has error: %d", !!error);
    g_task_return_boolean (task, FALSE);

    return;
  }

  key_list = g_hash_table_get_keys (fp_table);

  for (GList *item = key_list; item; item = item->next) {
    g_autoptr(HdyValueObject) object = NULL;
    const char *fp;
    char *id;

    fp = g_hash_table_lookup (fp_table, item->data);
    g_debug ("DeviceId: %i fingerprint: %s", *((guint32 *) item->data),
             fp ? fp : "(no session)");

    if (!fp)
      continue;

    object = hdy_value_object_new_string (fp);
    id = g_strdup_printf ("%u", *((guint32 *) item->data));
    g_object_set_data_full (G_OBJECT (object), "device-id", id, g_free);
    g_list_store_append (self->fp_list, object);
  }

  g_task_return_boolean (task, TRUE);
}

static void
chatty_pp_chat_set_purple_chat (ChattyPpChat *self,
                                PurpleChat   *chat)
{
  PurpleBlistNode *node;

  g_assert (CHATTY_IS_PP_CHAT (self));
  g_assert (chat);

  self->pp_chat = chat;

  if (!chat)
    return;

  node = PURPLE_BLIST_NODE (chat);
  node->ui_data = self;
  g_object_add_weak_pointer (G_OBJECT (self), (gpointer *)&node->ui_data);
}

static void
chatty_pp_chat_set_purple_buddy (ChattyPpChat *self,
                                 PurpleBuddy  *buddy)
{
  PurpleBlistNode *node;

  g_assert (CHATTY_IS_PP_CHAT (self));
  g_assert (buddy);

  self->buddy = buddy;

  if (!buddy)
    return;

  node = PURPLE_BLIST_NODE (buddy);

  if (!node->ui_data)
    return;

  g_object_set_data (node->ui_data, "chat", self);
  g_signal_handlers_disconnect_by_func (node->ui_data,
                                        emit_avatar_changed,
                                        self);
  g_signal_connect_object (node->ui_data, "avatar-changed",
                           G_CALLBACK (emit_avatar_changed),
                           self,
                           G_CONNECT_SWAPPED);
}

static gboolean
chatty_pp_chat_has_encryption_support (ChattyPpChat *self)
{
  ChattyProtocol protocol;
  PurpleConversationType type = PURPLE_CONV_TYPE_UNKNOWN;

  g_assert (CHATTY_IS_PP_CHAT (self));

  protocol = chatty_item_get_protocols (CHATTY_ITEM (self));

  if (protocol == CHATTY_PROTOCOL_XMPP &&
      !self->supports_encryption)
    return FALSE;

  if (self->conv)
    type = purple_conversation_get_type (self->conv);

  /* Currently we support only XMPP IM chats */
  if (self->conv &&
      type == PURPLE_CONV_TYPE_IM &&
      protocol == CHATTY_PROTOCOL_XMPP)
    return TRUE;

  return FALSE;
}

static gint
sort_chat_buddy (ChattyPpBuddy *a,
                 ChattyPpBuddy *b)
{
  ChattyUserFlag flag_a, flag_b;

  flag_a = chatty_pp_buddy_get_flags (a);
  flag_b = chatty_pp_buddy_get_flags (b);

  flag_a = flag_a & (CHATTY_USER_FLAG_MEMBER | CHATTY_USER_FLAG_MODERATOR | CHATTY_USER_FLAG_OWNER);
  flag_b = flag_b & (CHATTY_USER_FLAG_MEMBER | CHATTY_USER_FLAG_MODERATOR | CHATTY_USER_FLAG_OWNER);

  if (flag_a == flag_b)
    return chatty_item_compare (CHATTY_ITEM (a), CHATTY_ITEM (b));

  if (flag_a > flag_b)
    return -1;

  /* @a should be after @b */
  return 1;
}

static void
chatty_pp_chat_lurch_changed_cb (int      err,
                                 gpointer user_data)
{
  g_autoptr(ChattyPpChat) self = user_data;

  g_assert (CHATTY_IS_PP_CHAT (self));

  if (err) {
    g_warning ("Failed to change OMEMO encryption.");
    return;
  }

  chatty_pp_chat_load_encryption_status (self);
}

static void
lurch_status_changed_cb (int      err,
                         int      status,
                         gpointer user_data)
{
  g_autoptr(ChattyPpChat) self = user_data;

  g_assert (CHATTY_IS_PP_CHAT (self));

  if (err) {
    g_debug ("Failed to get the OMEMO status.");
    return;
  }

  switch (status) {
  case LURCH_STATUS_OK:
    self->encrypt = CHATTY_ENCRYPTION_ENABLED;
    break;

  case LURCH_STATUS_DISABLED:
  case LURCH_STATUS_NO_SESSION:
    self->encrypt = CHATTY_ENCRYPTION_DISABLED;
    break;

  default:
    self->encrypt = CHATTY_ENCRYPTION_UNSUPPORTED;
    break;
  }

  g_object_notify (G_OBJECT (self), "encrypt");
}

static ChattyPpBuddy *
chat_find_user (ChattyPpChat *self,
                const char   *user,
                guint        *index)
{
  guint n_items;

  g_assert (CHATTY_IS_PP_CHAT (self));

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->chat_users));
  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyPpBuddy) buddy = NULL;

    buddy = g_list_model_get_item (G_LIST_MODEL (self->chat_users), i);
    if (chatty_pp_buddy_get_id (buddy) == user) {
      if (index)
        *index = i;

      return buddy;
    }
  }

  return NULL;
}

static void
pp_chat_load_db_messages_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  g_autoptr(ChattyPpChat) self = user_data;
  g_autoptr(GPtrArray) messages = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (CHATTY_IS_PP_CHAT (self));

  messages = chatty_history_get_messages_finish (self->history, result, &error);
  self->history_is_loading = FALSE;
  g_object_notify (G_OBJECT (self), "loading-history");

  if (!error && !messages && !self->initial_history_loaded)
    chatty_pp_chat_set_show_notifications (self, TRUE);

  self->initial_history_loaded = TRUE;

  if (messages && messages->len &&
      chatty_pp_chat_get_auto_join (self)) {
    g_list_store_splice (self->message_store, 0, 0, messages->pdata, messages->len);
    g_signal_emit_by_name (self, "changed", 0);
  } else if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
   g_warning ("Error fetching messages: %s,", error->message);
 }
}

static void
chatty_pp_chat_set_data (ChattyChat *chat,
                         gpointer    account,
                         gpointer    history)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;

  g_assert (CHATTY_IS_PP_CHAT (self));
  g_return_if_fail (!account || CHATTY_PP_ACCOUNT (account));
  g_assert (CHATTY_IS_HISTORY (history));

  g_set_object (&self->pp_account, account);
  g_set_object (&self->history, history);
}

static gboolean
chatty_pp_chat_is_im (ChattyChat *chat)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;
  PurpleConversationType type = PURPLE_CONV_TYPE_UNKNOWN;

  g_assert (CHATTY_IS_PP_CHAT (self));

  if (self->buddy)
    return TRUE;

  if (self->conv)
    type = purple_conversation_get_type (self->conv);

  return type == PURPLE_CONV_TYPE_IM;
}

static gboolean
chatty_pp_chat_has_file_upload (ChattyChat *chat)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;

  g_assert (CHATTY_IS_PP_CHAT (self));

  if (!self->file_upload_checked)
    pp_chat_setup_file_upload (self);

  return self->upload_file_callback != NULL;
}

static const char *
chatty_pp_chat_get_chat_name (ChattyChat *chat)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;
  const char *chat_name = NULL;

  g_assert (CHATTY_IS_PP_CHAT (self));

  if (self->chat_name)
    return self->chat_name;

  if (self->conv)
    chat_name = purple_conversation_get_name (self->conv);
  else if (self->buddy)
    chat_name = purple_buddy_get_name (self->buddy);

  if (chat_name)
    self->chat_name = chatty_utils_jabber_id_strip (chat_name);

  if (self->chat_name)
    return self->chat_name;

  return "";
}

static const char *
chatty_pp_chat_get_username (ChattyChat *chat)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;
  const char *username = NULL;

  g_assert (CHATTY_IS_PP_CHAT (self));

  if (self->pp_chat)
    username = purple_account_get_username (self->pp_chat->account);

  if (self->buddy)
    username = purple_account_get_username (self->buddy->account);

  if (self->conv)
    username = purple_account_get_username (self->conv->account);

  if (username && *username && !self->username)
    self->username = chatty_utils_jabber_id_strip (username);

  if (self->username && *self->username)
    return self->username;

  return "";
}

static ChattyAccount *
chatty_pp_chat_get_account (ChattyChat *chat)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;
  PurpleAccount *account;

  g_assert (CHATTY_IS_PP_CHAT (self));

  if (self->account)
    account = self->account;
  else if (self->conv)
    account = self->conv->account;
  else if (self->buddy)
    account = self->buddy->account;
  else if (self->pp_chat)
    account = self->pp_chat->account;
  else
    return NULL;

  return account->ui_data;
}

static void
chatty_pp_chat_load_past_messages (ChattyChat *chat,
                                   int         count)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;
  GListModel *model;

  g_assert (CHATTY_IS_PP_CHAT (self));
  g_assert (count > 0);

  if (self->history_is_loading)
    return;

  self->history_is_loading = TRUE;
  g_object_notify (G_OBJECT (self), "loading-history");

  model = chatty_chat_get_messages (chat);

  chatty_history_get_messages_async (self->history, chat,
                                     g_list_model_get_item (model, 0),
                                     count, pp_chat_load_db_messages_cb,
                                     g_object_ref (self));
}

static gboolean
chatty_pp_chat_is_loading_history (ChattyChat *chat)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;

  g_assert (CHATTY_IS_PP_CHAT (self));

  return self->history_is_loading;
}

static GListModel *
chatty_pp_chat_get_messages (ChattyChat *chat)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;

  g_assert (CHATTY_IS_PP_CHAT (self));

  return G_LIST_MODEL (self->message_store);
}

static GListModel *
chatty_pp_chat_get_users (ChattyChat *chat)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;

  g_assert (CHATTY_IS_PP_CHAT (self));

  return G_LIST_MODEL (self->sorted_chat_users);
}

static const char *
chatty_pp_chat_get_topic (ChattyChat *chat)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;

  g_assert (CHATTY_IS_PP_CHAT (self));

  if (!self->conv)
    return "";

  return purple_conv_chat_get_topic (PURPLE_CONV_CHAT (self->conv));
}

static void
chatty_pp_chat_set_topic (ChattyChat *chat,
                          const char *topic)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;
  PurplePluginProtocolInfo *prpl_info = NULL;
  PurpleConnection *gc;
  int chat_id;

  g_assert (CHATTY_IS_PP_CHAT (self));

  if (!self->conv)
    return;

  gc = purple_conversation_get_gc (self->conv);
  prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO (gc->prpl);

  if (!gc || !prpl_info ||
      !prpl_info->set_chat_topic)
    return;

  chat_id = purple_conv_chat_get_id (PURPLE_CONV_CHAT (self->conv));
  prpl_info->set_chat_topic (gc, chat_id, topic);
}

static const char *
chatty_pp_chat_get_last_message (ChattyChat *chat)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;
  g_autoptr(ChattyMessage) message = NULL;
  GListModel *model;
  guint n_items;

  g_assert (CHATTY_IS_PP_CHAT (self));

  model = G_LIST_MODEL (self->message_store);
  n_items = g_list_model_get_n_items (model);

  if (n_items == 0)
    return "";

  message = g_list_model_get_item (model, n_items - 1);

  return chatty_message_get_text (message);
}

static guint
chatty_pp_chat_get_unread_count (ChattyChat *chat)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;

  g_assert (CHATTY_IS_PP_CHAT (self));

  return self->unread_count;
}

static void
chatty_pp_chat_set_unread_count (ChattyChat *chat,
                                 guint       unread_count)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;

  g_assert (CHATTY_IS_PP_CHAT (self));

  if (self->unread_count == unread_count)
    return;

  self->unread_count = unread_count;
  g_signal_emit_by_name (self, "changed", 0);
}

static time_t
chatty_pp_chat_get_last_msg_time (ChattyChat *chat)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;
  g_autoptr(ChattyMessage) message = NULL;
  GListModel *model;
  guint n_items;

  g_assert (CHATTY_IS_PP_CHAT (self));

  model = G_LIST_MODEL (self->message_store);
  n_items = g_list_model_get_n_items (model);

  if (n_items == 0)
    return 0;

  message = g_list_model_get_item (model, n_items - 1);

  return chatty_message_get_time (message);
}

static void
chatty_pp_chat_send_message_async (ChattyChat          *chat,
                                   ChattyMessage       *message,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;
  g_autoptr(GTask) task = NULL;
  const char *msg;

  g_assert (CHATTY_IS_PP_CHAT (self));
  g_assert (CHATTY_IS_MESSAGE (message));

  task = g_task_new (self, NULL, callback, user_data);

  if (!self->conv) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED,
                             "PurpleConversation not found");
    return;
  }

  msg = chatty_message_get_text (message);

  if (purple_conversation_get_type (self->conv) == PURPLE_CONV_TYPE_IM)
    purple_conv_im_send (PURPLE_CONV_IM (self->conv), msg);
  else if (purple_conversation_get_type (self->conv) == PURPLE_CONV_TYPE_CHAT)
    purple_conv_chat_send (PURPLE_CONV_CHAT (self->conv), msg);

  g_task_return_boolean (task, TRUE);
}

static ChattyEncryption
chatty_pp_chat_get_encryption (ChattyChat *chat)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;

  g_assert (CHATTY_IS_PP_CHAT (self));

  return self->encrypt;
}

static void
chatty_pp_chat_set_encryption (ChattyChat *chat,
                               gboolean    enable)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;
  PurpleAccount *pp_account;
  g_autofree char *stripped = NULL;
  const char *name;

  g_assert (CHATTY_IS_PP_CHAT (self));

  if (!chatty_pp_chat_has_encryption_support (self)) {
    g_object_notify (G_OBJECT (self), "encrypt");

    return;
  }

  name = purple_conversation_get_name (self->conv);
  pp_account = purple_conversation_get_account (self->conv);
  stripped = jabber_id_strip_resource (name);

  purple_signal_emit (purple_plugins_get_handle (),
                      enable ? "lurch-enable-im" : "lurch-disable-im",
                      pp_account,
                      stripped,
                      chatty_pp_chat_lurch_changed_cb,
                      g_object_ref (self));
}

static gboolean
chatty_pp_chat_get_buddy_typing (ChattyChat *chat)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;

  g_assert (CHATTY_IS_PP_CHAT (self));

  return self->buddy_typing;
}

static void
chatty_pp_chat_set_typing (ChattyChat *chat,
                           gboolean    is_typing)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;
  PurpleConvIm *im;

  g_assert (CHATTY_IS_PP_CHAT (self));

  if (!self->conv || !chatty_chat_is_im (chat))
    return;

  im = PURPLE_CONV_IM (self->conv);

  if (is_typing) {
    gboolean send = (purple_conv_im_get_send_typed_timeout (im) == 0);

    purple_conv_im_stop_send_typed_timeout (im);
    purple_conv_im_start_send_typed_timeout (im);

    if (send || (purple_conv_im_get_type_again (im) != 0 &&
                 time (NULL) > purple_conv_im_get_type_again (im))) {
      unsigned int timeout;

      timeout = serv_send_typing (purple_conversation_get_gc (self->conv),
                                  purple_conversation_get_name (self->conv),
                                  PURPLE_TYPING);

      purple_conv_im_set_type_again (im, timeout);
    }
  } else {
    purple_conv_im_stop_send_typed_timeout (im);

    serv_send_typing (purple_conversation_get_gc (self->conv),
                      purple_conversation_get_name (self->conv),
                      PURPLE_NOT_TYPING);
  }
}

static void
chatty_pp_chat_invite_async (ChattyChat          *chat,
                             const char          *username,
                             const char          *invite_msg,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  ChattyPpChat *self = (ChattyPpChat *)chat;
  g_autoptr(GTask) task = NULL;

  g_assert (CHATTY_IS_PP_CHAT (self));

  task = g_task_new (self, cancellable, callback, user_data);
  if (!self->conv ||
      chatty_item_get_protocols (CHATTY_ITEM (self)) != CHATTY_PROTOCOL_XMPP ||
      chatty_chat_is_im (chat)) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                             "Chat doesn't supports invite");
    return;
  }

  CHATTY_DEBUG_MSG ("Inviting user %s, invite message: %s", username, invite_msg);

  serv_chat_invite (purple_conversation_get_gc (self->conv),
                    purple_conv_chat_get_id (PURPLE_CONV_CHAT (self->conv)),
                    invite_msg, username);

  g_task_return_boolean (task, TRUE);
}

static const char *
chatty_pp_chat_get_name (ChattyItem *item)
{
  ChattyPpChat *self = (ChattyPpChat *)item;
  const char *name = NULL;

  g_assert (CHATTY_IS_PP_CHAT (self));

  /* If available, return locally saved contact name for SMS chats */
  if (self->buddy &&
      chatty_item_get_protocols (CHATTY_ITEM (item)) == CHATTY_PROTOCOL_SMS) {
    PurpleBlistNode *node;

    node = PURPLE_BLIST_NODE (self->buddy);

    if (node->ui_data &&
        chatty_pp_buddy_get_contact (node->ui_data))
      return chatty_item_get_name (node->ui_data);
  }

  if (self->pp_chat)
    name = purple_chat_get_name (self->pp_chat);
  else if (self->buddy)
    name = purple_buddy_get_alias_only (self->buddy);

  if (name)
    return name;

  /* If we have a cached name, return that */
  if (self->chat_name)
    return self->chat_name;

  if (self->buddy) {
    const char *name_end;

    name = purple_buddy_get_name (self->buddy);
    name_end = strchr (name, '/');

    /* Strip ‘/’ and following string from the username, if found */
    if (name_end)
      self->chat_name = g_strndup (name, name_end - name);
    else
      self->chat_name = g_strdup (name);

    return self->chat_name;
  }

  if (self->conv)
    name = purple_conversation_get_title (self->conv);

  if (!name)
    name = "Invalid user";

  return name;
}

static ChattyProtocol
chatty_pp_chat_get_protocols (ChattyItem *item)
{
  ChattyPpChat *self = (ChattyPpChat *)item;
  PurpleAccount *pp_account = NULL;
  ChattyPpAccount *account = NULL;

  g_assert (CHATTY_IS_PP_CHAT (self));

  if (self->buddy)
    pp_account = self->buddy->account;
  else if (self->pp_chat)
    pp_account = self->pp_chat->account;
  else if (self->conv)
    pp_account = self->conv->account;
  else
    return CHATTY_PROTOCOL_ANY;

  account = chatty_pp_account_get_object (pp_account);

  return chatty_item_get_protocols (CHATTY_ITEM (account));
}

static GdkPixbuf *
chatty_pp_chat_get_avatar (ChattyItem *item)
{
  ChattyPpChat *self = (ChattyPpChat *)item;

  g_assert (CHATTY_IS_PP_CHAT (self));

  if (self->buddy) {
    ChattyPpBuddy *buddy;

    buddy = chatty_pp_buddy_get_object (self->buddy);

    if (buddy)
      return chatty_item_get_avatar (CHATTY_ITEM (buddy));

    return NULL;
  }

  if (self->pp_chat)
    return chatty_pp_chat_get_buddy_icon ((PurpleBlistNode *)self->pp_chat);

  return NULL;
}

static void
chatty_pp_chat_set_avatar_async (ChattyItem          *item,
                                 const char          *file_name,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
  ChattyPpChat *self = (ChattyPpChat *)item;
  g_autoptr(GTask) task = NULL;
  gboolean ret = FALSE;

  g_assert (CHATTY_IS_PP_CHAT (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  if (self->buddy) {
    PurpleContact *contact;
    PurpleStoredImage *icon;

    contact = purple_buddy_get_contact (self->buddy);
    icon = purple_buddy_icons_node_set_custom_icon_from_file ((PurpleBlistNode*)contact,
                                                              file_name);
    ret = icon != NULL;
  }

  g_signal_emit_by_name (self, "avatar-changed");

  /* Purple does not support multi-thread.  Just create the task and return */
  task = g_task_new (self, cancellable, callback, user_data);
  g_task_return_boolean (task, ret);
}

static void
chatty_pp_chat_finalize (GObject *object)
{
  ChattyPpChat *self = (ChattyPpChat *)object;

  if (self->buddy) {
    PurpleBlistNode *node;

    node = PURPLE_BLIST_NODE (self->buddy);
    if (node->ui_data)
      g_object_set_data (node->ui_data, "chat", NULL);
  }

  g_list_store_remove_all (self->chat_users);
  g_list_store_remove_all (self->message_store);
  g_list_store_remove_all (self->fp_list);
  g_object_unref (self->fp_list);
  g_object_unref (self->message_store);
  g_object_unref (self->chat_users);
  g_object_unref (self->sorted_chat_users);
  g_free (self->last_message);
  g_free (self->chat_name);

  G_OBJECT_CLASS (chatty_pp_chat_parent_class)->finalize (object);
}

static void
chatty_pp_chat_class_init (ChattyPpChatClass *klass)
{
  GObjectClass *object_class  = G_OBJECT_CLASS (klass);
  ChattyItemClass *item_class = CHATTY_ITEM_CLASS (klass);
  ChattyChatClass *chat_class = CHATTY_CHAT_CLASS (klass);

  object_class->finalize = chatty_pp_chat_finalize;

  item_class->get_name = chatty_pp_chat_get_name;
  item_class->get_protocols = chatty_pp_chat_get_protocols;
  item_class->get_avatar = chatty_pp_chat_get_avatar;
  item_class->set_avatar_async = chatty_pp_chat_set_avatar_async;

  chat_class->set_data = chatty_pp_chat_set_data;
  chat_class->is_im = chatty_pp_chat_is_im;
  chat_class->has_file_upload = chatty_pp_chat_has_file_upload;
  chat_class->get_chat_name = chatty_pp_chat_get_chat_name;
  chat_class->get_username = chatty_pp_chat_get_username;
  chat_class->get_account = chatty_pp_chat_get_account;
  chat_class->load_past_messages = chatty_pp_chat_load_past_messages;
  chat_class->is_loading_history = chatty_pp_chat_is_loading_history;
  chat_class->get_messages = chatty_pp_chat_get_messages;
  chat_class->get_users = chatty_pp_chat_get_users;
  chat_class->get_topic = chatty_pp_chat_get_topic;
  chat_class->set_topic = chatty_pp_chat_set_topic;
  chat_class->get_last_message = chatty_pp_chat_get_last_message;
  chat_class->get_unread_count = chatty_pp_chat_get_unread_count;
  chat_class->set_unread_count = chatty_pp_chat_set_unread_count;
  chat_class->get_last_msg_time = chatty_pp_chat_get_last_msg_time;
  chat_class->send_message_async = chatty_pp_chat_send_message_async;
  chat_class->get_encryption = chatty_pp_chat_get_encryption;
  chat_class->set_encryption = chatty_pp_chat_set_encryption;
  chat_class->get_buddy_typing = chatty_pp_chat_get_buddy_typing;
  chat_class->set_typing = chatty_pp_chat_set_typing;
  chat_class->invite_async = chatty_pp_chat_invite_async;
}

static void
chatty_pp_chat_init (ChattyPpChat *self)
{
  g_autoptr(GtkSorter) sorter = NULL;

  sorter = gtk_custom_sorter_new ((GCompareDataFunc)sort_chat_buddy, NULL, NULL);
  self->chat_users = g_list_store_new (CHATTY_TYPE_PP_BUDDY);
  self->sorted_chat_users = gtk_sort_list_model_new (G_LIST_MODEL (self->chat_users), sorter);

  self->message_store = g_list_store_new (CHATTY_TYPE_MESSAGE);
  self->fp_list = g_list_store_new (HDY_TYPE_VALUE_OBJECT);
  self->encrypt = CHATTY_ENCRYPTION_UNSUPPORTED;
}

ChattyChat *
chatty_pp_chat_get_object (PurpleChat *pp_chat)
{
  PurpleBlistNode *node;

  g_return_val_if_fail (pp_chat, NULL);

  node = PURPLE_BLIST_NODE (pp_chat);

  return node->ui_data;
}

ChattyPpChat *
chatty_pp_chat_new_im_chat (PurpleAccount *account,
                            PurpleBuddy   *buddy,
                            gboolean       supports_encryption)
{
  ChattyPpChat *self;

  g_return_val_if_fail (account, NULL);
  g_return_val_if_fail (buddy, NULL);

  self = g_object_new (CHATTY_TYPE_PP_CHAT, NULL);
  self->account = account;
  self->supports_encryption = !!supports_encryption;
  chatty_pp_chat_set_purple_buddy (self, buddy);

  return self;
}

ChattyPpChat *
chatty_pp_chat_new_buddy_chat (ChattyPpBuddy *buddy,
                               gboolean       supports_encryption)
{
  ChattyAccount *account;
  PurpleAccount *pp_account;
  PurpleBuddy *pp_buddy;

  g_return_val_if_fail (CHATTY_IS_PP_BUDDY (buddy), NULL);

  account = chatty_pp_buddy_get_account (buddy);
  pp_account = chatty_pp_account_get_account (CHATTY_PP_ACCOUNT (account));
  pp_buddy = chatty_pp_buddy_get_buddy (buddy);

  return chatty_pp_chat_new_im_chat (pp_account, pp_buddy, supports_encryption);
}

ChattyPpChat *
chatty_pp_chat_new_purple_chat (PurpleChat *pp_chat,
                                gboolean    supports_encryption)
{
  ChattyPpChat *self;

  self = g_object_new (CHATTY_TYPE_PP_CHAT, NULL);
  self->supports_encryption = !!supports_encryption;
  chatty_pp_chat_set_purple_chat (self, pp_chat);

  return self;
}

ChattyPpChat *
chatty_pp_chat_new_purple_conv (PurpleConversation *conv,
                                gboolean            supports_encryption)
{
  ChattyPpChat *self;

  self = g_object_new (CHATTY_TYPE_PP_CHAT, NULL);
  self->supports_encryption = !!supports_encryption;
  chatty_pp_chat_set_purple_conv (self, conv);

  return self;
}

void
chatty_pp_chat_set_purple_conv (ChattyPpChat       *self,
                                PurpleConversation *conv)
{
  PurpleBlistNode *node;

  g_return_if_fail (CHATTY_IS_PP_CHAT (self));

  if (self->conv && self->conv->ui_data) {
    self->conv->ui_data = NULL;
    g_object_remove_weak_pointer (G_OBJECT (self), (gpointer *)&self->conv->ui_data);
  }

  self->conv = conv;

  if (!conv)
    return;

  conv->ui_data = self;
  g_object_add_weak_pointer (G_OBJECT (self), (gpointer *)&conv->ui_data);

  if (self->pp_chat || self->buddy)
    return;

  node = chatty_utils_get_conv_blist_node (conv);

  if (node && PURPLE_BLIST_NODE_IS_CHAT (node))
    chatty_pp_chat_set_purple_chat (self, PURPLE_CHAT (node));
  else if (node && PURPLE_BLIST_NODE_IS_BUDDY (node))
    chatty_pp_chat_set_purple_buddy (self, PURPLE_BUDDY (node));
}

ChattyProtocol
chatty_pp_chat_get_protocol (ChattyPpChat *self)
{
  ChattyPpAccount *account;
  PurpleAccount *pp_account;

  g_return_val_if_fail (CHATTY_IS_PP_CHAT (self), CHATTY_PROTOCOL_NONE);

  if (self->account)
    pp_account = self->account;
  else if (self->conv)
    pp_account = self->conv->account;
  else if (self->pp_chat)
    pp_account = self->pp_chat->account;
  else
    return CHATTY_PROTOCOL_NONE;

  account = chatty_pp_account_get_object (pp_account);

  if (account)
    return chatty_item_get_protocols (CHATTY_ITEM (account));

  return CHATTY_PROTOCOL_NONE;
}

PurpleChat *
chatty_pp_chat_get_purple_chat (ChattyPpChat *self)
{
  g_return_val_if_fail (CHATTY_IS_PP_CHAT (self), NULL);

  return self->pp_chat;
}

PurpleBuddy *
chatty_pp_chat_get_purple_buddy (ChattyPpChat *self)
{
  g_return_val_if_fail (CHATTY_IS_PP_CHAT (self), NULL);

  return self->buddy;
}

PurpleConversation *
chatty_pp_chat_get_purple_conv (ChattyPpChat *self)
{
  g_return_val_if_fail (CHATTY_IS_PP_CHAT (self), NULL);

  return self->conv;
}

void
chatty_pp_chat_show_file_upload (ChattyPpChat *self)
{
  PurpleBlistNode *node;

  g_return_if_fail (CHATTY_IS_PP_CHAT (self));

  node = chatty_utils_get_conv_blist_node (self->conv);

  if (self->upload_file_callback)
    self->upload_file_callback (node, self->callback_userdata);
}

gboolean
chatty_pp_chat_are_same (ChattyPpChat *a,
                         ChattyPpChat *b)
{
  g_return_val_if_fail (CHATTY_IS_PP_CHAT (a), FALSE);
  g_return_val_if_fail (CHATTY_IS_PP_CHAT (b), FALSE);

  if (a == b)
    return TRUE;

  if (a->account && a->buddy &&
      a->account == b->account &&
      a->buddy == b->buddy)
    return TRUE;

  if (a->conv && a->conv == b->conv)
    return TRUE;

  if (a->pp_chat && a->pp_chat == b->pp_chat)
    return TRUE;

  if (a->conv &&
      chatty_pp_chat_match_purple_conv (b, a->conv))
    return TRUE;

  if (b->conv &&
      chatty_pp_chat_match_purple_conv (a, b->conv))
    return TRUE;

  return FALSE;
}

/**
 * chatty_pp_chat_run_command:
 * @self: A #ChattyPpChat
 * @command: The command to run
 *
 * Try to run the command @command.
 *
 * Returns: %TRUE if @command was handled.
 * %FALSE otherwise.
 */
gboolean
chatty_pp_chat_run_command (ChattyPpChat *self,
                            const char   *command)
{
  g_autofree char *error = NULL;
  PurpleCmdStatus status;
  PurpleMessageFlags flags = PURPLE_MESSAGE_NO_LOG | PURPLE_MESSAGE_SYSTEM;

  g_return_val_if_fail (CHATTY_IS_PP_CHAT (self), FALSE);

  if (!command || *command != '/' || !self->conv)
    return FALSE;

  command = command + strlen ("/");
  purple_conversation_write (self->conv, "", command, flags, time (NULL));
  status = purple_cmd_do_command (self->conv, command, command, &error);

  switch (status) {
  case PURPLE_CMD_STATUS_OK:
    return TRUE;

  case PURPLE_CMD_STATUS_NOT_FOUND:
    {
      PurplePluginProtocolInfo *prpl_info = NULL;
      PurpleConnection *gc;

      if ((gc = purple_conversation_get_gc (self->conv)))
        prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO(gc->prpl);

      if ((prpl_info != NULL) &&
          (prpl_info->options & OPT_PROTO_SLASH_COMMANDS_NATIVE)) {
        const char *spaceslash;

        /* If the first word in the entered text has a '/' in it, then the user
         * probably didn't mean it as a command. So send the text as message. */
        spaceslash = command;

        while (*spaceslash && *spaceslash != ' ' && *spaceslash != '/')
          spaceslash++;

        if (*spaceslash != '/') {
          purple_conversation_write (self->conv, "",
                                     "Unknown command. Get a list of available commands with '/chatty help'",
                                     flags, time (NULL));
          return TRUE;
        }
      }
    }
    break;

  case PURPLE_CMD_STATUS_WRONG_ARGS:
    purple_conversation_write (self->conv, "",
                               "Wrong number of arguments for the command.",
                               flags, time (NULL));
    return TRUE;

  case PURPLE_CMD_STATUS_FAILED:
    purple_conversation_write (self->conv, "",
                               error ? error : "The command failed.",
                               flags, time (NULL));
    return TRUE;

  case PURPLE_CMD_STATUS_WRONG_TYPE:
    if (purple_conversation_get_type (self->conv) == PURPLE_CONV_TYPE_IM)
      purple_conversation_write (self->conv, "",
                                 "That command only works in chats, not IMs.",
                                 flags, time (NULL));
    else
      purple_conversation_write (self->conv, "",
                                 "That command only works in IMs, not chats.",
                                 flags, time (NULL));
    return TRUE;

  case PURPLE_CMD_STATUS_WRONG_PRPL:
    purple_conversation_write (self->conv, "",
                               "That command doesn't work on this protocol.",
                               flags, time (NULL));
    return TRUE;
  default:
    break;
  }

  return FALSE;
}

gboolean
chatty_pp_chat_match_purple_conv (ChattyPpChat       *self,
                                  PurpleConversation *conv)
{
  gpointer node;

  g_return_val_if_fail (CHATTY_IS_PP_CHAT (self), FALSE);
  g_return_val_if_fail (conv, FALSE);

  if (self->conv && conv == self->conv)
    return TRUE;

  if (self->account && self->account != conv->account)
    return FALSE;

  node = chatty_utils_get_conv_blist_node (conv);

  if (!node)
    return FALSE;

  if (node == self->pp_chat ||
      node == self->buddy) {
    self->conv = conv;

    return TRUE;
  }

  return FALSE;
}

ChattyMessage *
chatty_pp_chat_find_message_with_id (ChattyPpChat *self,
                                     const char   *id)
{
  guint n_items;

  g_return_val_if_fail (CHATTY_IS_PP_CHAT (self), NULL);
  g_return_val_if_fail (id, NULL);

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->message_store));

  if (n_items == 0)
    return NULL;

  /* Search from end, the item is more likely to be at the end */
  for (guint i = n_items; i > 0; i--) {
    g_autoptr(ChattyMessage) message = NULL;
    const char *message_id;

    message = g_list_model_get_item (G_LIST_MODEL (self->message_store), i - 1);
    message_id = chatty_message_get_id (message);

    /*
     * Once we have a message with no id, all preceding items shall likely
     * have loaded from database, and thus no id, so don’t bother searching.
     */
    if (!message_id)
      break;

    if (g_str_equal (id, message_id))
      return message;
  }

  return NULL;
}

void
chatty_pp_chat_append_message (ChattyPpChat  *self,
                               ChattyMessage *message)
{
  g_return_if_fail (CHATTY_IS_PP_CHAT (self));
  g_return_if_fail (CHATTY_IS_MESSAGE (message));

  g_list_store_append (self->message_store, message);
  g_signal_emit_by_name (self, "changed", 0);
}

void
chatty_pp_chat_prepend_message (ChattyPpChat  *self,
                                ChattyMessage *message)
{
  g_return_if_fail (CHATTY_IS_PP_CHAT (self));
  g_return_if_fail (CHATTY_IS_MESSAGE (message));

  g_list_store_insert (self->message_store, 0, message);
  g_signal_emit_by_name (self, "changed", 0);
}

void
chatty_pp_chat_prepend_messages (ChattyPpChat *self,
                                 GPtrArray    *messages)
{
  g_return_if_fail (CHATTY_IS_PP_CHAT (self));

  if (!messages || messages->len == 0)
    return;

  g_return_if_fail (CHATTY_IS_MESSAGE (messages->pdata[0]));

  g_list_store_splice (self->message_store, 0, 0, messages->pdata, messages->len);
  g_signal_emit_by_name (self, "changed", 0);
}

/**
 * chatty_pp_chat_add_users:
 * @self: a #ChattyChat
 * @users: A #GList of added users
 *
 * Add a #GList of #PurpleConvChatBuddy users to
 * @self.  This function only adds the items to
 * the internal list model, so that it can be
 * used to create widgets.
 */
void
chatty_pp_chat_add_users (ChattyPpChat *self,
                          GList        *users)
{
  ChattyPpBuddy *buddy;
  GPtrArray *users_array;

  g_return_if_fail (CHATTY_IS_PP_CHAT (self));

  users_array = g_ptr_array_new_with_free_func (g_object_unref);

  for (GList *node = users; node; node = node->next) {
    buddy = g_object_new (CHATTY_TYPE_PP_BUDDY,
                          "chat-buddy", node->data, NULL);
    chatty_pp_buddy_set_chat (buddy, self->conv);
    g_ptr_array_add (users_array, buddy);
  }

  g_list_store_splice (self->chat_users, 0, 0,
                       users_array->pdata, users_array->len);

  g_ptr_array_free (users_array, TRUE);
}


/**
 * chatty_pp_chat_remove_users:
 * @self: a #ChattyChat
 * @users: A #GList of removed users
 *
 * Remove a #GList of `const char*` users to
 * @self.  This function only remove the items
 * the internal list model, so that it can be
 * used to create widgets.
 */
void
chatty_pp_chat_remove_user (ChattyPpChat *self,
                            const char   *user)
{
  PurpleConvChatBuddy *cb = NULL;
  PurpleConvChat *chat;
  ChattyPpBuddy *buddy = NULL;
  guint index;

  g_return_if_fail (CHATTY_IS_PP_CHAT (self));

  chat = purple_conversation_get_chat_data (self->conv);

  if (chat)
    cb = purple_conv_chat_cb_find (chat, user);

  if (cb)
    buddy = chat_find_user (self, cb->name, &index);

  if (buddy)
    g_list_store_remove (self->chat_users, index);
}

ChattyPpBuddy *
chatty_pp_chat_find_user (ChattyPpChat *self,
                          const char   *username)
{
  g_return_val_if_fail (CHATTY_IS_PP_CHAT (self), NULL);
  g_return_val_if_fail (username, NULL);

  return chat_find_user (self, username, NULL);
}

/**
 * chatty_pp_chat_get_buddy_name:
 * @self: A #ChattyPpChat
 * @who: The name of buddy
 *
 * Get Full buddy username for @who.  You
 * may get `alice@example.com/wonderland`
 * when @who is `alice`.
 */
char *
chatty_pp_chat_get_buddy_name (ChattyPpChat *self,
                               const char   *who)
{
  PurplePluginProtocolInfo *prpl_info;
  PurpleConnection *gc;
  PurpleAccount *account;

  g_return_val_if_fail (CHATTY_IS_PP_CHAT (self), NULL);
  g_return_val_if_fail (who && *who, NULL);

  if (chatty_chat_is_im (CHATTY_CHAT (self)) || !self->conv)
    return NULL;

  account = self->conv->account;
  gc = purple_account_get_connection (account);

  if (!gc)
    return NULL;

  prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO (gc->prpl);

  if (prpl_info && prpl_info->get_cb_real_name) {
    int chat_id;

    chat_id = purple_conv_chat_get_id (PURPLE_CONV_CHAT (self->conv));
    return prpl_info->get_cb_real_name(gc, chat_id, who);
  }

  return NULL;
}

void
chatty_pp_chat_emit_user_changed (ChattyPpChat *self,
                                  const char   *user)
{
  ChattyPpBuddy *buddy;

  g_return_if_fail (CHATTY_IS_PP_CHAT (self));
  g_return_if_fail (user);

  buddy = chat_find_user (self, user, NULL);

  if (buddy)
    g_signal_emit_by_name (buddy, "changed");
}

/**
 * chatty_pp_chat_load_encryption_status:
 * @self: A #ChattyChat
 *
 * Load encryption status of the chat @self.
 * Once the status is loaded, notify::encrypt
 * is emitted.
 *
 * Currently only XMPP IM conversations are supported.
 * Otherwise, the function simply returns.
 */
void
chatty_pp_chat_load_encryption_status (ChattyPpChat *self)
{
  PurpleAccount   *pp_account;
  const char      *name;
  g_autofree char *stripped = NULL;

  g_return_if_fail (CHATTY_IS_PP_CHAT (self));

  if (!chatty_pp_chat_has_encryption_support (self))
    return;

  name = purple_conversation_get_name (self->conv);
  pp_account = purple_conversation_get_account (self->conv);
  stripped = jabber_id_strip_resource (name);

  purple_signal_emit (purple_plugins_get_handle(),
                      "lurch-status-im",
                      pp_account,
                      stripped,
                      lurch_status_changed_cb,
                      g_object_ref (self));

}

GListModel *
chatty_pp_chat_get_fp_list (ChattyPpChat *self)
{
  g_return_val_if_fail (CHATTY_IS_PP_CHAT (self), NULL);

  return G_LIST_MODEL (self->fp_list);
}

void
chatty_pp_chat_load_fp_list_async (ChattyPpChat        *self,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autofree char *user_id = NULL;
  const char *name;
  void *handle;

  g_return_if_fail (CHATTY_IS_PP_CHAT (self));

  task = g_task_new (self, NULL, callback, user_data);
  name = chatty_chat_get_chat_name (CHATTY_CHAT (self));
  user_id = chatty_utils_jabber_id_strip (name);
  handle = purple_plugins_get_handle ();

  if (!self->buddy || !self->supports_encryption) {
    g_task_return_boolean (task, FALSE);
    return;
  }

  g_list_store_remove_all (self->fp_list);
  purple_signal_emit (handle, "lurch-fp-other", self->buddy->account,
                      user_id, list_fingerprints_cb, g_steal_pointer (&task));
}

gboolean
chatty_pp_chat_load_fp_list_finish (ChattyPpChat  *self,
                                    GAsyncResult  *result,
                                    GError       **error)
{
  g_return_val_if_fail (CHATTY_IS_PP_CHAT (self), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

gboolean
chatty_pp_chat_get_show_notifications (ChattyPpChat *self)
{
  PurpleBlistNode *node;

  g_return_val_if_fail (CHATTY_IS_PP_CHAT (self), FALSE);

  if (!self->buddy && !self->pp_chat)
    return FALSE;

  node = PURPLE_BLIST_NODE (self->buddy);

  if (!node)
    node = PURPLE_BLIST_NODE (self->pp_chat);

  return purple_blist_node_get_bool (node, "chatty-notifications");
}

gboolean
chatty_pp_chat_get_show_status_msg (ChattyPpChat *self)
{
  PurpleBlistNode *node;

  if (!self->buddy && !self->pp_chat)
    return FALSE;

  node = PURPLE_BLIST_NODE (self->buddy);

  if (!node)
    node = PURPLE_BLIST_NODE (self->pp_chat);

  return purple_blist_node_get_bool (node, "chatty-status-msg");
}

void
chatty_pp_chat_set_show_notifications (ChattyPpChat *self,
                                       gboolean      show)
{
  PurpleBlistNode *node = NULL;

  g_return_if_fail (CHATTY_IS_PP_CHAT (self));

  if (self->buddy)
    node = (PurpleBlistNode *)self->buddy;
  else if (self->pp_chat)
    node = (PurpleBlistNode *)self->pp_chat;
  else
    return;

  purple_blist_node_set_bool (node, "chatty-notifications", !!show);
}

void
chatty_pp_chat_set_show_status_msg (ChattyPpChat *self,
                                    gboolean      show)
{
  PurpleConversation *conv;
  PurpleBlistNode *node;

  g_return_if_fail (CHATTY_IS_PP_CHAT (self));

  if (!self->conv)
    return;

  conv = self->conv;
  node = PURPLE_BLIST_NODE (purple_blist_find_chat (conv->account, conv->name));
  purple_blist_node_set_bool (node, "chatty-status-msg", show);
}

const char *
chatty_pp_chat_get_status (ChattyPpChat *self)
{
  PurplePresence *presence;
  PurpleStatus *status = NULL;

  g_return_val_if_fail (CHATTY_IS_PP_CHAT (self), "");

  if (!self->buddy)
    return "";

  presence = purple_buddy_get_presence (self->buddy);

  if (presence)
    status = purple_presence_get_active_status (presence);

  if (status)
    return purple_status_get_name (status);

  return "";
}

gboolean
chatty_pp_chat_get_auto_join (ChattyPpChat *self)
{
  PurpleBlistNode *node = NULL;

  g_return_val_if_fail (CHATTY_IS_PP_CHAT (self), FALSE);

  if (self->buddy)
    node = (PurpleBlistNode *)self->buddy;
  else if (self->pp_chat)
    node = (PurpleBlistNode *)self->pp_chat;
  else
    return FALSE;

  return purple_blist_node_get_bool (node, "chatty-autojoin");
}

/**
 * chatty_pp_chat_get_buddy_typing:
 * @self: A #ChattyChat
 *
 * Get if the associated buddy is typing or
 * not.  This is accurate only for IM chat.
 * For multi user chat, this function always
 * returns %FALSE
 *
 * Returns: %TRUE if the chat buddy is typing.
 * %FALSE otherwise.
 */

void
chatty_pp_chat_set_buddy_typing (ChattyPpChat *self,
                                 gboolean      is_typing)
{
  g_return_if_fail (CHATTY_IS_PP_CHAT (self));

  if (self->buddy_typing == !!is_typing)
    return;

  self->buddy_typing = !!is_typing;
  g_object_notify (G_OBJECT (self), "buddy-typing");
}

void
chatty_pp_chat_leave (ChattyPpChat *self)
{
  PurpleBlistNode *node;

  g_return_if_fail (CHATTY_IS_PP_CHAT (self));

  node = (gpointer)self->buddy;

  if (!node)
    node = (gpointer)self->pp_chat;

  if (node)
    purple_blist_node_set_bool (node, "chatty-autojoin", FALSE);

  if (self->conv)
    purple_conversation_destroy (self->conv);
}

void
chatty_pp_chat_join (ChattyPpChat *self)
{
  ChattyAccount *account;
  PurplePluginProtocolInfo *prpl_info;
  PurpleAccount *pp_account;
  PurpleConversation *conv;
  PurpleBlistNode *node;
  GHashTable *components;
  g_autofree char *chat_name = NULL;
  const char *name = NULL;

  g_return_if_fail (CHATTY_IS_PP_CHAT (self));
  g_return_if_fail (self->pp_chat || self->buddy);

  node = (PurpleBlistNode *)self->pp_chat;

  if (!node)
    node = (PurpleBlistNode *)self->buddy;

  account = chatty_chat_get_account (CHATTY_CHAT (self));
  purple_blist_node_set_bool (node, "chatty-autojoin", TRUE);

  if (self->buddy) {
    GPtrArray *buddies;

    buddies = g_ptr_array_new_full (1, g_free);
    g_ptr_array_add (buddies, g_strdup (purple_buddy_get_name (self->buddy)));
    chatty_account_start_direct_chat_async (account, buddies, NULL, NULL);

    return;
  }

  pp_account = purple_chat_get_account (self->pp_chat);
  prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO (purple_find_prpl (purple_account_get_protocol_id (pp_account)));

  components = purple_chat_get_components (self->pp_chat);

  if (prpl_info && prpl_info->get_chat_name)
    name = chat_name = prpl_info->get_chat_name (components);

  if (!name)
    name = purple_chat_get_name (self->pp_chat);

  conv = purple_find_conversation_with_account (PURPLE_CONV_TYPE_CHAT, name, pp_account);

  if (conv)
    self->conv = conv;

  if (!conv || purple_conv_chat_has_left (PURPLE_CONV_CHAT (conv))) {
    pp_chat_add_history_since_component (self, components, pp_account->username, name);
    serv_join_chat (purple_account_get_connection (pp_account), components);
  } else if (conv) {
    purple_conversation_present (conv);
  }
}

void
chatty_pp_chat_delete (ChattyPpChat *self)
{
  g_return_if_fail (CHATTY_IS_PP_CHAT (self));

  if (chatty_chat_is_im (CHATTY_CHAT (self))) {
    PurpleBuddy *buddy;

    buddy = self->buddy;
    purple_account_remove_buddy (buddy->account, buddy, NULL);
    purple_conversation_destroy (self->conv);
    purple_blist_remove_buddy (buddy);
  } else {
    GHashTable *components;

    if (self->conv)
      purple_conversation_destroy (self->conv);

    // TODO: LELAND: Is this the right place? After recreating a recently
    // deleted chat (same session), the conversation is still in memory
    // somewhere and when re-joining the same chat, the db is not re-populated
    // (until next app session) since there is no server call. Ask @Andrea
    components = purple_chat_get_components (self->pp_chat);
    g_hash_table_steal (components, "history_since");
    purple_blist_remove_chat (self->pp_chat);
  }
}

static void
write_buddy_contact_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;
  gboolean status;

  g_assert (G_IS_TASK (task));

  status = chatty_eds_write_contact_finish (result, &error);

  if (error)
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, status);
}

void
chatty_pp_chat_save_to_contacts_async (ChattyPpChat        *self,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
  PurpleAccount *pp_account;
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (CHATTY_IS_PP_CHAT (self));
  g_return_if_fail (self->buddy);

  pp_account = purple_conversation_get_account (self->conv);
  purple_account_add_buddy (pp_account, self->buddy);
  purple_blist_node_remove_setting (PURPLE_BLIST_NODE (self->buddy), "chatty-unknown-contact");
  purple_blist_node_set_bool (PURPLE_BLIST_NODE (self->buddy), "chatty-notifications", TRUE);

  task = g_task_new (self, NULL, callback, user_data);

  if (chatty_item_get_protocols (CHATTY_ITEM (self)) == CHATTY_PROTOCOL_SMS) {
    ChattyPpBuddy *buddy;
    g_autofree char *number = NULL;
    const char *who, *country_code;

    who = purple_buddy_get_name (self->buddy);
    country_code = chatty_settings_get_country_iso_code (chatty_settings_get_default ());
    number = chatty_utils_check_phonenumber (who, country_code);
    buddy = chatty_pp_buddy_get_object (self->buddy);

    if (!chatty_pp_buddy_get_contact (buddy))
      chatty_eds_write_contact_async (who, number, write_buddy_contact_cb,
                                      g_steal_pointer (&task));
    else
      g_task_return_boolean (task, TRUE);
  } else {
    g_task_return_boolean (task, TRUE);
  }

}

gboolean
chatty_pp_chat_save_to_contacts_finish (ChattyPpChat  *self,
                                        GAsyncResult  *result,
                                        GError       **error)
{
  g_return_val_if_fail (CHATTY_IS_PP_CHAT (self), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
