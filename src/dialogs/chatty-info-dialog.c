/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-info-dialog.c
 *
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#define G_LOG_DOMAIN "chatty-info-dialog"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>

#include "chatty-avatar.h"
#include "chatty-list-row.h"
#include "chatty-fp-row.h"
#include "chatty-pp-chat.h"
#include "matrix/chatty-ma-chat.h"
#include "chatty-manager.h"
#include "chatty-utils.h"
#include "chatty-ma-chat-info.h"
#include "chatty-pp-chat-info.h"
#include "chatty-info-dialog.h"

struct _ChattyInfoDialog
{
  GtkDialog       parent_instance;

  GtkWidget      *main_stack;
  GtkWidget      *chat_type_stack;
  GtkWidget      *ma_chat_info;
  GtkWidget      *pp_chat_info;
  GtkWidget      *invite_page;

  GtkWidget      *new_invite_button;
  GtkWidget      *invite_button;

  GtkWidget      *contact_id_entry;
  GtkWidget      *message_entry;

  ChattyChat     *chat;
};

G_DEFINE_TYPE (ChattyInfoDialog, chatty_info_dialog, GTK_TYPE_DIALOG)

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
                               self->chat_type_stack);
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
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, chat_type_stack);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, ma_chat_info);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, pp_chat_info);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, invite_page);

  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, new_invite_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, invite_button);

  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, contact_id_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattyInfoDialog, message_entry);

  gtk_widget_class_bind_template_callback (widget_class, info_dialog_new_invite_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, info_dialog_cancel_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, info_dialog_invite_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, info_dialog_contact_id_changed_cb);

  g_type_ensure (CHATTY_TYPE_MA_CHAT_INFO);
  g_type_ensure (CHATTY_TYPE_PP_CHAT_INFO);
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

  gtk_entry_set_text (GTK_ENTRY (self->contact_id_entry), "");
  gtk_entry_set_text (GTK_ENTRY (self->message_entry), "");

  if (!g_set_object (&self->chat, chat))
    return;

  if (CHATTY_IS_MA_CHAT (chat)) {
    chatty_ma_chat_info_set_item (CHATTY_MA_CHAT_INFO (self->ma_chat_info), chat);
    chatty_pp_chat_info_set_item (CHATTY_PP_CHAT_INFO (self->pp_chat_info), NULL);
    gtk_stack_set_visible_child (GTK_STACK (self->chat_type_stack), self->ma_chat_info);
  } else {
    chatty_ma_chat_info_set_item (CHATTY_MA_CHAT_INFO (self->ma_chat_info), NULL);
    chatty_pp_chat_info_set_item (CHATTY_PP_CHAT_INFO (self->pp_chat_info), chat);
    gtk_stack_set_visible_child (GTK_STACK (self->chat_type_stack), self->pp_chat_info);
  }

  if (chatty_item_get_protocols (CHATTY_ITEM (chat)) == CHATTY_PROTOCOL_XMPP &&
      !chatty_chat_is_im (self->chat))
    gtk_widget_show (self->new_invite_button);
  else
    gtk_widget_hide (self->new_invite_button);
}
