/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-selectable-row.h
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "chatty-item.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_SELECTABLE_ROW (chatty_selectable_row_get_type ())

G_DECLARE_FINAL_TYPE (ChattySelectableRow, chatty_selectable_row, CHATTY, SELECTABLE_ROW, GtkListBoxRow)

GtkWidget  *chatty_selectable_row_new          (const char          *title);
gboolean    chatty_selectable_row_get_selected (ChattySelectableRow *self);
void        chatty_selectable_row_set_selected (ChattySelectableRow *self,
                                                gboolean             is_selected);
const char *chatty_selectable_row_get_title    (ChattySelectableRow *self);


G_END_DECLS
