/*
 * Copyright (C) 2022 Purism SPC
 *
 * Authors:
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "chatty-enums.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_CONTACT_LIST (chatty_contact_list_get_type ())

G_DECLARE_FINAL_TYPE (ChattyContactList, chatty_contact_list, CHATTY, CONTACT_LIST, GtkBox)

GtkWidget  *chatty_contact_list_new                 (void);
void        chatty_contact_list_set_selection_store (ChattyContactList *self,
                                                     GListStore        *list_store);
void        chatty_contact_list_show_selected_only  (ChattyContactList *self);
void        chatty_contact_list_can_multi_select    (ChattyContactList *self,
                                                     gboolean           can_multi_select);
void        chatty_contact_list_set_filter          (ChattyContactList *self,
                                                     ChattyProtocol     protocol,
                                                     const char        *needle);

G_END_DECLS
