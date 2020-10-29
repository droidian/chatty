/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#define _GNU_SOURCE
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <purple.h>
#include "contrib/gtk.h"
#include "chatty-config.h"
#include "chatty-window.h"
#include "chatty-dbus.h"
#include "chatty-history.h"
#include "chatty-avatar.h"
#include "chatty-manager.h"
#include "chatty-list-row.h"
#include "chatty-settings.h"
#include "chatty-pp-chat.h"
#include "chatty-chat-view.h"
#include "chatty-manager.h"
#include "chatty-icons.h"
#include "chatty-utils.h"
#include "dialogs/chatty-settings-dialog.h"
#include "dialogs/chatty-new-chat-dialog.h"
#include "dialogs/chatty-new-muc-dialog.h"
#include "dialogs/chatty-user-info-dialog.h"
#include "dialogs/chatty-muc-info-dialog.h"

#define LAZY_LOAD_INITIAL_MSGS_LIMIT 20

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

  GtkWidget *search_button;
  GtkWidget *chats_search_bar;
  GtkWidget *chats_search_entry;

  GtkWidget *header_chat_list_new_msg_popover;

  GtkWidget *menu_add_contact_button;
  GtkWidget *menu_add_in_contacts_button;
  GtkWidget *menu_new_message_button;
  GtkWidget *menu_new_group_message_button;
  GtkWidget *menu_new_bulk_sms_button;
  GtkWidget *header_chat_info_button;
  GtkWidget *header_add_chat_button;
  GtkWidget *header_sub_menu_button;

  GtkWidget *convs_notebook;

  GtkWidget *overlay_icon;
  GtkWidget *overlay_label_1;
  GtkWidget *overlay_label_2;
  GtkWidget *overlay_label_3;


  ChattyItem    *selected_item;
  ChattyManager *manager;

  char          *chat_needle;
  GtkFilter     *chat_filter;
  GtkFilterListModel *filter_model;
};


G_DEFINE_TYPE (ChattyWindow, chatty_window, GTK_TYPE_APPLICATION_WINDOW)


static void chatty_update_header (ChattyWindow *self);
static void chatty_window_show_new_muc_dialog (ChattyWindow *self);


typedef struct {
  const char *title;
  const char *text_1;
  const char *text_2;
  const char *icon_name;
  int         icon_size;
} overlay_content_t;

overlay_content_t OverlayContent[6] = {
  {.title  = N_("Choose a contact"),
   .text_1 = N_("Select an <b>SMS</b> or <b>Instant Message</b> "
                "contact with the <b>\"+\"</b> button in the titlebar."),
   .text_2 = NULL,
  },
  {.title  = N_("Choose a contact"),
   .text_1 = N_("Select an <b>Instant Message</b> contact with "
                "the \"+\" button in the titlebar."),
   .text_2 = NULL,
  },
  {.title  = N_("Choose a contact"),
   .text_1 = N_("Start a <b>SMS</b> chat with the \"+\" button in the titlebar."),
   .text_2 = N_("For <b>Instant Messaging</b> add or activate "
                "an account in <i>\"preferences\"</i>."),
  },
  {.title  = N_("Start chatting"),
   .text_1 = N_("For <b>Instant Messaging</b> add or activate "
                "an account in <i>\"preferences\"</i>."),
   .text_2 = NULL,
  }
};


static GtkWidget *
window_chat_list_row_new (ChattyItem   *item,
                          ChattyWindow *self)
{
  GtkWidget *row;

  g_assert (CHATTY_IS_ITEM (item));
  g_assert (CHATTY_IS_WINDOW (self));

  row = chatty_list_row_new (item);

  if (self->selected_item == item)
    gtk_widget_set_state_flags (row, GTK_STATE_FLAG_SELECTED, FALSE);

  return row;
}

static GtkWidget *
window_get_view_for_chat (ChattyWindow *self,
                          ChattyChat   *chat)
{
  g_autoptr(GList) children = NULL;

  g_assert (CHATTY_IS_WINDOW (self));
  g_assert (CHATTY_IS_CHAT (chat));

  children = gtk_container_get_children (GTK_CONTAINER (self->convs_notebook));

  for (GList *child = children; child; child = child->next)
    if (CHATTY_IS_CHAT_VIEW (child->data) &&
        chatty_chat_view_get_chat (child->data) == chat)
      return child->data;

  return NULL;
}

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
}

static void
chatty_window_update_sidebar_view (ChattyWindow *self)
{
  GtkWidget *current_view;
  GListModel *model;
  gboolean has_child;

  g_assert (CHATTY_IS_WINDOW (self));

  model = chatty_manager_get_chat_list (self->manager);
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
  ChattyProtocol protocols;
  gint mode = CHATTY_OVERLAY_EMPTY_CHAT_NO_SMS_IM;
  gboolean has_sms, has_im;
  gboolean has_child;

  g_assert (CHATTY_IS_WINDOW (self));

  model = chatty_manager_get_chat_list (self->manager);
  has_child = g_list_model_get_n_items (model) > 0;

  gtk_widget_set_sensitive (self->header_sub_menu_button, has_child);

  /*
   * When the items are re-arranged, the selection will be lost.
   * Re-select it.  In GTK4, A #GtkListView with #GtkSingleSelection
   * would suite here better.
   */
  if (self->selected_item && has_child) {
    guint position;

    chatty_chat_set_unread_count (CHATTY_CHAT (self->selected_item), 0);

    if (chatty_utils_get_item_position (G_LIST_MODEL (self->filter_model), self->selected_item, &position)) {
      GtkListBoxRow *row;

      row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->chats_listbox), position);
      gtk_list_box_select_row (GTK_LIST_BOX (self->chats_listbox), row);
    }
  }

  chatty_window_update_sidebar_view (self);

  if (has_child)
    return;

  protocols = chatty_manager_get_active_protocols (self->manager);
  has_sms = !!(protocols & CHATTY_PROTOCOL_SMS);
  has_im  = !!(protocols & ~CHATTY_PROTOCOL_SMS);

  if (has_sms && has_im)
    mode = CHATTY_OVERLAY_EMPTY_CHAT;
  else if (has_sms)
    mode = CHATTY_OVERLAY_EMPTY_CHAT_NO_IM;
  else if (has_im)
    mode = CHATTY_OVERLAY_EMPTY_CHAT_NO_SMS;

  gtk_label_set_markup (GTK_LABEL (self->overlay_label_1),
                        gettext (OverlayContent[mode].title));
  gtk_label_set_markup (GTK_LABEL (self->overlay_label_2),
                        gettext (OverlayContent[mode].text_1));
  gtk_label_set_markup (GTK_LABEL (self->overlay_label_3),
                        gettext (OverlayContent[mode].text_2));
}

static void
window_notebook_after_switch_cb (GtkNotebook  *notebook,
                                 GtkWidget    *page,
                                 gint          page_num,
                                 ChattyWindow *self)
{
  ChattyChat *chat;

  g_assert (CHATTY_IS_CHAT_VIEW (page));

  chat = chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (page));
  window_set_item (self, CHATTY_ITEM (chat));
  window_chat_changed_cb (self);

  g_debug ("%s: Chat name: %s", G_STRFUNC, chatty_chat_get_chat_name (chat));

  chatty_chat_set_unread_count (chat, 0);
  gtk_widget_set_visible (self->header_chat_info_button,
                          !chatty_chat_is_im (chat));
}


static gboolean
window_chat_name_matches (ChattyItem   *item,
                          ChattyWindow *self)
{
  ChattyProtocol protocols;

  g_assert (CHATTY_IS_CHAT (item));
  g_assert (CHATTY_IS_WINDOW (self));

  protocols = chatty_manager_get_active_protocols (self->manager);

  if (!(protocols & chatty_item_get_protocols (item)))
    return FALSE;

  /* FIXME: Not a good idea */
  if (chatty_item_get_protocols (item) != CHATTY_PROTOCOL_SMS) {
    ChattyAccount *account;

    if (CHATTY_IS_PP_CHAT (item) &&
        !chatty_pp_chat_get_auto_join (CHATTY_PP_CHAT (item)))
      return FALSE;

    account = chatty_chat_get_account (CHATTY_CHAT (item));

    if (!account || chatty_account_get_status (account) != CHATTY_CONNECTED)
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
  PurpleBlistNode *node = NULL;
  PurpleChat *chat;

  g_assert (CHATTY_IS_WINDOW (self));
  g_assert (CHATTY_IS_ITEM (item));

  if (CHATTY_IS_CONTACT (item)) {
    const char *number;

    number = chatty_contact_get_value (CHATTY_CONTACT (item));
    chatty_window_set_uri (self, number);

    return;
  }

  if (CHATTY_IS_PP_BUDDY (item))
    node = (PurpleBlistNode *)chatty_pp_buddy_get_buddy (CHATTY_PP_BUDDY (item));

  if (CHATTY_IS_PP_CHAT (item))
    node = (PurpleBlistNode *)chatty_pp_chat_get_purple_buddy (CHATTY_PP_CHAT (item));

  if (node) {
    PurpleAccount *account;
    PurpleBuddy *buddy;

    buddy = (PurpleBuddy*)node;
    account = purple_buddy_get_account (buddy);

    if (chatty_item_get_protocols (item) == CHATTY_PROTOCOL_SMS) {
      ChattyEds *chatty_eds;
      ChattyContact *contact;
      const char *number;

      chatty_eds = chatty_manager_get_eds (self->manager);
      number = purple_buddy_get_name (buddy);
      contact = chatty_eds_find_by_number (chatty_eds, number);

      if (!contact)
        gtk_widget_show (self->menu_add_in_contacts_button);
    }

    if (purple_blist_node_get_bool (node, "chatty-unknown-contact"))
      gtk_widget_show (self->menu_add_contact_button);

    purple_blist_node_set_bool (PURPLE_BLIST_NODE (buddy), "chatty-autojoin", TRUE);
    chatty_conv_im_with_buddy (account, purple_buddy_get_name (buddy));

    return;
  }

  if (CHATTY_IS_PP_CHAT (item) &&
      (chat = chatty_pp_chat_get_purple_chat (CHATTY_PP_CHAT (item)))) {
    chatty_conv_join_chat (chat);

    purple_blist_node_set_bool ((PurpleBlistNode *)chat, "chatty-autojoin", TRUE);
    chatty_window_change_view (self, CHATTY_VIEW_MESSAGE_LIST);

    gtk_filter_changed (self->chat_filter, GTK_FILTER_CHANGE_DIFFERENT);
    window_chat_changed_cb (self);
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

  chatty_window_open_item (self, self->selected_item);
}


static void
header_visible_child_cb (GObject      *sender,
                         GParamSpec   *pspec,
                         ChattyWindow *self)
{
  g_assert (CHATTY_IS_WINDOW (self));

  chatty_update_header (self);
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
notify_fold_cb (GObject      *sender,
                GParamSpec   *pspec,
                ChattyWindow *self)
{
  HdyFold fold = hdy_leaflet_get_fold (HDY_LEAFLET (self->header_box));

  if (fold == HDY_FOLD_FOLDED)
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->chats_listbox), GTK_SELECTION_NONE);
  else
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->chats_listbox), GTK_SELECTION_SINGLE);

  if (fold == HDY_FOLD_FOLDED) {
    window_set_item (self, NULL);
    hdy_leaflet_set_visible_child_name (HDY_LEAFLET (self->content_box), "sidebar");
  } else if (self->selected_item) {
    window_chat_changed_cb (self);
  } else {
    chatty_window_chat_list_select_first (self);
  }

  chatty_update_header (self);
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
    phone_number = chatty_contact_get_value (CHATTY_CONTACT (item));

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
  g_assert (CHATTY_IS_WINDOW (self));

  chatty_window_show_new_muc_dialog (self);
}


static void
window_add_chat_button_clicked_cb (ChattyWindow *self)
{
  g_assert (CHATTY_IS_WINDOW (self));

  if (chatty_manager_get_active_protocols (self->manager) == CHATTY_PROTOCOL_SMS)
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
  chatty_window_change_view (self, CHATTY_VIEW_CHAT_LIST);
}


void
chatty_window_chat_list_select_first (ChattyWindow *self)
{
  GtkListBoxRow *row;

  row = gtk_list_box_get_row_at_index (GTK_LIST_BOX(self->chats_listbox), 0);

  if (row != NULL) {
    gtk_list_box_select_row (GTK_LIST_BOX(self->chats_listbox), row);
    window_chat_row_activated_cb (GTK_LIST_BOX(self->chats_listbox), row, self);
  } else {
    chatty_window_change_view (self, CHATTY_VIEW_CHAT_LIST);
  }
}


static void
chatty_update_header (ChattyWindow *self)
{
  GtkWidget *header_child = hdy_leaflet_get_visible_child (HDY_LEAFLET (self->header_box));
  HdyFold fold = hdy_leaflet_get_fold (HDY_LEAFLET (self->header_box));

  g_assert (CHATTY_IS_WINDOW (self));
  g_assert (header_child == NULL || GTK_IS_HEADER_BAR (header_child));

  hdy_header_group_set_focus (HDY_HEADER_GROUP (self->header_group), 
                              fold == HDY_FOLD_FOLDED ? 
                              GTK_HEADER_BAR (header_child) : NULL);
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

  if (!self->selected_item) {
    chatty_window_change_view (self, CHATTY_VIEW_CHAT_LIST);

    g_return_if_reached ();
  }

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
    chatty_history_delete_chat (CHATTY_CHAT (self->selected_item));
    chatty_pp_chat_delete (CHATTY_PP_CHAT (self->selected_item));

    window_set_item (self, NULL);
    chatty_window_chat_list_select_first (self);

    chatty_window_change_view (self, CHATTY_VIEW_CHAT_LIST);
  }

  gtk_widget_destroy (dialog);
}


static void
window_leave_chat_clicked_cb (ChattyWindow *self)
{
  PurpleBlistNode *node;

  g_assert (CHATTY_IS_WINDOW (self));

  if (!self->selected_item) {
    chatty_window_change_view (self, CHATTY_VIEW_CHAT_LIST);

    g_return_if_reached ();
  }

  node = (PurpleBlistNode *)chatty_pp_chat_get_purple_buddy (CHATTY_PP_CHAT (self->selected_item));

  if (!node)
    node = (PurpleBlistNode *)chatty_pp_chat_get_purple_chat (CHATTY_PP_CHAT (self->selected_item));

  if (node) {
    purple_blist_node_set_bool (node, "chatty-autojoin", FALSE);
    purple_conversation_destroy (chatty_pp_chat_get_purple_conv (CHATTY_PP_CHAT (self->selected_item)));
  }

  window_set_item (self, NULL);
  chatty_window_chat_list_select_first (self);
  chatty_window_change_view (self, CHATTY_VIEW_CHAT_LIST);
}


static void
window_add_contact_clicked_cb (ChattyWindow *self)
{
  PurpleAccount      *account;
  PurpleConversation *conv;
  PurpleBuddy        *buddy;
  const char         *who;
  g_autofree gchar   *number = NULL;

  g_assert (CHATTY_IS_WINDOW (self));
  g_return_if_fail (self->selected_item);

  buddy = chatty_pp_chat_get_purple_buddy (CHATTY_PP_CHAT (self->selected_item));
  g_return_if_fail (buddy != NULL);

  conv = chatty_pp_chat_get_purple_conv (CHATTY_PP_CHAT (self->selected_item));

  account = purple_conversation_get_account (conv);
  purple_account_add_buddy (account, buddy);
  purple_blist_node_remove_setting (PURPLE_BLIST_NODE(buddy), "chatty-unknown-contact");
  purple_blist_node_set_bool (PURPLE_BLIST_NODE (buddy), "chatty-notifications", TRUE);

  if (chatty_item_get_protocols (CHATTY_ITEM (self->selected_item)) == CHATTY_PROTOCOL_SMS) {
    ChattyEds *chatty_eds;
    ChattyContact *contact = NULL;

    chatty_eds = chatty_manager_get_eds (self->manager);

    who = purple_buddy_get_name (buddy);

    number = chatty_utils_check_phonenumber (who);

    if (number)
      contact = chatty_eds_find_by_number (chatty_eds, number);

    if (contact)
      chatty_dbus_gc_write_contact (who, number);
  }
}


static void
window_add_in_contacts_clicked_cb (ChattyWindow *self)
{
  PurpleBuddy      *buddy;
  PurpleContact    *contact;
  const char       *who;
  const char       *alias;
  g_autofree gchar *number = NULL;

  g_assert (CHATTY_IS_WINDOW (self));
  g_return_if_fail (self->selected_item);

  buddy = chatty_pp_chat_get_purple_buddy (CHATTY_PP_CHAT (self->selected_item));
  g_return_if_fail (buddy != NULL);

  who = purple_buddy_get_name (buddy);
  contact = purple_buddy_get_contact (buddy);
  alias = purple_contact_get_alias (contact);

  number = chatty_utils_check_phonenumber (who);

  chatty_dbus_gc_write_contact (alias, number);
}


static void
window_show_chat_info_clicked_cb (ChattyWindow *self)
{
  ChattyChat *chat;
  GtkWidget *dialog;

  g_assert (CHATTY_IS_WINDOW (self));
  g_return_if_fail (CHATTY_IS_CHAT (self->selected_item));

  chat = CHATTY_CHAT (self->selected_item);

  if (chatty_chat_is_im (chat)) {
    dialog = chatty_user_info_dialog_new (GTK_WINDOW (self));
    chatty_user_info_dialog_set_chat (CHATTY_USER_INFO_DIALOG (dialog), chat);
  } else {
    dialog = chatty_muc_info_dialog_new (GTK_WINDOW (self));
    chatty_muc_info_dialog_set_chat (CHATTY_MUC_INFO_DIALOG (dialog), chat);
  }

  gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);
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


static void
chatty_window_show_new_muc_dialog (ChattyWindow *self)
{
  GtkWidget *dialog;

  g_assert (CHATTY_IS_WINDOW (self));

  dialog = chatty_new_muc_dialog_new (GTK_WINDOW (self));
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
                         "copyright", "© 2018–2020 Purism SPC",
                         "license-type", GTK_LICENSE_GPL_3_0,
                         "authors", authors,
                         "artists", artists,
                         "documenters", documenters,
                         "translator-credits", _("translator-credits"),
                         NULL);
}


static void
chatty_window_show_new_chat_dialog (ChattyWindow *self)
{
  /* XXX: Not used */
  gtk_dialog_run (GTK_DIALOG (self->new_chat_dialog));
}

void
chatty_window_change_view (ChattyWindow      *self,
                           ChattyWindowState  view)
{
  g_assert (CHATTY_IS_WINDOW (self));

  switch (view) {
    case CHATTY_VIEW_MESSAGE_LIST:
      hdy_leaflet_set_visible_child_name (HDY_LEAFLET (self->content_box), "content");
      break;
    case CHATTY_VIEW_CHAT_LIST:
      hdy_leaflet_set_visible_child_name (HDY_LEAFLET (self->content_box), "sidebar");
      break;
    default:
      ;
  }
}

static void
window_active_protocols_changed_cb (ChattyWindow *self)
{
  ChattyProtocol protocols;
  gboolean has_sms, has_im;

  g_assert (CHATTY_IS_WINDOW (self));

  protocols = chatty_manager_get_active_protocols (self->manager);
  has_sms = !!(protocols & CHATTY_PROTOCOL_SMS);
  has_im  = !!(protocols & ~CHATTY_PROTOCOL_SMS);

  gtk_widget_set_sensitive (self->header_add_chat_button, has_sms || has_im);
  gtk_widget_set_sensitive (self->menu_new_group_message_button, has_im);
  gtk_widget_set_sensitive (self->menu_new_bulk_sms_button, has_sms);
  
  gtk_filter_changed (self->chat_filter, GTK_FILTER_CHANGE_DIFFERENT);
  window_chat_changed_cb (self);
}

static void
window_chat_deleted_cb (ChattyWindow *self,
                        ChattyChat   *chat)
{
  GtkWidget *view;

  g_assert (CHATTY_IS_WINDOW (self));
  g_assert (CHATTY_IS_CHAT (chat));

  if (self->selected_item == (gpointer)chat)
    window_set_item (self, NULL);

  view = window_get_view_for_chat (self, chat);

  if (view)
    gtk_widget_destroy (view);
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

  hdy_leaflet_set_visible_child_name (HDY_LEAFLET (self->content_box), "sidebar");

  hdy_search_bar_connect_entry (HDY_SEARCH_BAR(self->chats_search_bar),
                                GTK_ENTRY (self->chats_search_entry));

  gtk_widget_set_sensitive (GTK_WIDGET (self->header_sub_menu_button), FALSE);

  chatty_window_change_view (self, CHATTY_VIEW_CHAT_LIST);

  gtk_widget_set_sensitive (self->header_add_chat_button, FALSE);

  self->chat_filter = gtk_custom_filter_new ((GtkCustomFilterFunc)window_chat_name_matches,
                                             g_object_ref (self),
                                             g_object_unref);
  self->filter_model = gtk_filter_list_model_new (chatty_manager_get_chat_list (self->manager),
                                                  self->chat_filter);
  gtk_list_box_bind_model (GTK_LIST_BOX (self->chats_listbox),
                           G_LIST_MODEL (self->filter_model),
                           (GtkListBoxCreateWidgetFunc)window_chat_list_row_new,
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
                                               "/sm/puri/chatty/"
                                               "ui/chatty-window.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, sub_header_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, sub_header_icon);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, menu_add_contact_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, menu_add_in_contacts_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, menu_new_message_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, menu_new_group_message_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, menu_new_bulk_sms_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_chat_info_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_add_chat_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_sub_menu_button);

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

  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, convs_notebook);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, overlay_icon);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, overlay_label_1);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, overlay_label_2);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, overlay_label_3);
  gtk_widget_class_bind_template_child (widget_class, ChattyWindow, header_chat_list_new_msg_popover);

  gtk_widget_class_bind_template_callback (widget_class, notify_fold_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_new_message_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_new_muc_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_add_chat_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_back_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_show_chat_info_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_add_contact_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_add_in_contacts_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_leave_chat_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_delete_buddy_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, header_visible_child_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_search_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_chat_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, chatty_window_show_new_muc_dialog);
  gtk_widget_class_bind_template_callback (widget_class, chatty_window_show_new_chat_dialog);
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

  g_signal_connect_after (G_OBJECT (self->convs_notebook),
                          "switch-page",
                          G_CALLBACK (window_notebook_after_switch_cb), self);

  if (purple_prefs_get_bool (CHATTY_PREFS_ROOT "/conversations/show_tabs"))
    gtk_notebook_set_show_tabs (GTK_NOTEBOOK (self->convs_notebook), TRUE);
  else
    gtk_notebook_set_show_tabs (GTK_NOTEBOOK (self->convs_notebook), FALSE);
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
  g_autoptr(ChattyChat) chat = NULL;
  ChattyChat    *item;
  ChattyEds     *chatty_eds;
  ChattyContact *contact;
  PurpleAccount *account;
  ChattyPpBuddy *pp_buddy;
  PurpleBuddy   *buddy;
  char          *who = NULL;
  const char    *alias;

  account = purple_accounts_find ("SMS", "prpl-mm-sms");

  if (!purple_account_is_connected (account))
    return;

  who = chatty_utils_check_phonenumber (uri);

  chatty_eds = chatty_manager_get_eds (self->manager);
  contact = chatty_eds_find_by_number (chatty_eds, who);

  if (contact)
    alias = chatty_item_get_name (CHATTY_ITEM (contact));
  else
    alias = who;

  g_return_if_fail (who != NULL);

  buddy = purple_find_buddy (account, who);

  if (!buddy) {
    buddy = purple_buddy_new (account, who, alias);

    purple_blist_add_buddy (buddy, NULL, NULL, NULL);
  }

  pp_buddy = chatty_pp_buddy_get_object (buddy);

  if (pp_buddy && contact)
    chatty_pp_buddy_set_contact (pp_buddy, contact);

  chat = (ChattyChat *)chatty_pp_chat_new_im_chat (account, buddy);
  item = chatty_manager_add_chat (chatty_manager_get_default (), chat);

  purple_blist_node_set_bool (PURPLE_BLIST_NODE(buddy), "chatty-autojoin", TRUE);

  chatty_conv_im_with_buddy (account, g_strdup (who));

  gtk_widget_hide (self->new_chat_dialog);

  chatty_window_change_view (self, CHATTY_VIEW_MESSAGE_LIST);
  g_signal_emit_by_name (item, "changed");

  g_free (who);
}

ChattyChat *
chatty_window_get_active_chat (ChattyWindow *self)
{
  GtkWidget *child;
  gint current_page;

  g_return_val_if_fail (CHATTY_IS_WINDOW (self), NULL);

  current_page = gtk_notebook_get_current_page (GTK_NOTEBOOK (self->convs_notebook));

  if (current_page == -1)
    return NULL;

  child = gtk_notebook_get_nth_page (GTK_NOTEBOOK (self->convs_notebook), current_page);
  if (!gtk_widget_is_drawable (child))
    return NULL;

  return chatty_chat_view_get_chat (CHATTY_CHAT_VIEW (child));
}

void
chatty_window_open_chat (ChattyWindow *self,
                         ChattyChat   *chat)
{
  GtkWidget *view;
  int page_num;

  g_return_if_fail (CHATTY_IS_WINDOW (self));
  g_return_if_fail (CHATTY_IS_CHAT (chat));

  view = window_get_view_for_chat (self, chat);

  if (!view) {
    const char *name;
    g_auto(GStrv) split = NULL;
    g_autofree char *label = NULL;

    view  = chatty_chat_view_new ();
    name  = chatty_item_get_name (CHATTY_ITEM (chat));
    split = g_strsplit (name, "@", -1);
    label = g_strdup_printf ("%s %s", split[0], " >");

    chatty_chat_view_set_chat (CHATTY_CHAT_VIEW (view), chat);
    gtk_widget_show (view);

    gtk_container_add (GTK_CONTAINER (self->convs_notebook), view);
    gtk_notebook_set_tab_label_text (GTK_NOTEBOOK(self->convs_notebook), view, label);

    chatty_manager_load_more_chat (chatty_manager_get_default (), chat, LAZY_LOAD_INITIAL_MSGS_LIMIT);
  }

  page_num = gtk_notebook_page_num (GTK_NOTEBOOK (self->convs_notebook), view);
  gtk_notebook_set_current_page (GTK_NOTEBOOK (self->convs_notebook), page_num);
  hdy_leaflet_set_visible_child (HDY_LEAFLET (self->content_box), self->convs_notebook);

  chatty_chat_set_unread_count (chat, 0);
  gtk_window_present (GTK_WINDOW (self));
}
