/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-info-dialog.c
 *
 * Copyright (C) 2021 Purism SPC
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

#define CHATTY_TYPE_INFO_DIALOG (chatty_info_dialog_get_type ())

G_DECLARE_FINAL_TYPE (ChattyInfoDialog, chatty_info_dialog, CHATTY, INFO_DIALOG, GtkDialog)

GtkWidget     *chatty_info_dialog_new      (GtkWindow        *parent_window);
void           chatty_info_dialog_set_chat (ChattyInfoDialog *self,
                                            ChattyChat       *chat);

G_END_DECLS
