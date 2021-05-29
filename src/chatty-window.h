/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#pragma once


#include <gtk/gtk.h>

#include "chatty-chat.h"
#include "chatty-settings.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_WINDOW (chatty_window_get_type())

G_DECLARE_FINAL_TYPE (ChattyWindow, chatty_window, CHATTY, WINDOW, GtkApplicationWindow)

typedef enum {
  CHATTY_VIEW_CHAT_LIST,
  CHATTY_VIEW_MESSAGE_LIST,
} ChattyWindowState;


GtkWidget *chatty_window_new     (GtkApplication *application);
void       chatty_window_set_uri (ChattyWindow *self,
                                 const char   *uri);

void chatty_window_change_view (ChattyWindow *self, guint state);

void chatty_window_chat_list_select_first (ChattyWindow *self);
ChattyChat *chatty_window_get_active_chat (ChattyWindow *self);
void        chatty_window_open_chat       (ChattyWindow *self,
                                           ChattyChat   *chat);


G_END_DECLS
