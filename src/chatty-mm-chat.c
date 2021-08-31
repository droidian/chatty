/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-mm-chat.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-mm-chat"

#include <purple.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "contrib/gtk.h"
#include "chatty-settings.h"
#include "chatty-utils.h"
#include "users/chatty-mm-buddy.h"
#include "users/chatty-mm-account.h"
#include "chatty-notification.h"
#include "chatty-history.h"
#include "chatty-mm-chat.h"
#include "chatty-log.h"

/**
 * SECTION: chatty-mm-chat
 * @title: ChattyMmChat
 * @short_description: An abstraction over ModemManager
 * @include: "chatty-mm-chat.h"
 */

struct _ChattyMmChat
{
  ChattyChat       parent_instance;

  ChattyNotification *notification;
  ChattyMessage   *last_notify_message;
  ChattyEds       *chatty_eds;
  ChattyMmAccount *account;
  ChattyHistory   *history_db;
  GListStore      *chat_users;
  GListStore      *message_store;
  /* A Queue of #GTask */
  GQueue          *message_queue;
  /* Index of @chat_users for the message
     that's currently being sent */
  guint            sender_index;

  char            *last_message;
  char            *chat_id;
  char            *name;
  guint            unread_count;
  guint            last_msg_time;
  ChattyProtocol   protocol;
  gboolean         is_im;
  gboolean         history_is_loading;
  gboolean         is_sending_message;
};

G_DEFINE_TYPE (ChattyMmChat, chatty_mm_chat, CHATTY_TYPE_CHAT)

static void
chatty_mm_chat_set_data (ChattyChat *chat,
                         gpointer    account,
                         gpointer    history_db)
{
  ChattyMmChat *self = (ChattyMmChat *)chat;

  g_assert (CHATTY_IS_MM_CHAT (self));
  g_assert (CHATTY_IS_MM_ACCOUNT (account));
  g_assert (!self->account);
  g_assert (!self->history_db);

  g_set_object (&self->account, account);
  g_set_object (&self->history_db, history_db);
}

static gboolean
chatty_mm_chat_is_im (ChattyChat *chat)
{
  ChattyMmChat *self = (ChattyMmChat *)chat;

  g_assert (CHATTY_IS_MM_CHAT (self));

  return self->is_im;
}

static const char *
chatty_mm_chat_get_chat_name (ChattyChat *chat)
{
  ChattyMmChat *self = (ChattyMmChat *)chat;

  g_assert (CHATTY_IS_MM_CHAT (self));

  if (self->chat_id)
    return self->chat_id;

  return "";
}

static ChattyAccount *
chatty_mm_chat_get_account (ChattyChat *chat)
{
  ChattyMmChat *self = (ChattyMmChat *)chat;

  g_assert (CHATTY_IS_MM_CHAT (self));

  return CHATTY_ACCOUNT (self->account);
}

static void
mm_chat_load_db_messages_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  g_autoptr(ChattyMmChat) self = user_data;
  g_autoptr(GPtrArray) messages = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (CHATTY_IS_MM_CHAT (self));

  messages = chatty_history_get_messages_finish (self->history_db, result, &error);
  self->history_is_loading = FALSE;

  if (messages && messages->len) {
    g_list_store_splice (self->message_store, 0, 0, messages->pdata, messages->len);
    self->last_notify_message = g_object_ref (messages->pdata[messages->len - 1]);
    g_signal_emit_by_name (self, "changed", 0);
  } else if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    g_warning ("Error fetching messages: %s,", error->message);
  }
}

static void
chatty_mm_chat_load_past_messages (ChattyChat *chat,
                                   int         count)
{
  ChattyMmChat *self = (ChattyMmChat *)chat;
  GListModel *model;

  g_assert (CHATTY_IS_MM_CHAT (self));
  g_assert (count > 0);

  if (self->history_is_loading)
    return;

  self->history_is_loading = TRUE;
  model = chatty_chat_get_messages (chat);

  chatty_history_get_messages_async (self->history_db, chat,
                                     g_list_model_get_item (model, 0),
                                     count, mm_chat_load_db_messages_cb,
                                     g_object_ref (self));
}

static GListModel *
chatty_mm_chat_get_messages (ChattyChat *chat)
{
  ChattyMmChat *self = (ChattyMmChat *)chat;

  g_assert (CHATTY_IS_MM_CHAT (self));

  return G_LIST_MODEL (self->message_store);
}

static GListModel *
chatty_mm_chat_get_users (ChattyChat *chat)
{
  ChattyMmChat *self = (ChattyMmChat *)chat;

  g_assert (CHATTY_IS_MM_CHAT (self));

  return G_LIST_MODEL (self->chat_users);
}

static const char *
chatty_mm_chat_get_last_message (ChattyChat *chat)
{
  ChattyMmChat *self = (ChattyMmChat *)chat;
  g_autoptr(ChattyMessage) message = NULL;
  GListModel *model;
  guint n_items;

  g_assert (CHATTY_IS_MM_CHAT (self));

  model = G_LIST_MODEL (self->message_store);
  n_items = g_list_model_get_n_items (model);

  if (n_items == 0)
    return "";

  message = g_list_model_get_item (model, n_items - 1);

  return chatty_message_get_text (message);
}

static guint
chatty_mm_chat_get_unread_count (ChattyChat *chat)
{
  ChattyMmChat *self = (ChattyMmChat *)chat;

  g_assert (CHATTY_IS_MM_CHAT (self));

  return self->unread_count;
}

static void
chatty_mm_chat_set_unread_count (ChattyChat *chat,
                                 guint       unread_count)
{
  ChattyMmChat *self = (ChattyMmChat *)chat;

  g_assert (CHATTY_IS_MM_CHAT (self));

  if (self->unread_count == unread_count)
    return;

  self->unread_count = unread_count;
  g_signal_emit_by_name (self, "changed", 0);
}

static time_t
chatty_mm_chat_get_last_msg_time (ChattyChat *chat)
{
  ChattyMmChat *self = (ChattyMmChat *)chat;
  g_autoptr(ChattyMessage) message = NULL;
  GListModel *model;
  guint n_items;

  g_assert (CHATTY_IS_MM_CHAT (self));

  model = G_LIST_MODEL (self->message_store);
  n_items = g_list_model_get_n_items (model);

  if (n_items == 0)
    return 0;

  message = g_list_model_get_item (model, n_items - 1);

  return chatty_message_get_time (message);
}

static void mm_chat_send_message_from_queue (ChattyMmChat *self);

static void
mm_chat_send_message_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  ChattyMmChat *self;
  g_autoptr(GTask) task = user_data;
  g_autoptr(ChattyMmBuddy) buddy = NULL;
  g_autoptr(GError) error = NULL;
  ChattyMessage *message;
  gboolean is_mms;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (CHATTY_IS_MM_CHAT (self));

  message = g_task_get_task_data (task);
  g_assert (CHATTY_IS_MESSAGE (message));

  chatty_mm_account_send_message_finish (self->account, result, &error);

  /* Regardless of the error state, we continue to send the rest
   * of the messages from queue.
   */
  if (error) {
    CHATTY_DEBUG_MSG ("Error sending message: %s", error->message);
    chatty_message_set_status (message, CHATTY_STATUS_SENDING_FAILED, 0);
  }

  is_mms = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (message), "mms"));

  if (!is_mms) {
    GListModel *users;

    users = G_LIST_MODEL (self->chat_users);
    buddy = g_list_model_get_item (users, self->sender_index);
  }

  self->is_sending_message = FALSE;

  /* If we don't have any more buddies, we have sent the message
     to all buddies in the list.  Reset sender index and mark task
     as done */
  if (!buddy || is_mms) {
    self->sender_index = 0;
    g_task_return_boolean (task, TRUE);
    g_object_unref (g_queue_pop_head (self->message_queue));
  }

  mm_chat_send_message_from_queue (self);
}

/*
 * Try sending message from queue if not empty.
 * For SMS messages, the message is sent to individual
 * buddies sequentially and the message is removed
 * from queue only after the message is sent to every
 * buddy in the chat list
 */
static void
mm_chat_send_message_from_queue (ChattyMmChat *self)
{
  g_autoptr(ChattyMmBuddy) buddy = NULL;
  ChattyMessage *message;
  GListModel *users;
  GTask *task;
  gboolean is_mms;

  g_assert (CHATTY_IS_MM_CHAT (self));

  if (self->is_sending_message ||
      !self->message_queue ||
      !self->message_queue->length)
    return;

  users = G_LIST_MODEL (self->chat_users);
  g_return_if_fail (g_list_model_get_n_items (users) > 0);

  self->is_sending_message = TRUE;
  task = g_queue_peek_head (self->message_queue);
  message = g_task_get_task_data (task);
  is_mms = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (message), "mms"));

  /*
   * For SMS, we iterate over the buddy list and send individual message
   * to each buddy sequentially
   */
  if (!is_mms) {
    /* Get the current buddy in the index */
    buddy = g_list_model_get_item (users, self->sender_index);
    /* Increment the index which shall be used in the next iteration */
    self->sender_index++;
    g_assert (buddy);
  }

  chatty_mm_account_send_message_async (self->account, CHATTY_CHAT (self),
                                        buddy, message,
                                        g_task_get_cancellable (task),
                                        mm_chat_send_message_cb,
                                        g_object_ref (task));
}

static void
chatty_mm_chat_send_message_async (ChattyChat          *chat,
                                   ChattyMessage       *message,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  ChattyMmChat *self = (ChattyMmChat *)chat;
  GTask *task;

  g_assert (CHATTY_IS_MM_CHAT (self));
  g_assert (CHATTY_IS_MESSAGE (message));

  if (!chatty_message_get_uid (message)) {
    g_autofree char *uuid = NULL;

    uuid = g_uuid_string_random ();
    chatty_message_set_uid (message, uuid);
  }

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_task_data (task, g_object_ref (message), g_object_unref);
  g_queue_push_head (self->message_queue, task);

  mm_chat_send_message_from_queue (self);
}

static const char *
chatty_mm_chat_get_name (ChattyItem *item)
{
  ChattyMmChat *self = (ChattyMmChat *)item;

  g_assert (CHATTY_IS_MM_CHAT (self));

  /* If we have a cached name, return that */
  if (self->name && *self->name)
    return self->name;

  if (self->chat_id)
    return self->chat_id;

  return "";
}

static const char *
chatty_mm_chat_get_username (ChattyItem *item)
{
  return "SMS";
}

static ChattyProtocol
chatty_mm_chat_get_protocols (ChattyItem *item)
{
  ChattyMmChat *self = (ChattyMmChat *)item;

  g_assert (CHATTY_IS_MM_CHAT (self));

  return CHATTY_PROTOCOL_MMS_SMS;
}

static GdkPixbuf *
chatty_mm_chat_get_avatar (ChattyItem *item)
{
  ChattyMmChat *self = (ChattyMmChat *)item;

  g_assert (CHATTY_IS_MM_CHAT (self));

  if (g_list_model_get_n_items (G_LIST_MODEL (self->chat_users)) == 1) {
    g_autoptr(ChattyMmBuddy) buddy = NULL;

    buddy = g_list_model_get_item (G_LIST_MODEL (self->chat_users), 0);

    return chatty_item_get_avatar (CHATTY_ITEM (buddy));
  }

  return NULL;
}

static void
chatty_mm_chat_finalize (GObject *object)
{
  ChattyMmChat *self = (ChattyMmChat *)object;

  g_queue_free_full (self->message_queue, g_object_unref);
  g_list_store_remove_all (self->chat_users);
  g_list_store_remove_all (self->message_store);
  g_clear_object (&self->notification);
  g_clear_object (&self->last_notify_message);
  g_clear_object (&self->history_db);
  g_clear_object (&self->chatty_eds);
  g_object_unref (self->message_store);
  g_object_unref (self->chat_users);
  g_free (self->last_message);
  g_free (self->chat_id);

  G_OBJECT_CLASS (chatty_mm_chat_parent_class)->finalize (object);
}

static void
chatty_mm_chat_class_init (ChattyMmChatClass *klass)
{
  GObjectClass *object_class  = G_OBJECT_CLASS (klass);
  ChattyItemClass *item_class = CHATTY_ITEM_CLASS (klass);
  ChattyChatClass *chat_class = CHATTY_CHAT_CLASS (klass);

  object_class->finalize = chatty_mm_chat_finalize;

  item_class->get_name = chatty_mm_chat_get_name;
  item_class->get_username = chatty_mm_chat_get_username;
  item_class->get_protocols = chatty_mm_chat_get_protocols;
  item_class->get_avatar = chatty_mm_chat_get_avatar;

  chat_class->set_data = chatty_mm_chat_set_data;
  chat_class->is_im = chatty_mm_chat_is_im;
  chat_class->get_chat_name = chatty_mm_chat_get_chat_name;
  chat_class->get_account = chatty_mm_chat_get_account;
  chat_class->load_past_messages = chatty_mm_chat_load_past_messages;
  chat_class->get_messages = chatty_mm_chat_get_messages;
  chat_class->get_users = chatty_mm_chat_get_users;
  chat_class->get_last_message = chatty_mm_chat_get_last_message;
  chat_class->get_unread_count = chatty_mm_chat_get_unread_count;
  chat_class->set_unread_count = chatty_mm_chat_set_unread_count;
  chat_class->get_last_msg_time = chatty_mm_chat_get_last_msg_time;
  chat_class->send_message_async = chatty_mm_chat_send_message_async;
}

static void
chatty_mm_chat_init (ChattyMmChat *self)
{
  self->chat_users = g_list_store_new (CHATTY_TYPE_MM_BUDDY);
  self->message_store = g_list_store_new (CHATTY_TYPE_MESSAGE);
  self->message_queue = g_queue_new ();
  self->notification  = chatty_notification_new ();
}

ChattyMmChat *
chatty_mm_chat_new (const char     *name,
                    const char     *alias,
                    ChattyProtocol  protocol,
                    gboolean        is_im)
{
  ChattyMmChat *self;

  self = g_object_new (CHATTY_TYPE_MM_CHAT, NULL);
  self->chat_id = g_strdup (name);
  self->name = g_strdup (alias);
  self->protocol = protocol;
  self->is_im = !!is_im;

  return self;
}

void
chatty_mm_chat_set_eds (ChattyMmChat *self,
                        ChattyEds    *chatty_eds)
{
  guint n_items;

  g_return_if_fail (CHATTY_IS_MM_CHAT (self));
  g_return_if_fail (!chatty_eds || CHATTY_IS_EDS (chatty_eds));

  if (!g_set_object (&self->chatty_eds, chatty_eds))
    return;

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->chat_users));

  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyMmBuddy) buddy = NULL;
    ChattyContact *contact;
    const char *phone;

    buddy = g_list_model_get_item (G_LIST_MODEL (self->chat_users), i);
    phone = chatty_mm_buddy_get_number (buddy);
    contact = chatty_eds_find_by_number (self->chatty_eds, phone);
    if (contact) {
      chatty_mm_buddy_set_contact (buddy, contact);

      if (n_items == 1) {
        g_free (self->name);
        self->name = g_strdup (chatty_item_get_name (CHATTY_ITEM (contact)));
        g_object_notify (G_OBJECT (self), "name");
        g_signal_emit_by_name (self, "avatar-changed");
      }
    }
  }
}

ChattyMessage *
chatty_mm_chat_find_message_with_id (ChattyMmChat *self,
                                     const char   *id)
{
  guint n_items;

  g_return_val_if_fail (CHATTY_IS_MM_CHAT (self), NULL);
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

ChattyMmBuddy *
chatty_mm_chat_get_user (ChattyMmChat *self)
{
  g_autoptr(ChattyMmBuddy) buddy = NULL;

  g_return_val_if_fail (CHATTY_IS_MM_CHAT (self), NULL);

  if (g_list_model_get_n_items (G_LIST_MODEL (self->chat_users)) != 1)
    return NULL;

  buddy = g_list_model_get_item (G_LIST_MODEL (self->chat_users), 0);

  return buddy;
}

void
chatty_mm_chat_append_message (ChattyMmChat  *self,
                               ChattyMessage *message)
{
  g_return_if_fail (CHATTY_IS_MM_CHAT (self));
  g_return_if_fail (CHATTY_IS_MESSAGE (message));

  g_list_store_append (self->message_store, message);
  g_signal_emit_by_name (self, "changed", 0);
}

void
chatty_mm_chat_prepend_message (ChattyMmChat  *self,
                                ChattyMessage *message)
{
  g_return_if_fail (CHATTY_IS_MM_CHAT (self));
  g_return_if_fail (CHATTY_IS_MESSAGE (message));

  g_list_store_insert (self->message_store, 0, message);
  g_signal_emit_by_name (self, "changed", 0);
}

void
chatty_mm_chat_prepend_messages (ChattyMmChat *self,
                                 GPtrArray    *messages)
{
  g_return_if_fail (CHATTY_IS_MM_CHAT (self));

  if (!messages || messages->len == 0)
    return;

  g_return_if_fail (CHATTY_IS_MESSAGE (messages->pdata[0]));

  g_list_store_splice (self->message_store, 0, 0, messages->pdata, messages->len);
  g_signal_emit_by_name (self, "changed", 0);
}

void
chatty_mm_chat_add_user (ChattyMmChat  *self,
                         ChattyMmBuddy *buddy)
{
  g_return_if_fail (CHATTY_IS_MM_CHAT (self));
  g_return_if_fail (CHATTY_IS_MM_BUDDY (buddy));

  g_list_store_append (self->chat_users, buddy);
}

void
chatty_mm_chat_add_users (ChattyMmChat *self,
                          GPtrArray    *users)
{
  g_return_if_fail (CHATTY_IS_MM_CHAT (self));

  if (!users || users->len == 0)
    return;

  g_return_if_fail (CHATTY_IS_MM_BUDDY (users->pdata[0]));

  g_list_store_splice (self->chat_users, 0, 0, users->pdata, users->len);
}

void
chatty_mm_chat_delete (ChattyMmChat *self)
{
  ChattyAccount *account;

  g_return_if_fail (CHATTY_IS_MM_CHAT (self));

  account = chatty_chat_get_account (CHATTY_CHAT (self));
  chatty_mm_account_delete_chat (CHATTY_MM_ACCOUNT (account), CHATTY_CHAT (self));
}

/**
 * chatty_mm_chat_show_notification:
 * @self: A #ChattyMmChat
 *
 * Show notification for the last unread #ChattyMessage,
 * if any.
 */
void
chatty_mm_chat_show_notification (ChattyMmChat *self)
{
  g_autoptr(ChattyMessage) message = NULL;
  ChattyChat *chat;
  guint n_items;

  g_return_if_fail (CHATTY_IS_MM_CHAT (self));

  chat = CHATTY_CHAT (self);

  n_items = g_list_model_get_n_items (chatty_chat_get_messages (chat));
  message = g_list_model_get_item (chatty_chat_get_messages (chat), n_items - 1);

  if (g_set_object (&self->last_notify_message, message))
    chatty_notification_show_message (self->notification, chat, message, NULL);
}
