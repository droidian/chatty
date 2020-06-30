/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-chat.h
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>
#include <purple.h>

#include "users/chatty-item.h"
#include "users/chatty-pp-buddy.h"
#include "chatty-message.h"
#include "chatty-enums.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_CHAT (chatty_chat_get_type ())

G_DECLARE_FINAL_TYPE (ChattyChat, chatty_chat, CHATTY, CHAT, ChattyItem)

typedef enum {
  MSG_IS_OUTGOING,
  MSG_IS_INCOMING,
  MSG_IS_SYSTEM
} e_msg_dir;


ChattyChat         *chatty_chat_new_im_chat           (PurpleAccount      *account,
                                                       PurpleBuddy        *buddy);
ChattyChat         *chatty_chat_new_purple_chat       (PurpleChat         *pp_chat);
ChattyChat         *chatty_chat_new_purple_conv       (PurpleConversation *conv);
void                chatty_chat_set_purple_conv       (ChattyChat         *self,
                                                       PurpleConversation *conv);
ChattyProtocol      chatty_chat_get_protocol          (ChattyChat         *self);
PurpleChat         *chatty_chat_get_purple_chat       (ChattyChat         *self);
PurpleBuddy        *chatty_chat_get_purple_buddy      (ChattyChat         *self);
PurpleConversation *chatty_chat_get_purple_conv       (ChattyChat         *self);
const char         *chatty_chat_get_username          (ChattyChat         *self);
gboolean            chatty_chat_are_same              (ChattyChat         *a,
                                                       ChattyChat         *b);
gboolean            chatty_chat_match_purple_conv     (ChattyChat         *self,
                                                       PurpleConversation *conv);
GListModel         *chatty_chat_get_messages          (ChattyChat         *self);
ChattyMessage      *chatty_chat_find_message_with_id  (ChattyChat         *self,
                                                       const char         *id);
void                chatty_chat_append_message       (ChattyChat         *self,
                                                       ChattyMessage      *message);
void                chatty_chat_prepend_message       (ChattyChat         *self,
                                                       ChattyMessage      *message);
void                chatty_chat_add_users             (ChattyChat         *self,
                                                       GList              *users);
void                chatty_chat_remove_user           (ChattyChat         *self,
                                                       const char         *user);
GListModel         *chatty_chat_get_users             (ChattyChat         *self);
ChattyPpBuddy      *chatty_chat_find_user             (ChattyChat         *self,
                                                       const char         *username);
void                chatty_chat_emit_user_changed     (ChattyChat         *self,
                                                       const char         *user);
const char         *chatty_chat_get_last_message      (ChattyChat         *self);
guint               chatty_chat_get_unread_count      (ChattyChat         *self);
void                chatty_chat_set_unread_count      (ChattyChat         *self,
                                                       guint               unread_count);
time_t              chatty_chat_get_last_msg_time      (ChattyChat        *self);
ChattyEncryption    chatty_chat_get_encryption_status  (ChattyChat        *self);
void                chatty_chat_load_encryption_status (ChattyChat        *self);
void                chatty_chat_set_encryption         (ChattyChat        *self,
                                                        gboolean           enable);

G_END_DECLS
