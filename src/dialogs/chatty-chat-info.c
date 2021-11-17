/* -*- mode: c; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* chatty-chat-info.c
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-chat-info"

#include "config.h"

#include "chatty-chat-info.h"
#include "chatty-log.h"

typedef struct
{
} ChattyAccountPrivate;

typedef struct
{
  ChattyChat *chat;
} ChattyChatInfoPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ChattyChatInfo, chatty_chat_info, HDY_TYPE_PREFERENCES_PAGE)

static void
chatty_info_real_set_item (ChattyChatInfo *self,
                           ChattyChat     *chat)
{
  /* Do nothing */
}

static void
chatty_chat_info_class_init (ChattyChatInfoClass *klass)
{
  klass->set_item = chatty_info_real_set_item;
}

static void
chatty_chat_info_init (ChattyChatInfo *self)
{
}

ChattyChat *
chatty_chat_info_get_item (ChattyChatInfo *self)
{
  ChattyChatInfoPrivate *priv = chatty_chat_info_get_instance_private (self);

  g_return_val_if_fail (CHATTY_IS_CHAT_INFO (self), NULL);

  return priv->chat;
}

void
chatty_chat_info_set_item (ChattyChatInfo *self,
                           ChattyChat     *chat)
{
  ChattyChatInfoPrivate *priv = chatty_chat_info_get_instance_private (self);

  g_return_if_fail (CHATTY_IS_CHAT_INFO (self));
  g_return_if_fail (!chat || CHATTY_IS_CHAT (chat));

  if (g_set_weak_pointer (&priv->chat, chat))
    CHATTY_CHAT_INFO_GET_CLASS (self)->set_item (self, chat);
}
