/* -*- mode: c; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* chatty-ma-chat-info.c
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

#define G_LOG_DOMAIN "chatty-ma-chat-info"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>

#include "chatty-avatar.h"
#include "chatty-list-row.h"
#include "chatty-fp-row.h"
#include "matrix/chatty-ma-chat.h"
#include "chatty-ma-chat-info.h"
#include "chatty-log.h"

struct _ChattyMaChatInfo
{
  HdyPreferencesPage parent_instance;

  ChattyChat    *chat;

  GtkWidget     *avatar_button;
  GtkWidget     *avatar;

  GtkWidget     *name_label;
  GtkWidget     *matrix_id_label;

  GtkWidget     *avatar_chooser_dialog;
};

G_DEFINE_TYPE (ChattyMaChatInfo, chatty_ma_chat_info, HDY_TYPE_PREFERENCES_PAGE)

static void
chatty_ma_chat_info_finalize (GObject *object)
{
  ChattyMaChatInfo *self = (ChattyMaChatInfo *)object;

  g_clear_object (&self->chat);

  G_OBJECT_CLASS (chatty_ma_chat_info_parent_class)->finalize (object);
}

static void
chatty_ma_chat_info_class_init (ChattyMaChatInfoClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = chatty_ma_chat_info_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-ma-chat-info.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyMaChatInfo, avatar_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyMaChatInfo, avatar);

  gtk_widget_class_bind_template_child (widget_class, ChattyMaChatInfo, name_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyMaChatInfo, matrix_id_label);

  gtk_widget_class_bind_template_child (widget_class, ChattyMaChatInfo, avatar_chooser_dialog);
}

static void
chatty_ma_chat_info_init (ChattyMaChatInfo *self)
{
  GtkWidget *clamp, *window;

  gtk_widget_init_template (GTK_WIDGET (self));

  clamp = gtk_widget_get_ancestor (self->avatar, HDY_TYPE_CLAMP);

  if (clamp) {
    hdy_clamp_set_maximum_size (HDY_CLAMP (clamp), 360);
    hdy_clamp_set_tightening_threshold (HDY_CLAMP (clamp), 320);
  }

  window = gtk_widget_get_ancestor (self->avatar, GTK_TYPE_DIALOG);

  if (window)
    gtk_window_set_transient_for (GTK_WINDOW (self->avatar_chooser_dialog),
                                  GTK_WINDOW (window));
}

GtkWidget *
chatty_ma_chat_info_new (void)
{
  return g_object_new (CHATTY_TYPE_MA_CHAT_INFO, NULL);
}

ChattyChat *
chatty_ma_chat_info_get_item (ChattyMaChatInfo *self)
{
  g_return_val_if_fail (CHATTY_IS_MA_CHAT_INFO (self), NULL);

  return self->chat;
}

void
chatty_ma_chat_info_set_item (ChattyMaChatInfo *self,
                              ChattyChat       *chat)
{
  g_return_if_fail (CHATTY_IS_MA_CHAT_INFO (self));
  g_return_if_fail (!chat || CHATTY_IS_CHAT (chat));

  if (!g_set_object (&self->chat, chat) || !chat)
    return;

  gtk_label_set_text (GTK_LABEL (self->matrix_id_label),
                      chatty_chat_get_chat_name (self->chat));
  gtk_label_set_text (GTK_LABEL (self->name_label),
                      chatty_item_get_name (CHATTY_ITEM (self->chat)));
}
