/* -*- mode: c; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* chatty-chat-info.h
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

#define CHATTY_TYPE_CHAT_INFO (chatty_chat_info_get_type ())

G_DECLARE_DERIVABLE_TYPE (ChattyChatInfo, chatty_chat_info, CHATTY, CHAT_INFO, HdyPreferencesPage)

struct _ChattyChatInfoClass
{
  HdyPreferencesPageClass parent_class;

  void         (*set_item)          (ChattyChatInfo *self,
                                     ChattyChat     *chat);
};

ChattyChat    *chatty_chat_info_get_item    (ChattyChatInfo *self);
void           chatty_chat_info_set_item    (ChattyChatInfo *self,
                                             ChattyChat     *chat);

G_END_DECLS
