/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */



#define G_LOG_DOMAIN "chatty-new-chat-dialog"

#include "config.h"

#define _GNU_SOURCE
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include "contrib/gtk.h"

#include "chatty-manager.h"
#include "chatty-chat.h"
#include "chatty-purple.h"
#include "chatty-contact.h"
#include "chatty-mm-account.h"
#include "chatty-ma-account.h"
#include "chatty-list-row.h"
#include "chatty-log.h"
#include "chatty-utils.h"
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
  GtkWidget *contacts_listbox;
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
  GtkWidget *start_button;
  GtkWidget *cancel_button;
  GtkWidget *add_in_contacts_button;
  GtkWidget *dummy_prefix_radio;

  GtkWidget *header_view_new_chat;
  GtkWidget *contact_list_stack;
  GtkWidget *contact_list_view;
  GtkWidget *empty_search_view;

  GtkSliceListModel  *slice_model;
  GtkFilter *filter;
  char      *search_str;

  ChattyAccount   *selected_account;
  ChattyManager   *manager;
  ChattyProtocol   active_protocols;

  GPtrArray  *selected_items;
  ChattyItem *selected_item;
  char       *phone_number;

  ChattyContact *dummy_contact;
  GCancellable  *cancellable;

  gboolean multi_selection;
};


G_DEFINE_TYPE (ChattyNewChatDialog, chatty_new_chat_dialog, GTK_TYPE_DIALOG)


static GtkWidget *
new_chat_contact_row_new (ChattyItem          *item,
                          ChattyNewChatDialog *self)
{
  GtkWidget *row;

  row = chatty_list_contact_row_new (item);
  chatty_list_row_set_selectable (CHATTY_LIST_ROW (row), self->multi_selection);

  return row;
}

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
new_chat_dialog_contact_is_selected (ChattyContact *contact)
{
  g_return_val_if_fail (CHATTY_IS_CONTACT (contact), FALSE);

  return !!g_object_get_data (G_OBJECT (contact), "selected");
}

static gboolean
dialog_filter_item_cb (ChattyItem          *item,
                       ChattyNewChatDialog *self)
{
  g_return_val_if_fail (CHATTY_IS_NEW_CHAT_DIALOG (self), FALSE);

  if (self->multi_selection) {
    ChattyAccount *mm_account;

    mm_account  = chatty_manager_get_mm_account (self->manager);
    if (chatty_account_get_status (mm_account) == CHATTY_CONNECTED) {
      if (chatty_item_matches (item, self->search_str, CHATTY_PROTOCOL_MMS_SMS, TRUE) &&
          !new_chat_dialog_contact_is_selected (CHATTY_CONTACT (item)))
        return TRUE;
      else
        return FALSE;
    } else
      return FALSE;
  }

#ifdef PURPLE_ENABLED
  if (CHATTY_IS_PP_BUDDY (item)) {
    ChattyAccount *account;

    account = chatty_pp_buddy_get_account (CHATTY_PP_BUDDY (item));

    if (chatty_account_get_status (account) != CHATTY_CONNECTED)
      return FALSE;
  }
#endif

  if (CHATTY_IS_CHAT (item)) {
    ChattyAccount *account;

#ifdef PURPLE_ENABLED
    /* Hide chat if it's buddy chat as the buddy is shown separately */
    if (CHATTY_IS_PP_CHAT (item) &&
        chatty_pp_chat_get_purple_buddy (CHATTY_PP_CHAT (item)))
      return FALSE;
#endif

    account = chatty_chat_get_account (CHATTY_CHAT (item));

    if (!account || chatty_account_get_status (account) != CHATTY_CONNECTED)
      return FALSE;
  }

  return chatty_item_matches (item, self->search_str, self->active_protocols, TRUE);
}

static void
start_button_clicked_cb (ChattyNewChatDialog *self)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_OK);
}

static void
cancel_button_clicked_cb (ChattyNewChatDialog *self)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_CANCEL);
}

static void
chatty_new_chat_dialog_update_selectable (GtkWidget *widget,
                                          gpointer   callback_data)
{
  ChattyNewChatDialog *self = callback_data;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  chatty_list_row_set_selectable ((ChattyListRow *)widget, self->multi_selection);
}

void
chatty_new_chat_dialog_set_multi_selection (ChattyNewChatDialog *self,
                                            gboolean             enable)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  self->multi_selection = enable;
  gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (self->header_view_new_chat), !enable);

  chatty_list_row_set_selectable (CHATTY_LIST_ROW (self->new_contact_row), enable);
  gtk_container_foreach (GTK_CONTAINER (self->chats_listbox),
                         (GtkCallback)chatty_new_chat_dialog_update_selectable, self);

  if (enable) {
    gtk_widget_show (self->start_button);
    gtk_widget_show (self->contacts_listbox);
    gtk_widget_show (self->cancel_button);
    gtk_widget_hide (self->edit_contact_button);
    gtk_widget_set_sensitive (self->start_button, FALSE);
  } else {
    gtk_widget_show (self->edit_contact_button);
    gtk_widget_hide (self->contacts_listbox);
    gtk_widget_hide (self->start_button);
    gtk_widget_hide (self->cancel_button);
  }
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
  gtk_widget_set_sensitive (self->add_in_contacts_button, TRUE);
  gtk_stack_set_visible_child_name (GTK_STACK (self->new_chat_stack), "view-new-chat");

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
  gtk_widget_set_sensitive (self->add_in_contacts_button, FALSE);
  chatty_eds_open_contacts_app (chatty_eds,
                                self->cancellable,
                                open_contacts_finish_cb, self);
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
contact_search_entry_activated_cb (ChattyNewChatDialog *self)
{
  GtkListBox *box;
  GtkWidget *row;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  box = GTK_LIST_BOX (self->chats_listbox);

  if (gtk_widget_is_visible (self->new_contact_row))
    row = self->new_contact_row;
  else
    row = (GtkWidget *)gtk_list_box_get_row_at_index (box, 0);

  if (row) {
    self->selected_item = chatty_list_row_get_item (CHATTY_LIST_ROW (row));

    gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_OK);
  }
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
  guint len;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));
  g_assert (GTK_IS_ENTRY (entry));

  str = gtk_entry_get_text (entry);

  if (!str)
    str = "";

  old_needle = self->search_str;
  self->search_str = g_utf8_casefold (str, -1);
  len = strlen (self->search_str);

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
  /* It is confusing for the dummy contact to disappear if the length is less than 3 */
  if (!valid && len < 3 && len > 0) {
    guint end_len;
    end_len = strspn (self->search_str, "0123456789");

    if (end_len == len)
      valid = TRUE;
  }
  account = chatty_manager_get_mm_account (self->manager);
  valid = valid && chatty_account_get_status (account) == CHATTY_CONNECTED;
  gtk_widget_set_visible (self->new_contact_row, valid);

  if (valid || g_list_model_get_n_items (G_LIST_MODEL (self->slice_model)) > 0) {
    gtk_stack_set_visible_child (GTK_STACK (self->contact_list_stack), self->contact_list_view);
    if (self->multi_selection) {
      ChattyItem *selected_item;
      const char *phone_number;
      gboolean has_match = FALSE;

      selected_item = chatty_list_row_get_item (CHATTY_LIST_ROW (self->new_contact_row));
      phone_number = chatty_item_get_username (selected_item);

      /* Check if search string matches an item in the selected list */
      for (guint i = 0; i < self->selected_items->len; i++) {
        const char *item_number = NULL;
        ChattyItem *item;

        item = self->selected_items->pdata[i];
        if (item)
          item_number = chatty_item_get_username (item);

        if (g_strcmp0 (item_number, phone_number) == 0) {
          has_match = TRUE;
          break;
        }
      }

      chatty_list_row_select (CHATTY_LIST_ROW (self->new_contact_row), has_match);

      gtk_container_foreach (GTK_CONTAINER (self->chats_listbox),
                             (GtkCallback)chatty_new_chat_dialog_update_selectable,
                             self);
    }
  } else
    gtk_stack_set_visible_child (GTK_STACK (self->contact_list_stack), self->empty_search_view);
}

static void
selected_contact_row_activated_cb (ChattyNewChatDialog *self,
                                   ChattyListRow       *row)
{
  const char *phone_number = NULL;
  ChattyItem *selected_item;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));
  g_assert (CHATTY_IS_LIST_ROW (row));

  selected_item = chatty_list_row_get_item (row);
  phone_number = chatty_item_get_username (selected_item);

  for (guint i = 0; i < self->selected_items->len; i++) {
    const char *item_number = NULL;
    ChattyItem *item;

    item = self->selected_items->pdata[i];
    if (item)
      item_number = chatty_item_get_username (item);

    if (g_strcmp0 (item_number, phone_number) == 0) {
      CHATTY_DEBUG (phone_number, "Removing item from list:");
      g_ptr_array_remove_index (self->selected_items, i);

      if (!self->selected_items->len)
        gtk_widget_set_sensitive (self->start_button, FALSE);

      break;
    }
  }
  if (chatty_contact_is_dummy (CHATTY_CONTACT (selected_item))) {
    ChattyItem *dummy_item;
    const char *dummy_number;

    dummy_item = chatty_list_row_get_item (CHATTY_LIST_ROW (self->new_contact_row));
    dummy_number = chatty_item_get_username (dummy_item);
    if (g_strcmp0 (dummy_number, phone_number) == 0)
      chatty_list_row_select (CHATTY_LIST_ROW (self->new_contact_row), FALSE);

  } else {
    g_autofree char *contacts_search_entry = NULL;

    contacts_search_entry = g_strdup(gtk_entry_get_text (GTK_ENTRY (self->contacts_search_entry)));
    g_object_set_data (G_OBJECT (selected_item), "selected", GINT_TO_POINTER (FALSE));
    if (!*contacts_search_entry)
      gtk_entry_set_text (GTK_ENTRY (self->contacts_search_entry), "reset");
    else
      gtk_entry_set_text (GTK_ENTRY (self->contacts_search_entry), "");

    contact_search_entry_changed_cb (self, GTK_ENTRY (self->contacts_search_entry));
    gtk_entry_set_text (GTK_ENTRY (self->contacts_search_entry), contacts_search_entry);
  }
  gtk_widget_destroy (GTK_WIDGET (row));
}

static void
contact_row_activated_cb (ChattyNewChatDialog *self,
                          ChattyListRow       *row)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));
  g_assert (CHATTY_IS_LIST_ROW (row));

  if (self->multi_selection) {
    GtkListBoxRow *chats_listbox_row;
    GtkWidget *new_row = NULL;
    ChattyItem *selected_item;
    const char *phone_number;

    selected_item = chatty_list_row_get_item (row);
    phone_number = chatty_item_get_username (selected_item);
    /* If there is a dummy contact, make sure it is not already selected */
    if (chatty_contact_is_dummy (CHATTY_CONTACT (selected_item))) {
      guint i = 0;

      chatty_list_row_select (CHATTY_LIST_ROW (self->new_contact_row), TRUE);
      chats_listbox_row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->contacts_listbox), 0);
      while (chats_listbox_row != NULL) {
        ChattyItem *item = chatty_list_row_get_item (CHATTY_LIST_ROW (chats_listbox_row));
        const char *item_number = chatty_item_get_username (item);
        if (g_strcmp0 (item_number, phone_number) == 0) {
          selected_contact_row_activated_cb (self, (CHATTY_LIST_ROW (chats_listbox_row)));
          return;
        }
        i++;
        chats_listbox_row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->contacts_listbox), i);
      }
    }

    CHATTY_DEBUG_MSG ("Adding %s to list", phone_number);
    if (chatty_contact_is_dummy (CHATTY_CONTACT (selected_item))) {
      ChattyContact *dummy_contact = NULL;

      dummy_contact = g_object_new (CHATTY_TYPE_CONTACT, NULL);
      chatty_contact_set_name (dummy_contact, _("Unknown Contact"));
      chatty_contact_set_value (dummy_contact,
                                gtk_entry_get_text (GTK_ENTRY (self->contacts_search_entry)));
      g_object_set_data (G_OBJECT (dummy_contact), "dummy", GINT_TO_POINTER (TRUE));
      new_row = chatty_list_row_new (CHATTY_ITEM (dummy_contact));
      g_ptr_array_add (self->selected_items, dummy_contact);
    } else {
      ChattyItem *dummy_item;
      const char *dummy_number;

      dummy_item = chatty_list_row_get_item (CHATTY_LIST_ROW (self->new_contact_row));
      dummy_number = chatty_item_get_username (dummy_item);
      new_row = chatty_list_row_new (chatty_list_row_get_item (row));
      g_ptr_array_add (self->selected_items, g_object_ref (selected_item));

      g_object_set_data (G_OBJECT (selected_item), "selected", GINT_TO_POINTER (TRUE));
      gtk_widget_hide (GTK_WIDGET (row));
      if (g_strcmp0 (dummy_number, phone_number) == 0) {
        chatty_list_row_select (CHATTY_LIST_ROW (self->new_contact_row), TRUE);
      }
    }

    chatty_list_row_set_selectable (CHATTY_LIST_ROW (new_row), TRUE);
    chatty_list_row_select (CHATTY_LIST_ROW (new_row), TRUE);
    gtk_list_box_prepend (GTK_LIST_BOX (self->contacts_listbox),
                          GTK_WIDGET (new_row));
    gtk_widget_set_sensitive (self->start_button, TRUE);
  } else {
     self->selected_item = chatty_list_row_get_item (row);

     gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_OK);
  }
}

static void
add_contact_button_clicked_cb (ChattyNewChatDialog *self)
{
#ifdef PURPLE_ENABLED
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
#endif

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
  if (CHATTY_IS_MM_ACCOUNT (account))
    chatty_new_chat_set_edit_mode (self, FALSE);
  else
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

  if (chatty_account_get_status (account) <= CHATTY_DISCONNECTED &&
      !CHATTY_IS_MM_ACCOUNT (account))
    return;

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
  ChattyAccount *mm_account;
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

  /* Add sms account */
  mm_account = chatty_manager_get_mm_account (self->manager);
  chatty_new_chat_add_account_to_list (self, mm_account);

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
chatty_new_chat_unset_items (gpointer data,
                             gpointer user_data)
{
  g_object_set_data (data, "selected", GINT_TO_POINTER (FALSE));
}

static void
chatty_new_chat_dialog_show (GtkWidget *widget)
{
  ChattyNewChatDialog *self = (ChattyNewChatDialog *)widget;
  const char *contacts_search_entry;

  g_return_if_fail (CHATTY_IS_NEW_CHAT_DIALOG (self));

  g_ptr_array_foreach (self->selected_items,
                       chatty_new_chat_unset_items, NULL);
  g_ptr_array_set_size (self->selected_items, 0);
  self->selected_items->pdata[0] = NULL;

  chatty_list_row_select (CHATTY_LIST_ROW (self->new_contact_row), FALSE);

  gtk_widget_set_sensitive (self->start_button, FALSE);

  gtk_container_foreach (GTK_CONTAINER (self->contacts_listbox),
                         (GtkCallback)gtk_widget_destroy, NULL);

  /* Reset selection list */
  g_clear_pointer (&self->phone_number, g_free);
  self->selected_item = NULL;

  contacts_search_entry = gtk_entry_get_text (GTK_ENTRY (self->contacts_search_entry));
  if (!*contacts_search_entry) {
    gtk_entry_set_text (GTK_ENTRY (self->contacts_search_entry), "reset");
    contact_search_entry_changed_cb (self, GTK_ENTRY (self->contacts_search_entry));
  }
  gtk_entry_set_text (GTK_ENTRY (self->contacts_search_entry), "");

  GTK_WIDGET_CLASS (chatty_new_chat_dialog_parent_class)->show (widget);
}

static void
chatty_new_chat_dialog_dispose (GObject *object)
{
  ChattyNewChatDialog *self = (ChattyNewChatDialog *)object;

  if (self->cancellable)
    g_cancellable_cancel (self->cancellable);

  g_clear_pointer (&self->selected_items, g_ptr_array_unref);
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
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, header_view_new_chat);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contacts_search_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, new_contact_row);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contacts_listbox);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, chats_listbox);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contact_edit_grid);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contact_name_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contact_alias_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, accounts_list);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, back_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, edit_contact_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, add_contact_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, add_in_contacts_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, start_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, cancel_button);

  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contact_list_stack);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contact_list_view);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, empty_search_view);

  gtk_widget_class_bind_template_callback (widget_class, back_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, edit_contact_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, contact_stroll_edge_reached_cb);
  gtk_widget_class_bind_template_callback (widget_class, contact_search_entry_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, contact_search_entry_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, selected_contact_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, contact_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, add_contact_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, add_in_contacts_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, contact_name_text_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, account_list_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, start_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, cancel_button_clicked_cb);
}


static void
chatty_new_chat_dialog_init (ChattyNewChatDialog *self)
{
  g_autoptr(GtkFilterListModel) filter_model = NULL;
  g_autoptr(GtkSortListModel) sort_model = NULL;
  g_autoptr(GtkSorter) sorter = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));
  self->cancellable = g_cancellable_new ();

  self->multi_selection = FALSE;
  self->selected_items = g_ptr_array_new_full (1, g_object_unref);
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
                           (GtkListBoxCreateWidgetFunc)new_chat_contact_row_new,
                           g_object_ref (self), g_object_unref);

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

GPtrArray *
chatty_new_chat_dialog_get_selected_items (ChattyNewChatDialog *self)
{
  g_return_val_if_fail (CHATTY_IS_NEW_CHAT_DIALOG (self), NULL);

  return self->selected_items;
}
