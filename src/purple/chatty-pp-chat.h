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
#include "chatty-account.h"
#include "chatty-pp-buddy.h"
#include "chatty-message.h"
#include "chatty-enums.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_PP_CHAT (chatty_pp_chat_get_type ())

G_DECLARE_FINAL_TYPE (ChattyPpChat, chatty_pp_chat, CHATTY, PP_CHAT, ChattyChat)

ChattyChat         *chatty_pp_chat_get_object             (PurpleChat         *pp_chat);
ChattyPpChat       *chatty_pp_chat_new_im_chat            (PurpleAccount      *account,
                                                           PurpleBuddy        *buddy,
                                                           gboolean            supports_encryption);
ChattyPpChat       *chatty_pp_chat_new_buddy_chat         (ChattyPpBuddy      *buddy,
                                                           gboolean            supports_encryption);
ChattyPpChat       *chatty_pp_chat_new_purple_chat        (PurpleChat         *pp_chat,
                                                           gboolean            supports_encryption);
ChattyPpChat       *chatty_pp_chat_new_purple_conv        (PurpleConversation *conv,
                                                           gboolean            supports_encryption);
void                chatty_pp_chat_set_purple_conv        (ChattyPpChat       *self,
                                                           PurpleConversation *conv);
PurpleChat         *chatty_pp_chat_get_purple_chat        (ChattyPpChat       *self);
PurpleBuddy        *chatty_pp_chat_get_purple_buddy       (ChattyPpChat       *self);
void                chatty_pp_chat_remove_purple_buddy    (ChattyPpChat       *self);
PurpleConversation *chatty_pp_chat_get_purple_conv        (ChattyPpChat       *self);
void                chatty_pp_chat_show_file_upload       (ChattyPpChat       *self);
gboolean            chatty_pp_chat_are_same               (ChattyPpChat       *a,
                                                           ChattyPpChat       *b);
gboolean            chatty_pp_chat_run_command            (ChattyPpChat       *self,
                                                           const char         *command);
gboolean            chatty_pp_chat_match_purple_conv      (ChattyPpChat       *self,
                                                           PurpleConversation *conv);
void                chatty_pp_chat_append_message         (ChattyPpChat       *self,
                                                           ChattyMessage      *message);
void                chatty_pp_chat_prepend_messages       (ChattyPpChat       *self,
                                                           GPtrArray          *messages);
void                chatty_pp_chat_add_users              (ChattyPpChat       *self,
                                                           GList              *users);
void                chatty_pp_chat_remove_user            (ChattyPpChat       *self,
                                                           const char         *user);
char               *chatty_pp_chat_get_buddy_name         (ChattyPpChat       *chat,
                                                           const char         *who);
void                chatty_pp_chat_emit_user_changed      (ChattyPpChat       *self,
                                                           const char         *user);
void                chatty_pp_chat_load_encryption_status (ChattyPpChat       *self);
GListModel         *chatty_pp_chat_get_fp_list            (ChattyPpChat       *self);
void                chatty_pp_chat_load_fp_list_async     (ChattyPpChat       *self,
                                                           GAsyncReadyCallback callback,
                                                           gpointer            user_data);
gboolean            chatty_pp_chat_load_fp_list_finish    (ChattyPpChat       *self,
                                                           GAsyncResult       *result,
                                                           GError            **error);
gboolean            chatty_pp_chat_get_show_notifications (ChattyPpChat       *self);
gboolean            chatty_pp_chat_get_show_status_msg    (ChattyPpChat       *self);
void                chatty_pp_chat_set_show_notifications (ChattyPpChat       *self,
                                                           gboolean            show);
void                chatty_pp_chat_set_show_status_msg    (ChattyPpChat       *self,
                                                           gboolean            show);
const char         *chatty_pp_chat_get_status             (ChattyPpChat       *self);
gboolean            chatty_pp_chat_get_auto_join          (ChattyPpChat       *self);
void                chatty_pp_chat_set_buddy_typing       (ChattyPpChat       *self,
                                                           gboolean            is_typing);
void                chatty_pp_chat_join                   (ChattyPpChat       *self);
void                chatty_pp_chat_leave                  (ChattyPpChat       *self);
void                chatty_pp_chat_delete                 (ChattyPpChat       *self);
void                chatty_pp_chat_save_to_contacts_async (ChattyPpChat       *self,
                                                           GAsyncReadyCallback callback,
                                                           gpointer            user_data);
gboolean            chatty_pp_chat_save_to_contacts_finish (ChattyPpChat       *self,
                                                            GAsyncResult       *result,
                                                            GError            **error);

G_END_DECLS
