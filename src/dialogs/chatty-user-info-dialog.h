/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#pragma once

#define HANDY_USE_UNSTABLE_API
#include <handy.h>

G_BEGIN_DECLS

#define CHATTY_TYPE_USER_INFO_DIALOG (chatty_user_info_dialog_get_type())
G_DECLARE_FINAL_TYPE (ChattyUserInfoDialog, chatty_user_info_dialog, CHATTY, USER_INFO_DIALOG, HdyDialog)


GtkWidget *chatty_user_info_dialog_new      (GtkWindow            *parent_window);
void       chatty_user_info_dialog_set_chat (ChattyUserInfoDialog *self,
                                             ChattyChat           *chat);

G_END_DECLS
