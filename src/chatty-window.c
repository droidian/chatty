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
#include "chatty-chat-list.h"
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
  HdyApplicationWindow parent_instance;

  ChattySettings *settings;

  GtkWidget *chat_list;

  GtkWidget *content_box;
  GtkWidget *sidebar;
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
  GtkWidget *header_bar;
  GtkWidget *header_back_button;
  GtkWidget *header_add_chat_button;
  GtkWidget *call_button;
  GtkWidget *header_sub_menu_button;
  GtkWidget *leave_button;
  GtkWidget *delete_button;
  GtkWidget *block_button;
  GtkWidget *unblock_button;
  GtkWidget *archive_button;
  GtkWidget *unarchive_button;

  GtkWidget *chat_view;
  GtkWidget *settings_dialog;

  GtkWidget *protocol_list;
  GtkWidget *protocol_any_row;
  gulong     chat_changed_handler;

  GBinding  *header_label_binding;
  GdTaggedEntryTag *protocol_tag;

  ChattyManager *manager;

  GtkWidget          *selected_protocol_row;
  ChattyProtocol protocol_filter;
};


G_DEFINE_TYPE (ChattyWindow, chatty_window, HDY_TYPE_APPLICATION_WINDOW)

static void
window_update_item_state_button (ChattyWindow *self,
                                 ChattyItem   *item)
{
  ChattyItemState state;

  state = chatty_item_get_state (item);

  gtk_widget_hide (self->block_button);
  gtk_widget_hide (self->unblock_button);
  gtk_widget_hide (self->archive_button);
  gtk_widget_hide (self->unarchive_button);

  if (state == CHATTY_ITEM_VISIBLE) {
    gtk_widget_show (self->block_button);
    gtk_widget_show (self->archive_button);
  } else if (state == CHATTY_ITEM_ARCHIVED) {
    gtk_widget_show (self->unarchive_button);
  } else if (state == CHATTY_ITEM_BLOCKED) {
    gtk_widget_show (self->unblock_button);
  }
}

static void
window_chat_changed_cb (ChattyWindow *self,
                        ChattyChat   *chat)
{
  GListModel *users;

  users = chatty_chat_get_users (chat);

  /* allow changing state only for 1:1 SMS/MMS chats  */
  if (CHATTY_IS_MM_CHAT (chat) && g_list_model_get_n_items (users) == 1) {
    if (chat == chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view)))
      window_update_item_state_button (self, CHATTY_ITEM (chat));
    chatty_chat_list_refilter (CHATTY_CHAT_LIST (self->chat_list));
  }
}

static void
window_set_item (ChattyWindow *self,
                 ChattyChat   *chat)
{
  g_assert (CHATTY_IS_WINDOW (self));

  chatty_avatar_set_item (CHATTY_AVATAR (self->sub_header_icon), CHATTY_ITEM (chat));
  g_clear_object (&self->header_label_binding);
  gtk_label_set_label (GTK_LABEL (self->sub_header_label), "");
  g_clear_signal_handler (&self->chat_changed_handler,
                          chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view)));

  if (CHATTY_IS_CHAT (chat)) {
    self->header_label_binding = g_object_bind_property (chat, "name",
                                                         self->sub_header_label, "label",
                                                         G_BINDING_SYNC_CREATE);
    self->chat_changed_handler = g_signal_connect_object (chat, "changed",
                                                          G_CALLBACK (window_chat_changed_cb),
                                                          self, G_CONNECT_SWAPPED);
  }

  if (!chat)
    hdy_leaflet_set_visible_child_name (HDY_LEAFLET (self->content_box), "sidebar");

  chatty_chat_view_set_chat (CHATTY_CHAT_VIEW (self->chat_view), chat);
  gtk_widget_set_visible (self->header_sub_menu_button, !!chat);
}

static void
chatty_window_update_search_mode (ChattyWindow *self)
{
  GListModel *model;
  gboolean has_child;

  g_assert (CHATTY_IS_WINDOW (self));

  model = chatty_chat_list_get_filter_model (CHATTY_CHAT_LIST (self->chat_list));
  has_child = g_list_model_get_n_items (model) > 0;

  gtk_widget_set_visible (self->search_button, has_child);

  if (!has_child)
    hdy_search_bar_set_search_mode (HDY_SEARCH_BAR (self->chats_search_bar), FALSE);
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
    chatty_window_set_uri (self, number, NULL);

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
  g_assert (GTK_IS_ENTRY (entry));

  chatty_chat_list_filter_string (CHATTY_CHAT_LIST (self->chat_list),
                                  gtk_entry_get_text (entry));
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
window_chat_list_selection_changed (ChattyWindow   *self,
                                    ChattyChatList *list)
{
  GPtrArray *chat_list;
  ChattyChat *chat;

  g_assert (CHATTY_IS_WINDOW (self));
  g_assert (CHATTY_IS_CHAT_LIST (list));

  chat_list = chatty_chat_list_get_selected (list);

  if (!chat_list->len) {
    GListModel *model;

    model = chatty_chat_list_get_filter_model (CHATTY_CHAT_LIST (self->chat_list));
    if (g_list_model_get_n_items (model) == 0) {
      chatty_chat_view_set_chat (CHATTY_CHAT_VIEW (self->chat_view), NULL);
      gtk_widget_hide (self->header_sub_menu_button);
    }

    return;
  }

  chat = chat_list->pdata[0];
  g_return_if_fail (CHATTY_IS_CHAT (chat));

  if (chat == chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view)))
    return;

#ifdef PURPLE_ENABLED
  if (CHATTY_IS_PP_CHAT (chat))
    chatty_window_open_item (self, CHATTY_ITEM (chat));
  else
#endif
    chatty_window_open_chat (self, chat);
}

static void
window_search_entry_activated_cb (ChattyWindow *self)
{
  g_assert (CHATTY_IS_WINDOW (self));

  chatty_chat_list_select_first (CHATTY_CHAT_LIST (self->chat_list));
}

static void
notify_fold_cb (ChattyWindow *self)
{
  gboolean folded;

  folded = hdy_leaflet_get_folded (HDY_LEAFLET (self->header_box));
  chatty_chat_list_set_selection_mode (CHATTY_CHAT_LIST (self->chat_list), !folded);

  if (folded) {
    window_set_item (self, NULL);
  }
}

static void
window_content_box_changed (ChattyWindow *self)
{
  if (hdy_leaflet_get_folded (HDY_LEAFLET (self->content_box)) &&
      hdy_leaflet_get_visible_child (HDY_LEAFLET (self->content_box)) == self->sidebar) {
    window_set_item (self, NULL);
  }
}
static void
window_show_unarchived_clicked_cb (ChattyWindow *self)
{
  g_assert (CHATTY_IS_WINDOW (self));

  hdy_header_bar_set_title (HDY_HEADER_BAR (self->header_bar), _("Chats"));
  chatty_chat_list_show_archived (CHATTY_CHAT_LIST (self->chat_list), FALSE);
  gtk_widget_hide (self->header_back_button);
  gtk_widget_show (self->header_add_chat_button);
  chatty_window_update_search_mode (self);
}

static void
window_show_new_chat_dialog (ChattyWindow *self,
                             gboolean      can_multi_select)
{
  ChattyNewChatDialog *dialog;

  g_assert (CHATTY_IS_WINDOW (self));

  dialog = CHATTY_NEW_CHAT_DIALOG (self->new_chat_dialog);
  chatty_new_chat_dialog_set_multi_selection (dialog, can_multi_select);
  gtk_window_present (GTK_WINDOW (self->new_chat_dialog));
}

static void
window_new_message_clicked_cb (ChattyWindow *self)
{
  window_show_new_chat_dialog (self, FALSE);
}

static void
window_new_sms_mms_message_clicked_cb (ChattyWindow *self)
{
  window_show_new_chat_dialog (self, TRUE);
}

static void
window_new_muc_clicked_cb (ChattyWindow *self)
{
  GtkWidget *dialog;

  g_assert (CHATTY_IS_WINDOW (self));

  dialog = chatty_new_muc_dialog_new (GTK_WINDOW (self));
  gtk_window_present (GTK_WINDOW (dialog));
}

static void
window_back_clicked_cb (ChattyWindow *self)
{
  g_assert (CHATTY_IS_WINDOW (self));

  window_set_item (self, NULL);
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

    if (!hdy_leaflet_get_folded (HDY_LEAFLET (self->header_box)))
      chatty_chat_list_select_first (CHATTY_CHAT_LIST (self->chat_list));
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

  if (!hdy_leaflet_get_folded (HDY_LEAFLET (self->header_box)))
    chatty_chat_list_select_first (CHATTY_CHAT_LIST (self->chat_list));
}

static void
window_block_contact_clicked_cb (ChattyWindow *self)
{
  GtkWidget *message;
  ChattyChat *chat;
  int result;

  g_assert (CHATTY_IS_WINDOW (self));

  chat = chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view));
  g_return_if_fail (CHATTY_IS_MM_CHAT (chat));
  g_return_if_fail (chatty_chat_is_im (chat));

  message = gtk_message_dialog_new (GTK_WINDOW (self),
                                    GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
                                    GTK_MESSAGE_INFO,
                                    GTK_BUTTONS_OK_CANCEL,
                                    _("You shall no longer be notified for new messages, continue?"));

  result = gtk_dialog_run (GTK_DIALOG (message));
  gtk_widget_destroy (message);

  if (result == GTK_RESPONSE_CANCEL)
    return;

  chatty_item_set_state (CHATTY_ITEM (chat), CHATTY_ITEM_BLOCKED);
}

static void
window_unblock_contact_clicked_cb (ChattyWindow *self)
{
  ChattyChat *chat;

  g_assert (CHATTY_IS_WINDOW (self));

  chat = chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view));
  g_return_if_fail (CHATTY_IS_MM_CHAT (chat));
  g_return_if_fail (chatty_chat_is_im (chat));

  chatty_item_set_state (CHATTY_ITEM (chat), CHATTY_ITEM_VISIBLE);
}

static void
window_archive_chat_clicked_cb (ChattyWindow *self)
{
  ChattyChat *chat;

  g_assert (CHATTY_IS_WINDOW (self));

  chat = chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view));
  g_return_if_fail (CHATTY_IS_MM_CHAT (chat));
  g_return_if_fail (chatty_chat_is_im (chat));

  chatty_item_set_state (CHATTY_ITEM (chat), CHATTY_ITEM_ARCHIVED);
}

static void
window_unarchive_chat_clicked_cb (ChattyWindow *self)
{
  ChattyChat *chat;

  g_assert (CHATTY_IS_WINDOW (self));

  chat = chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view));
  g_return_if_fail (CHATTY_IS_MM_CHAT (chat));
  g_return_if_fail (chatty_chat_is_im (chat));

  chatty_item_set_state (CHATTY_ITEM (chat), CHATTY_ITEM_VISIBLE);
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
chatty_window_show_archived (ChattyWindow *self)
{
  g_assert (CHATTY_IS_WINDOW (self));

  hdy_header_bar_set_title (HDY_HEADER_BAR (self->header_bar), _("Archived"));
  chatty_chat_list_show_archived (CHATTY_CHAT_LIST (self->chat_list), TRUE);
  gtk_widget_show (self->header_back_button);
  gtk_widget_hide (self->header_add_chat_button);
}

static void
chatty_window_show_settings_dialog (ChattyWindow *self)
{
  g_assert (CHATTY_IS_WINDOW (self));

  if (!self->settings_dialog)
    self->settings_dialog = chatty_settings_dialog_new (GTK_WINDOW (self));
  gtk_window_present (GTK_WINDOW (self->settings_dialog));
}

/* Copied from chatty-dialogs.c written by Andrea Schäfer <mosibasu@me.com> */
static void
chatty_window_show_about_dialog (ChattyWindow *self)
{
  static const gchar *authors[] = {
    "Adrien Plazas <kekun.plazas@laposte.net>",
    "Andrea Schäfer <mosibasu@me.com>",
    "Benedikt Wildenhain <benedikt.wildenhain@hs-bochum.de>",
    "Chris Talbot (kop316) <chris@talbothome.com>",
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
                         "copyright", "© 2018–2022 Purism SPC",
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
  chatty_chat_list_filter_protocol (CHATTY_CHAT_LIST (self->chat_list), self->protocol_filter);
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
new_chat_selection_changed_cb (ChattyWindow        *self,
                               ChattyNewChatDialog *dialog)
{
  g_autoptr(GString) users = g_string_new (NULL);
  GListModel *model;
  guint n_items;
  const char *name;

  g_assert (CHATTY_IS_WINDOW (self));
  g_assert (CHATTY_IS_NEW_CHAT_DIALOG (dialog));

  model = chatty_new_chat_dialog_get_selected_items (dialog);
  n_items = g_list_model_get_n_items (model);

  if (n_items == 0)
    goto end;

  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyItem) item = NULL;
    const char *phone_number;

    item = g_list_model_get_item (model, i);

    if (CHATTY_IS_CONTACT (item)) {
      phone_number = chatty_item_get_username (item);
      g_string_append (users, phone_number);
      g_string_append (users, ",");
    }
  }

  /* Remove the trailing "," */
  if (users->len >= 1)
    g_string_truncate (users, users->len - 1);

  if (n_items == 1) {
    g_autoptr(ChattyItem) item = NULL;

    item = g_list_model_get_item (model, 0);

    if (!CHATTY_IS_CONTACT (item) ||
        !chatty_contact_is_dummy (CHATTY_CONTACT (item))) {
      chatty_window_open_item (self, item);
      goto end;
    }
  }

  name = chatty_new_chat_dialog_get_chat_title (dialog);
  chatty_window_set_uri (self, users->str, name);

 end:
  gtk_widget_hide (GTK_WIDGET (dialog));
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

  g_signal_connect_object (chatty_chat_list_get_filter_model (CHATTY_CHAT_LIST (self->chat_list)),
                           "items-changed",
                           G_CALLBACK (chatty_window_update_search_mode), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->manager, "notify::active-protocols",
                           G_CALLBACK (window_active_protocols_changed_cb), self,
                           G_CONNECT_SWAPPED);

  notify_fold_cb (self);
  window_active_protocols_changed_cb (self);
  chatty_window_update_search_mode (self);

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
  g_signal_connect_object (self->new_chat_dialog, "selection-changed",
                           G_CALLBACK (new_chat_selection_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  self->chat_info_dialog = chatty_info_dialog_new (GTK_WINDOW (self));

  G_OBJECT_CLASS (chatty_window_parent_class)->constructed (object);
}


static void
chatty_window_finalize (GObject *object)
{
  ChattyWindow *self = (ChattyWindow *)object;

  g_clear_pointer (&self->new_chat_dialog, gtk_widget_destroy);
  g_clear_object (&self->settings);
  g_clear_object (&self->protocol_tag);

  G_OBJECT_CLASS (chatty_window_parent_class)->finalize (object);
}

static void
chatty_window_dispose (GObject *object)
{
  ChattyWindow *self = (ChattyWindow *)object;

  /* XXX: Why it fails without the check? */
  if (CHATTY_IS_CHAT_VIEW (self->chat_view))
    g_clear_signal_handler (&self->chat_changed_handler,
                            chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (self->chat_view)));
  g_clear_object (&self->manager);

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
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_bar);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_back_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_add_chat_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, call_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_sub_menu_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, leave_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, delete_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, block_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, unblock_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, archive_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, unarchive_button);

  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, search_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, chats_search_bar);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, chats_search_entry);

  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, content_box);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, sidebar);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_box);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_group);

  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, chat_list);

  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, chat_view);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_chat_list_new_msg_popover);

  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, protocol_list);

  gtk_widget_class_bind_template_callback (widget_class, notify_fold_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_content_box_changed);
  gtk_widget_class_bind_template_callback (widget_class, window_show_unarchived_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_new_message_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_new_sms_mms_message_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_new_muc_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_back_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_show_chat_info_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_leave_chat_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_block_contact_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_unblock_contact_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_archive_chat_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_unarchive_chat_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_delete_buddy_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_call_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_search_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, search_tag_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_search_toggled_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_search_entry_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_chat_list_selection_changed);
  gtk_widget_class_bind_template_callback (widget_class, chatty_window_show_archived);
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
  chatty_chat_view_set_db (CHATTY_CHAT_VIEW (self->chat_view),
                           chatty_manager_get_history (self->manager));
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
                       const char   *uri,
                       const char   *name)
{
  if (!chatty_manager_set_uri (self->manager, uri, name))
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

  gtk_widget_set_visible (self->leave_button, !CHATTY_IS_MM_CHAT (chat));
  /* We can't delete MaChat */
  can_delete = !CHATTY_IS_MA_CHAT (chat);
  gtk_widget_set_visible (self->delete_button, can_delete);

  /* We shall update the ability to change state below */
  gtk_widget_hide (self->block_button);
  gtk_widget_hide (self->unblock_button);
  gtk_widget_hide (self->archive_button);
  gtk_widget_hide (self->unarchive_button);

  hdy_leaflet_set_visible_child (HDY_LEAFLET (self->content_box), self->chat_view);
  gtk_widget_hide (self->call_button);

  if (chatty_window_get_active_chat (self))
    chatty_chat_set_unread_count (chat, 0);

  if (CHATTY_IS_MM_CHAT (chat)) {
    GListModel *users;
    const char *name;

    users = chatty_chat_get_users (chat);
    name = chatty_chat_get_chat_name (chat);

    /* allow changing state only for 1:1 SMS/MMS chats  */
    if (g_list_model_get_n_items (users) == 1) {
      window_update_item_state_button (self, CHATTY_ITEM (chat));
    }

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
