/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-message.h
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

#include "users/chatty-pp-buddy.h"
#include "chatty-enums.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_MESSAGE (chatty_message_get_type ())

G_DECLARE_FINAL_TYPE (ChattyMessage, chatty_message, CHATTY, MESSAGE, GObject)

ChattyMessage      *chatty_message_new             (ChattyItem         *user,
                                                    const char         *user_alias,
                                                    const char         *message,
                                                    const char         *uid,
                                                    time_t              time,
                                                    ChattyMsgDirection  direction,
                                                    ChattyMsgStatus     status);
ChattyMessage      *chatty_message_new_purple      (ChattyPpBuddy      *buddy,
                                                    PurpleConvMessage  *message);
const char         *chatty_message_get_uid         (ChattyMessage      *self);
const char         *chatty_message_get_id          (ChattyMessage      *self);
void                chatty_message_set_id          (ChattyMessage      *self,
                                                    const char         *id);
const char         *chatty_message_get_text        (ChattyMessage      *self);
ChattyItem         *chatty_message_get_user        (ChattyMessage      *self);
const char         *chatty_message_get_user_alias  (ChattyMessage      *self);
time_t              chatty_message_get_time        (ChattyMessage      *self);
ChattyMsgStatus     chatty_message_get_status      (ChattyMessage      *self);
void                chatty_message_set_status      (ChattyMessage      *self,
                                                    ChattyMsgStatus     status,
                                                    time_t              mtime);
ChattyMsgDirection  chatty_message_get_msg_direction (ChattyMessage    *self);

G_END_DECLS
