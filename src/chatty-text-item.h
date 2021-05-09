/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-text-item.h
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

#include "chatty-message.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_TEXT_ITEM (chatty_text_item_get_type ())

G_DECLARE_FINAL_TYPE (ChattyTextItem, chatty_text_item, CHATTY, TEXT_ITEM, GtkBin)

GtkWidget     *chatty_text_item_new        (ChattyMessage  *message,
                                            ChattyProtocol  protocol);
ChattyMessage *chatty_text_item_get_item   (ChattyTextItem *self);
const char    *chatty_text_item_get_text   (ChattyTextItem *self);

G_END_DECLS
