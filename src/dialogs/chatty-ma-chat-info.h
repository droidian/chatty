/* -*- mode: c; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* chatty-ma-chat-info.h
 *
 * Copyright 2021 Purism SPC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

#define CHATTY_TYPE_MA_CHAT_INFO (chatty_ma_chat_info_get_type ())

G_DECLARE_FINAL_TYPE (ChattyMaChatInfo, chatty_ma_chat_info, CHATTY, MA_CHAT_INFO, HdyPreferencesPage)

GtkWidget     *chatty_ma_chat_info_new         (void);
ChattyChat    *chatty_ma_chat_info_get_item    (ChattyMaChatInfo *self);
void           chatty_ma_chat_info_set_item    (ChattyMaChatInfo *self,
                                                ChattyChat       *chat);

G_END_DECLS
