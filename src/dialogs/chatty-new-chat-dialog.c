/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */



#define G_LOG_DOMAIN "chatty-new-chat-dialog"

#define _GNU_SOURCE
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include "chatty-window.h"
#include "chatty-manager.h"
#include "chatty-chat.h"
#include "chatty-pp-chat.h"
#include "users/chatty-contact.h"
#include "contrib/gtk.h"
#include "users/chatty-pp-account.h"
#include "matrix/chatty-ma-account.h"
#include "chatty-list-row.h"
#include "chatty-utils.h"
#include "chatty-phone-utils.h"
#include "chatty-new-chat-dialog.h"


static void chatty_new_chat_dialog_update (ChattyNewChatDialog *self);

static void chatty_new_chat_name_check (ChattyNewChatDialog *self, 
                                        GtkEntry            *entry, 
                                        GtkWidget           *button);


#define ITEMS_COUNT 50

struct _ChattyNewChatDialog
{
  GtkDialog  parent_instance;

  GtkWidget *chats_listbox;
  GtkWidget *new_contact_row;
  GtkWidget *contacts_search_entry;
  GtkWidget *contact_edit_grid;
  GtkWidget *new_chat_stack;
  GtkWidget *accounts_list;
  GtkWidget *contact_name_entry;
  GtkWidget *contact_alias_entry;
  GtkWidget *back_button;
  GtkWidget *add_contact_button;
  GtkWidget *edit_contact_button;
  GtkWidget *add_in_contacts_button;
  GtkWidget *dummy_prefix_radio;

  GtkWidget *contact_list_stack;
  GtkWidget *contact_list_view;
  GtkWidget *empty_search_view;

  GtkSliceListModel  *slice_model;
  GtkFilter *filter;
  char      *search_str;

  ChattyAccount   *selected_account;
  ChattyManager   *manager;
  ChattyProtocol   active_protocols;

  ChattyItem *selected_item;
  char       *phone_number;

  ChattyContact *dummy_contact;
  GCancellable  *cancellable;
};


G_DEFINE_TYPE (ChattyNewChatDialog, chatty_new_chat_dialog, GTK_TYPE_DIALOG)


static void
dialog_active_protocols_changed_cb (ChattyNewChatDialog *self)
{
  ChattyAccount *mm_account;
  ChattyProtocol protocol;
  gboolean valid;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  self->active_protocols = chatty_manager_get_active_protocols (self->manager);
  gtk_filter_changed (self->filter, GTK_FILTER_CHANGE_DIFFERENT);

  protocol = CHATTY_PROTOCOL_MMS_SMS;
  valid = protocol == chatty_utils_username_is_valid (self->search_str, protocol);
  mm_account = chatty_manager_get_mm_account (self->manager);
  valid = valid && chatty_account_get_status (mm_account) == CHATTY_CONNECTED;
  gtk_widget_set_visible (self->new_contact_row, valid);

  if (valid || g_list_model_get_n_items (G_LIST_MODEL (self->slice_model)) > 0)
    gtk_stack_set_visible_child (GTK_STACK (self->contact_list_stack), self->contact_list_view);
  else
    gtk_stack_set_visible_child (GTK_STACK (self->contact_list_stack), self->empty_search_view);
}


static gboolean
dialog_filter_item_cb (ChattyItem          *item,
                       ChattyNewChatDialog *self)
{
  g_return_val_if_fail (CHATTY_IS_NEW_CHAT_DIALOG (self), FALSE);

  if (CHATTY_IS_PP_BUDDY (item))
    if (chatty_pp_buddy_get_contact (CHATTY_PP_BUDDY (item)))
      return FALSE;

  if (CHATTY_IS_PP_BUDDY (item)) {
    ChattyAccount *account;

    account = chatty_pp_buddy_get_account (CHATTY_PP_BUDDY (item));

    if (chatty_account_get_status (account) != CHATTY_CONNECTED)
      return FALSE;
  }

  if (CHATTY_IS_CHAT (item)) {
    ChattyAccount *account;

    /* Hide chat if it's buddy chat as the buddy is shown separately */
    if (CHATTY_IS_PP_CHAT (item) &&
        chatty_pp_chat_get_purple_buddy (CHATTY_PP_CHAT (item)))
      return FALSE;

    account = chatty_chat_get_account (CHATTY_CHAT (item));

    if (!account || chatty_account_get_status (account) != CHATTY_CONNECTED)
      return FALSE;
  }

  return chatty_item_matches (item, self->search_str, self->active_protocols, TRUE);
}

static void
new_chat_list_changed_cb (ChattyNewChatDialog *self)
{
  guint n_items;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->slice_model));

  if (n_items > 0 || gtk_widget_get_visible (self->new_contact_row))
    gtk_stack_set_visible_child (GTK_STACK (self->contact_list_stack), self->contact_list_view);
  else
    gtk_stack_set_visible_child (GTK_STACK (self->contact_list_stack), self->empty_search_view);
}

static void
chatty_new_chat_dialog_update_new_contact_row (ChattyNewChatDialog *self)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  self->dummy_contact = g_object_new (CHATTY_TYPE_CONTACT, NULL);
  chatty_contact_set_name (self->dummy_contact, _("Send To"));
  g_object_set_data (G_OBJECT (self->dummy_contact), "dummy", GINT_TO_POINTER (TRUE));
}

static void
back_button_clicked_cb (ChattyNewChatDialog *self)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  gtk_stack_set_visible_child_name (GTK_STACK (self->new_chat_stack), "view-new-chat");
}


static void
edit_contact_button_clicked_cb (ChattyNewChatDialog *self)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  chatty_new_chat_dialog_update (self);

  gtk_stack_set_visible_child_name (GTK_STACK (self->new_chat_stack), "view-new-contact");
}

static void
open_contacts_finish_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  ChattyNewChatDialog *self = user_data;
  ChattyEds *chatty_eds = (ChattyEds *)object;
  GtkWidget *dialog;
  g_autoptr(GError) error = NULL;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));
  g_assert (CHATTY_IS_EDS (chatty_eds));

  chatty_eds_open_contacts_app_finish (chatty_eds, result, &error);

  if (!error)
    return;

  dialog = gtk_message_dialog_new (GTK_WINDOW (self),
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_WARNING,
                                   GTK_BUTTONS_CLOSE,
                                   _("Error opening GNOME Contacts: %s"),
                                   error->message);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

static void
add_in_contacts_button_clicked_cb (ChattyNewChatDialog *self)
{
  ChattyEds *chatty_eds;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  chatty_eds = chatty_manager_get_eds (self->manager);
  chatty_eds_open_contacts_app (chatty_eds,
                                self->cancellable,
                                open_contacts_finish_cb, self);

  gtk_stack_set_visible_child_name (GTK_STACK (self->new_chat_stack), "view-new-chat");
}



static void
contact_stroll_edge_reached_cb (ChattyNewChatDialog *self,
                                GtkPositionType      position)
{
  const char *name;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  if (position != GTK_POS_BOTTOM)
    return;

  name = gtk_stack_get_visible_child_name (GTK_STACK (self->new_chat_stack));

  if (!g_str_equal (name, "view-new-chat"))
    return;

  gtk_slice_list_model_set_size (self->slice_model,
                                 gtk_slice_list_model_get_size (self->slice_model) + ITEMS_COUNT);
}

static void
contact_search_entry_changed_cb (ChattyNewChatDialog *self,
                                 GtkEntry            *entry)
{
  ChattyAccount *account;
  g_autofree char *old_needle = NULL;
  const char *str;
  GtkFilterChange change;
  ChattyProtocol protocol;
  gboolean valid;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));
  g_assert (GTK_IS_ENTRY (entry));

  str = gtk_entry_get_text (entry);

  if (!str)
    str = "";

  old_needle = self->search_str;
  self->search_str = g_utf8_casefold (str, -1);

  if (!old_needle)
    old_needle = g_strdup ("");

  if (g_str_has_prefix (self->search_str, old_needle))
    change = GTK_FILTER_CHANGE_MORE_STRICT;
  else if (g_str_has_prefix (old_needle, self->search_str))
    change = GTK_FILTER_CHANGE_LESS_STRICT;
  else
    change = GTK_FILTER_CHANGE_DIFFERENT;

  gtk_slice_list_model_set_size (self->slice_model, ITEMS_COUNT);
  gtk_filter_changed (self->filter, change);

  chatty_contact_set_value (self->dummy_contact, self->search_str);
  chatty_list_row_set_item (CHATTY_LIST_ROW (self->new_contact_row),
                            CHATTY_ITEM (self->dummy_contact));

  protocol = CHATTY_PROTOCOL_MMS_SMS;
  valid = protocol == chatty_utils_username_is_valid (self->search_str, protocol);
  account = chatty_manager_get_mm_account (self->manager);
  valid = valid && chatty_account_get_status (account) == CHATTY_CONNECTED;
  gtk_widget_set_visible (self->new_contact_row, valid);

  if (valid || g_list_model_get_n_items (G_LIST_MODEL (self->slice_model)) > 0)
    gtk_stack_set_visible_child (GTK_STACK (self->contact_list_stack), self->contact_list_view);
  else
    gtk_stack_set_visible_child (GTK_STACK (self->contact_list_stack), self->empty_search_view);
}

static void
contact_row_activated_cb (ChattyNewChatDialog *self,
                          ChattyListRow       *row)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));
  g_assert (CHATTY_IS_LIST_ROW (row));

  self->selected_item = chatty_list_row_get_item (row);

  gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_OK);
}

static void
add_contact_button_clicked_cb (ChattyNewChatDialog *self)
{
  GPtrArray *buddies;
  const char *who, *alias;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  if (!gtk_widget_get_sensitive (self->add_contact_button))
    return;

  buddies = g_ptr_array_new_full (1, g_free);

  who = gtk_entry_get_text (GTK_ENTRY (self->contact_name_entry));
  alias = gtk_entry_get_text (GTK_ENTRY (self->contact_alias_entry));
  chatty_pp_account_add_buddy (CHATTY_PP_ACCOUNT (self->selected_account), who, alias);

  g_ptr_array_add (buddies, g_strdup (who));
  chatty_account_start_direct_chat_async (self->selected_account, buddies, NULL, NULL);

  gtk_widget_hide (GTK_WIDGET (self));

  gtk_entry_set_text (GTK_ENTRY (self->contact_name_entry), "");
  gtk_entry_set_text (GTK_ENTRY (self->contact_alias_entry), "");

  gtk_stack_set_visible_child_name (GTK_STACK (self->new_chat_stack), "view-new-chat");
}


static void
contact_name_text_changed_cb (ChattyNewChatDialog *self)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  chatty_new_chat_name_check (self,
                              GTK_ENTRY (self->contact_name_entry), 
                              self->add_contact_button);
}


static void
account_list_row_activated_cb (ChattyNewChatDialog *self,
                               GtkListBoxRow       *row,
                               GtkListBox          *box)
{
  ChattyAccount *account;
  GtkWidget       *prefix_radio;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  account = g_object_get_data (G_OBJECT (row), "row-account");
  prefix_radio = g_object_get_data (G_OBJECT (row), "row-prefix");

  self->selected_account = account;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (prefix_radio), TRUE);
  chatty_new_chat_set_edit_mode (self, TRUE);
}


static void
chatty_new_chat_name_check (ChattyNewChatDialog *self,
                            GtkEntry            *entry,
                            GtkWidget           *button)
{
  const char *name;
  ChattyProtocol protocol, valid_protocol;
  gboolean valid = TRUE;

  g_return_if_fail (CHATTY_IS_NEW_CHAT_DIALOG (self));

  name = gtk_entry_get_text (entry);

  protocol = chatty_item_get_protocols (CHATTY_ITEM (self->selected_account));
  valid_protocol = chatty_utils_username_is_valid (name, protocol);
  valid = protocol == valid_protocol;

  if (valid)
    valid &= !chatty_account_buddy_exists (CHATTY_ACCOUNT (self->selected_account), name);

  gtk_widget_set_sensitive (button, valid);
}


void
chatty_new_chat_set_edit_mode (ChattyNewChatDialog *self,
                               gboolean             edit)
{
  g_return_if_fail (CHATTY_IS_NEW_CHAT_DIALOG (self));
  
  if (edit) {
    gtk_widget_show (GTK_WIDGET (self->contact_edit_grid));
    gtk_widget_show (GTK_WIDGET (self->add_contact_button));
    gtk_widget_hide (GTK_WIDGET (self->add_in_contacts_button));
  } else {
    gtk_widget_hide (GTK_WIDGET (self->contact_edit_grid));
    gtk_widget_hide (GTK_WIDGET (self->add_contact_button));
    gtk_widget_show (GTK_WIDGET (self->add_in_contacts_button));
  }
}


static void
chatty_new_chat_add_account_to_list (ChattyNewChatDialog *self,
                                     ChattyAccount     *account)
{
  HdyActionRow   *row;
  GtkWidget      *prefix_radio_button;
  ChattyProtocol  protocol;

  g_return_if_fail (CHATTY_IS_NEW_CHAT_DIALOG (self));

  protocol = chatty_item_get_protocols (CHATTY_ITEM (account));

  // TODO list supported protocols here
  if (protocol & ~(CHATTY_PROTOCOL_MMS_SMS |
                   CHATTY_PROTOCOL_XMPP |
                   CHATTY_PROTOCOL_MATRIX |
                   CHATTY_PROTOCOL_TELEGRAM |
                   CHATTY_PROTOCOL_DELTA |
                   CHATTY_PROTOCOL_THREEPL))
    return;

  if (chatty_account_get_status (account) == CHATTY_DISCONNECTED) {
    return;
  }

  /* We don't handle native matrix accounts here  */
  if (CHATTY_IS_MA_ACCOUNT (account))
    return;

  row = HDY_ACTION_ROW (hdy_action_row_new ());
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
  g_object_set_data (G_OBJECT (row), "row-account", (gpointer)account);

  prefix_radio_button = gtk_radio_button_new_from_widget (GTK_RADIO_BUTTON (self->dummy_prefix_radio));
  gtk_widget_show (GTK_WIDGET (prefix_radio_button));

  gtk_widget_set_sensitive (prefix_radio_button, FALSE);

  g_object_set_data (G_OBJECT (row),
                     "row-prefix",
                     (gpointer)prefix_radio_button);

  hdy_action_row_add_prefix (row, GTK_WIDGET (prefix_radio_button ));
  hdy_preferences_row_set_title (HDY_PREFERENCES_ROW (row),
                                 chatty_item_get_username (CHATTY_ITEM (account)));

  gtk_container_add (GTK_CONTAINER (self->accounts_list), GTK_WIDGET (row));

  gtk_widget_show (GTK_WIDGET (row));
}


static void
chatty_new_chat_account_list_clear (GtkWidget *list)
{
  GList *children;
  GList *iter;

  g_return_if_fail (GTK_IS_LIST_BOX(list));

  children = gtk_container_get_children (GTK_CONTAINER (list));

  for (iter = children; iter != NULL; iter = g_list_next (iter)) {
    gtk_container_remove (GTK_CONTAINER (list), GTK_WIDGET (iter->data));
  }

  g_list_free (children);
}


static void
chatty_new_chat_populate_account_list (ChattyNewChatDialog *self)
{
  GListModel   *model;
  HdyActionRow *row;
  guint         n_items;

  g_return_if_fail (CHATTY_IS_NEW_CHAT_DIALOG (self));

  chatty_new_chat_account_list_clear (self->accounts_list);

  model = chatty_manager_get_accounts (chatty_manager_get_default ());

  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyAccount) account = NULL;

    account = g_list_model_get_item (model, i);

    chatty_new_chat_add_account_to_list (self, account);
  }

  row = HDY_ACTION_ROW(gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->accounts_list), 0));

  if (row) {
    account_list_row_activated_cb (self,
                                   GTK_LIST_BOX_ROW (row), 
                                   GTK_LIST_BOX (self->accounts_list));
  }
}


static void
chatty_new_chat_dialog_update (ChattyNewChatDialog *self)
{
  g_return_if_fail (CHATTY_IS_NEW_CHAT_DIALOG (self));

  gtk_entry_set_text (GTK_ENTRY (self->contact_name_entry), "");
  gtk_entry_set_text (GTK_ENTRY (self->contact_alias_entry), "");
  gtk_widget_grab_focus (self->contact_name_entry);

  chatty_new_chat_populate_account_list (self);
}


static void
chatty_new_chat_dialog_show (GtkWidget *widget)
{
  ChattyNewChatDialog *self = (ChattyNewChatDialog *)widget;

  /* Reset selection list */
  g_clear_pointer (&self->phone_number, g_free);
  self->selected_item = NULL;
  gtk_entry_set_text (GTK_ENTRY (self->contacts_search_entry), "");

  GTK_WIDGET_CLASS (chatty_new_chat_dialog_parent_class)->show (widget);
}

static void
chatty_new_chat_dialog_dispose (GObject *object)
{
  ChattyNewChatDialog *self = (ChattyNewChatDialog *)object;

  if (self->cancellable)
    g_cancellable_cancel (self->cancellable);

  g_clear_object (&self->cancellable);
  g_clear_object (&self->manager);
  g_clear_object (&self->slice_model);
  g_clear_object (&self->filter);
  g_clear_pointer (&self->phone_number, g_free);

  G_OBJECT_CLASS (chatty_new_chat_dialog_parent_class)->dispose (object);
}

static void
chatty_new_chat_dialog_class_init (ChattyNewChatDialogClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = chatty_new_chat_dialog_dispose;

  widget_class->show = chatty_new_chat_dialog_show;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-dialog-new-chat.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, new_chat_stack);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contacts_search_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, new_contact_row);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, chats_listbox);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contact_edit_grid);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contact_name_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contact_alias_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, accounts_list);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, back_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, edit_contact_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, add_contact_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, add_in_contacts_button);

  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contact_list_stack);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contact_list_view);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, empty_search_view);

  gtk_widget_class_bind_template_callback (widget_class, back_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, edit_contact_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, contact_stroll_edge_reached_cb);
  gtk_widget_class_bind_template_callback (widget_class, contact_search_entry_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, contact_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, add_contact_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, add_in_contacts_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, contact_name_text_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, account_list_row_activated_cb);
}


static void
chatty_new_chat_dialog_init (ChattyNewChatDialog *self)
{
  g_autoptr(GtkFilterListModel) filter_model = NULL;
  g_autoptr(GtkSortListModel) sort_model = NULL;
  g_autoptr(GtkSorter) sorter = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));
  self->cancellable = g_cancellable_new ();

  self->dummy_prefix_radio = gtk_radio_button_new_from_widget (GTK_RADIO_BUTTON (NULL));

  self->manager = g_object_ref (chatty_manager_get_default ());

  sorter = gtk_custom_sorter_new ((GCompareDataFunc)chatty_item_compare, NULL, NULL);
  sort_model = gtk_sort_list_model_new (chatty_manager_get_contact_list (self->manager), sorter);

  self->filter = gtk_custom_filter_new ((GtkCustomFilterFunc)dialog_filter_item_cb, self, NULL);
  filter_model = gtk_filter_list_model_new (G_LIST_MODEL (sort_model), self->filter);

  self->slice_model = gtk_slice_list_model_new (G_LIST_MODEL (filter_model), 0, ITEMS_COUNT);
  g_signal_connect_object (self->slice_model, "items-changed",
                           G_CALLBACK (new_chat_list_changed_cb), self,
                           G_CONNECT_SWAPPED);
  gtk_list_box_bind_model (GTK_LIST_BOX (self->chats_listbox),
                           G_LIST_MODEL (self->slice_model),
                           (GtkListBoxCreateWidgetFunc)chatty_list_contact_row_new,
                           NULL, NULL);

  g_signal_connect_object (self->manager, "notify::active-protocols",
                           G_CALLBACK (dialog_active_protocols_changed_cb), self, G_CONNECT_SWAPPED);
  dialog_active_protocols_changed_cb (self);

  chatty_new_chat_dialog_update_new_contact_row (self);
}


GtkWidget *
chatty_new_chat_dialog_new (GtkWindow *parent_window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (parent_window), NULL);

  return g_object_new (CHATTY_TYPE_NEW_CHAT_DIALOG,
                       "transient-for", parent_window,
                       "use-header-bar", 1,
                       NULL);
}


ChattyItem *
chatty_new_chat_dialog_get_selected_item (ChattyNewChatDialog *self)
{
  g_return_val_if_fail (CHATTY_IS_NEW_CHAT_DIALOG (self), NULL);

  return self->selected_item;
}
