/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-attachments-view.h
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

#define CHATTY_TYPE_ATTACHMENTS_VIEW (chatty_attachments_view_get_type ())

G_DECLARE_FINAL_TYPE (ChattyAttachmentsView, chatty_attachments_view, CHATTY, ATTACHMENTS_VIEW, GtkBox)

GtkWidget *chatty_attachments_view_new       (void);
void       chatty_attachments_view_reset     (ChattyAttachmentsView *self);
void       chatty_attachments_view_add_file  (ChattyAttachmentsView *self,
                                              const char            *file_path);
GList     *chatty_attachments_view_get_files (ChattyAttachmentsView *self);

G_END_DECLS

