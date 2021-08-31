/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-image-item.c
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-image-item"

#include <glib/gi18n.h>

#include "chatty-avatar.h"
#include "chatty-utils.h"
#include "chatty-chat-view.h"
#include "chatty-image-item.h"


#define MAX_GMT_ISO_SIZE 256

struct _ChattyImageItem
{
  GtkBin         parent_instance;

  GtkWidget     *image_overlay;
  GtkWidget     *overlay_stack;
  GtkWidget     *download_spinner;
  GtkWidget     *download_button;
  GtkWidget     *image;

  ChattyMessage *message;
  ChattyProtocol protocol;
};

G_DEFINE_TYPE (ChattyImageItem, chatty_image_item, GTK_TYPE_BIN)


static gboolean
item_set_image (gpointer user_data)
{
  ChattyImageItem *self = user_data;
  g_autoptr(GdkPixbuf) pixbuf = NULL;
  cairo_surface_t *surface = NULL;
  g_autofree char *path = NULL;
  ChattyFileInfo *file;
  GtkStyleContext *sc;
  GList *files;
  int scale_factor;

  files = chatty_message_get_files (self->message);
  g_return_val_if_fail (files && files->data, G_SOURCE_REMOVE);
  file = files->data;
  scale_factor = gtk_widget_get_scale_factor (GTK_WIDGET (self));
  sc = gtk_widget_get_style_context (self->image);

  if (file->path) {
    if (self->protocol == CHATTY_PROTOCOL_MMS_SMS || self->protocol == CHATTY_PROTOCOL_MMS)
      path = g_build_filename (g_get_user_data_dir (), "chatty", file->path, NULL);
    else
      path = g_build_filename (g_get_user_cache_dir (), "chatty", file->path, NULL);
  }

  if (path)
    pixbuf = gdk_pixbuf_new_from_file_at_scale (path, 240 * scale_factor,
                                                -1, TRUE, NULL);
  if (pixbuf)
    surface = gdk_cairo_surface_create_from_pixbuf (pixbuf, 0,
                                                    gtk_widget_get_window (GTK_WIDGET (self)));

  if (file->status == CHATTY_FILE_DOWNLOADED && pixbuf) {
    gtk_style_context_remove_class (sc, "dim-label");
    gtk_image_set_from_surface (GTK_IMAGE (self->image), surface);
  } else {
    gtk_style_context_add_class (sc, "dim-label");
    gtk_image_set_from_icon_name (GTK_IMAGE (self->image),
                                  "image-x-generic-symbolic",
                                  GTK_ICON_SIZE_BUTTON);
  }

  g_clear_pointer (&surface, cairo_surface_destroy);

  return G_SOURCE_REMOVE;
}

static void
image_item_update_message (ChattyImageItem *self)
{
  ChattyFileInfo *file;
  GtkStack *stack;
  GList *files;

  g_assert (CHATTY_IS_IMAGE_ITEM (self));
  g_assert (self->message);

  files = chatty_message_get_files (self->message);
  g_return_if_fail (files && files->data);

  /* XXX: Currently only first file is handled */
  file = files->data;
  stack = GTK_STACK (self->overlay_stack);

  g_object_set (self->download_spinner,
                "active", file->status == CHATTY_FILE_DOWNLOADING,
                NULL);

  if (file->status == CHATTY_FILE_UNKNOWN)
    gtk_stack_set_visible_child (stack, self->download_button);
  else if (file->status == CHATTY_FILE_DOWNLOADING)
    gtk_stack_set_visible_child (stack, self->download_spinner);
  else
    gtk_widget_hide (self->overlay_stack);

  /* Update in idle so that self is added to the parent container */
  if (file->status == CHATTY_FILE_DOWNLOADED)
    g_idle_add (item_set_image, self);
}

static void
chatty_image_download_clicked_cb (ChattyImageItem *self)
{
  GtkWidget *view;

  g_assert (CHATTY_IS_IMAGE_ITEM (self));

  view = gtk_widget_get_ancestor (GTK_WIDGET (self), CHATTY_TYPE_CHAT_VIEW);

  g_signal_emit_by_name (view, "file-requested", self->message);
}

static void
chatty_image_item_dispose (GObject *object)
{
  ChattyImageItem *self = (ChattyImageItem *)object;

  g_clear_object (&self->message);

  G_OBJECT_CLASS (chatty_image_item_parent_class)->dispose (object);
}

static void
chatty_image_item_class_init (ChattyImageItemClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = chatty_image_item_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-image-item.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyImageItem, image_overlay);
  gtk_widget_class_bind_template_child (widget_class, ChattyImageItem, overlay_stack);
  gtk_widget_class_bind_template_child (widget_class, ChattyImageItem, download_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyImageItem, download_spinner);
  gtk_widget_class_bind_template_child (widget_class, ChattyImageItem, image);

  gtk_widget_class_bind_template_callback (widget_class, chatty_image_download_clicked_cb);
}

static void
chatty_image_item_init (ChattyImageItem *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
chatty_image_item_new (ChattyMessage  *message,
                      ChattyProtocol  protocol)
{
  ChattyImageItem *self;
  ChattyMsgType type;

  g_return_val_if_fail (CHATTY_IS_MESSAGE (message), NULL);

  type = chatty_message_get_msg_type (message);
  g_return_val_if_fail (type == CHATTY_MESSAGE_IMAGE, NULL);

  self = g_object_new (CHATTY_TYPE_IMAGE_ITEM, NULL);
  self->protocol = protocol;
  self->message = g_object_ref (message);

  g_signal_connect_object (message, "updated",
                           G_CALLBACK (image_item_update_message),
                           self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self, "notify::scale-factor",
                           G_CALLBACK (image_item_update_message),
                           self, G_CONNECT_SWAPPED);
  image_item_update_message (self);

  return GTK_WIDGET (self);
}

GtkStyleContext *
chatty_image_item_get_style (ChattyImageItem *self)
{
  g_return_val_if_fail (CHATTY_IS_IMAGE_ITEM (self), NULL);

  return gtk_widget_get_style_context (self->image_overlay);

}

ChattyMessage *
chatty_image_item_get_item (ChattyImageItem *self)
{
  g_return_val_if_fail (CHATTY_IS_IMAGE_ITEM (self), NULL);

  return self->message;
}
