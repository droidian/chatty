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
#include <purple.h>

#include "users/chatty-item.h"
#include "chatty-enums.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_CHAT (chatty_chat_get_type ())

G_DECLARE_FINAL_TYPE (ChattyChat, chatty_chat, CHATTY, CHAT, ChattyItem)

ChattyChat         *chatty_chat_new_purple_chat       (PurpleChat         *pp_chat);
PurpleChat         *chatty_chat_get_purple_chat       (ChattyChat         *self);
const char         *chatty_chat_get_username          (ChattyChat         *self);

G_END_DECLS
