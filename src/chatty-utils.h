/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#ifndef __UTILS_H_INCLUDE__
#define __UTILS_H_INCLUDE__

#include <stdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "users/chatty-pp-account.h"

#define MAX_GMT_ISO_SIZE 256
#define SECONDS_PER_DAY    86400.0

char *chatty_utils_jabber_id_strip (const char *name);
char *chatty_utils_strip_blanks (const char *string);
char *chatty_utils_check_phonenumber (const char *phone_number);
gboolean chatty_utils_get_item_position (GListModel *list,
                                         gpointer    item,
                                         guint      *position);
gboolean chatty_utils_remove_list_item  (GListStore *store,
                                         gpointer    item);
GtkWidget* chatty_utils_create_fingerprint_row (const char *fp, guint id);
gpointer   chatty_utils_get_node_object (PurpleBlistNode *node);
const char *chatty_utils_get_color_for_str (const char *str);
char       *chatty_utils_get_human_time (time_t unix_time);
PurpleBlistNode *chatty_utils_get_conv_blist_node (PurpleConversation *conv);

#endif
