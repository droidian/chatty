/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-pp-chat.h
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

#include "chatty-chat.h"
#include "users/chatty-account.h"
#include "users/chatty-pp-buddy.h"
#include "chatty-message.h"
#include "chatty-enums.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_PP_CHAT (chatty_pp_chat_get_type ())

G_DECLARE_FINAL_TYPE (ChattyPpChat, chatty_pp_chat, CHATTY, PP_CHAT, ChattyChat)

ChattyPpChat       *chatty_pp_chat_new_im_chat            (PurpleAccount      *account,
                                                           PurpleBuddy        *buddy);
ChattyPpChat       *chatty_pp_chat_new_purple_chat        (PurpleChat         *pp_chat);
ChattyPpChat       *chatty_pp_chat_new_purple_conv        (PurpleConversation *conv);
void                chatty_pp_chat_set_purple_conv        (ChattyPpChat       *self,
                                                           PurpleConversation *conv);
ChattyProtocol      chatty_pp_chat_get_protocol           (ChattyPpChat       *self);
PurpleChat         *chatty_pp_chat_get_purple_chat        (ChattyPpChat       *self);
PurpleBuddy        *chatty_pp_chat_get_purple_buddy       (ChattyPpChat       *self);
PurpleConversation *chatty_pp_chat_get_purple_conv        (ChattyPpChat       *self);
gboolean            chatty_pp_chat_are_same               (ChattyPpChat       *a,
                                                           ChattyPpChat       *b);
gboolean            chatty_pp_chat_match_purple_conv      (ChattyPpChat       *self,
                                                           PurpleConversation *conv);
ChattyMessage      *chatty_pp_chat_find_message_with_id   (ChattyPpChat       *self,
                                                           const char         *id);
void                chatty_pp_chat_append_message         (ChattyPpChat       *self,
                                                           ChattyMessage      *message);
void                chatty_pp_chat_prepend_message        (ChattyPpChat       *self,
                                                           ChattyMessage      *message);
void                chatty_pp_chat_prepend_messages       (ChattyPpChat       *self,
                                                           GPtrArray          *messages);
void                chatty_pp_chat_add_users              (ChattyPpChat       *self,
                                                           GList              *users);
void                chatty_pp_chat_remove_user            (ChattyPpChat       *self,
                                                           const char         *user);
ChattyPpBuddy      *chatty_pp_chat_find_user              (ChattyPpChat       *self,
                                                           const char         *username);
char               *chatty_pp_chat_get_buddy_name         (ChattyPpChat       *chat,
                                                           const char         *who);
void                chatty_pp_chat_emit_user_changed      (ChattyPpChat       *self,
                                                           const char         *user);
void                chatty_pp_chat_load_encryption_status (ChattyPpChat       *self);
void                chatty_pp_chat_set_show_notifications (ChattyPpChat       *self,
                                                           gboolean            show);
gboolean            chatty_pp_chat_get_auto_join          (ChattyPpChat       *self);
void                chatty_pp_chat_set_buddy_typing       (ChattyPpChat       *self,
                                                           gboolean            is_typing);
void                chatty_pp_chat_delete                 (ChattyPpChat       *self);

G_END_DECLS
