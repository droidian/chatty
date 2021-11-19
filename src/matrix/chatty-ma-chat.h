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

#include "chatty-item.h"
#include "chatty-account.h"
#include "chatty-message.h"
#include "chatty-chat.h"
#include "chatty-enums.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_MA_CHAT (chatty_ma_chat_get_type ())

G_DECLARE_FINAL_TYPE (ChattyMaChat, chatty_ma_chat, CHATTY, MA_CHAT, ChattyChat)

ChattyMaChat *chatty_ma_chat_new                (const char     *room_id,
                                                 const char     *name,
                                                 ChattyFileInfo *avatar,
                                                 gboolean        encrypted);
void          chatty_ma_chat_set_history_db     (ChattyMaChat  *self,
                                                 gpointer       history_db);
void          chatty_ma_chat_set_matrix_db      (ChattyMaChat  *self,
                                                 gpointer       matrix_db);
void          chatty_ma_chat_set_data           (ChattyMaChat  *self,
                                                 ChattyAccount *account,
                                                 gpointer       api,
                                                 gpointer       enc);
gboolean      chatty_ma_chat_matches_id         (ChattyMaChat  *self,
                                                 const char    *room_id);
void          chatty_ma_chat_set_prev_batch     (ChattyMaChat  *self,
                                                 char          *prev_batch);
void          chatty_ma_chat_set_last_batch     (ChattyMaChat  *self,
                                                 const char    *last_batch);
void          chatty_ma_chat_add_messages       (ChattyMaChat  *self,
                                                 GPtrArray     *messages);

G_END_DECLS
