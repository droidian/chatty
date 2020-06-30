/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */



#define G_LOG_DOMAIN "chatty-new-muc-dialog"

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include "chatty-window.h"
#include "users/chatty-pp-account.h"
#include "chatty-conversation.h"
#include "chatty-dbus.h"
#include "chatty-utils.h"
#include "chatty-new-muc-dialog.h"


static void chatty_new_muc_name_check (ChattyNewMucDialog *self, 
                                       GtkEntry           *entry, 
                                       GtkWidget          *button);


struct _ChattyNewMucDialog
{
  HdyDialog  parent_instance;

  GtkWidget *accounts_list;
  GtkWidget *button_join_chat;
  GtkWidget *entry_group_chat_id;
  GtkWidget *entry_group_chat_room_alias;
  GtkWidget *entry_group_chat_user_alias;
  GtkWidget *entry_group_chat_pw;
  GtkWidget *dummy_prefix_radio;

  ChattyPpAccount *selected_account;
};


G_DEFINE_TYPE (ChattyNewMucDialog, chatty_new_muc_dialog, HDY_TYPE_DIALOG)


static void
chatty_blist_join_group_chat (PurpleAccount *account,
                              const char    *group_chat_id,
                              const char    *room_alias,
                              const char    *user_alias,
                              const char    *pwd)
{
  PurpleChat               *chat;
  PurpleGroup              *group;
  PurpleConnection         *gc;
  PurplePluginProtocolInfo *info;
  GHashTable               *hash = NULL;

  if (!purple_account_is_connected (account) || !group_chat_id)
    return;

  gc = purple_account_get_connection (account);

  info = PURPLE_PLUGIN_PROTOCOL_INFO(purple_connection_get_prpl (gc));

  if (info->chat_info_defaults != NULL)
    hash = info->chat_info_defaults(gc, group_chat_id);

  if (*user_alias != '\0')
    g_hash_table_replace (hash, "handle", g_strdup (user_alias));

  chat = purple_chat_new (account, group_chat_id, hash);

  if (chat != NULL) {
    if ((group = purple_find_group ("Chats")) == NULL) {
      group = purple_group_new ("Chats");
      purple_blist_add_group (group, NULL);
    }

    purple_blist_add_chat (chat, group, NULL);
    purple_blist_alias_chat (chat, room_alias);
    purple_blist_node_set_bool ((PurpleBlistNode*)chat,
                                "chatty-autojoin",
                                TRUE);

    chatty_conv_join_chat (chat);
  }
}


static void
button_join_chat_clicked_cb (ChattyNewMucDialog *self)
{
  PurpleAccount *account;

  g_assert (CHATTY_IS_NEW_MUC_DIALOG(self));

  account = chatty_pp_account_get_account (self->selected_account);

  chatty_blist_join_group_chat (account,
                                gtk_entry_get_text (GTK_ENTRY(self->entry_group_chat_id)),
                                gtk_entry_get_text (GTK_ENTRY(self->entry_group_chat_room_alias)),
                                gtk_entry_get_text (GTK_ENTRY(self->entry_group_chat_user_alias)),
                                gtk_entry_get_text (GTK_ENTRY(self->entry_group_chat_pw)));
}


static void
chat_name_changed_cb (ChattyNewMucDialog *self)
{
  g_assert (CHATTY_IS_NEW_MUC_DIALOG(self));

  chatty_new_muc_name_check (self,
                             GTK_ENTRY(self->entry_group_chat_id), 
                             self->button_join_chat);
}


static void
account_list_row_activated_cb (ChattyNewMucDialog *self,
                               GtkListBoxRow      *row,
                               GtkListBox         *box)
{
  ChattyPpAccount *account;
  GtkWidget       *prefix_radio;

  g_assert (CHATTY_IS_NEW_MUC_DIALOG(self));

  account = g_object_get_data (G_OBJECT(row), "row-account");
  prefix_radio = g_object_get_data (G_OBJECT(row), "row-prefix");

  self->selected_account = account;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(prefix_radio), TRUE);
}


static void
chatty_new_muc_name_check (ChattyNewMucDialog *self,
                           GtkEntry           *entry,
                           GtkWidget          *button)
{
  PurpleAccount *account;
  PurpleBuddy   *buddy = NULL;
  const char    *name;

  g_return_if_fail (CHATTY_IS_NEW_MUC_DIALOG(self));

  name = gtk_entry_get_text (entry);

  account = chatty_pp_account_get_account (self->selected_account);

  if ((*name != '\0') && account) {
    buddy = purple_find_buddy (account, name);
  }

  if ((*name != '\0') && !buddy) {
    gtk_widget_set_sensitive (button, TRUE);
  } else {
    gtk_widget_set_sensitive (button, FALSE);
  }
}


static void
chatty_new_muc_add_account_to_list (ChattyNewMucDialog *self,
                                    ChattyPpAccount    *account)
{
  HdyActionRow *row;
  const gchar  *protocol_id;
  GtkWidget    *prefix_radio_button;

  g_return_if_fail (CHATTY_IS_NEW_MUC_DIALOG(self));

  row = hdy_action_row_new ();
  g_object_set_data (G_OBJECT(row),
                     "row-account",
                     (gpointer) account);

  protocol_id = chatty_pp_account_get_protocol_id (account);

  // TODO list supported protocols here
  if ((g_strcmp0 (protocol_id, "prpl-jabber")) != 0 &&
      (g_strcmp0 (protocol_id, "prpl-matrix")) != 0 &&
      (g_strcmp0 (protocol_id, "prpl-telegram")) != 0 &&
      (g_strcmp0 (protocol_id, "prpl-delta")) != 0 &&
      (g_strcmp0 (protocol_id, "prpl-threepl")) != 0) {
    return;
  }

  if (chatty_account_get_status (CHATTY_ACCOUNT (account)) == CHATTY_DISCONNECTED) {
    return;
  }

  prefix_radio_button = gtk_radio_button_new_from_widget (GTK_RADIO_BUTTON(self->dummy_prefix_radio));
  gtk_widget_show (GTK_WIDGET(prefix_radio_button));
  gtk_widget_set_sensitive (prefix_radio_button, FALSE);
  
  g_object_set_data (G_OBJECT(row),
                     "row-prefix",
                     (gpointer)prefix_radio_button);

  hdy_action_row_add_prefix (row, GTK_WIDGET(prefix_radio_button ));
  hdy_action_row_set_title (row, chatty_pp_account_get_username (account));

  gtk_container_add (GTK_CONTAINER(self->accounts_list), GTK_WIDGET(row));

  gtk_widget_show (GTK_WIDGET(row));
}


static gboolean
chatty_new_muc_populate_account_list (ChattyNewMucDialog *self)
{
  GList         *l;
  gboolean       ret = FALSE;
  HdyActionRow  *row;

  g_return_val_if_fail (CHATTY_IS_NEW_MUC_DIALOG(self), FALSE);

  for (l = purple_accounts_get_all (); l != NULL; l = l->next) {
    ChattyPpAccount *account;
    ret = TRUE;

    account = chatty_pp_account_get_object (l->data);

    if (!chatty_pp_account_is_sms (account)) {
      chatty_new_muc_add_account_to_list (self, account);
    }
  }

  row = HDY_ACTION_ROW(gtk_list_box_get_row_at_index (GTK_LIST_BOX(self->accounts_list), 0));

  if (row) {
    account_list_row_activated_cb (self,
                                   GTK_LIST_BOX_ROW(row), 
                                   GTK_LIST_BOX(self->accounts_list));
  }

  return ret;
}


static void
chatty_new_muc_dialog_class_init (ChattyNewMucDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/chatty/"
                                               "ui/chatty-dialog-join-muc.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyNewMucDialog, accounts_list);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewMucDialog, button_join_chat);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewMucDialog, entry_group_chat_id);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewMucDialog, entry_group_chat_room_alias);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewMucDialog, entry_group_chat_user_alias);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewMucDialog, entry_group_chat_pw);

  gtk_widget_class_bind_template_callback (widget_class, chat_name_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, button_join_chat_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, account_list_row_activated_cb);
}


static void
chatty_new_muc_dialog_init (ChattyNewMucDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET(self));

  gtk_list_box_set_header_func (GTK_LIST_BOX(self->accounts_list),
                                hdy_list_box_separator_header,
                                NULL, NULL);

  self->dummy_prefix_radio = gtk_radio_button_new_from_widget (GTK_RADIO_BUTTON(NULL));

  chatty_new_muc_populate_account_list (self);
}


GtkWidget *
chatty_new_muc_dialog_new (GtkWindow *parent_window)
{
  g_return_val_if_fail (GTK_IS_WINDOW(parent_window), NULL);

  return g_object_new (CHATTY_TYPE_NEW_MUC_DIALOG,
                       "transient-for", parent_window,
                       "use-header-bar", 1,
                       NULL);
}
