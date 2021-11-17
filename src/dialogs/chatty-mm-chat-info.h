/*
 * Copyright (C) 2021 Purism SPC
 *               2021 Chris Talbot
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#pragma once

#include <handy.h>

#include "chatty-chat.h"
#include "chatty-chat-info.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_MM_CHAT_INFO (chatty_mm_chat_info_get_type())
G_DECLARE_FINAL_TYPE (ChattyMmChatInfo, chatty_mm_chat_info, CHATTY, MM_CHAT_INFO, ChattyChatInfo)

GtkWidget  *chatty_mm_chat_info_new                  (GtkWindow *parent_window);
void        chatty_mm_chat_info_cancel_changes       (ChattyMmChatInfo *self,
                                                      ChattyChat       *chat);
void        chatty_mm_chat_info_apply_changes        (ChattyMmChatInfo *self,
                                                      ChattyChat       *chat);

G_END_DECLS
