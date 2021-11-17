/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-file-item.h
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

G_BEGIN_DECLS

#define CHATTY_TYPE_FILE_ITEM (chatty_file_item_get_type ())

G_DECLARE_FINAL_TYPE (ChattyFileItem, chatty_file_item, CHATTY, FILE_ITEM, GtkBox)

GtkWidget  *chatty_file_item_new       (const char *file_name);
const char *chatty_file_item_get_file  (ChattyFileItem *self);

G_END_DECLS
