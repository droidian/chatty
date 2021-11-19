/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-attachments-view.c
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-attachments-view"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "chatty-utils.h"
#include "chatty-file-item.h"
#include "chatty-attachments-view.h"

struct _ChattyAttachmentsView
{
  GtkBox parent_instance;

  GtkWidget *scrolled_window;
  GtkWidget *files_box;
};

G_DEFINE_TYPE (ChattyAttachmentsView, chatty_attachments_view, GTK_TYPE_BOX)

static void
attachments_view_item_removed_cb (ChattyAttachmentsView *self)
{
  g_autoptr(GList) children = NULL;
  g_assert (CHATTY_IS_ATTACHMENTS_VIEW (self));

  if (gtk_widget_in_destruction (GTK_WIDGET (self)))
    return;

  children = gtk_container_get_children (GTK_CONTAINER (self->files_box));

  if (!children || (children && !children->data)) {
    GtkWidget *parent;

    parent = gtk_widget_get_parent (GTK_WIDGET (self));

    if (GTK_IS_REVEALER (parent))
      gtk_revealer_set_reveal_child (GTK_REVEALER (parent), FALSE);
    else
      gtk_widget_hide (parent);
  }
}

static void
chatty_attachments_view_class_init (ChattyAttachmentsViewClass *klass)
{
}

static void
chatty_attachments_view_init (ChattyAttachmentsView *self)
{
  GtkStyleContext *st;

  self->scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_set_size_request (self->scrolled_window, -1, 194);
  gtk_widget_set_hexpand (self->scrolled_window, TRUE);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scrolled_window),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);

  self->files_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_add (GTK_CONTAINER (self->scrolled_window), self->files_box);
  gtk_container_add (GTK_CONTAINER (self), self->scrolled_window);
  gtk_widget_show_all (GTK_WIDGET (self));

  st = gtk_widget_get_style_context (self->scrolled_window);
  gtk_style_context_add_class (st, "content");
  gtk_style_context_add_class (st, "view");
  gtk_style_context_add_class (st, "frame");

  g_signal_connect_object (self->files_box, "remove",
                           G_CALLBACK (attachments_view_item_removed_cb),
                           self, G_CONNECT_SWAPPED | G_CONNECT_AFTER);
}

GtkWidget *
chatty_attachments_view_new (void)
{
  return g_object_new (CHATTY_TYPE_ATTACHMENTS_VIEW, NULL);
}

void
chatty_attachments_view_reset (ChattyAttachmentsView *self)
{
  g_return_if_fail (CHATTY_IS_ATTACHMENTS_VIEW (self));

  gtk_container_foreach (GTK_CONTAINER (self->files_box),
                         (GtkCallback)gtk_widget_destroy, NULL);
}

void
chatty_attachments_view_add_file (ChattyAttachmentsView *self,
                                  const char            *file_path)
{
  GtkWidget *child;

  g_return_if_fail (CHATTY_IS_ATTACHMENTS_VIEW (self));
  g_return_if_fail (file_path && *file_path);

  child = chatty_file_item_new (file_path);
  gtk_widget_show_all (child);
  gtk_container_add (GTK_CONTAINER (self->files_box), child);
}

/**
 * chatty_attachments_view_get_files:
 * @self: A #ChattyAttachmentsView
 *
 * Get the list of files attached. The list contains
 * ChattyFileInfo and the list should be freed with
 * g_list_free_full(list, (GDestroyNotify)chatty_file_info_free)
 *
 * Returns: (transfer full) (nullable): A List of strings.
 */
GList *
chatty_attachments_view_get_files (ChattyAttachmentsView *self)
{
  g_autoptr(GList) children = NULL;
  GList *files = NULL;

  g_return_val_if_fail (CHATTY_IS_ATTACHMENTS_VIEW (self), NULL);

  children = gtk_container_get_children (GTK_CONTAINER (self->files_box));

  for (GList *child = children; child; child = child->next) {
    ChattyFileInfo *attachment;
    const char *name;

    name = chatty_file_item_get_file (child->data);
    attachment = chatty_file_info_new_for_path (name);
    files = g_list_append (files, attachment);
  }

  return files;
}
