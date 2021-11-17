/*
 * Copyright (C) 2021 Purism SPC
 *               2021 Chris Talbot
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#define G_LOG_DOMAIN "chatty-mm-chat-info"

#define _GNU_SOURCE
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include "contrib/gtk.h"

#include "chatty-avatar.h"
#include "chatty-chat.h"
#include "chatty-contact.h"
#include "chatty-mm-buddy.h"
#include "chatty-list-row.h"
#include "dialogs/chatty-mm-chat-info.h"

struct _ChattyMmChatInfo
{
  ChattyChatInfo parent_instance;

  GtkWidget *contacts_list_box;
  GtkWidget *title_group;
  GtkWidget *title_entry;
  GtkWidget *avatar;

  ChattyChat *chat;
};

G_DEFINE_TYPE (ChattyMmChatInfo, chatty_mm_chat_info, CHATTY_TYPE_CHAT_INFO)

void
chatty_mm_chat_info_cancel_changes (ChattyMmChatInfo *self,
                                    ChattyChat       *chat)
{
  g_return_if_fail (CHATTY_IS_MM_CHAT_INFO (self));
  g_return_if_fail (!chat || CHATTY_IS_CHAT (chat));

  gtk_entry_set_text (GTK_ENTRY (self->title_entry),
                      chatty_item_get_name (CHATTY_ITEM (chat)));
}

void
chatty_mm_chat_info_apply_changes (ChattyMmChatInfo *self,
                                   ChattyChat       *chat)
{
  const char *name;
  g_return_if_fail (CHATTY_IS_MM_CHAT_INFO (self));
  g_return_if_fail (!chat || CHATTY_IS_CHAT (chat));

  name = gtk_entry_get_text (GTK_ENTRY (self->title_entry));
  if (g_strcmp0 (name, chatty_item_get_name (CHATTY_ITEM (chat))) == 0)
    return;

  chatty_item_set_name (CHATTY_ITEM (chat), name);

  if (!*name)
    gtk_entry_set_text (GTK_ENTRY (self->title_entry),
                        chatty_item_get_name (CHATTY_ITEM (chat)));
}

static void
chatty_mm_chat_info_set_item (ChattyChatInfo *info,
                              ChattyChat     *chat)
{
  ChattyMmChatInfo *self = (ChattyMmChatInfo *)info;
  g_autoptr (ChattyContact) self_contact;
  GListModel *users;
  GtkWidget *contact_row;
  guint n_items = 0;

  g_return_if_fail (CHATTY_IS_MM_CHAT_INFO (self));
  g_return_if_fail (!chat || CHATTY_IS_CHAT (chat));

  gtk_container_foreach (GTK_CONTAINER (self->contacts_list_box),
                         (GtkCallback)gtk_widget_destroy, NULL);

  chatty_avatar_set_item (CHATTY_AVATAR (self->avatar), CHATTY_ITEM (chat));

  if (chatty_chat_is_im (chat)) {
    gtk_widget_hide (GTK_WIDGET (self->title_group));
    gtk_entry_set_text (GTK_ENTRY (self->title_entry), "");
  } else {
    gtk_widget_show (GTK_WIDGET (self->title_group));
    gtk_entry_set_text (GTK_ENTRY (self->title_entry),
                        chatty_item_get_name (CHATTY_ITEM (chat)));
  }

  users = chatty_chat_get_users (chat);
  n_items = g_list_model_get_n_items (users);

  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyMmBuddy) buddy = NULL;
    ChattyContact *contact;

    buddy = g_list_model_get_item (users, i);
    contact = chatty_mm_buddy_get_contact (buddy);
    if (contact) {
     contact_row = chatty_list_row_new (CHATTY_ITEM (contact));
     gtk_list_box_prepend (GTK_LIST_BOX (self->contacts_list_box),
                           GTK_WIDGET (contact_row));
    } else {
      g_autoptr (ChattyContact) new_contact;
      const char *phone;

      phone = chatty_mm_buddy_get_number (buddy);

      new_contact = chatty_contact_new (NULL, NULL, CHATTY_PROTOCOL_MMS_SMS);
      chatty_contact_set_name (new_contact, _("Unknown Contact"));
      chatty_contact_set_value (new_contact, phone);

      contact_row = chatty_list_row_new (CHATTY_ITEM (new_contact));
      chatty_list_row_set_contact ((ChattyListRow *)contact_row, TRUE);

      /* We validate all multi-user chat users on creation. If there is
       * only one contact, it's possible that it's not a phone number,
       * check and disable add contact button if that's the case */
      if (n_items == 1 &&
          !chatty_utils_username_is_valid (phone, CHATTY_PROTOCOL_MMS_SMS))
        chatty_list_row_set_contact ((ChattyListRow *)contact_row, FALSE);

      gtk_list_box_prepend (GTK_LIST_BOX (self->contacts_list_box),
                             GTK_WIDGET (contact_row));
    }
  }
  self_contact = chatty_contact_new (NULL, NULL, CHATTY_PROTOCOL_MMS_SMS);
  chatty_contact_set_name (self_contact, C_("Refer to self in contact list", "You"));

  contact_row = chatty_list_row_new (CHATTY_ITEM (self_contact));
  gtk_list_box_prepend (GTK_LIST_BOX (self->contacts_list_box),
                         GTK_WIDGET (contact_row));
}

static void
chatty_mm_chat_info_class_init (ChattyMmChatInfoClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  ChattyChatInfoClass *info_class = CHATTY_CHAT_INFO_CLASS (klass);

  info_class->set_item = chatty_mm_chat_info_set_item;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-mm-chat-info.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyMmChatInfo, avatar);
  gtk_widget_class_bind_template_child (widget_class, ChattyMmChatInfo, contacts_list_box);
  gtk_widget_class_bind_template_child (widget_class, ChattyMmChatInfo, title_group);
  gtk_widget_class_bind_template_child (widget_class, ChattyMmChatInfo, title_entry);
}

static void
chatty_mm_chat_info_init (ChattyMmChatInfo *self)
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
chatty_mm_chat_info_new (GtkWindow *parent_window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (parent_window), NULL);

  return g_object_new (CHATTY_TYPE_MM_CHAT_INFO, NULL);
}
