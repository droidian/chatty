/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#ifndef __DIALOGS_H_INCLUDE__
#define __DIALOGS_H_INCLUDE__

#include <gtk/gtk.h>
#include <purple.h>

typedef struct {
  GtkStack          *stack_panes_settings;
  GtkStack          *stack_panes_new_chat;
  GtkLabel          *label_name;
  GtkLabel          *label_protocol;
  GtkLabel          *label_status;
  GtkListBox        *list_select_account;
  GtkEntry          *entry_invite_msg;
  GtkWidget         *dialog_edit_account;
  GtkWidget         *button_add_account;
  GtkWidget         *button_save_account;
  GtkEntry          *entry_account_name;
  GtkEntry          *entry_account_pwd;
  GtkEntry          *entry_contact_name;
  GtkEntry          *entry_contact_nick;
} chatty_dialog_data_t;

chatty_dialog_data_t *chatty_get_dialog_data(void);

GtkWidget * chatty_dialogs_create_dialog_settings (void);
GtkWidget * chatty_dialogs_create_dialog_new_chat (void);
void chatty_dialogs_show_dialog_new_contact (void);
void chatty_dialogs_show_dialog_join_muc (void);

#endif
