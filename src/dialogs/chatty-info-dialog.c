/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-info-dialog.c
 *
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#define G_LOG_DOMAIN "chatty-info-dialog"

#include <glib/gi18n.h>

#include "chatty-config.h"
#include "chatty-avatar.h"
#include "chatty-list-row.h"
#include "chatty-fp-row.h"
#include "chatty-pp-chat.h"
#include "chatty-manager.h"
#include "chatty-utils.h"
#include "chatty-info-dialog.h"

struct _ChattyInfoDialog
{
  GtkDialog       parent_instance;

  GtkWidget      *main_stack;
  GtkWidget      *main_page;
  GtkWidget      *invite_page;

  GtkWidget      *new_invite_button;
  GtkWidget      *invite_button;

  GtkWidget      *topic_text_view;
  GtkTextBuffer  *topic_buffer;

  GtkWidget      *room_name;
  GtkWidget      *avatar_button;
  GtkWidget      *avatar;

  GtkWidget      *name_label;
  GtkWidget      *user_id_title;
  GtkWidget      *user_id_label;
  GtkWidget      *encryption_label;
  GtkWidget      *status_label;

  GtkWidget      *settings_label;
  GtkWidget      *notification_switch;
  GtkWidget      *show_status_switch;
  GtkWidget      *encryption_switch;
  GtkWidget      *key_list;

  GtkWidget      *user_list_label;
  GtkWidget      *user_list;

  GtkWidget      *contact_id_entry;
  GtkWidget      *message_entry;

  GtkWidget      *avatar_chooser_dialog;

  ChattyChat     *chat;
  GBinding       *binding;
};

G_DEFINE_TYPE (ChattyInfoDialog, chatty_info_dialog, GTK_TYPE_DIALOG)

static void
chatty_info_dialog_list_users (ChattyInfoDialog *self)
{
  GListModel *user_list;
  g_autofree char *count_str = NULL;
  guint n_items;

  g_assert (CHATTY_IS_INFO_DIALOG (self));

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
info_dialog_encrypt_changed_cb (ChattyInfoDialog *self)
{
  const char *status_msg = "";
  ChattyEncryption encryption;
  gboolean has_encryption;

  g_assert (CHATTY_IS_INFO_DIALOG (self));

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
info_dialog_list_fp_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  ChattyInfoDialog *self = user_data;
  ChattyPpChat *chat;

  g_assert (CHATTY_IS_INFO_DIALOG (self));

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
chatty_info_dialog_update (ChattyInfoDialog *self)
{
  ChattyProtocol protocol;

  g_assert (CHATTY_IS_INFO_DIALOG (self));

  if (!self->chat) {

    return;
  }

  protocol = chatty_item_get_protocols (CHATTY_ITEM (self->chat));

  chatty_avatar_set_item (CHATTY_AVATAR (self->avatar), CHATTY_ITEM (self->chat));

  self->binding = g_object_bind_property (self->chat, "encrypt",
                                          self->encryption_switch, "active",
                                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  g_signal_connect_object (self->chat, "notify::encrypt",
                           G_CALLBACK (info_dialog_encrypt_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  info_dialog_encrypt_changed_cb (self);

  if (protocol == CHATTY_PROTOCOL_SMS) {
    gtk_label_set_text (GTK_LABEL (self->user_id_title), _("Phone Number:"));
  } else if (protocol == CHATTY_PROTOCOL_XMPP) {
    gtk_label_set_text (GTK_LABEL (self->user_id_title), _("XMPP ID:"));
    gtk_widget_show (GTK_WIDGET (self->status_label));
    gtk_label_set_text (GTK_LABEL (self->status_label),
                        chatty_pp_chat_get_status (CHATTY_PP_CHAT (self->chat)));
  } else if (protocol == CHATTY_PROTOCOL_MATRIX) {
    gtk_label_set_text (GTK_LABEL (self->user_id_title), _("Matrix ID:"));
    gtk_widget_set_sensitive (self->avatar_button, FALSE);
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
    gtk_widget_show (self->avatar_button);
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
                                       info_dialog_list_fp_cb, self);
  } else {
    gtk_widget_hide (self->encryption_label);
    gtk_widget_hide (self->status_label);
  }

  gtk_widget_set_visible (self->notification_switch,
                          protocol != CHATTY_PROTOCOL_MATRIX);
  if (protocol != CHATTY_PROTOCOL_MATRIX)
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
    gtk_widget_show (self->new_invite_button);
    gtk_widget_show (self->topic_text_view);
    gtk_widget_show (self->user_list);
    chatty_info_dialog_list_users (self);
    gtk_switch_set_state (GTK_SWITCH (self->show_status_switch),
                          chatty_pp_chat_get_show_status_msg (CHATTY_PP_CHAT (self->chat)));
    gtk_text_buffer_set_text (self->topic_buffer,
                              chatty_chat_get_topic (self->chat), -1);
  } else {
    gtk_widget_hide (self->user_list);
    gtk_widget_hide (self->new_invite_button);
    gtk_widget_hide (self->show_status_switch);
    gtk_widget_hide (self->topic_text_view);
  }
}

static void
info_dialog_new_invite_clicked_cb (ChattyInfoDialog *self)
{
  g_assert (CHATTY_IS_INFO_DIALOG (self));

  gtk_widget_hide (self->new_invite_button);
  gtk_widget_show (self->invite_button);
  gtk_stack_set_visible_child (GTK_STACK (self->main_stack),
                               self->invite_page);
  gtk_widget_grab_focus (self->contact_id_entry);
}

static void
info_dialog_cancel_clicked_cb (ChattyInfoDialog *self)
{
  g_assert (CHATTY_IS_INFO_DIALOG (self));

  gtk_widget_hide (self->invite_button);
  gtk_widget_show (self->new_invite_button);
  gtk_stack_set_visible_child (GTK_STACK (self->main_stack),
                               self->main_page);
}

static void
chat_invite_cb (GObject      *object,
                GAsyncResult *result,
                gpointer      user_data)
{
  g_autoptr(ChattyInfoDialog) self = user_data;

  g_assert (CHATTY_IS_INFO_DIALOG (self));
}

static void
info_dialog_invite_clicked_cb (ChattyInfoDialog *self)
{
  const char *name, *invite_message;

  g_assert (CHATTY_IS_INFO_DIALOG (self));
  g_return_if_fail (self->chat);

  name = gtk_entry_get_text (GTK_ENTRY (self->contact_id_entry));
  invite_message = gtk_entry_get_text (GTK_ENTRY (self->message_entry));

  if (name && *name)
    chatty_chat_invite_async (CHATTY_CHAT (self->chat), name, invite_message, NULL,
                              chat_invite_cb, g_object_ref (self));

  gtk_entry_set_text (GTK_ENTRY (self->contact_id_entry), "");
  gtk_entry_set_text (GTK_ENTRY (self->message_entry), "");
  info_dialog_cancel_clicked_cb (self);
}

static void
info_dialog_avatar_button_clicked_cb (ChattyInfoDialog *self)
{
  GtkDialog *dialog;
  g_autofree char *file_name = NULL;
  int response;

  g_assert (CHATTY_IS_INFO_DIALOG (self));

  dialog = GTK_DIALOG (self->avatar_chooser_dialog);
  response = gtk_dialog_run (dialog);
  gtk_widget_hide (GTK_WIDGET (dialog));

  if (response == GTK_RESPONSE_APPLY)
    file_name = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

  if (file_name)
    chatty_item_set_avatar_async (CHATTY_ITEM (self->chat), file_name,
                                  NULL, NULL, NULL);
}

static void
show_status_switch_changed_cb (ChattyInfoDialog *self)
{
  gboolean active;

  g_assert (CHATTY_IS_INFO_DIALOG (self));
  g_return_if_fail (CHATTY_IS_PP_CHAT (self->chat));

  active = gtk_switch_get_active (GTK_SWITCH (self->show_status_switch));
  chatty_pp_chat_set_show_status_msg (CHATTY_PP_CHAT (self->chat), active);
}

static void
info_dialog_edit_topic_clicked_cb (ChattyInfoDialog *self,
                                   GtkToggleButton  *edit_button)
{
  gboolean editable;

  g_assert (CHATTY_IS_INFO_DIALOG (self));
  g_assert (GTK_IS_TOGGLE_BUTTON (edit_button));

  editable = gtk_toggle_button_get_active (edit_button);
  gtk_text_view_set_editable (GTK_TEXT_VIEW (self->topic_text_view), editable);
  gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (self->topic_text_view), editable);
  gtk_widget_set_can_focus (self->topic_text_view, editable);

  if (editable) {
    gtk_widget_grab_focus (self->topic_text_view);
  } else if (gtk_text_buffer_get_modified (self->topic_buffer)) {
    g_autofree char *text = NULL;

    g_object_get (self->topic_buffer, "text", &text, NULL);
    chatty_chat_set_topic (CHATTY_CHAT (self->chat), text);
  }

  gtk_text_buffer_set_modified (self->topic_buffer, FALSE);
}

static void
notification_switch_changed_cb (ChattyInfoDialog *self)
{
  gboolean active;

  g_assert (CHATTY_IS_INFO_DIALOG (self));

  active = gtk_switch_get_active (GTK_SWITCH (self->notification_switch));
  chatty_pp_chat_set_show_notifications (CHATTY_PP_CHAT (self->chat), active);
}

static void
info_dialog_contact_id_changed_cb (ChattyInfoDialog *self,
                                   GtkEntry         *entry)
{
  const char *username;
  ChattyProtocol protocol, item_protocol;

  g_assert (CHATTY_IS_INFO_DIALOG (self));
  g_assert (GTK_IS_ENTRY (entry));
  g_return_if_fail (self->chat);

  item_protocol = chatty_item_get_protocols (CHATTY_ITEM (self->chat));
  username = gtk_entry_get_text (entry);

  protocol = chatty_utils_username_is_valid (username, item_protocol);
  gtk_widget_set_sensitive (self->invite_button, protocol == item_protocol);
}

static void
chatty_info_dialog_finalize (GObject *object)
{
  ChattyInfoDialog *self = (ChattyInfoDialog *)object;

  g_clear_object (&self->chat);

  G_OBJECT_CLASS (chatty_info_dialog_parent_class)->finalize (object);
}

static void
chatty_info_dialog_class_init (ChattyInfoDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = chatty_info_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-info-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, main_stack);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, main_page);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, invite_page);

  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, topic_text_view);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, topic_buffer);

  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, new_invite_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, invite_button);

  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, room_name);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, avatar_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, avatar);

  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, name_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, user_id_title);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, user_id_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, encryption_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, status_label);

  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, settings_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, notification_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, show_status_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, encryption_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, key_list);

  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, user_list_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, user_list);

  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, contact_id_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, message_entry);

  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, avatar_chooser_dialog);

  gtk_widget_class_bind_template_callback (widget_class, info_dialog_new_invite_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, info_dialog_cancel_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, info_dialog_invite_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, info_dialog_avatar_button_clicked_cb);

  gtk_widget_class_bind_template_callback (widget_class, show_status_switch_changed_cb);

  gtk_widget_class_bind_template_callback (widget_class, info_dialog_edit_topic_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, notification_switch_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, info_dialog_contact_id_changed_cb);
}

static void
chatty_info_dialog_init (ChattyInfoDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET(self));
}

GtkWidget *
chatty_info_dialog_new (GtkWindow *parent_window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (parent_window), NULL);

  return g_object_new (CHATTY_TYPE_INFO_DIALOG,
                       "transient-for", parent_window,
                       "use-header-bar", 1,
                       NULL);
}

void
chatty_info_dialog_set_chat (ChattyInfoDialog *self,
                             ChattyChat       *chat)
{
  g_return_if_fail (CHATTY_IS_INFO_DIALOG (self));
  g_return_if_fail (!chat || CHATTY_IS_CHAT (chat));

  if (self->chat == chat)
    return;

  gtk_entry_set_text (GTK_ENTRY (self->contact_id_entry), "");
  gtk_entry_set_text (GTK_ENTRY (self->message_entry), "");
  gtk_text_buffer_set_text (self->topic_buffer, "", 0);
  gtk_widget_hide (self->key_list);

  g_clear_object (&self->binding);
  gtk_container_foreach (GTK_CONTAINER (self->key_list),
                         (GtkCallback)gtk_widget_destroy, NULL);

  if (self->chat) {
    g_signal_handlers_disconnect_by_func (self->chat,
                                          info_dialog_encrypt_changed_cb,
                                          self);
  }

  g_set_object (&self->chat, chat);
  chatty_info_dialog_update (self);
}
