/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */



#define G_LOG_DOMAIN "chatty-new-muc-dialog"

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "chatty-window.h"
#include "chatty-purple.h"
#include "chatty-utils.h"
#include "chatty-manager.h"
#include "chatty-new-muc-dialog.h"


struct _ChattyNewMucDialog
{
  GtkDialog  parent_instance;

  GtkWidget *accounts_list;
  GtkWidget *button_join_chat;
  GtkWidget *entry_group_chat_id;
  GtkWidget *entry_group_chat_room_alias;
  GtkWidget *entry_group_chat_user_alias;
  GtkWidget *entry_group_chat_pw;
  GtkWidget *dummy_prefix_radio;

  ChattyAccount *selected_account;
};


G_DEFINE_TYPE (ChattyNewMucDialog, chatty_new_muc_dialog, GTK_TYPE_DIALOG)


#ifdef PURPLE_ENABLED
static void
join_new_chat_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  g_autoptr(ChattyNewMucDialog) self = user_data;

  g_assert (CHATTY_IS_NEW_MUC_DIALOG (self));
}
#endif

static void
button_join_chat_clicked_cb (ChattyNewMucDialog *self)
{
#ifdef PURPLE_ENABLED
  ChattyChat *chat;

  g_assert (CHATTY_IS_NEW_MUC_DIALOG(self));

  chat = chatty_pp_account_join_chat (CHATTY_PP_ACCOUNT (self->selected_account),
                                      gtk_entry_get_text (GTK_ENTRY(self->entry_group_chat_id)),
                                      gtk_entry_get_text (GTK_ENTRY(self->entry_group_chat_room_alias)),
                                      gtk_entry_get_text (GTK_ENTRY(self->entry_group_chat_user_alias)),
                                      gtk_entry_get_text (GTK_ENTRY(self->entry_group_chat_pw)));
  chatty_account_join_chat_async (CHATTY_ACCOUNT (self->selected_account), chat,
                                  join_new_chat_cb, g_object_ref (self));
#endif
}


static void
chat_name_changed_cb (ChattyNewMucDialog *self)
{
  const char *name;
  gboolean buddy_exists;

  g_assert (CHATTY_IS_NEW_MUC_DIALOG(self));

  name = gtk_entry_get_text (GTK_ENTRY (self->entry_group_chat_id));
  buddy_exists = chatty_account_buddy_exists (CHATTY_ACCOUNT (self->selected_account), name);
  gtk_widget_set_sensitive (self->button_join_chat, !buddy_exists);
}


static void
account_list_row_activated_cb (ChattyNewMucDialog *self,
                               GtkListBoxRow      *row,
                               GtkListBox         *box)
{
  ChattyAccount *account;
  GtkWidget       *prefix_radio;

  g_assert (CHATTY_IS_NEW_MUC_DIALOG(self));

  account = g_object_get_data (G_OBJECT(row), "row-account");
  prefix_radio = g_object_get_data (G_OBJECT(row), "row-prefix");

  self->selected_account = account;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(prefix_radio), TRUE);
}

#ifdef PURPLE_ENABLED
static void
chatty_new_muc_add_account_to_list (ChattyNewMucDialog *self,
                                    ChattyPpAccount    *account)
{
  HdyActionRow *row;
  GtkWidget    *prefix_radio_button;
  ChattyProtocol protocol;

  g_return_if_fail (CHATTY_IS_NEW_MUC_DIALOG(self));

  row = HDY_ACTION_ROW (hdy_action_row_new ());
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
  g_object_set_data (G_OBJECT(row),
                     "row-account",
                     (gpointer) account);

  protocol = chatty_item_get_protocols (CHATTY_ITEM (account));

  // TODO list supported protocols here
  if (protocol & ~(CHATTY_PROTOCOL_XMPP |
                   CHATTY_PROTOCOL_MATRIX |
                   CHATTY_PROTOCOL_TELEGRAM |
                   CHATTY_PROTOCOL_DELTA |
                   CHATTY_PROTOCOL_THREEPL))
    return;

  if (chatty_account_get_status (CHATTY_ACCOUNT (account)) <= CHATTY_DISCONNECTED) {
    return;
  }

  prefix_radio_button = gtk_radio_button_new_from_widget (GTK_RADIO_BUTTON(self->dummy_prefix_radio));
  gtk_widget_show (GTK_WIDGET(prefix_radio_button));
  gtk_widget_set_sensitive (prefix_radio_button, FALSE);
  
  g_object_set_data (G_OBJECT(row),
                     "row-prefix",
                     (gpointer)prefix_radio_button);

  hdy_action_row_add_prefix (row, GTK_WIDGET(prefix_radio_button ));
  hdy_preferences_row_set_title (HDY_PREFERENCES_ROW (row), chatty_item_get_username (CHATTY_ITEM (account)));

  gtk_container_add (GTK_CONTAINER(self->accounts_list), GTK_WIDGET(row));

  gtk_widget_show (GTK_WIDGET(row));
}
#endif


static gboolean
chatty_new_muc_populate_account_list (ChattyNewMucDialog *self)
{
  gboolean       ret = FALSE;
  HdyActionRow  *row;

  g_return_val_if_fail (CHATTY_IS_NEW_MUC_DIALOG(self), FALSE);

#ifdef PURPLE_ENABLED
  for (GList *l = purple_accounts_get_all (); l != NULL; l = l->next) {
    ChattyPpAccount *account;
    ret = TRUE;

    account = chatty_pp_account_get_object (l->data);

    if (account)
      chatty_new_muc_add_account_to_list (self, account);
  }
#endif

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
                                               "/sm/puri/Chatty/"
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
