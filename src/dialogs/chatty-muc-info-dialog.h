/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#pragma once

#include <handy.h>

G_BEGIN_DECLS

#define CHATTY_TYPE_MUC_INFO_DIALOG (chatty_muc_info_dialog_get_type())
G_DECLARE_FINAL_TYPE (ChattyMucInfoDialog, chatty_muc_info_dialog, CHATTY, MUC_INFO_DIALOG, GtkDialog)


GtkWidget *chatty_muc_info_dialog_new      (GtkWindow           *parent_window);
void       chatty_muc_info_dialog_set_chat (ChattyMucInfoDialog *self,
                                            ChattyChat          *chat);

G_END_DECLS
