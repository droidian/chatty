/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-window"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define _GNU_SOURCE
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include "contrib/gtk.h"

#include "chatty-window.h"
#include "chatty-history.h"
#include "chatty-avatar.h"
#include "chatty-manager.h"
#include "chatty-list-row.h"
#include "chatty-settings.h"
#include "chatty-mm-chat.h"
#include "chatty-pp-chat.h"
#include "chatty-chat-view.h"
#include "chatty-manager.h"
#include "chatty-utils.h"
#include "matrix/chatty-ma-account.h"
#include "matrix/chatty-ma-chat.h"
#include "dialogs/chatty-info-dialog.h"
#include "dialogs/chatty-settings-dialog.h"
#include "dialogs/chatty-new-chat-dialog.h"
#include "dialogs/chatty-new-muc-dialog.h"
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

  GtkWidget *menu_add_contact_button;
  GtkWidget *menu_new_message_button;
  GtkWidget *menu_new_group_message_button;
  GtkWidget *header_add_chat_button;
  GtkWidget *header_sub_menu_button;
  GtkWidget *leave_button;
  GtkWidget *delete_button;

  GtkWidget *chat_view;

  ChattyItem    *selected_item;
  ChattyManager *manager;

  char          *chat_needle;
  GtkFilter     *chat_filter;
  GtkFilterListModel *filter_model;
};


G_DEFINE_TYPE (ChattyWindow, chatty_window, GTK_TYPE_APPLICATION_WINDOW)

static void chatty_window_chat_list_select_first (ChattyWindow *self);

static void
window_set_item (ChattyWindow *self,
                 ChattyItem   *item)
{
  const char *header_label = "";

  g_assert (CHATTY_IS_WINDOW (self));

  if (CHATTY_IS_ITEM (item))
    header_label = chatty_item_get_name (item);

  self->selected_item = item;
  chatty_avatar_set_item (CHATTY_AVATAR (self->sub_header_icon), item);
  gtk_label_set_label (GTK_LABEL (self->sub_header_label), header_label);

  if (!item)
    hdy_leaflet_set_visible_child_name (HDY_LEAFLET (self->content_box), "sidebar");
}

static void
chatty_window_update_sidebar_view (ChattyWindow *self)
{
  GtkWidget *current_view;
  GListModel *model;
  gboolean has_child;

  g_assert (CHATTY_IS_WINDOW (self));

  model = G_LIST_MODEL (self->filter_model);
  has_child = g_list_model_get_n_items (model) > 0;

  if (has_child)
    current_view = self->chat_list_view;
  else
    current_view = self->empty_view;

  gtk_widget_set_sensitive (self->search_button, has_child);
  gtk_stack_set_visible_child (GTK_STACK (self->sidebar_stack), current_view);

  if (!has_child)
    hdy_search_bar_set_search_mode (HDY_SEARCH_BAR (self->chats_search_bar), FALSE);
}

static void
window_chat_changed_cb (ChattyWindow *self)
{
  GListModel *model;
  gboolean has_child;

  g_assert (CHATTY_IS_WINDOW (self));

  model = G_LIST_MODEL (self->filter_model);
  has_child = g_list_model_get_n_items (model) > 0;

  gtk_widget_set_sensitive (self->header_sub_menu_button, !!self->selected_item);

  /* Select the first item if none selected and we are in expanded mode */
  if (!self->selected_item &&
      gtk_list_box_get_selection_mode (GTK_LIST_BOX (self->chats_listbox)) == GTK_SELECTION_SINGLE)
    chatty_window_chat_list_select_first (self);

  /*
   * When the items are re-arranged, the selection will be lost.
   * Re-select it.  In GTK4, A #GtkListView with #GtkSingleSelection
   * would suite here better.
   */
  if (self->selected_item && has_child) {
    guint position;

    if (chatty_utils_get_item_position (G_LIST_MODEL (self->filter_model), self->selected_item, &position)) {
      GtkListBoxRow *row;

      row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->chats_listbox), position);
      gtk_list_box_select_row (GTK_LIST_BOX (self->chats_listbox), row);
    }
  }

  chatty_window_update_sidebar_view (self);

  if (has_child)
    return;

  if (chatty_manager_get_active_protocols (self->manager))
    hdy_status_page_set_description (HDY_STATUS_PAGE (self->empty_view),
                                     _("Select a contact with the "
                                       "<b>“+”</b> button in the titlebar."));
  else
    hdy_status_page_set_description (HDY_STATUS_PAGE (self->empty_view),
                                     _("For <b>Instant Messaging</b> add or activate "
                                       "an account in <i>\"preferences\"</i>."));
}

static gboolean
window_chat_name_matches (ChattyItem   *item,
                          ChattyWindow *self)
{
  ChattyProtocol protocol;

  g_assert (CHATTY_IS_CHAT (item));
  g_assert (CHATTY_IS_WINDOW (self));

  protocol = chatty_item_get_protocols (item);

  if ((!self->chat_needle || !*self->chat_needle) &&
      CHATTY_IS_MM_CHAT (item))
    return TRUE;

  /* FIXME: Not a good idea */
  if (chatty_item_get_protocols (item) != CHATTY_PROTOCOL_MMS_SMS) {
    ChattyAccount *account;

    if (CHATTY_IS_PP_CHAT (item) &&
        !chatty_pp_chat_get_auto_join (CHATTY_PP_CHAT (item)))
      return FALSE;

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
  ChattyChat *chat = NULL;

  g_assert (CHATTY_IS_WINDOW (self));
  g_assert (CHATTY_IS_ITEM (item));
  g_debug ("opening item of type: %s, protocol: %d",
           G_OBJECT_TYPE_NAME (item),
           chatty_item_get_protocols (item));

  if (CHATTY_IS_CONTACT (item)) {
    const char *number;

    number = chatty_item_get_username (item);
    chatty_window_set_uri (self, number);

    return;
  }

  if (CHATTY_IS_PP_BUDDY (item)) {
    chat = g_object_get_data (G_OBJECT (item), "chat");

    if (!chat) {
      gboolean has_encryption;

      has_encryption = chatty_manager_lurch_plugin_is_loaded (self->manager);
      chat = (ChattyChat *)chatty_pp_chat_new_buddy_chat (CHATTY_PP_BUDDY (item),
                                                          has_encryption);
      chatty_manager_add_chat (self->manager, chat);
    }
  }

  if (CHATTY_IS_PP_CHAT (item))
    chat = CHATTY_CHAT (item);

  if (CHATTY_IS_PP_CHAT (chat)) {
    chatty_pp_chat_join (CHATTY_PP_CHAT (chat));

    gtk_filter_changed (self->chat_filter, GTK_FILTER_CHANGE_DIFFERENT);
    window_chat_changed_cb (self);
  }

  if (CHATTY_IS_MM_CHAT (item)) {
    chatty_window_open_chat (CHATTY_WINDOW (self), CHATTY_CHAT (item));
  }
}

static void
window_chat_row_activated_cb (GtkListBox    *box,
                              GtkListBoxRow *row,
                              ChattyWindow  *self)
{
  g_assert (CHATTY_WINDOW (self));

  window_set_item (self, chatty_list_row_get_item (CHATTY_LIST_ROW (row)));

  g_return_if_fail (CHATTY_IS_CHAT (self->selected_item));

  if (CHATTY_IS_PP_CHAT (self->selected_item))
    chatty_window_open_item (self, self->selected_item);
  else
    chatty_window_open_chat (self, CHATTY_CHAT (self->selected_item));
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
notify_fold_cb (ChattyWindow *self)
{
  gboolean folded = hdy_leaflet_get_folded (HDY_LEAFLET (self->header_box));

  if (folded)
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->chats_listbox), GTK_SELECTION_NONE);
  else
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->chats_listbox), GTK_SELECTION_SINGLE);

  if (folded) {
    window_set_item (self, NULL);
  } else if (self->selected_item) {
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

  response = gtk_dialog_run (GTK_DIALOG (self->new_chat_dialog));
  gtk_widget_hide (self->new_chat_dialog);

  if (response != GTK_RESPONSE_OK)
    return;


  dialog = CHATTY_NEW_CHAT_DIALOG (self->new_chat_dialog);
  item = chatty_new_chat_dialog_get_selected_item (dialog);

  if (CHATTY_IS_CHAT (item))
    window_set_item (self, item);

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
window_new_muc_clicked_cb (ChattyWindow *self)
{
  GtkWidget *dialog;

  g_assert (CHATTY_IS_WINDOW (self));

  dialog = chatty_new_muc_dialog_new (GTK_WINDOW (self));
  gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);
}


static void
window_add_chat_button_clicked_cb (ChattyWindow *self)
{
  g_assert (CHATTY_IS_WINDOW (self));

  if (chatty_manager_get_active_protocols (self->manager) == CHATTY_PROTOCOL_MMS_SMS)
    window_new_message_clicked_cb (self);
  else
    gtk_popover_popup (GTK_POPOVER (self->header_chat_list_new_msg_popover));
}


static void
window_back_clicked_cb (ChattyWindow *self)
{
  g_assert (CHATTY_IS_WINDOW (self));

  window_set_item (self, NULL);

  /*
   * Clears 'selected_node' which is evaluated to
   * block the counting of pending messages
   * while chatting with this node
   */
  gtk_list_box_unselect_all (GTK_LIST_BOX (self->chats_listbox));
}

static void
chatty_window_chat_list_select_first (ChattyWindow *self)
{
  GtkListBoxRow *row;

  row = gtk_list_box_get_row_at_index (GTK_LIST_BOX(self->chats_listbox), 0);

  if (row != NULL) {
    gtk_list_box_select_row (GTK_LIST_BOX(self->chats_listbox), row);
    window_chat_row_activated_cb (GTK_LIST_BOX(self->chats_listbox), row, self);
  } else {
    window_set_item (self, NULL);
  }
}

static void
window_delete_buddy_clicked_cb (ChattyWindow *self)
{
  GtkWidget *dialog;
  const char *name;
  const char *text;
  const char *sub_text;
  int response;

  g_assert (CHATTY_IS_WINDOW (self));
  g_return_if_fail (self->selected_item);

  name = chatty_item_get_name (CHATTY_ITEM (self->selected_item));

  if (chatty_chat_is_im (CHATTY_CHAT (self->selected_item))) {
    text = _("Delete chat with");
    sub_text = _("This deletes the conversation history");
  } else {
    text = _("Disconnect group chat");
    sub_text = _("This removes chat from chats list");
  }

  dialog = gtk_message_dialog_new (GTK_WINDOW (self),
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_NONE,
                                   "%s %s",
                                   text, name);

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
                                CHATTY_CHAT (self->selected_item));
    if (CHATTY_IS_PP_CHAT (self->selected_item)) {
      chatty_pp_chat_delete (CHATTY_PP_CHAT (self->selected_item));
    } else if (CHATTY_IS_MM_CHAT (self->selected_item)) {
      chatty_mm_chat_delete (CHATTY_MM_CHAT (self->selected_item));
    } else {
      g_return_if_reached ();
    }

    window_set_item (self, NULL);
    chatty_window_chat_list_select_first (self);
  }

  gtk_widget_destroy (dialog);
}


static void
window_leave_chat_clicked_cb (ChattyWindow *self)
{
  g_assert (CHATTY_IS_WINDOW (self));

  if (self->selected_item) {
    ChattyAccount *account;

    account = chatty_chat_get_account (CHATTY_CHAT (self->selected_item));
    chatty_account_leave_chat_async (account,
                                     CHATTY_CHAT (self->selected_item),
                                     NULL, NULL);
  }

  window_set_item (self, NULL);
  chatty_window_chat_list_select_first (self);
}

static void
write_contact_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  g_autoptr(ChattyWindow) self = user_data;
  ChattyPpChat *chat = CHATTY_PP_CHAT (object);
  g_autoptr(GError) error = NULL;
  GtkWidget *dialog;

  g_assert (CHATTY_IS_WINDOW (self));
  g_assert (CHATTY_IS_PP_CHAT (chat));

  if (chatty_pp_chat_save_to_contacts_finish (chat, result, &error))
    return;

  dialog = gtk_message_dialog_new (GTK_WINDOW (self),
                                   GTK_DIALOG_MODAL,
                                   GTK_MESSAGE_WARNING,
                                   GTK_BUTTONS_CLOSE,
                                   _("Error saving contact: %s"), error->message);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

static void
write_eds_contact_cb (GObject      *object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  g_autoptr(ChattyWindow) self = user_data;
  g_autoptr(GError) error = NULL;
  GtkWidget *dialog;

  g_assert (CHATTY_IS_WINDOW (self));

  if (chatty_eds_write_contact_finish (result, &error))
    return;

  dialog = gtk_message_dialog_new (GTK_WINDOW (self),
                                   GTK_DIALOG_MODAL,
                                   GTK_MESSAGE_WARNING,
                                   GTK_BUTTONS_CLOSE,
                                   _("Error saving contact: %s"), error->message);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

static void
window_add_contact_clicked_cb (ChattyWindow *self)
{
  g_assert (CHATTY_IS_WINDOW (self));
  g_return_if_fail (self->selected_item);

  if (CHATTY_IS_PP_CHAT (self->selected_item)) {
    chatty_pp_chat_save_to_contacts_async (CHATTY_PP_CHAT (self->selected_item),
                                           write_contact_cb, g_object_ref (self));
  } else if (CHATTY_IS_MM_CHAT (self->selected_item)) {
    const char *phone;

    phone = chatty_chat_get_chat_name (CHATTY_CHAT (self->selected_item));
    chatty_eds_write_contact_async ("", phone,
                                    write_eds_contact_cb,
                                    g_object_ref (self));
  }

  gtk_widget_hide (self->menu_add_contact_button);
}

static void
window_show_chat_info_clicked_cb (ChattyWindow *self)
{
  ChattyInfoDialog *dialog;

  g_assert (CHATTY_IS_WINDOW (self));
  g_return_if_fail (CHATTY_IS_CHAT (self->selected_item));

  dialog = CHATTY_INFO_DIALOG (self->chat_info_dialog);

  chatty_info_dialog_set_chat (dialog, CHATTY_CHAT (self->selected_item));
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
window_active_protocols_changed_cb (ChattyWindow *self)
{
  ChattyProtocol protocols;
  gboolean has_sms, has_im;

  g_assert (CHATTY_IS_WINDOW (self));

  protocols = chatty_manager_get_active_protocols (self->manager);
  has_sms = !!(protocols & CHATTY_PROTOCOL_MMS_SMS);
  has_im  = !!(protocols & ~CHATTY_PROTOCOL_MMS_SMS);

  gtk_widget_set_sensitive (self->header_add_chat_button, has_sms || has_im);
  gtk_widget_set_sensitive (self->menu_new_group_message_button, has_im);

  gtk_filter_changed (self->chat_filter, GTK_FILTER_CHANGE_DIFFERENT);
  window_chat_changed_cb (self);
}

static void
window_chat_deleted_cb (ChattyWindow *self,
                        ChattyChat   *chat)
{
  g_assert (CHATTY_IS_WINDOW (self));
  g_assert (CHATTY_IS_CHAT (chat));

  if (self->selected_item != (gpointer)chat)
    return;

  window_set_item (self, NULL);
  chatty_chat_view_set_chat (CHATTY_CHAT_VIEW (self->chat_view), NULL);
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

  g_signal_connect_object (gtk_filter_list_model_get_model (self->filter_model),
                           "items-changed",
                           G_CALLBACK (window_chat_changed_cb), self,
                           G_CONNECT_SWAPPED);
  window_chat_changed_cb (self);

  G_OBJECT_CLASS (chatty_window_parent_class)->constructed (object);
}


static void
chatty_window_finalize (GObject *object)
{
  ChattyWindow *self = (ChattyWindow *)object;

  g_clear_object (&self->settings);

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

  widget_class->unmap = chatty_window_unmap;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-window.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, sub_header_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, sub_header_icon);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, menu_add_contact_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, menu_new_message_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, menu_new_group_message_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_add_chat_button);
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

  gtk_widget_class_bind_template_callback (widget_class, notify_fold_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_new_message_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_new_muc_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_add_chat_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_back_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_show_chat_info_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_add_contact_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_leave_chat_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_delete_buddy_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_search_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_chat_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, chatty_window_show_settings_dialog);
  gtk_widget_class_bind_template_callback (widget_class, chatty_window_show_about_dialog);
}


static void
chatty_window_init (ChattyWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->manager = g_object_ref (chatty_manager_get_default ());
  g_signal_connect_object (self->manager, "notify::active-protocols",
                           G_CALLBACK (window_active_protocols_changed_cb), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->manager, "chat-deleted",
                           G_CALLBACK (window_chat_deleted_cb), self,
                           G_CONNECT_SWAPPED);
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

  who = chatty_utils_check_phonenumber (uri, chatty_settings_get_country_iso_code (self->settings));

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

  if (!chatty_manager_set_uri (self->manager, uri))
    return;

  gtk_widget_hide (self->new_chat_dialog);
}

ChattyChat *
chatty_window_get_active_chat (ChattyWindow *self)
{
  GtkWidget *widget;

  g_return_val_if_fail (CHATTY_IS_WINDOW (self), NULL);

  widget = gtk_window_get_focus (GTK_WINDOW (self));

  if (widget && gtk_widget_has_focus (widget))
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
  g_debug ("opening item of type: %s, protocol: %d",
           G_OBJECT_TYPE_NAME (chat),
           chatty_item_get_protocols (CHATTY_ITEM (chat)));

  chatty_chat_view_set_chat (CHATTY_CHAT_VIEW (self->chat_view), chat);
  window_set_item (self, CHATTY_ITEM (chat));
  window_chat_changed_cb (self);

  gtk_widget_set_visible (self->leave_button, !CHATTY_IS_MM_CHAT (chat));
  can_delete = CHATTY_IS_PP_CHAT (chat) || CHATTY_IS_MM_CHAT (chat);
  gtk_widget_set_visible (self->delete_button, can_delete);
  hdy_leaflet_set_visible_child (HDY_LEAFLET (self->content_box), self->chat_view);
  gtk_widget_hide (self->menu_add_contact_button);

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

      buddy = g_list_model_get_item (users, 0);

      if (!chatty_mm_buddy_get_contact (buddy))
        gtk_widget_show (self->menu_add_contact_button);
    }
  }
}
