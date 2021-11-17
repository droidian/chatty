/* -*- mode: c; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* chatty-pp-chat-info.c
 *
 * Copyright 2021 Purism SPC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-pp-chat-info"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>

#include "chatty-avatar.h"
#include "chatty-list-row.h"
#include "chatty-fp-row.h"
#include "chatty-pp-chat.h"
#include "chatty-pp-chat-info.h"
#include "chatty-log.h"

struct _ChattyPpChatInfo
{
  ChattyChatInfo parent_instance;

  ChattyChat    *chat;

  GtkWidget     *room_name;
  GtkWidget     *avatar;
  GtkWidget     *edit_avatar_button;
  GtkWidget     *delete_avatar_button;
  GtkWidget     *room_topic_view;
  GtkTextBuffer *topic_buffer;

  GtkWidget     *name_label;
  GtkWidget     *user_id_title;
  GtkWidget     *user_id_label;
  GtkWidget     *encryption_label;
  GtkWidget     *status_label;
  GtkWidget     *settings_label;

  GtkWidget     *notification_switch;
  GtkWidget     *show_status_switch;
  GtkWidget     *encryption_switch;

  GtkWidget     *key_list;
  GtkWidget     *user_list_label;
  GtkWidget     *user_list;

  GBinding      *binding;
  gulong         avatar_changed_id;
};

G_DEFINE_TYPE (ChattyPpChatInfo, chatty_pp_chat_info, CHATTY_TYPE_CHAT_INFO)

static void
pp_info_edit_avatar_button_clicked_cb (ChattyPpChatInfo *self)
{
  g_autoptr(GtkFileChooserNative) dialog = NULL;
  GtkFileFilter *filter;
  GtkWindow *window;
  g_autofree char *file_name = NULL;
  int response;

  g_assert (CHATTY_IS_PP_CHAT_INFO (self));

  window = (GtkWindow *)gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
  dialog = gtk_file_chooser_native_new (_("Set Avatar"),
                                        GTK_WINDOW (window),
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        _("Open"),
                                        _("Cancel"));
  gtk_native_dialog_set_transient_for (GTK_NATIVE_DIALOG (dialog), GTK_WINDOW (window));
  gtk_native_dialog_set_modal (GTK_NATIVE_DIALOG (dialog), TRUE);

  filter = gtk_file_filter_new ();
  gtk_file_filter_add_mime_type (filter, "image/*");
  gtk_file_filter_set_name (filter, _("Images"));
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  response = gtk_native_dialog_run (GTK_NATIVE_DIALOG (dialog));

  if (response == GTK_RESPONSE_ACCEPT)
    file_name = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

  if (file_name)
    chatty_item_set_avatar_async (CHATTY_ITEM (self->chat), file_name,
                                  NULL, NULL, NULL);
}

static void
pp_info_delete_avatar_button_clicked_cb (ChattyPpChatInfo *self)
{
  g_assert (CHATTY_IS_PP_CHAT_INFO (self));

  chatty_item_set_avatar_async (CHATTY_ITEM (self->chat),
                                NULL, NULL, NULL, NULL);
}

static void
pp_info_avatar_changed_cb (ChattyPpChatInfo *self)
{
  GdkPixbuf *avatar;
  ChattyProtocol protocol;
  gboolean can_change;

  g_assert (CHATTY_IS_PP_CHAT_INFO (self));

  if (!self->chat)
    return;

  protocol = chatty_item_get_protocols (CHATTY_ITEM (self->chat));
  avatar = chatty_item_get_avatar (CHATTY_ITEM (self->chat));
  can_change = protocol & (CHATTY_PROTOCOL_XMPP | CHATTY_PROTOCOL_TELEGRAM);
  can_change = can_change && chatty_chat_is_im (CHATTY_CHAT (self->chat));
  gtk_widget_set_visible (self->delete_avatar_button, can_change && !!avatar);
  gtk_widget_set_visible (self->edit_avatar_button, can_change);
}

static void
pp_chat_info_edit_topic_clicked_cb (ChattyPpChatInfo *self,
                                    GtkToggleButton  *edit_button)
{
  gboolean editable;

  g_assert (CHATTY_IS_PP_CHAT_INFO (self));
  g_assert (GTK_IS_TOGGLE_BUTTON (edit_button));

  editable = gtk_toggle_button_get_active (edit_button);
  gtk_text_view_set_editable (GTK_TEXT_VIEW (self->room_topic_view), editable);
  gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (self->room_topic_view), editable);
  gtk_widget_set_can_focus (self->room_topic_view, editable);

  if (editable) {
    gtk_widget_grab_focus (self->room_topic_view);
  } else if (gtk_text_buffer_get_modified (self->topic_buffer)) {
    g_autofree char *text = NULL;

    g_object_get (self->topic_buffer, "text", &text, NULL);
    chatty_chat_set_topic (CHATTY_CHAT (self->chat), text);
  }

  gtk_text_buffer_set_modified (self->topic_buffer, FALSE);
}

static void
pp_chat_info_notification_switch_changed_cb (ChattyPpChatInfo *self)
{
  gboolean active;

  g_assert (CHATTY_IS_PP_CHAT_INFO (self));

  active = gtk_switch_get_active (GTK_SWITCH (self->notification_switch));
  chatty_pp_chat_set_show_notifications (CHATTY_PP_CHAT (self->chat), active);
}

static void
pp_chat_info_show_status_switch_changed_cb (ChattyPpChatInfo *self)
{
  gboolean active;

  g_assert (CHATTY_IS_PP_CHAT_INFO (self));
  g_return_if_fail (CHATTY_IS_PP_CHAT (self->chat));

  active = gtk_switch_get_active (GTK_SWITCH (self->show_status_switch));
  chatty_pp_chat_set_show_status_msg (CHATTY_PP_CHAT (self->chat), active);
}

static void
pp_chat_info_encrypt_changed_cb (ChattyPpChatInfo *self)
{
  const char *status_msg = "";
  ChattyEncryption encryption;
  gboolean has_encryption;

  g_assert (CHATTY_IS_PP_CHAT_INFO (self));

  encryption = chatty_chat_get_encryption (self->chat);
  has_encryption = encryption != CHATTY_ENCRYPTION_UNSUPPORTED;

  gtk_widget_set_visible (self->settings_label, CHATTY_IS_PP_CHAT (self->chat));
  gtk_widget_set_visible (self->encryption_switch,
                          has_encryption && CHATTY_IS_PP_CHAT (self->chat));

  switch (encryption) {
  case CHATTY_ENCRYPTION_UNSUPPORTED:
  case CHATTY_ENCRYPTION_UNKNOWN:
    status_msg = _("Encryption is not available");
    break;

  case CHATTY_ENCRYPTION_ENABLED:
    status_msg = _("This chat is encrypted");
    break;

  case CHATTY_ENCRYPTION_DISABLED:
    status_msg = _("This chat is not encrypted");
    break;

  default:
    g_return_if_reached ();
  }

  gtk_label_set_text (GTK_LABEL (self->encryption_label), status_msg);
}

static void
pp_chat_info_list_fp_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  ChattyPpChatInfo *self = user_data;
  ChattyPpChat *chat;

  g_assert (CHATTY_IS_PP_CHAT_INFO (self));

  chat = CHATTY_PP_CHAT (self->chat);

  if (!chatty_pp_chat_load_fp_list_finish (chat, result, NULL)) {
    gtk_widget_hide (self->key_list);
    gtk_label_set_text (GTK_LABEL (self->encryption_label), _("Encryption not available"));
  } else {
    gtk_widget_show (self->key_list);
    gtk_list_box_bind_model (GTK_LIST_BOX (self->key_list),
                             chatty_pp_chat_get_fp_list (chat),
                             (GtkListBoxCreateWidgetFunc) chatty_fp_row_new,
                             NULL, NULL);
  }
}

static void
chatty_pp_chat_info_list_users (ChattyPpChatInfo *self)
{
  GListModel *user_list;
  g_autofree char *count_str = NULL;
  guint n_items;

  g_assert (CHATTY_IS_PP_CHAT_INFO (self));

  user_list = chatty_chat_get_users (self->chat);
  n_items = g_list_model_get_n_items (user_list);
  count_str = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%u Member",
                                            "%u Members", n_items),
                               n_items);
  gtk_label_set_text (GTK_LABEL (self->user_list_label), count_str);
  gtk_list_box_bind_model (GTK_LIST_BOX (self->user_list), user_list,
                           (GtkListBoxCreateWidgetFunc)chatty_list_row_new,
                           NULL, NULL);
}

static void
chatty_pp_chat_info_update (ChattyPpChatInfo *self)
{
  ChattyProtocol protocol;

  g_assert (CHATTY_IS_PP_CHAT_INFO (self));

  if (!self->chat)
    return;

  protocol = chatty_item_get_protocols (CHATTY_ITEM (self->chat));

  chatty_avatar_set_item (CHATTY_AVATAR (self->avatar), CHATTY_ITEM (self->chat));

  self->binding = g_object_bind_property (self->chat, "encrypt",
                                          self->encryption_switch, "active",
                                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  g_signal_connect_object (self->chat, "notify::encrypt",
                           G_CALLBACK (pp_chat_info_encrypt_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  pp_chat_info_encrypt_changed_cb (self);

  if (protocol == CHATTY_PROTOCOL_MMS_SMS ||
      protocol == CHATTY_PROTOCOL_MMS) {
    gtk_label_set_text (GTK_LABEL (self->user_id_title), _("Phone Number:"));
  } else if (protocol == CHATTY_PROTOCOL_XMPP) {
    gtk_label_set_text (GTK_LABEL (self->user_id_title), _("XMPP ID:"));
    gtk_widget_show (GTK_WIDGET (self->status_label));
    gtk_label_set_text (GTK_LABEL (self->status_label),
                        chatty_pp_chat_get_status (CHATTY_PP_CHAT (self->chat)));
  } else if (protocol == CHATTY_PROTOCOL_MATRIX) {
    gtk_label_set_text (GTK_LABEL (self->user_id_title), _("Matrix ID:"));
    gtk_widget_hide (self->user_id_label);
  } else if (protocol == CHATTY_PROTOCOL_TELEGRAM) {
    gtk_label_set_text (GTK_LABEL (self->user_id_title), _("Telegram ID:"));
  }

  if (chatty_chat_is_im (self->chat))
    gtk_label_set_text (GTK_LABEL (self->user_id_label),
                        chatty_chat_get_chat_name (self->chat));
  gtk_label_set_text (GTK_LABEL (self->name_label),
                      chatty_item_get_name (CHATTY_ITEM (self->chat)));

  if (chatty_chat_is_im (self->chat)) {
    gtk_widget_show (self->user_id_label);
    gtk_widget_show (self->name_label);
  } else {
    gtk_widget_hide (self->user_id_label);
    gtk_widget_hide (self->name_label);
    gtk_widget_show (self->room_name);
    gtk_label_set_text (GTK_LABEL (self->room_name),
                        chatty_item_get_name (CHATTY_ITEM (self->chat)));
  }

  gtk_list_box_bind_model (GTK_LIST_BOX (self->key_list),
                           NULL, NULL, NULL, NULL);

  if (protocol == CHATTY_PROTOCOL_XMPP &&
      chatty_chat_is_im (self->chat)) {
    gtk_widget_show (self->encryption_label);
    gtk_widget_show (self->status_label);
    chatty_pp_chat_load_fp_list_async (CHATTY_PP_CHAT (self->chat),
                                       pp_chat_info_list_fp_cb, self);
  } else {
    gtk_widget_hide (self->encryption_label);
    gtk_widget_hide (self->status_label);
  }

  gtk_widget_set_visible (self->notification_switch, CHATTY_IS_PP_CHAT (self->chat));
  if (CHATTY_IS_PP_CHAT (self->chat))
    gtk_switch_set_state (GTK_SWITCH (self->notification_switch),
                          chatty_pp_chat_get_show_notifications (CHATTY_PP_CHAT (self->chat)));

  if ((protocol == CHATTY_PROTOCOL_XMPP ||
       protocol == CHATTY_PROTOCOL_TELEGRAM) &&
      !chatty_chat_is_im (self->chat)) {
    gtk_widget_show (self->show_status_switch);
  } else {
    gtk_widget_hide (self->show_status_switch);
  }

  if (protocol == CHATTY_PROTOCOL_XMPP &&
      !chatty_chat_is_im (self->chat)) {
    gtk_widget_show (self->room_topic_view);
    gtk_widget_show (self->user_list);
    chatty_pp_chat_info_list_users (self);
    gtk_switch_set_state (GTK_SWITCH (self->show_status_switch),
                          chatty_pp_chat_get_show_status_msg (CHATTY_PP_CHAT (self->chat)));
    gtk_text_buffer_set_text (self->topic_buffer,
                              chatty_chat_get_topic (self->chat), -1);
  } else {
    gtk_widget_hide (self->user_list);
    gtk_widget_hide (self->show_status_switch);
    gtk_widget_hide (self->room_topic_view);
  }
}

static void
chatty_pp_chat_info_set_item (ChattyChatInfo *info,
                              ChattyChat     *chat)
{
  ChattyPpChatInfo *self = (ChattyPpChatInfo *)info;

  g_assert (CHATTY_IS_PP_CHAT_INFO (self));
  g_assert (!chat || CHATTY_IS_CHAT (chat));

  if (self->chat && self->chat != chat) {
    g_clear_object (&self->binding);
    g_clear_signal_handler (&self->avatar_changed_id, self->chat);
    g_signal_handlers_disconnect_by_func (self->chat,
                                          pp_chat_info_encrypt_changed_cb,
                                          self);
  }

  if (!g_set_object (&self->chat, chat) || !chat)
    return;

  self->avatar_changed_id = g_signal_connect_object (self->chat, "avatar-changed",
                                                     G_CALLBACK (pp_info_avatar_changed_cb),
                                                     self, G_CONNECT_SWAPPED);
  pp_info_avatar_changed_cb (self);
  gtk_text_buffer_set_text (self->topic_buffer, "", 0);
  gtk_widget_hide (self->key_list);

  gtk_container_foreach (GTK_CONTAINER (self->key_list),
                         (GtkCallback)gtk_widget_destroy, NULL);

  chatty_pp_chat_info_update (self);
}

static void
chatty_pp_chat_info_finalize (GObject *object)
{
  ChattyPpChatInfo *self = (ChattyPpChatInfo *)object;

  g_clear_object (&self->chat);

  G_OBJECT_CLASS (chatty_pp_chat_info_parent_class)->finalize (object);
}

static void
chatty_pp_chat_info_class_init (ChattyPpChatInfoClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  ChattyChatInfoClass *info_class = CHATTY_CHAT_INFO_CLASS (klass);

  object_class->finalize = chatty_pp_chat_info_finalize;

  info_class->set_item = chatty_pp_chat_info_set_item;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-pp-chat-info.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, room_name);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, avatar);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, edit_avatar_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, delete_avatar_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, room_topic_view);

  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, name_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, user_id_title);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, user_id_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, encryption_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, status_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, settings_label);

  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, notification_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, show_status_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, encryption_switch);

  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, key_list);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, user_list_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, user_list);

  gtk_widget_class_bind_template_child (widget_class, ChattyPpChatInfo, topic_buffer);

  gtk_widget_class_bind_template_callback (widget_class, pp_info_edit_avatar_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, pp_info_delete_avatar_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, pp_chat_info_edit_topic_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, pp_chat_info_notification_switch_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, pp_chat_info_show_status_switch_changed_cb);
}

static void
chatty_pp_chat_info_init (ChattyPpChatInfo *self)
{
  GtkWidget *clamp;

  gtk_widget_init_template (GTK_WIDGET (self));

  clamp = gtk_widget_get_ancestor (self->avatar, HDY_TYPE_CLAMP);

  if (clamp) {
    hdy_clamp_set_maximum_size (HDY_CLAMP (clamp), 360);
    hdy_clamp_set_tightening_threshold (HDY_CLAMP (clamp), 320);
  }
}

GtkWidget *
chatty_pp_chat_info_new (void)
{
  return g_object_new (CHATTY_TYPE_PP_CHAT_INFO, NULL);
}
