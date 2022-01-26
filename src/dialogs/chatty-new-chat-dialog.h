/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#pragma once

#include <handy.h>

#include "chatty-item.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_NEW_CHAT_DIALOG (chatty_new_chat_dialog_get_type())
G_DECLARE_FINAL_TYPE (ChattyNewChatDialog, chatty_new_chat_dialog, CHATTY, NEW_CHAT_DIALOG, GtkDialog)


GtkWidget *chatty_new_chat_dialog_new (GtkWindow *parent_window);
void chatty_new_chat_set_edit_mode (ChattyNewChatDialog *self, gboolean edit);
GListModel *chatty_new_chat_dialog_get_selected_items (ChattyNewChatDialog *self);
void chatty_new_chat_dialog_set_multi_selection (ChattyNewChatDialog *self,
                                                 gboolean             enable);
const char *chatty_new_chat_dialog_get_chat_title (ChattyNewChatDialog *self);

G_END_DECLS
