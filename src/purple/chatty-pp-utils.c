/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-pp-utils"


#include <purple.h>

#include "chatty-pp-utils.h"

PurpleBlistNode *
chatty_pp_utils_get_conv_blist_node (PurpleConversation *conv)
{
  PurpleBlistNode *node = NULL;

  switch (purple_conversation_get_type (conv)) {
  case PURPLE_CONV_TYPE_IM:
    node = PURPLE_BLIST_NODE (purple_find_buddy (conv->account,
                                                 conv->name));
    break;
  case PURPLE_CONV_TYPE_CHAT:
    node = PURPLE_BLIST_NODE (purple_blist_find_chat (conv->account,
                                                      conv->name));
    break;
  case PURPLE_CONV_TYPE_UNKNOWN:
  case PURPLE_CONV_TYPE_MISC:
  case PURPLE_CONV_TYPE_ANY:
  default:
    g_warning ("Unhandled conversation type %d",
               purple_conversation_get_type (conv));
    break;
  }
  return node;
}

ChattyMsgDirection
chatty_pp_utils_direction_from_flag (PurpleMessageFlags flag)
{
  if (flag & PURPLE_MESSAGE_SYSTEM)
    return CHATTY_DIRECTION_SYSTEM;

  if (flag & PURPLE_MESSAGE_SEND)
    return CHATTY_DIRECTION_OUT;

  if (flag & PURPLE_MESSAGE_RECV)
    return CHATTY_DIRECTION_IN;

  g_return_val_if_reached (CHATTY_DIRECTION_UNKNOWN);
}
