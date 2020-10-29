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

#include "users/chatty-item.h"
#include "users/chatty-account.h"
#include "chatty-enums.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_CHAT (chatty_chat_get_type ())

G_DECLARE_DERIVABLE_TYPE (ChattyChat, chatty_chat, CHATTY, CHAT, ChattyItem)

struct _ChattyChatClass
{
  ChattyItemClass  parent_class;

  gboolean          (*is_im)              (ChattyChat *self);
  const char       *(*get_chat_name)      (ChattyChat *self);
  const char       *(*get_username)       (ChattyChat *self);
  ChattyAccount    *(*get_account)        (ChattyChat *self);
  GListModel       *(*get_messages)       (ChattyChat *self);
  GListModel       *(*get_users)          (ChattyChat *self);
  const char       *(*get_last_message)   (ChattyChat *self);
  guint             (*get_unread_count)   (ChattyChat *self);
  void              (*set_unread_count)   (ChattyChat *self,
                                           guint       unread_count);
  time_t            (*get_last_msg_time)  (ChattyChat *self);
  ChattyEncryption  (*get_encryption)     (ChattyChat *self);
  void              (*set_encryption)     (ChattyChat *self,
                                           gboolean    enable);
  gboolean          (*get_buddy_typing)   (ChattyChat *self);
};

ChattyChat         *chatty_chat_new                (const char *account_username,
                                                    const char *chat_name,
                                                    gboolean    is_im);
gboolean            chatty_chat_is_im              (ChattyChat *self);
const char         *chatty_chat_get_chat_name      (ChattyChat *self);
const char         *chatty_chat_get_username       (ChattyChat *self);
ChattyAccount      *chatty_chat_get_account        (ChattyChat *self);
GListModel         *chatty_chat_get_messages       (ChattyChat *self);
GListModel         *chatty_chat_get_users          (ChattyChat *self);
const char         *chatty_chat_get_last_message   (ChattyChat *self);
guint               chatty_chat_get_unread_count   (ChattyChat *self);
void                chatty_chat_set_unread_count   (ChattyChat *self,
                                                    guint       unread_count);
time_t              chatty_chat_get_last_msg_time  (ChattyChat *self);
ChattyEncryption    chatty_chat_get_encryption     (ChattyChat *self);
void                chatty_chat_set_encryption     (ChattyChat *self,
                                                    gboolean    enable);
gboolean            chatty_chat_get_buddy_typing   (ChattyChat *self);

G_END_DECLS
