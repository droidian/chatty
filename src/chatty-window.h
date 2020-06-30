/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#pragma once


#include <gtk/gtk.h>
#include "chatty-settings.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_WINDOW (chatty_window_get_type())

G_DECLARE_FINAL_TYPE (ChattyWindow, chatty_window, CHATTY, WINDOW, GtkApplicationWindow)

#define CHATTY_COLOR_GREEN     "6BBA3D"
#define CHATTY_COLOR_BLUE      "4A8FD9"
#define CHATTY_COLOR_PURPLE    "842B84"
#define CHATTY_COLOR_GREY      "B2B2B2"
#define CHATTY_COLOR_DIM_GREY  "797979"
#define CHATTY_COLOR_DARK_GREY "323232"

typedef enum {
  CHATTY_OVERLAY_EMPTY_CHAT,
  CHATTY_OVERLAY_EMPTY_CHAT_NO_SMS,
  CHATTY_OVERLAY_EMPTY_CHAT_NO_IM,
  CHATTY_OVERLAY_EMPTY_CHAT_NO_SMS_IM,
} ChattyOverlayMode;


typedef enum {
  CHATTY_VIEW_CHAT_LIST,
  CHATTY_VIEW_MESSAGE_LIST,
  CHATTY_VIEW_SETTINGS,
} ChattyWindowState;


GtkWidget *chatty_window_new     (GtkApplication *application);
void       chatty_window_set_uri (ChattyWindow *self,
                                 const char   *uri);

void chatty_window_change_view (ChattyWindow *self, guint state);

GtkWidget *chatty_window_get_convs_notebook (ChattyWindow *self);

void chatty_window_chat_list_select_first (ChattyWindow *self);
void chatty_window_set_header_chat_info_button_visible (ChattyWindow *self, gboolean visible);


G_END_DECLS
