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
#include "chatty-contact-list.h"
#include "chatty-list-row.h"
#include "chatty-mm-account.h"
#include "chatty-ma-account.h"
#include "chatty-log.h"
#include "chatty-utils.h"
#include "chatty-new-chat-dialog.h"


static void chatty_new_chat_dialog_update (ChattyNewChatDialog *self);

static void chatty_new_chat_name_check (ChattyNewChatDialog *self, 
                                        GtkEntry            *entry, 
                                        GtkWidget           *button);


struct _ChattyNewChatDialog
{
  HdyWindow  parent_instance;

  GtkWidget *header_bar;
  GtkWidget *edit_contact_button;
  GtkWidget *back_button;
  GtkWidget *cancel_button;
  GtkWidget *apply_button;
  GtkWidget *progress_spinner;

  GtkWidget *group_title_entry;
  GtkWidget *self_contact_row;
  GtkWidget *selected_contact_list;
  GtkWidget *add_more_row;

  GtkWidget *contact_list;
  GtkWidget *contacts_search_entry;
  GtkWidget *contact_edit_grid;
  GtkWidget *new_chat_stack;
  GtkWidget *accounts_list;
  GtkWidget *contact_name_entry;
  GtkWidget *contact_alias_entry;
  GtkWidget *add_in_contacts_button;

  GtkWidget *main_stack;
  GtkWidget *new_group_chat_view;
  GtkWidget *contact_list_view;
  GtkWidget *add_contact_view;

  GtkWidget *dummy_prefix_radio;

  ChattyItem      *self_contact;
  ChattyAccount   *selected_account;
  ChattyManager   *manager;
  GListStore      *selection_list;

  GCancellable  *cancellable;

  gboolean multi_selection;
};


G_DEFINE_TYPE (ChattyNewChatDialog, chatty_new_chat_dialog, HDY_TYPE_WINDOW)

enum {
  SELECTION_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
start_button_clicked_cb (ChattyNewChatDialog *self)
{
  GListModel *model;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  model = G_LIST_MODEL (self->selection_list);

  if (g_list_model_get_n_items (model) < 1)
    return;

  g_signal_emit (self, signals[SELECTION_CHANGED], 0);
}

static void
cancel_button_clicked_cb (ChattyNewChatDialog *self)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  gtk_widget_hide (GTK_WIDGET (self));
}

void
chatty_new_chat_dialog_set_multi_selection (ChattyNewChatDialog *self,
                                            gboolean             enable)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  self->multi_selection = enable;
  hdy_header_bar_set_show_close_button (HDY_HEADER_BAR (self->header_bar), !enable);
  chatty_contact_list_can_multi_select (CHATTY_CONTACT_LIST (self->contact_list), enable);
  gtk_widget_set_sensitive (self->apply_button, FALSE);

  if (enable) {
    gtk_widget_set_sensitive (self->apply_button, FALSE);
    gtk_stack_set_visible_child (GTK_STACK (self->main_stack), self->new_group_chat_view);
  }
}

static void
back_button_clicked_cb (ChattyNewChatDialog *self)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  if (self->multi_selection)
    gtk_stack_set_visible_child (GTK_STACK (self->main_stack), self->new_group_chat_view);
  else
    gtk_stack_set_visible_child (GTK_STACK (self->main_stack), self->contact_list_view);
}


static void
edit_contact_button_clicked_cb (ChattyNewChatDialog *self)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  chatty_new_chat_dialog_update (self);

  gtk_stack_set_visible_child (GTK_STACK (self->main_stack), self->add_contact_view);
}

static void
new_chat_dialog_page_changed_cb (ChattyNewChatDialog *self)
{
  GtkWidget *visible_child;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  gtk_widget_hide (self->back_button);
  gtk_widget_hide (self->cancel_button);
  gtk_widget_hide (self->apply_button);
  gtk_widget_hide (self->edit_contact_button);
  gtk_widget_show (self->apply_button);
  hdy_header_bar_set_show_close_button (HDY_HEADER_BAR (self->header_bar), FALSE);

  visible_child = gtk_stack_get_visible_child (GTK_STACK (self->main_stack));

  if (visible_child == self->contact_list_view) {
    if (self->multi_selection) {
      gtk_button_set_label (GTK_BUTTON (self->apply_button), _("Add"));
      gtk_widget_show (self->back_button);
    } else {
      gtk_widget_hide (self->apply_button);
      hdy_header_bar_set_show_close_button (HDY_HEADER_BAR (self->header_bar), TRUE);
      gtk_widget_show (self->edit_contact_button);
    }
  } else if (visible_child == self->new_group_chat_view) {
    gboolean can_create;

    gtk_button_set_label (GTK_BUTTON (self->apply_button), _("Create"));
    gtk_widget_show (self->cancel_button);

    can_create = g_list_model_get_n_items (G_LIST_MODEL (self->selection_list)) > 0;
    gtk_widget_set_sensitive (self->apply_button, can_create);
  } else {
    gtk_button_set_label (GTK_BUTTON (self->apply_button), _("Add Contact"));
   gtk_widget_show (self->back_button);
  }
}

static void
add_more_contact_row_activated_cb (ChattyNewChatDialog *self)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  gtk_stack_set_visible_child (GTK_STACK (self->main_stack), self->contact_list_view);
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
  gtk_stack_set_visible_child (GTK_STACK (self->main_stack), self->contact_list_view);

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
contact_list_selection_changed_cb (ChattyNewChatDialog *self)
{
  GListModel *model;
  guint n_items;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  model = G_LIST_MODEL (self->selection_list);
  n_items = g_list_model_get_n_items (model);

  if (self->multi_selection) {
    gtk_widget_set_sensitive (self->apply_button, n_items > 0);
  } else if (n_items > 0) {
    g_signal_emit (self, signals[SELECTION_CHANGED], 0);
  }
}

static void
contact_search_entry_activated_cb (ChattyNewChatDialog *self)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  if (self->multi_selection)
    start_button_clicked_cb (self);
  else
    contact_list_selection_changed_cb (self);
}

static void
contact_search_entry_changed_cb (ChattyNewChatDialog *self,
                                 GtkEntry            *entry)
{
  const char *str;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));
  g_assert (GTK_IS_ENTRY (entry));

  str = gtk_entry_get_text (entry);

  chatty_contact_list_set_filter (CHATTY_CONTACT_LIST (self->contact_list),
                                  CHATTY_PROTOCOL_ANY, str ?: "");
}

static void
contact_name_text_changed_cb (ChattyNewChatDialog *self)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  chatty_new_chat_name_check (self,
                              GTK_ENTRY (self->contact_name_entry), 
                              self->apply_button);
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
new_chat_dialog_apply_button_clicked_cb (ChattyNewChatDialog *self)
{
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  if (gtk_stack_get_visible_child (GTK_STACK (self->main_stack)) == self->add_contact_view) {
#ifdef PURPLE_ENABLED
    GPtrArray *buddies;
    const char *who, *alias;

    g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

    if (!gtk_widget_get_sensitive (self->apply_button))
      return;

    buddies = g_ptr_array_new_full (1, g_free);

    who = gtk_entry_get_text (GTK_ENTRY (self->contact_name_entry));
    alias = gtk_entry_get_text (GTK_ENTRY (self->contact_alias_entry));
    chatty_pp_account_add_buddy (CHATTY_PP_ACCOUNT (self->selected_account), who, alias);

    g_ptr_array_add (buddies, g_strdup (who));
    chatty_account_start_direct_chat_async (self->selected_account, buddies, NULL, NULL);
#endif

    gtk_widget_hide (GTK_WIDGET (self));
  } else if (gtk_stack_get_visible_child (GTK_STACK (self->main_stack)) == self->contact_list_view) {
    GListModel *model;

    g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

    model = G_LIST_MODEL (self->selection_list);

    if (g_list_model_get_n_items (model) == 0)
      return;

    if (self->multi_selection)
      gtk_stack_set_visible_child (GTK_STACK (self->main_stack), self->new_group_chat_view);
    else
      g_signal_emit (self, signals[SELECTION_CHANGED], 0);
  } else {
    g_signal_emit (self, signals[SELECTION_CHANGED], 0);
  }
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
    gtk_widget_show (GTK_WIDGET (self->apply_button));
    gtk_widget_hide (GTK_WIDGET (self->add_in_contacts_button));
  } else {
    gtk_widget_hide (GTK_WIDGET (self->contact_edit_grid));
    gtk_widget_hide (GTK_WIDGET (self->apply_button));
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
  if (CHATTY_IS_MM_ACCOUNT (account))
    hdy_preferences_row_set_title (HDY_PREFERENCES_ROW (row), _("SMS"));
  else
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
new_chat_dialog_selected_contacts_changed_cb (ChattyNewChatDialog *self)
{
  guint n_items;

  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (self));

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->selection_list));
  gtk_widget_set_visible (GTK_WIDGET (self->selected_contact_list), n_items > 0);
  gtk_widget_set_sensitive (GTK_WIDGET (self->apply_button), n_items > 0);
}

static void
chatty_new_chat_dialog_show (GtkWidget *widget)
{
  ChattyNewChatDialog *self = (ChattyNewChatDialog *)widget;

  g_return_if_fail (CHATTY_IS_NEW_CHAT_DIALOG (self));

  /* Reset selection list */
  g_list_store_remove_all (self->selection_list);

  gtk_widget_set_sensitive (self->apply_button, FALSE);
  gtk_entry_set_text (GTK_ENTRY (self->contacts_search_entry), "");
  gtk_stack_set_visible_child (GTK_STACK (self->main_stack), self->contact_list_view);
  new_chat_dialog_page_changed_cb (self);
  chatty_new_chat_dialog_set_multi_selection (self, self->multi_selection);
  new_chat_dialog_selected_contacts_changed_cb (self);

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

  G_OBJECT_CLASS (chatty_new_chat_dialog_parent_class)->dispose (object);
}

static void
chatty_new_chat_dialog_class_init (ChattyNewChatDialogClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = chatty_new_chat_dialog_dispose;

  widget_class->show = chatty_new_chat_dialog_show;

  signals [SELECTION_CHANGED] =
    g_signal_new ("selection-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-dialog-new-chat.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, header_bar);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, edit_contact_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, back_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, cancel_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, apply_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, progress_spinner);

  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, group_title_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, self_contact_row);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, selected_contact_list);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, add_more_row);

  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contacts_search_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contact_list);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contact_edit_grid);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contact_name_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contact_alias_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, accounts_list);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, add_in_contacts_button);

  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, main_stack);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, new_group_chat_view);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, contact_list_view);
  gtk_widget_class_bind_template_child (widget_class, ChattyNewChatDialog, add_contact_view);

  gtk_widget_class_bind_template_callback (widget_class, edit_contact_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, back_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, cancel_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, new_chat_dialog_apply_button_clicked_cb);

  gtk_widget_class_bind_template_callback (widget_class, new_chat_dialog_page_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, add_more_contact_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, contact_search_entry_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, contact_search_entry_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, contact_list_selection_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, add_in_contacts_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, contact_name_text_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, account_list_row_activated_cb);
}


static void
chatty_new_chat_dialog_init (ChattyNewChatDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
  self->cancellable = g_cancellable_new ();

  self->self_contact = CHATTY_ITEM (chatty_contact_dummy_new (_("You"), "MMS"));
  chatty_list_row_set_item (CHATTY_LIST_ROW (self->self_contact_row), self->self_contact);

  self->selection_list = g_list_store_new (CHATTY_TYPE_ITEM);
  g_signal_connect_object (self->selection_list, "items-changed",
                           G_CALLBACK (new_chat_dialog_selected_contacts_changed_cb),
                           self, G_CONNECT_SWAPPED);
  chatty_contact_list_set_selection_store (CHATTY_CONTACT_LIST (self->contact_list),
                                           self->selection_list);
  chatty_contact_list_show_selected_only (CHATTY_CONTACT_LIST (self->selected_contact_list));
  chatty_contact_list_set_selection_store (CHATTY_CONTACT_LIST (self->selected_contact_list),
                                           self->selection_list);
  chatty_contact_list_can_multi_select (CHATTY_CONTACT_LIST (self->selected_contact_list), TRUE);

  self->multi_selection = FALSE;
  self->dummy_prefix_radio = gtk_radio_button_new_from_widget (GTK_RADIO_BUTTON (NULL));

  self->manager = g_object_ref (chatty_manager_get_default ());
}


GtkWidget *
chatty_new_chat_dialog_new (GtkWindow *parent_window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (parent_window), NULL);

  return g_object_new (CHATTY_TYPE_NEW_CHAT_DIALOG,
                       "transient-for", parent_window,
                       NULL);
}

GListModel *
chatty_new_chat_dialog_get_selected_items (ChattyNewChatDialog *self)
{
  g_return_val_if_fail (CHATTY_IS_NEW_CHAT_DIALOG (self), NULL);

  return G_LIST_MODEL (self->selection_list);
}

const char *
chatty_new_chat_dialog_get_chat_title (ChattyNewChatDialog *self)
{
  const char *group_title;

  g_return_val_if_fail (CHATTY_IS_NEW_CHAT_DIALOG (self), NULL);

  group_title = gtk_entry_get_text (GTK_ENTRY (self->group_title_entry));

  if (group_title && *group_title)
    return group_title;

  return NULL;
}
