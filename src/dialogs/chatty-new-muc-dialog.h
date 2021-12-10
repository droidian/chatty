/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#pragma once

#include <handy.h>

G_BEGIN_DECLS

#define CHATTY_TYPE_NEW_MUC_DIALOG (chatty_new_muc_dialog_get_type())
G_DECLARE_FINAL_TYPE (ChattyNewMucDialog, chatty_new_muc_dialog, CHATTY, NEW_MUC_DIALOG, HdyWindow)


GtkWidget *chatty_new_muc_dialog_new (GtkWindow *parent_window);

G_END_DECLS
