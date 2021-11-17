/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-list-row.h
 *
 * Copyright 2020 Purism SPC
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

#define CHATTY_TYPE_MESSAGE_ROW (chatty_message_row_get_type ())

G_DECLARE_FINAL_TYPE (ChattyMessageRow, chatty_message_row, CHATTY, MESSAGE_ROW, GtkListBoxRow)

GtkWidget     *chatty_message_row_new              (ChattyMessage  *message,
                                                    ChattyProtocol  protocol,
                                                    gboolean        is_im);
ChattyMessage *chatty_message_row_get_item         (ChattyMessageRow *self);
void           chatty_message_row_hide_footer      (ChattyMessageRow *self);
void           chatty_message_row_set_alias        (ChattyMessageRow *self,
                                                    const char       *alias);
void           chatty_message_row_show_user_detail (ChattyMessageRow *self);
void           chatty_message_row_hide_user_detail (ChattyMessageRow *self);

G_END_DECLS
