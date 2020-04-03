/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-chat-view.h
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "chatty-conversation.h"
#include "chatty-chat.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_CHAT_VIEW (chatty_chat_view_get_type ())

G_DECLARE_FINAL_TYPE (ChattyChatView, chatty_chat_view, CHATTY, CHAT_VIEW, GtkBox)

GtkWidget  *chatty_chat_view_new      (void);
void        chatty_chat_view_purple_init   (void);
void        chatty_chat_view_purple_uninit (void);
void        chatty_chat_view_set_chat (ChattyChatView *self,
                                       ChattyChat     *chat);
ChattyChat *chatty_chat_view_get_chat (ChattyChatView *self);
void        chatty_chat_view_load     (ChattyChatView *self,
                                       guint           limit);
void        chatty_chat_view_remove_footer (ChattyChatView *self);
void        chatty_chat_view_focus_entry   (ChattyChatView *self);
void        chatty_chat_view_set_conv      (ChattyChatView *self,
                                            ChattyConversation *chatty_conv);
ChattyConversation *chatty_chat_view_get_conv (ChattyChatView *self);

G_END_DECLS
