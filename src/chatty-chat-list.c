/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-chat-list.c
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-chat-list"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>
#include "contrib/gtk.h"

#include "chatty-manager.h"
#include "chatty-list-row.h"
#include "chatty-ma-chat.h"
#include "chatty-mm-chat.h"
#include "chatty-purple.h"
#include "chatty-chat-list.h"
#include "chatty-log.h"

/**
 * SECTION: chatty-chat-list
 * @title: ChattyChatList
 * @short_description:
 * @include: "chatty-chat-list.h"
 *
 * Chat list to be shown in the main window sidebar
 *
 */

struct _ChattyChatList
{
  GtkBox              parent_instance;

  GtkWidget          *main_stack;
  GtkWidget          *empty_view;
  GtkWidget          *chat_list_view;
  GtkWidget          *chats_listbox;


  char               *chat_needle;
  GtkFilter          *filter;
  GtkFilterListModel *filter_model;
  GtkFilterListModel *archive_filter_model;
  ChattyProtocol     protocol_filter;

  ChattyManager     *manager;
  GPtrArray         *selected_items;
  GtkSelectionMode   mode;

  gboolean           show_archived;
};

G_DEFINE_TYPE (ChattyChatList, chatty_chat_list, GTK_TYPE_BOX)

enum {
  SELECTION_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static gboolean
chat_list_filter_archived_chat (ChattyItem     *item,
                                ChattyChatList *self)
{
  ChattyItemState state;

  g_assert (CHATTY_IS_CHAT (item));
  g_assert (CHATTY_IS_CHAT_LIST (self));

  state = chatty_item_get_state (item);

  if (self->show_archived && state == CHATTY_ITEM_VISIBLE)
    return FALSE;

  if (!self->show_archived && state != CHATTY_ITEM_VISIBLE)
    return FALSE;

  return TRUE;
}

static gboolean
chat_list_filter_chat (ChattyItem     *item,
                       ChattyChatList *self)
{
  ChattyProtocol protocol;

  g_assert (CHATTY_IS_CHAT (item));
  g_assert (CHATTY_IS_CHAT_LIST (self));

  protocol = chatty_item_get_protocols (item);

  if (!(self->protocol_filter & protocol))
    return FALSE;

  if (protocol != CHATTY_PROTOCOL_MATRIX) {
    GListModel *message_list;
    guint n_items;

    message_list = chatty_chat_get_messages (CHATTY_CHAT (item));
    n_items = g_list_model_get_n_items (message_list);

    if (n_items == 0)
      return FALSE;
  }

  return chatty_item_matches (item, self->chat_needle,
                              self->protocol_filter, TRUE);
}

static void
chatty_chat_list_update_selection (ChattyChatList *self)
{
  g_assert (CHATTY_IS_CHAT_LIST (self));

  if (self->mode != GTK_SELECTION_SINGLE)
    return;

  if (!self->selected_items->len) {
    chatty_chat_list_select_first (self);
  } else if (self->selected_items->len) {
    guint position;

    /* Reselect the item so that the selection highlight is updated */
    if (chatty_utils_get_item_position (G_LIST_MODEL (self->filter_model),
                                        self->selected_items->pdata[0], &position)) {
      GtkListBoxRow *row;

      row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->chats_listbox), position);
      gtk_list_box_select_row (GTK_LIST_BOX (self->chats_listbox), row);
    }
  }
}

static void
chat_list_chat_changed_cb (ChattyChatList *self)
{
  GListModel *model;
  gboolean has_child;

  g_assert (CHATTY_IS_CHAT_LIST (self));

  model = chatty_manager_get_chat_list (self->manager);
  has_child = g_list_model_get_n_items (model) > 0;

  if (self->selected_items->len &&
      !chatty_utils_get_item_position (model,  self->selected_items->pdata[0], NULL))
    g_ptr_array_remove_index (self->selected_items, 0);

  if (!self->selected_items->len)
    self->selected_items->pdata[0] = NULL;

  chatty_chat_list_update_selection (self);

  if (has_child)
    return;

  if (self->selected_items->len) {
    g_ptr_array_set_size (self->selected_items, 0);
    self->selected_items->pdata[0] = NULL;
    g_signal_emit (self, signals[SELECTION_CHANGED], 0);
  }

  if (chatty_manager_get_active_protocols (self->manager))
    hdy_status_page_set_description (HDY_STATUS_PAGE (self->empty_view),
                                     _("Select a contact with the "
                                       "<b>“+”</b> button in the titlebar."));
  else
    hdy_status_page_set_description (HDY_STATUS_PAGE (self->empty_view),
                                     _("Add instant messaging accounts in Preferences."));
}

static void
chat_list_filter_changed_cb (ChattyChatList *self)
{
  GtkWidget *current_view;
  HdyStatusPage *page;
  GListModel *model;
  gboolean search_active, has_child;

  g_assert (CHATTY_IS_CHAT_LIST (self));

  search_active = self->protocol_filter != CHATTY_PROTOCOL_ANY;
  search_active = search_active | !!self->chat_needle;

  model = G_LIST_MODEL (self->filter_model);
  has_child = g_list_model_get_n_items (model) > 0;

  if (has_child)
    current_view = self->chat_list_view;
  else
    current_view = self->empty_view;

  gtk_stack_set_visible_child (GTK_STACK (self->main_stack), current_view);

  if (self->selected_items->len)
    chatty_chat_list_update_selection (self);

  if (has_child)
    return;

  page = HDY_STATUS_PAGE (self->empty_view);

  if (search_active) {
    hdy_status_page_set_icon_name (page, "system-search-symbolic");
    hdy_status_page_set_title (page, _("No Search Results"));
    hdy_status_page_set_description (page, _("Try different search"));
  } else {
    hdy_status_page_set_icon_name (page, "sm.puri.Chatty-symbolic");
    if (self->show_archived)
      hdy_status_page_set_title (page, _("No archived chats"));
    else
      hdy_status_page_set_title (page, _("Start Chatting"));
    hdy_status_page_set_description (page, NULL);
  }
}

static void
chat_list_protocols_changed_cb (ChattyChatList *self)
{
  g_assert (CHATTY_IS_CHAT_LIST (self));

  gtk_filter_changed (self->filter, GTK_FILTER_CHANGE_DIFFERENT);
}

static void
chat_list_row_activated_cb (ChattyChatList *self,
                            ChattyListRow  *row,
                            GtkListBox     *box)
{
  ChattyItem *item;

  g_assert (CHATTY_IS_CHAT_LIST (self));
  g_assert (CHATTY_IS_LIST_ROW (row));
  g_assert (GTK_IS_LIST_BOX (box));

  item = chatty_list_row_get_item (row);

  g_ptr_array_set_size (self->selected_items, 0);
  g_ptr_array_add (self->selected_items, g_object_ref (item));

  g_signal_emit (self, signals[SELECTION_CHANGED], 0);
}

static void
chatty_chat_list_map (GtkWidget *widget)
{
  ChattyChatList *self = (ChattyChatList *)widget;

  GTK_WIDGET_CLASS (chatty_chat_list_parent_class)->map (widget);

  g_signal_connect_object (chatty_manager_get_chat_list (self->manager),
                           "items-changed",
                           G_CALLBACK (chat_list_chat_changed_cb), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->filter_model,
                           "items-changed",
                           G_CALLBACK (chat_list_filter_changed_cb), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->manager, "notify::active-protocols",
                           G_CALLBACK (chat_list_protocols_changed_cb), self,
                           G_CONNECT_SWAPPED);

  chat_list_chat_changed_cb (self);
  chat_list_filter_changed_cb (self);
  chat_list_protocols_changed_cb (self);
}

static void
chatty_chat_list_finalize (GObject *object)
{
  ChattyChatList *self = (ChattyChatList *)object;

  g_ptr_array_unref (self->selected_items);
  g_clear_pointer (&self->chat_needle, g_free);
  g_clear_object (&self->filter);
  g_clear_object (&self->filter_model);

  G_OBJECT_CLASS (chatty_chat_list_parent_class)->finalize (object);
}

static void
chatty_chat_list_class_init (ChattyChatListClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = chatty_chat_list_finalize;

  widget_class->map = chatty_chat_list_map;

  signals [SELECTION_CHANGED] =
    g_signal_new ("selection-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0, NULL);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-chat-list.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyChatList, main_stack);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatList, empty_view);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatList, chat_list_view);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatList, chats_listbox);

  gtk_widget_class_bind_template_callback (widget_class, chat_list_row_activated_cb);
}

static void
chatty_chat_list_init (ChattyChatList *self)
{
  g_autoptr(GtkFilter) archive_filter = NULL;
  GListModel *chat_list;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->protocol_filter = CHATTY_PROTOCOL_ANY;
  g_set_weak_pointer (&self->manager, chatty_manager_get_default ());
  self->selected_items = g_ptr_array_new_full (1, g_object_unref);

  chat_list = chatty_manager_get_chat_list (self->manager);
  archive_filter = gtk_custom_filter_new ((GtkCustomFilterFunc)chat_list_filter_archived_chat,
                                          g_object_ref (self),
                                          g_object_unref);
  self->archive_filter_model = gtk_filter_list_model_new (chat_list, archive_filter);

  self->filter = gtk_custom_filter_new ((GtkCustomFilterFunc)chat_list_filter_chat,
                                        g_object_ref (self),
                                        g_object_unref);
  chat_list = G_LIST_MODEL (self->archive_filter_model);
  self->filter_model = gtk_filter_list_model_new (chat_list, self->filter);

  gtk_list_box_bind_model (GTK_LIST_BOX (self->chats_listbox),
                           G_LIST_MODEL (self->filter_model),
                           (GtkListBoxCreateWidgetFunc)chatty_list_row_new,
                           g_object_ref(self), g_object_unref);
}

GPtrArray *
chatty_chat_list_get_selected (ChattyChatList *self)
{
  g_return_val_if_fail (CHATTY_IS_CHAT_LIST (self), NULL);

  return self->selected_items;
}

/**
 * chatty_chat_list_set_selection_mode:
 * @self: A #ChattyChatList
 * @mode: A #GtkSelectionMode
 *
 * Set the selection mode of the chat list. Selection mode
 * may be differently presented, eg: For GTK_SELECTION_SINGLE,
 * the row shall highlight with the selection color, for
 * GTK_SELECTION_MULTIPLE A check button shall be shown at the
 * end of the row, and it's marked as selected instead of
 * the row itself.
 */
void
chatty_chat_list_set_selection_mode (ChattyChatList   *self,
                                     GtkSelectionMode  mode)
{
  g_return_if_fail (CHATTY_IS_CHAT_LIST (self));

  /* TODO: handle GTK_SELECTION_MULTIPLE  */
  if (mode != GTK_SELECTION_SINGLE)
    mode = GTK_SELECTION_NONE;

  self->mode = mode;
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->chats_listbox), mode);
  chatty_chat_list_update_selection (self);
}

void
chatty_chat_list_select_first (ChattyChatList *self)
{
  GtkListBoxRow *row;

  g_assert (CHATTY_IS_CHAT_LIST (self));

  row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->chats_listbox), 0);

  if (row)
    gtk_widget_activate (GTK_WIDGET (row));
}

void
chatty_chat_list_filter_protocol (ChattyChatList *self,
                                  ChattyProtocol  protocol)
{
  g_return_if_fail (CHATTY_IS_CHAT_LIST (self));

  self->protocol_filter = protocol;
  gtk_filter_changed (self->filter, GTK_FILTER_CHANGE_DIFFERENT);
}

void
chatty_chat_list_filter_string (ChattyChatList *self,
                                const char     *needle)
{
  g_return_if_fail (CHATTY_IS_CHAT_LIST (self));

  g_clear_pointer (&self->chat_needle, g_free);

  if (needle && *needle)
    self->chat_needle = g_strdup (needle);

  gtk_filter_changed (self->filter, GTK_FILTER_CHANGE_DIFFERENT);
}

void
chatty_chat_list_show_archived (ChattyChatList *self,
                                gboolean        show_archived)
{
  g_return_if_fail (CHATTY_IS_CHAT_LIST (self));

  show_archived = !!show_archived;

  if (self->show_archived == show_archived)
    return;

  /* Reset filters */
  self->protocol_filter = CHATTY_PROTOCOL_ANY;
  g_clear_pointer (&self->chat_needle, g_free);

  self->show_archived = show_archived;
  chat_list_filter_changed_cb (self);
  chatty_chat_list_refilter (self);
}

gboolean
chatty_chat_list_is_archived (ChattyChatList *self)
{
  g_return_val_if_fail (CHATTY_IS_CHAT_LIST (self), FALSE);

  return self->show_archived;
}

void
chatty_chat_list_refilter (ChattyChatList *self)
{
  GtkFilter *filter;

  filter = gtk_filter_list_model_get_filter (self->archive_filter_model);
  gtk_filter_changed (filter, GTK_FILTER_CHANGE_DIFFERENT);
}

GListModel *
chatty_chat_list_get_filter_model (ChattyChatList *self)
{
  g_return_val_if_fail (CHATTY_IS_CHAT_LIST (self), NULL);

  return G_LIST_MODEL (self->archive_filter_model);
}
