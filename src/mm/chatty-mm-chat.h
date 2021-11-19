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

#include "chatty-chat.h"
#include "chatty-mm-buddy.h"
#include "chatty-message.h"
#include "chatty-contact-provider.h"
#include "chatty-enums.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_MM_CHAT (chatty_mm_chat_get_type ())

G_DECLARE_FINAL_TYPE (ChattyMmChat, chatty_mm_chat, CHATTY, MM_CHAT, ChattyChat)

ChattyMmChat     *chatty_mm_chat_new                    (const char     *name,
                                                         const char     *alias,
                                                         ChattyProtocol  protocol,
                                                         gboolean        is_im);
gboolean          chatty_mm_chat_has_custom_name        (ChattyMmChat   *self);
void              chatty_mm_chat_set_eds                (ChattyMmChat   *self,
                                                         ChattyEds      *chatty_eds);
void              chatty_mm_chat_append_message         (ChattyMmChat   *self,
                                                         ChattyMessage  *message);
void              chatty_mm_chat_prepend_message        (ChattyMmChat   *self,
                                                         ChattyMessage  *message);
void              chatty_mm_chat_prepend_messages       (ChattyMmChat   *self,
                                                         GPtrArray      *messages);
ChattyMessage    *chatty_mm_chat_find_message_with_id   (ChattyMmChat   *self,
                                                         const char     *id);
ChattyMmBuddy    *chatty_mm_chat_find_user              (ChattyMmChat   *self,
                                                         const char     *phone);
void              chatty_mm_chat_add_user               (ChattyMmChat   *self,
                                                         ChattyMmBuddy  *buddy);
void              chatty_mm_chat_add_users              (ChattyMmChat   *self,
                                                         GPtrArray      *users);
void              chatty_mm_chat_delete                 (ChattyMmChat   *self);
void              chatty_mm_chat_refresh                (ChattyMmChat    *self);

G_END_DECLS
