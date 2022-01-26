/*
 * Copyright (C) 2022 Purism SPC
 *
 * Authors:
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-contact-list"

#include "config.h"

#include <glib/gi18n.h>
#include "contrib/gtk.h"

#include "chatty-chat.h"
#include "chatty-purple.h"
#include "chatty-list-row.h"
#include "chatty-manager.h"
#include "chatty-contact-list.h"

#define ITEMS_COUNT 50

struct _ChattyContactList
{
  GtkBox              parent_instance;

  GtkWidget          *scrolled_window;
  GtkWidget          *main_stack;
  GtkWidget          *empty_view;
  GtkWidget          *contact_list_view;

  GtkWidget          *selected_contact_list;
  GtkWidget          *new_contact_list;
  GtkWidget          *new_contact_row;
  GtkWidget          *contact_list;

  ChattyItem         *dummy_contact;
  GListStore         *selection_store;
  GtkSliceListModel  *slice_model;
  GtkFilter          *filter;
  char               *search_str;

  ChattyManager      *manager;

  ChattyProtocol      active_protocols;
  ChattyProtocol      filter_protocols;
  gboolean            can_multi_select;
};

G_DEFINE_TYPE (ChattyContactList, chatty_contact_list, GTK_TYPE_BOX)

enum {
  DELETE_ROW,
  SELECTION_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static GtkWidget *
new_chat_contact_row_new (ChattyItem        *item,
                          ChattyContactList *self)
{
  GtkWidget *row;

  row = chatty_list_contact_row_new (item);
  chatty_list_row_set_selectable (CHATTY_LIST_ROW (row), self->can_multi_select);

  return row;
}

static GtkWidget *
new_selected_contact_row_new (ChattyItem        *item,
                              ChattyContactList *self)
{
  GtkWidget *row;

  row = new_chat_contact_row_new (item, self);
  chatty_list_row_select (CHATTY_LIST_ROW (row), TRUE);

  if (!gtk_widget_is_visible (self->contact_list))
    chatty_list_row_show_delete_button (CHATTY_LIST_ROW (row));

  return row;
}

static gboolean
new_chat_dialog_contact_is_selected (ChattyContact *contact)
{
  g_return_val_if_fail (CHATTY_IS_CONTACT (contact), FALSE);

  return !!g_object_get_data (G_OBJECT (contact), "selected");
}

static void
update_new_contact_row (ChattyContactList *self)
{
  const char *dummy_value;
  guint end_len, n_items;
  gboolean valid = FALSE;

  chatty_contact_set_value (CHATTY_CONTACT (self->dummy_contact), self->search_str);
  chatty_list_row_set_item (CHATTY_LIST_ROW (self->new_contact_row), self->dummy_contact);

  if (!self->search_str || !*self->search_str ||
      !(self->active_protocols & CHATTY_PROTOCOL_MMS_SMS)) {
    gtk_widget_hide (self->new_contact_row);
    return;
  }

  end_len = strspn (self->search_str, "0123456789");
  valid = end_len == strlen (self->search_str);
  valid = valid || !!chatty_utils_username_is_valid (self->search_str, CHATTY_PROTOCOL_MMS_SMS);
  gtk_widget_set_visible (self->new_contact_row, valid);

  if (!self->can_multi_select)
    return;

  dummy_value = chatty_item_get_username (self->dummy_contact);
  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->selection_store));

  /* If new custom contact row match any of the selected contact, hide it */
  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyItem) item = NULL;
    const char *value;

    item = g_list_model_get_item (G_LIST_MODEL (self->selection_store), i);
    value = chatty_item_get_username (item);

    if (g_strcmp0 (value, dummy_value) == 0) {
      gtk_widget_hide (self->new_contact_row);
      break;
    }
  }
}

static gboolean
contact_list_filter_item_cb (ChattyItem        *item,
                             ChattyContactList *self)
{
  ChattyAccount *account;
  ChattyProtocol protocols;

  g_return_val_if_fail (CHATTY_IS_CONTACT_LIST (self), FALSE);

  if (self->can_multi_select) {
    account = chatty_manager_get_mm_account (self->manager);

    if (chatty_account_get_status (account) == CHATTY_CONNECTED) {
      /* Show only non-selected items, selected items are shown elsewhere */
      if (CHATTY_IS_CONTACT (item) &&
          !new_chat_dialog_contact_is_selected (CHATTY_CONTACT (item)) &&
          chatty_item_matches (item, self->search_str, CHATTY_PROTOCOL_MMS_SMS, TRUE))
        return TRUE;
    }

    return FALSE;
  }

#ifdef PURPLE_ENABLED
  if (CHATTY_IS_PP_BUDDY (item)) {
    account = chatty_pp_buddy_get_account (CHATTY_PP_BUDDY (item));

    if (chatty_account_get_status (account) != CHATTY_CONNECTED)
      return FALSE;
  }
#endif

  if (CHATTY_IS_CHAT (item)) {
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

  protocols = self->active_protocols & self->filter_protocols;

  return chatty_item_matches (item, self->search_str, protocols, TRUE);
}

static void
contact_list_edge_reached_cb (ChattyContactList *self,
                              GtkPositionType    position)
{
  g_assert (CHATTY_IS_CONTACT_LIST (self));

  if (position != GTK_POS_BOTTOM ||
      gtk_stack_get_visible_child (GTK_STACK (self->main_stack)) != self->contact_list_view)
    return;

  gtk_slice_list_model_set_size (self->slice_model,
                                 gtk_slice_list_model_get_size (self->slice_model) + ITEMS_COUNT);
}

static void
selected_contact_row_activated_cb (ChattyContactList *self,
                                   ChattyListRow     *row,
                                   GtkListBox        *list)
{
  ChattyItem *item;

  item = chatty_list_row_get_item (row);
  g_object_set_data (G_OBJECT (item), "selected", GINT_TO_POINTER (FALSE));

  if (chatty_contact_is_dummy (CHATTY_CONTACT (item))) {
    /* If the deselected item value matches the search string, show new contact row */
    if (self->search_str &&
        g_strcmp0 (chatty_item_get_username (item), self->search_str) == 0)
      gtk_widget_show (self->new_contact_row);
  } else {
    guint position;

    /* Emit items-changed so that it will be re-filtered and thus shown as it's no longer selected */
    if (chatty_utils_get_item_position (chatty_manager_get_contact_list (self->manager), item, &position))
      g_list_model_items_changed (chatty_manager_get_contact_list (self->manager), position, 1, 1);
  }

  chatty_utils_remove_list_item (self->selection_store, item);
  g_signal_emit (self, signals[SELECTION_CHANGED], 0);
}

static void
contact_list_row_activated_cb (ChattyContactList *self,
                               ChattyListRow     *row,
                               GtkListBox        *list)
{
  ChattyItem *item;

  g_assert (CHATTY_IS_CONTACT_LIST (self));
  g_assert (CHATTY_IS_LIST_ROW (row));
  g_assert (GTK_IS_LIST_BOX (list));
  g_assert (self->selection_store);

  item = chatty_list_row_get_item (row);
  if (CHATTY_IS_CONTACT (item) && chatty_contact_is_dummy (CHATTY_CONTACT (item))) {
    g_autoptr(ChattyContact) contact = NULL;

    contact = chatty_contact_dummy_new (_("Unknown Contact"),
                                        chatty_item_get_username (item));
    g_list_store_append (self->selection_store, contact);
  } else {
    g_object_set_data (G_OBJECT (item), "selected", GINT_TO_POINTER (TRUE));
    g_list_store_append (self->selection_store, item);
  }

  if (self->can_multi_select)
    gtk_widget_hide (GTK_WIDGET (row));

  g_signal_emit (self, signals[SELECTION_CHANGED], 0);
}

static void
contact_list_changed_cb (ChattyContactList *self)
{
  HdyStatusPage *page;
  gboolean empty;

  g_assert (CHATTY_IS_CONTACT_LIST (self));

  empty = !gtk_widget_get_visible (self->new_contact_row);
  empty = empty && g_list_model_get_n_items (G_LIST_MODEL (self->slice_model)) == 0;
  if (self->selection_store)
    empty = empty && g_list_model_get_n_items (G_LIST_MODEL (self->selection_store)) == 0;

  if (empty)
    gtk_stack_set_visible_child (GTK_STACK (self->main_stack), self->empty_view);
  else
    gtk_stack_set_visible_child (GTK_STACK (self->main_stack), self->contact_list_view);

  page = HDY_STATUS_PAGE (self->empty_view);
  if (self->search_str && *self->search_str) {
    hdy_status_page_set_icon_name (page, "system-search-symbolic");
    hdy_status_page_set_title (page, _("No Search Results"));
    hdy_status_page_set_description (page, _("Try different search, or type a valid "
                                             "number to create new chat"));
  } else {
    hdy_status_page_set_icon_name (page, "sm.puri.Chatty-symbolic");
    hdy_status_page_set_title (page, _("No Contacts"));
    hdy_status_page_set_description (page, NULL);
  }
}

static void
contact_list_active_protocols_changed_cb (ChattyContactList *self)
{
  ChattyAccount *mm_account;
  ChattyProtocol protocol;
  gboolean valid;

  g_assert (CHATTY_IS_CONTACT_LIST (self));

  self->active_protocols = chatty_manager_get_active_protocols (self->manager);
  gtk_filter_changed (self->filter, GTK_FILTER_CHANGE_DIFFERENT);

  protocol = CHATTY_PROTOCOL_MMS_SMS;
  valid = protocol == chatty_utils_username_is_valid (self->search_str, protocol);
  mm_account = chatty_manager_get_mm_account (self->manager);
  valid = valid && chatty_account_get_status (mm_account) == CHATTY_CONNECTED;
  gtk_widget_set_visible (self->new_contact_row, valid);
  contact_list_changed_cb (self);
}

static void
contact_list_delete_item (ChattyContactList *self,
                          ChattyListRow     *row)
{
  g_assert (CHATTY_IS_CONTACT_LIST (self));
  g_assert (CHATTY_IS_LIST_ROW (row));

  selected_contact_row_activated_cb (self, row, GTK_LIST_BOX (self->selected_contact_list));
}

static void
chatty_contact_list_finalize (GObject *object)
{
  ChattyContactList *self = (ChattyContactList *)object;

  g_clear_object (&self->dummy_contact);
  g_clear_object (&self->slice_model);
  g_clear_object (&self->selection_store);
  g_clear_object (&self->filter);
  g_clear_object (&self->manager);
  g_free (self->search_str);

  G_OBJECT_CLASS (chatty_contact_list_parent_class)->finalize (object);
}

static void
chatty_contact_list_class_init (ChattyContactListClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = chatty_contact_list_finalize;

  signals [DELETE_ROW] =
    g_signal_new ("delete-row",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, CHATTY_TYPE_LIST_ROW);

  signals [SELECTION_CHANGED] =
    g_signal_new ("selection-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-contact-list.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyContactList, scrolled_window);
  gtk_widget_class_bind_template_child (widget_class, ChattyContactList, main_stack);
  gtk_widget_class_bind_template_child (widget_class, ChattyContactList, empty_view);
  gtk_widget_class_bind_template_child (widget_class, ChattyContactList, contact_list_view);

  gtk_widget_class_bind_template_child (widget_class, ChattyContactList, selected_contact_list);
  gtk_widget_class_bind_template_child (widget_class, ChattyContactList, new_contact_list);
  gtk_widget_class_bind_template_child (widget_class, ChattyContactList, new_contact_row);
  gtk_widget_class_bind_template_child (widget_class, ChattyContactList, contact_list);

  gtk_widget_class_bind_template_callback (widget_class, contact_list_edge_reached_cb);
  gtk_widget_class_bind_template_callback (widget_class, selected_contact_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, contact_list_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, contact_list_changed_cb);
}

static void
chatty_contact_list_init (ChattyContactList *self)
{
  g_autoptr(GtkFilterListModel) filter_model = NULL;
  g_autoptr(GtkSortListModel) sort_model = NULL;
  g_autoptr(GtkSorter) sorter = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->manager = g_object_ref (chatty_manager_get_default ());
  self->filter_protocols = CHATTY_PROTOCOL_ANY;

  self->dummy_contact = CHATTY_ITEM (chatty_contact_dummy_new (_("Send To"), NULL));
  chatty_list_row_set_item (CHATTY_LIST_ROW (self->new_contact_row), self->dummy_contact);

  sorter = gtk_custom_sorter_new ((GCompareDataFunc)chatty_item_compare, NULL, NULL);
  sort_model = gtk_sort_list_model_new (chatty_manager_get_contact_list (self->manager), sorter);

  self->filter = gtk_custom_filter_new ((GtkCustomFilterFunc)contact_list_filter_item_cb, self, NULL);
  filter_model = gtk_filter_list_model_new (G_LIST_MODEL (sort_model), self->filter);

  self->slice_model = gtk_slice_list_model_new (G_LIST_MODEL (filter_model), 0, ITEMS_COUNT);
  g_signal_connect_object (self->slice_model, "items-changed",
                           G_CALLBACK (contact_list_changed_cb), self,
                           G_CONNECT_SWAPPED);
  gtk_list_box_bind_model (GTK_LIST_BOX (self->contact_list),
                           G_LIST_MODEL (self->slice_model),
                           (GtkListBoxCreateWidgetFunc)new_chat_contact_row_new,
                           g_object_ref (self), g_object_unref);
  g_signal_connect_object (self->manager, "notify::active-protocols",
                           G_CALLBACK (contact_list_active_protocols_changed_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self, "delete-row",
                           G_CALLBACK (contact_list_delete_item),
                           self, G_CONNECT_AFTER);
  contact_list_active_protocols_changed_cb (self);
  contact_list_changed_cb (self);
}

GtkWidget *
chatty_contact_list_new (void)
{
  return g_object_new (CHATTY_TYPE_CONTACT_LIST, NULL);
}

void
chatty_contact_list_set_selection_store (ChattyContactList *self,
                                         GListStore        *list_store)
{
  g_return_if_fail (CHATTY_IS_CONTACT_LIST (self));
  g_return_if_fail (G_IS_LIST_STORE (list_store));
  g_return_if_fail (g_list_model_get_item_type (G_LIST_MODEL (list_store)) == CHATTY_TYPE_ITEM);

  g_set_object (&self->selection_store, list_store);

  gtk_list_box_bind_model (GTK_LIST_BOX (self->selected_contact_list),
                           (gpointer) list_store,
                           (GtkListBoxCreateWidgetFunc)new_selected_contact_row_new,
                           g_object_ref (self), g_object_unref);
  g_signal_connect_object (list_store, "items-changed",
                           G_CALLBACK (contact_list_changed_cb), self,
                           G_CONNECT_SWAPPED);
}

void
chatty_contact_list_show_selected_only (ChattyContactList *self)
{
  g_return_if_fail (CHATTY_IS_CONTACT_LIST (self));

  gtk_stack_set_visible_child (GTK_STACK (self->main_stack), self->contact_list_view);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scrolled_window),
                                  GTK_POLICY_NEVER, GTK_POLICY_NEVER);
  gtk_widget_hide (self->new_contact_row);
  gtk_widget_hide (self->contact_list);
}

static void
contact_list_update_selectable (GtkWidget *widget,
                                gpointer   callback_data)
{
  ChattyContactList *self = callback_data;

  g_assert (CHATTY_IS_CONTACT_LIST (self));

  chatty_list_row_set_selectable ((ChattyListRow *)widget, self->can_multi_select);
}

void
chatty_contact_list_can_multi_select (ChattyContactList *self,
                                      gboolean           can_multi_select)
{
  GListModel *model;
  guint n_items;

  g_return_if_fail (CHATTY_IS_CONTACT_LIST (self));
  g_return_if_fail (self->selection_store);

  self->can_multi_select = !!can_multi_select;

  gtk_widget_set_visible (self->selected_contact_list, can_multi_select);
  chatty_list_row_set_selectable (CHATTY_LIST_ROW (self->new_contact_row), can_multi_select);
  gtk_container_foreach (GTK_CONTAINER (self->contact_list),
                         (GtkCallback)contact_list_update_selectable, self);

  model = G_LIST_MODEL (self->selection_store);
  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++) {
    g_autoptr(GObject) item = NULL;

    item = g_list_model_get_item (model, i);
    g_object_set_data (item, "selected", GINT_TO_POINTER (FALSE));
  }

  g_list_store_remove_all (self->selection_store);
}

void
chatty_contact_list_set_filter (ChattyContactList *self,
                                ChattyProtocol     protocol,
                                const char        *needle)
{
  g_return_if_fail (CHATTY_IS_CONTACT_LIST (self));

  self->filter_protocols = protocol;
  g_free (self->search_str);
  self->search_str = g_utf8_casefold (needle, -1);

  update_new_contact_row (self);
  gtk_slice_list_model_set_size (self->slice_model, ITEMS_COUNT);
  gtk_filter_changed (self->filter, GTK_FILTER_CHANGE_DIFFERENT);
}
