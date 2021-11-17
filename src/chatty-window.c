/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-window"

#ifdef HAVE_CONFIG_H
# include "config.h"
# include "version.h"
#endif

#define _GNU_SOURCE
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <libgd/gd.h>
#include "contrib/gtk.h"

#include "chatty-window.h"
#include "chatty-history.h"
#include "chatty-avatar.h"
#include "chatty-manager.h"
#include "chatty-purple.h"
#include "chatty-list-row.h"
#include "chatty-settings.h"
#include "chatty-mm-chat.h"
#include "chatty-chat-view.h"
#include "chatty-manager.h"
#include "chatty-utils.h"
#include "chatty-selectable-row.h"
#include "dialogs/chatty-info-dialog.h"
#include "dialogs/chatty-settings-dialog.h"
#include "dialogs/chatty-new-chat-dialog.h"
#include "dialogs/chatty-new-muc-dialog.h"
#include "chatty-mm-account.h"
#include "chatty-ma-chat.h"
#include "chatty-log.h"

struct _ChattyWindow
{
  GtkApplicationWindow parent_instance;

  ChattySettings *settings;

  GtkWidget *sidebar_stack;
  GtkWidget *empty_view;
  GtkWidget *chat_list_view;
  GtkWidget *chats_listbox;

  GtkWidget *content_box;
  GtkWidget *header_box;
  GtkWidget *header_group;

  GtkWidget *sub_header_icon;
  GtkWidget *sub_header_label;

  GtkWidget *new_chat_dialog;
  GtkWidget *chat_info_dialog;

  GtkWidget *search_button;
  GtkWidget *chats_search_bar;
  GtkWidget *chats_search_entry;

  GtkWidget *header_chat_list_new_msg_popover;

  GtkWidget *menu_new_message_button;
  GtkWidget *menu_new_sms_mms_message_button;
  GtkWidget *menu_new_group_message_button;
  GtkWidget *header_add_chat_button;
  GtkWidget *call_button;
  GtkWidget *header_sub_menu_button;
  GtkWidget *leave_button;
  GtkWidget *delete_button;

  GtkWidget *chat_view;

  GtkWidget *protocol_list;
  GtkWidget *protocol_any_row;

  GdTaggedEntryTag *protocol_tag;

  ChattyManager *manager;

  char          *chat_needle;
  GtkFilter     *chat_filter;
  GtkFilterListModel *filter_model;
  GtkWidget          *selected_protocol_row;
  ChattyProtocol protocol_filter;
};


G_DEFINE_TYPE (ChattyWindow, chatty_window, GTK_TYPE_APPLICATION_WINDOW)

static void chatty_window_chat_list_select_first (ChattyWindow *self);

static void
window_set_item (ChattyWindow *self,
                 ChattyChat   *chat)
{
  const char *header_label = "";

  g_assert (CHATTY_IS_WINDOW (self));

  if (CHATTY_IS_CHAT (chat))
    header_label = chatty_item_get_name (CHATTY_ITEM (chat));

  chatty_avatar_set_item (CHATTY_AVATAR (self->sub_header_icon), CHATTY_ITEM (chat));
  gtk_label_set_label (GTK_LABEL (self->sub_header_label), header_label);

  if (!chat)
    hdy_leaflet_set_visible_child_name (HDY_LEAFLET (self->content_box), "sidebar");

  chatty_chat_view_set_chat (CHATTY_CHAT_VIEW (self->chat_view), chat);
}

static void
chatty_window_update_search_mode (ChattyWindow *self)
{
  GListModel *model;
  gboolean has_child;

  g_assert (CHATTY_IS_WINDOW (self));

  model = G_LIST_MODEL (self->filter_model);
  has_child = g_list_model_get_n_items (model) > 0;

  gtk_widget_set_sensitive (self->search_button, has_child);

  if (!has_child)
    hdy_search_bar_set_search_mode (HDY_SEARCH_BAR (self->chats_search_bar), FALSE);
}

static void
window_update_chat_list_selection (ChattyWindow *self)
{
  ChattyChat *chat;
  guint position;
  gboolean has_child;

  g_assert (CHATTY_IS_WINDOW (self));

  chat = chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view));
  has_child = g_list_model_get_n_items (G_LIST_MODEL (self->filter_model)) > 0;

  if (!chat || !has_child)
    return;

  /*
   * When the items are re-arranged, the selection will be lost.
   * Re-select it.  In GTK4, A #GtkListView with #GtkSingleSelection
   * would suite here better.
   */
  if (chatty_utils_get_item_position (G_LIST_MODEL (self->filter_model), chat, &position)) {
    GtkListBoxRow *row;

    row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->chats_listbox), position);
    gtk_list_box_select_row (GTK_LIST_BOX (self->chats_listbox), row);
  }
}

static void
window_filter_changed_cb (ChattyWindow *self)
{
  GtkWidget *current_view;
  HdyStatusPage *page;
  GListModel *model;
  gboolean search_active, has_child;

  g_assert (CHATTY_IS_WINDOW (self));

  search_active = self->protocol_filter != CHATTY_PROTOCOL_ANY;
  search_active |= self->chat_needle && *self->chat_needle;

  model = G_LIST_MODEL (self->filter_model);
  has_child = g_list_model_get_n_items (model) > 0;

  if (has_child)
    window_update_chat_list_selection (self);

  if (has_child)
    current_view = self->chat_list_view;
  else
    current_view = self->empty_view;

  gtk_stack_set_visible_child (GTK_STACK (self->sidebar_stack), current_view);

  if (has_child)
    return;

  page = HDY_STATUS_PAGE (self->empty_view);

  if (search_active) {
    hdy_status_page_set_icon_name (page, "system-search-symbolic");
    hdy_status_page_set_title (page, _("No Search Results"));
    hdy_status_page_set_description (page, _("Try different search"));
  } else {
    hdy_status_page_set_icon_name (page, "sm.puri.Chatty-symbolic");
    hdy_status_page_set_title (page, _("Start Chatting"));
    hdy_status_page_set_description (page, NULL);
  }
}

static void
window_chat_changed_cb (ChattyWindow *self)
{
  GListModel *model;
  ChattyChat *chat;
  gboolean has_child;

  g_assert (CHATTY_IS_WINDOW (self));

  chat = chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view));
  model = G_LIST_MODEL (self->filter_model);
  has_child = g_list_model_get_n_items (model) > 0;

  gtk_widget_set_sensitive (self->header_sub_menu_button, !!chat);
  chatty_window_chat_list_select_first (self);

  if (has_child)
    window_update_chat_list_selection (self);

  chatty_window_update_search_mode (self);

  if (has_child)
    return;

  if (chatty_manager_get_active_protocols (self->manager))
    hdy_status_page_set_description (HDY_STATUS_PAGE (self->empty_view),
                                     _("Select a contact with the "
                                       "<b>“+”</b> button in the titlebar."));
  else
    hdy_status_page_set_description (HDY_STATUS_PAGE (self->empty_view),
                                     _("Add instant messaging accounts in Preferences."));
}

static gboolean
window_chat_name_matches (ChattyItem   *item,
                          ChattyWindow *self)
{
  ChattyProtocol protocol;

  g_assert (CHATTY_IS_CHAT (item));
  g_assert (CHATTY_IS_WINDOW (self));

  protocol = chatty_item_get_protocols (item);

  if (!(self->protocol_filter & protocol))
    return FALSE;

  if ((!self->chat_needle || !*self->chat_needle) &&
      CHATTY_IS_MM_CHAT (item))
    return TRUE;

  /* FIXME: Not a good idea */
  if (chatty_item_get_protocols (item) != CHATTY_PROTOCOL_MMS_SMS) {
    ChattyAccount *account;

#ifdef PURPLE_ENABLED
    if (CHATTY_IS_PP_CHAT (item) &&
        !chatty_pp_chat_get_auto_join (CHATTY_PP_CHAT (item)))
      return FALSE;
#endif

    account = chatty_chat_get_account (CHATTY_CHAT (item));

    if (!account || chatty_account_get_status (account) != CHATTY_CONNECTED)
      return FALSE;
  }

  if (protocol != CHATTY_PROTOCOL_MATRIX &&
      hdy_leaflet_get_folded (HDY_LEAFLET (self->header_box))) {
    GListModel *message_list;
    guint n_items;

    message_list = chatty_chat_get_messages (CHATTY_CHAT (item));
    n_items = g_list_model_get_n_items (message_list);

    if (n_items == 0)
      return FALSE;
  }


  if (!self->chat_needle || !*self->chat_needle)
    return TRUE;

  return chatty_item_matches (item, self->chat_needle,
                              CHATTY_PROTOCOL_ANY, TRUE);
}


static void
chatty_window_open_item (ChattyWindow *self,
                         ChattyItem   *item)
{
  g_assert (CHATTY_IS_WINDOW (self));
  g_assert (CHATTY_IS_ITEM (item));
  CHATTY_INFO (chatty_item_get_name (item),
               "Opening item of type: %s, name:", G_OBJECT_TYPE_NAME (item));

  if (CHATTY_IS_CONTACT (item)) {
    const char *number;

    number = chatty_item_get_username (item);
    chatty_window_set_uri (self, number);

    return;
  }

#ifdef PURPLE_ENABLED
  if (CHATTY_IS_PP_BUDDY (item) ||
      CHATTY_IS_PP_CHAT (item))
    chatty_purple_start_chat (chatty_purple_get_default (), item);
#endif

  if (CHATTY_IS_MM_CHAT (item)) {
    chatty_window_open_chat (CHATTY_WINDOW (self), CHATTY_CHAT (item));
  }
}

static void
window_chat_row_activated_cb (GtkListBox    *box,
                              GtkListBoxRow *row,
                              ChattyWindow  *self)
{
  ChattyChat *chat;

  g_assert (CHATTY_WINDOW (self));

  chat = (ChattyChat *)chatty_list_row_get_item (CHATTY_LIST_ROW (row));

  g_return_if_fail (CHATTY_IS_CHAT (chat));

#ifdef PURPLE_ENABLED
  if (CHATTY_IS_PP_CHAT (chat))
    chatty_window_open_item (self, CHATTY_ITEM (chat));
  else
#endif
    chatty_window_open_chat (self, chat);
}

static void
window_call_button_clicked_cb (ChattyWindow *self)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *uri = NULL;
  ChattyChat *chat;

  g_assert (CHATTY_IS_WINDOW (self));

  chat = chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view));
  g_return_if_fail (CHATTY_IS_MM_CHAT (chat));

  uri = g_strconcat ("tel://", chatty_chat_get_chat_name (chat), NULL);

  CHATTY_INFO (uri, "Calling uri:");
  if (!gtk_show_uri_on_window (NULL, uri, GDK_CURRENT_TIME, &error))
    g_warning ("Failed to launch call: %s", error->message);
}

static void
window_search_changed_cb (ChattyWindow *self,
                          GtkEntry     *entry)
{
  g_assert (CHATTY_IS_WINDOW (self));

  g_free (self->chat_needle);
  self->chat_needle = g_strdup (gtk_entry_get_text (entry));

  gtk_filter_changed (self->chat_filter, GTK_FILTER_CHANGE_DIFFERENT);
}

static void
search_tag_button_clicked_cb (ChattyWindow     *self,
                              GdTaggedEntryTag *tag,
                              GdTaggedEntry    *entry)
{
  g_assert (CHATTY_IS_WINDOW (self));
  g_assert (GD_TAGGED_ENTRY_TAG (tag));

  gtk_widget_activate (self->protocol_any_row);
}

static void
window_search_toggled_cb (ChattyWindow    *self,
                          GtkToggleButton *button)
{
  g_assert (CHATTY_IS_WINDOW (self));

  if (!gtk_toggle_button_get_active (button) &&
      self->protocol_any_row != self->selected_protocol_row)
    gtk_widget_activate (self->protocol_any_row);
}

static void
window_search_entry_activated_cb (ChattyWindow *self)
{
  GtkListBoxRow *row;

  g_assert (CHATTY_IS_WINDOW (self));

  row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->chats_listbox), 0);

  if (row)
    window_chat_row_activated_cb (GTK_LIST_BOX (self->chats_listbox), row, self);
}

static void
notify_fold_cb (ChattyWindow *self)
{
  gboolean folded = hdy_leaflet_get_folded (HDY_LEAFLET (self->header_box));

  if (folded)
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->chats_listbox), GTK_SELECTION_NONE);
  else
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->chats_listbox), GTK_SELECTION_SINGLE);

  if (folded) {
    window_set_item (self, NULL);
  } else if (chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view))) {
    window_chat_changed_cb (self);
  } else {
    chatty_window_chat_list_select_first (self);
  }

  gtk_filter_changed (self->chat_filter, GTK_FILTER_CHANGE_DIFFERENT);
}

static void
window_new_message_clicked_cb (ChattyWindow *self)
{
  ChattyNewChatDialog *dialog;
  ChattyItem *item;
  const char *phone_number = NULL;
  gint response;

  g_assert (CHATTY_IS_WINDOW (self));

  dialog = CHATTY_NEW_CHAT_DIALOG (self->new_chat_dialog);
  chatty_new_chat_dialog_set_multi_selection (dialog, FALSE);

  response = gtk_dialog_run (GTK_DIALOG (self->new_chat_dialog));
  gtk_widget_hide (self->new_chat_dialog);

  if (response != GTK_RESPONSE_OK)
    return;

  item = chatty_new_chat_dialog_get_selected_item (dialog);

  if (CHATTY_IS_CONTACT (item) &&
      chatty_contact_is_dummy (CHATTY_CONTACT (item)))
    phone_number = chatty_item_get_username (item);

  if (phone_number)
    chatty_window_set_uri (self, phone_number);
  else if (item)
    chatty_window_open_item (self, item);
  else
    g_return_if_reached ();
}

static void
window_new_sms_mms_message_clicked_cb (ChattyWindow *self)
{
  g_autoptr(GString) sendlist = g_string_new (NULL);
  ChattyNewChatDialog *dialog;
  GPtrArray *items;
  gint response;

  g_assert (CHATTY_IS_WINDOW (self));

  dialog = CHATTY_NEW_CHAT_DIALOG (self->new_chat_dialog);
  chatty_new_chat_dialog_set_multi_selection (dialog, TRUE);

  response = gtk_dialog_run (GTK_DIALOG (self->new_chat_dialog));
  gtk_widget_hide (self->new_chat_dialog);

  if (response != GTK_RESPONSE_OK)
    return;

  items = chatty_new_chat_dialog_get_selected_items (dialog);

  for (guint i = 0; i < items->len; i++) {
    const char *phone_number;
    ChattyItem *item;

    item = items->pdata[i];

    if (CHATTY_IS_CONTACT (item)) {
      phone_number = chatty_item_get_username (item);
      sendlist = g_string_append (sendlist, phone_number);
      g_string_append (sendlist, ",");
    }
  }

  /* Remove the trailing "," */
  if (sendlist->len >= 1)
    g_string_truncate (sendlist, sendlist->len - 1);

  chatty_window_set_uri (self, sendlist->str);
}

static void
window_new_muc_clicked_cb (ChattyWindow *self)
{
  GtkWidget *dialog;

  g_assert (CHATTY_IS_WINDOW (self));

  dialog = chatty_new_muc_dialog_new (GTK_WINDOW (self));
  gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);
}

static void
window_back_clicked_cb (ChattyWindow *self)
{
  g_assert (CHATTY_IS_WINDOW (self));

  window_set_item (self, NULL);
}

static void
chatty_window_chat_list_select_first (ChattyWindow *self)
{
  GtkListBoxRow *row;

  if (chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view)))
    return;

  row = gtk_list_box_get_row_at_index (GTK_LIST_BOX(self->chats_listbox), 0);

  if (row && !hdy_leaflet_get_folded (HDY_LEAFLET (self->header_box))) {
    gtk_list_box_select_row (GTK_LIST_BOX(self->chats_listbox), row);
    window_chat_row_activated_cb (GTK_LIST_BOX(self->chats_listbox), row, self);
  } else {
    window_set_item (self, NULL);
  }
}

static void
window_delete_buddy_clicked_cb (ChattyWindow *self)
{
  g_autofree char *text = NULL;
  GtkWidget *dialog;
  ChattyChat *chat;
  const char *name;
  const char *sub_text;
  int response;

  g_assert (CHATTY_IS_WINDOW (self));

  chat = chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view));
  g_return_if_fail (chat);

  name = chatty_item_get_name (CHATTY_ITEM (chat));

  if (chatty_chat_is_im (chat)) {
    text = g_strdup_printf (_("Delete chat with “%s”"), name);
    sub_text = _("This deletes the conversation history");
  } else {
    text = g_strdup_printf (_("Disconnect group chat “%s”"), name);
    sub_text = _("This removes chat from chats list");
  }

  dialog = gtk_message_dialog_new (GTK_WINDOW (self),
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_NONE,
                                   "%s", text);

  gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                          _("Cancel"),
                          GTK_RESPONSE_CANCEL,
                          _("Delete"),
                          GTK_RESPONSE_OK,
                          NULL);

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            "%s",
                                            sub_text);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ON_PARENT);

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if (response == GTK_RESPONSE_OK) {
    chatty_history_delete_chat (chatty_manager_get_history (self->manager),
                                chat);
#ifdef PURPLE_ENABLED
    if (CHATTY_IS_PP_CHAT (chat)) {
      chatty_pp_chat_delete (CHATTY_PP_CHAT (chat));
    } else
#endif
    if (CHATTY_IS_MM_CHAT (chat)) {
      chatty_mm_chat_delete (CHATTY_MM_CHAT (chat));
    } else {
      g_return_if_reached ();
    }

    window_set_item (self, NULL);
    gtk_widget_hide (self->call_button);
    gtk_widget_set_sensitive (self->header_sub_menu_button, FALSE);
    chatty_window_chat_list_select_first (self);
  }

  gtk_widget_destroy (dialog);
}


static void
window_leave_chat_clicked_cb (ChattyWindow *self)
{
  ChattyChat *chat;

  g_assert (CHATTY_IS_WINDOW (self));

  chat = chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view));
  g_warn_if_fail (chat);

  if (chat) {
    ChattyAccount *account;

    account = chatty_chat_get_account (chat);
    chatty_account_leave_chat_async (account, chat, NULL, NULL);
  }

  window_set_item (self, NULL);
  chatty_window_chat_list_select_first (self);
}

static void
window_show_chat_info_clicked_cb (ChattyWindow *self)
{
  ChattyInfoDialog *dialog;
  ChattyChat *chat;

  g_assert (CHATTY_IS_WINDOW (self));

  chat = chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view));
  g_return_if_fail (CHATTY_IS_CHAT (chat));

  dialog = CHATTY_INFO_DIALOG (self->chat_info_dialog);

  chatty_info_dialog_set_chat (dialog, chat);
  gtk_dialog_run (GTK_DIALOG (dialog));
}

static void
chatty_window_show_settings_dialog (ChattyWindow *self)
{
  GtkWidget *dialog;

  g_assert (CHATTY_IS_WINDOW (self));

  dialog = chatty_settings_dialog_new (GTK_WINDOW (self));
  gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);
}

/* Copied from chatty-dialogs.c written by Andrea Schäfer <mosibasu@me.com> */
static void
chatty_window_show_about_dialog (ChattyWindow *self)
{
  static const gchar *authors[] = {
    "Adrien Plazas <kekun.plazas@laposte.net>",
    "Andrea Schäfer <mosibasu@me.com>",
    "Benedikt Wildenhain <benedikt.wildenhain@hs-bochum.de>",
    "Guido Günther <agx@sigxcpu.org>",
    "Julian Sparber <jsparber@gnome.org>",
    "Leland Carlye <leland.carlye@protonmail.com>",
    "Mohammed Sadiq https://www.sadiqpk.org/",
    "Richard Bayerle (OMEMO Plugin) https://github.com/gkdr/lurch",
    "Ruslan Marchenko <me@ruff.mobi>",
    "and more...",
    NULL
  };

  static const gchar *artists[] = {
    "Tobias Bernard <tbernard@gnome.org>",
    NULL
  };

  static const gchar *documenters[] = {
    "Heather Ellsworth <heather.ellsworth@puri.sm>",
    NULL
  };

  /*
   * “program-name” defaults to g_get_application_name().
   * Don’t set it explicitly so that there is one less
   * string to translate.
   */
  gtk_show_about_dialog (GTK_WINDOW (self),
                         "logo-icon-name", CHATTY_APP_ID,
                         "version", GIT_VERSION,
                         "comments", _("An SMS and XMPP messaging client"),
                         "website", "https://source.puri.sm/Librem5/chatty",
                         "copyright", "© 2018–2021 Purism SPC",
                         "license-type", GTK_LICENSE_GPL_3_0,
                         "authors", authors,
                         "artists", artists,
                         "documenters", documenters,
                         "translator-credits", _("translator-credits"),
                         NULL);
}

static void
window_search_protocol_changed_cb (ChattyWindow *self,
                                   GtkWidget    *selected_row,
                                   GtkListBox   *box)
{
  GdTaggedEntry *entry;
  GtkWidget *old_row;

  g_assert (CHATTY_IS_WINDOW (self));
  g_assert (GTK_IS_LIST_BOX (box));

  entry = GD_TAGGED_ENTRY (self->chats_search_entry);
  old_row = self->selected_protocol_row;

  if (old_row == selected_row)
    return;

  self->protocol_filter = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (selected_row), "protocol"));
  chatty_selectable_row_set_selected (CHATTY_SELECTABLE_ROW (old_row), FALSE);
  chatty_selectable_row_set_selected (CHATTY_SELECTABLE_ROW (selected_row), TRUE);
  self->selected_protocol_row = selected_row;

  if (selected_row == self->protocol_any_row) {
    gd_tagged_entry_remove_tag (entry, self->protocol_tag);
  } else {
    const char *title;

    gd_tagged_entry_add_tag (entry, self->protocol_tag);
    title = chatty_selectable_row_get_title (CHATTY_SELECTABLE_ROW (selected_row));
    gd_tagged_entry_tag_set_label (self->protocol_tag, title);
  }

  gtk_filter_changed (self->chat_filter, GTK_FILTER_CHANGE_DIFFERENT);
}

static void
window_active_protocols_changed_cb (ChattyWindow *self)
{
  ChattyAccount *mm_account;
  ChattyProtocol protocols;
  gboolean has_mms, has_sms, has_im;

  g_assert (CHATTY_IS_WINDOW (self));

  mm_account = chatty_manager_get_mm_account (self->manager);
  protocols = chatty_manager_get_active_protocols (self->manager);
  has_mms = chatty_mm_account_has_mms_feature (CHATTY_MM_ACCOUNT (mm_account));
  has_sms = !!(protocols & CHATTY_PROTOCOL_MMS_SMS);
  has_im  = !!(protocols & ~CHATTY_PROTOCOL_MMS_SMS);

  gtk_widget_set_sensitive (self->header_add_chat_button, has_sms || has_im);
  gtk_widget_set_sensitive (self->menu_new_group_message_button, has_im);

  gtk_widget_set_visible (self->menu_new_sms_mms_message_button,
                          has_mms && has_sms);

  gtk_filter_changed (self->chat_filter, GTK_FILTER_CHANGE_DIFFERENT);
  window_chat_changed_cb (self);
}

static void
window_chat_deleted_cb (ChattyWindow *self,
                        ChattyChat   *chat)
{
  g_assert (CHATTY_IS_WINDOW (self));
  g_assert (CHATTY_IS_CHAT (chat));

  if (chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view)) != chat)
    return;

  window_set_item (self, NULL);
}

static void
protocol_list_header_func (GtkListBoxRow *row,
                           GtkListBoxRow *before,
                           gpointer       user_data)
{
  if (!before) {
    gtk_list_box_row_set_header (row, NULL);
  } else if (!gtk_list_box_row_get_header (row)) {
    GtkWidget *separator;

    separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_show (separator);
    gtk_list_box_row_set_header (row, separator);
  }
}


static void
chatty_window_unmap (GtkWidget *widget)
{
  ChattyWindow *self = (ChattyWindow *)widget;
  GtkWindow    *window = (GtkWindow *)widget;
  GdkRectangle  geometry;
  gboolean      is_maximized;

  is_maximized = gtk_window_is_maximized (window);

  chatty_settings_set_window_maximized (self->settings, is_maximized);

  if (!is_maximized) {
    gtk_window_get_size (window, &geometry.width, &geometry.height);
    chatty_settings_set_window_geometry (self->settings, &geometry);
  }

  GTK_WIDGET_CLASS (chatty_window_parent_class)->unmap (widget);
}

static void
chatty_window_map (GtkWidget *widget)
{
  ChattyWindow *self = (ChattyWindow *)widget;

  g_signal_connect_object (self->filter_model,
                           "items-changed",
                           G_CALLBACK (window_filter_changed_cb), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (gtk_filter_list_model_get_model (self->filter_model),
                           "items-changed",
                           G_CALLBACK (window_chat_changed_cb), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->manager, "notify::active-protocols",
                           G_CALLBACK (window_active_protocols_changed_cb), self,
                           G_CONNECT_SWAPPED);

  notify_fold_cb (self);
  window_chat_changed_cb (self);
  window_filter_changed_cb (self);
  window_active_protocols_changed_cb (self);

  GTK_WIDGET_CLASS (chatty_window_parent_class)->map (widget);
}

static void
chatty_window_constructed (GObject *object)
{
  ChattyWindow *self = (ChattyWindow *)object;
  GtkWindow    *window = (GtkWindow *)object;
  GdkRectangle  geometry;

  self->settings = g_object_ref (chatty_settings_get_default ());
  chatty_settings_get_window_geometry (self->settings, &geometry);
  gtk_window_set_default_size (window, geometry.width, geometry.height);

  if (chatty_settings_get_window_maximized (self->settings))
    gtk_window_maximize (window);

  self->new_chat_dialog = chatty_new_chat_dialog_new (GTK_WINDOW (self));
  self->chat_info_dialog = chatty_info_dialog_new (GTK_WINDOW (self));

  self->chat_filter = gtk_custom_filter_new ((GtkCustomFilterFunc)window_chat_name_matches,
                                             g_object_ref (self),
                                             g_object_unref);
  self->filter_model = gtk_filter_list_model_new (chatty_manager_get_chat_list (self->manager),
                                                  self->chat_filter);
  gtk_list_box_bind_model (GTK_LIST_BOX (self->chats_listbox),
                           G_LIST_MODEL (self->filter_model),
                           (GtkListBoxCreateWidgetFunc)chatty_list_row_new,
                           g_object_ref(self), g_object_unref);

  G_OBJECT_CLASS (chatty_window_parent_class)->constructed (object);
}


static void
chatty_window_finalize (GObject *object)
{
  ChattyWindow *self = (ChattyWindow *)object;

  g_clear_object (&self->settings);
  g_clear_object (&self->protocol_tag);

  G_OBJECT_CLASS (chatty_window_parent_class)->finalize (object);
}

static void
chatty_window_dispose (GObject *object)
{
  ChattyWindow *self = (ChattyWindow *)object;

  g_clear_object (&self->filter_model);
  g_clear_object (&self->chat_filter);
  g_clear_object (&self->manager);
  g_clear_pointer (&self->chat_needle, g_free);

  G_OBJECT_CLASS (chatty_window_parent_class)->dispose (object);
}


static void
chatty_window_class_init (ChattyWindowClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed  = chatty_window_constructed;
  object_class->finalize     = chatty_window_finalize;
  object_class->dispose      = chatty_window_dispose;

  widget_class->map = chatty_window_map;
  widget_class->unmap = chatty_window_unmap;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-window.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, sub_header_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, sub_header_icon);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, menu_new_message_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, menu_new_sms_mms_message_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, menu_new_group_message_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_add_chat_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, call_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_sub_menu_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, leave_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, delete_button);

  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, search_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, chats_search_bar);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, chats_search_entry);

  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, content_box);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_box);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_group);

  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, sidebar_stack);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, empty_view);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, chat_list_view);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, chats_listbox);

  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, chat_view);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_chat_list_new_msg_popover);

  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, protocol_list);

  gtk_widget_class_bind_template_callback (widget_class, notify_fold_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_new_message_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_new_sms_mms_message_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_new_muc_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_back_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_show_chat_info_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_leave_chat_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_delete_buddy_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_call_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_search_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, search_tag_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_search_toggled_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_search_entry_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_chat_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, chatty_window_show_settings_dialog);
  gtk_widget_class_bind_template_callback (widget_class, chatty_window_show_about_dialog);
  gtk_widget_class_bind_template_callback (widget_class, window_search_protocol_changed_cb);

  g_type_ensure (CHATTY_TYPE_SELECTABLE_ROW);
}

static void
window_add_selectable_row (ChattyWindow   *self,
                           const char     *name,
                           ChattyProtocol  protocol,
                           gboolean        selected)
{
  GtkWidget *row;

  row = chatty_selectable_row_new (name);
  g_object_set_data (G_OBJECT (row), "protocol", GINT_TO_POINTER (protocol));
  chatty_selectable_row_set_selected (CHATTY_SELECTABLE_ROW (row), selected);
  gtk_container_add (GTK_CONTAINER (self->protocol_list), row);

  if (protocol == CHATTY_PROTOCOL_ANY)
    self->protocol_any_row = self->selected_protocol_row = row;
}

static void
chatty_window_init (ChattyWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->protocol_filter = CHATTY_PROTOCOL_ANY;
  hdy_search_bar_connect_entry (HDY_SEARCH_BAR (self->chats_search_bar),
                                GTK_ENTRY (self->chats_search_entry));
  self->manager = g_object_ref (chatty_manager_get_default ());
  g_signal_connect_object (self->manager, "chat-deleted",
                           G_CALLBACK (window_chat_deleted_cb), self,
                           G_CONNECT_SWAPPED);

  gtk_list_box_set_header_func (GTK_LIST_BOX (self->protocol_list),
                                protocol_list_header_func,
                                NULL, NULL);

  self->protocol_tag = gd_tagged_entry_tag_new ("");
  gd_tagged_entry_tag_set_style (self->protocol_tag, "protocol-tag");

  window_add_selectable_row (self, _("Any Protocol"), CHATTY_PROTOCOL_ANY, TRUE);
  window_add_selectable_row (self, _("Matrix"), CHATTY_PROTOCOL_MATRIX, FALSE);
  window_add_selectable_row (self, _("SMS/MMS"), CHATTY_PROTOCOL_MMS_SMS, FALSE);

#ifdef PURPLE_ENABLED
  window_add_selectable_row (self, _("XMPP"), CHATTY_PROTOCOL_XMPP, FALSE);

  if (chatty_purple_has_telegram_loaded (chatty_purple_get_default ()))
    window_add_selectable_row (self, _("Telegram"), CHATTY_PROTOCOL_TELEGRAM, FALSE);
#endif

  gtk_widget_show_all (self->protocol_list);
}


GtkWidget *
chatty_window_new (GtkApplication *application)
{
  g_assert (GTK_IS_APPLICATION (application));

  return g_object_new (CHATTY_TYPE_WINDOW,
                       "application", application,
                       NULL);
}


void
chatty_window_set_uri (ChattyWindow *self,
                       const char   *uri)
{
  g_autofree char *who = NULL;
  g_auto(GStrv) recipients = NULL;
  guint num;

  recipients = g_strsplit (uri, ",", -1);
  num = g_strv_length (recipients);
  for (int i = 0; i < num; i++) {
    who = chatty_utils_check_phonenumber (recipients[i], chatty_settings_get_country_iso_code (self->settings));
    if (!who) {
      GtkWidget *dialog;

      dialog = gtk_message_dialog_new (GTK_WINDOW (self),
                                       GTK_DIALOG_MODAL,
                                       GTK_MESSAGE_WARNING,
                                       GTK_BUTTONS_CLOSE,
                                     _("“%s” is not a valid phone number"), uri);
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);

      return;
    }
  }

  if (!chatty_manager_set_uri (self->manager, uri))
    return;

  gtk_widget_hide (self->new_chat_dialog);
}

ChattyChat *
chatty_window_get_active_chat (ChattyWindow *self)
{
  g_return_val_if_fail (CHATTY_IS_WINDOW (self), NULL);

  if (gtk_window_has_toplevel_focus (GTK_WINDOW (self)))
    return chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view));

  return NULL;
}

void
chatty_window_open_chat (ChattyWindow *self,
                         ChattyChat   *chat)
{
  gboolean can_delete;

  g_return_if_fail (CHATTY_IS_WINDOW (self));
  g_return_if_fail (CHATTY_IS_CHAT (chat));

  CHATTY_INFO (chatty_chat_get_chat_name (chat),
               "opening chat, type: %s, chat-name:", G_OBJECT_TYPE_NAME (chat));

  window_set_item (self, chat);
  window_chat_changed_cb (self);

  gtk_widget_set_visible (self->leave_button, !CHATTY_IS_MM_CHAT (chat));
  /* We can't delete MaChat */
  can_delete = !CHATTY_IS_MA_CHAT (chat);
  gtk_widget_set_visible (self->delete_button, can_delete);
  hdy_leaflet_set_visible_child (HDY_LEAFLET (self->content_box), self->chat_view);
  gtk_widget_hide (self->call_button);

  if (chatty_window_get_active_chat (self))
    chatty_chat_set_unread_count (chat, 0);

  if (CHATTY_IS_MM_CHAT (chat)) {
    GListModel *users;
    const char *name;

    users = chatty_chat_get_users (chat);
    name = chatty_chat_get_chat_name (chat);

    if (g_list_model_get_n_items (users) == 1 &&
        chatty_utils_username_is_valid (name, CHATTY_PROTOCOL_MMS_SMS)) {
      g_autoptr(ChattyMmBuddy) buddy = NULL;
      g_autoptr(GAppInfo) app_info = NULL;

      app_info = g_app_info_get_default_for_uri_scheme ("tel");
      buddy = g_list_model_get_item (users, 0);

      if (app_info)
        gtk_widget_show (self->call_button);
    }
  }
}
