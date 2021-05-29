/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-image-item.h
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

#include "chatty-message.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_IMAGE_ITEM (chatty_image_item_get_type ())

G_DECLARE_FINAL_TYPE (ChattyImageItem, chatty_image_item, CHATTY, IMAGE_ITEM, GtkBin)

GtkWidget       *chatty_image_item_new        (ChattyMessage  *message,
                                               ChattyProtocol  protocol);
GtkStyleContext *chatty_image_item_get_style  (ChattyImageItem *self);
ChattyMessage   *chatty_image_item_get_item   (ChattyImageItem *self);

G_END_DECLS
