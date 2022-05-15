/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-chat-list.h
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "chatty-chat.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_CHAT_LIST (chatty_chat_list_get_type ())

G_DECLARE_FINAL_TYPE (ChattyChatList, chatty_chat_list, CHATTY, CHAT_LIST, GtkBox)

GPtrArray  *chatty_chat_list_get_selected        (ChattyChatList   *self);
void        chatty_chat_list_set_selection_mode  (ChattyChatList   *self,
                                                  GtkSelectionMode  mode);
void        chatty_chat_list_select_first        (ChattyChatList   *self);
void        chatty_chat_list_filter_protocol     (ChattyChatList   *self,
                                                  ChattyProtocol    protocol);
void        chatty_chat_list_filter_string       (ChattyChatList   *self,
                                                  const char       *needle);
void        chatty_chat_list_show_archived       (ChattyChatList   *self,
                                                  gboolean          show_archived);
gboolean    chatty_chat_list_is_archived         (ChattyChatList   *self);
void        chatty_chat_list_refilter            (ChattyChatList   *self);
GListModel *chatty_chat_list_get_filter_model    (ChattyChatList   *self);

G_END_DECLS
