/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-file-item.c
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-file-item"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "chatty-utils.h"
#include "chatty-file-item.h"
#include "chatty-log.h"

struct _ChattyFileItem
{
  GtkBox       parent_instance;

  GtkWidget   *overlay;
  GtkWidget   *remove_button;

  char        *file_name;
};

G_DEFINE_TYPE (ChattyFileItem, chatty_file_item, GTK_TYPE_BOX)

typedef struct _FileItemData {
  ChattyFileItem *self;
  GtkWidget *image;
  GFile *file;
} FileItemData;

static void file_item_data_free (FileItemData *data);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FileItemData, file_item_data_free)

static void
file_item_data_free (FileItemData *data)
{
  if (!data)
    return;

  g_clear_object (&data->image);
  g_clear_object (&data->self);
  g_free (data);
}

static void
file_item_update_image (ChattyFileItem *self,
                        GtkWidget      *image,
                        GFile          *file)
{
  g_autoptr(GError) error = NULL;
  GFileInfo *file_info;
  const char *thumbnail;

  g_assert (CHATTY_IS_FILE_ITEM (self));
  g_assert (image);
  g_assert (file);

  file_info = g_file_query_info (file,
                                 G_FILE_ATTRIBUTE_STANDARD_ICON ","
                                 G_FILE_ATTRIBUTE_THUMBNAIL_PATH,
                                 G_FILE_QUERY_INFO_NONE,
                                 NULL, &error);
  if (error)
    g_warning ("Error querying info: %s", error->message);

  thumbnail = g_file_info_get_attribute_byte_string (file_info, G_FILE_ATTRIBUTE_THUMBNAIL_PATH);

  if (thumbnail) {
    GtkWidget *frame;

    frame = gtk_frame_new (NULL);
    gtk_widget_set_margin_end (frame, 6);
    gtk_widget_set_margin_top (frame, 6);
    gtk_image_set_from_file (GTK_IMAGE (image), thumbnail);
    gtk_container_add (GTK_CONTAINER (frame), image);
    gtk_container_add (GTK_CONTAINER (self->overlay), frame);
  } else {
    GIcon *icon;

    gtk_widget_set_margin_end (image, 6);
    icon = (GIcon *)g_file_info_get_attribute_object (file_info, G_FILE_ATTRIBUTE_STANDARD_ICON);
    gtk_image_set_from_gicon (GTK_IMAGE (image), icon, GTK_ICON_SIZE_DND);
    image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_DND);
    gtk_image_set_pixel_size (GTK_IMAGE (image), 96);
    gtk_container_add (GTK_CONTAINER (self->overlay), image);
  }

  gtk_widget_show_all (self->overlay);
}

static void
file_create_thumbnail_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  g_autoptr(FileItemData) data = user_data;

  if (gtk_widget_in_destruction (GTK_WIDGET (data->self)))
    return;

  file_item_update_image (data->self, data->image, data->file);
}

static void
chatty_file_item_set_file (ChattyFileItem *self,
                           const char     *file_name)
{
  g_autoptr(GFileInfo) file_info = NULL;
  g_autoptr(GFile) file = NULL;
  g_autoptr(GError) error = NULL;
  const char *thumbnail;
  GtkWidget *image;
  gboolean thumbnail_failed, thumbnail_valid;

  g_assert (CHATTY_IS_FILE_ITEM (self));
  g_assert (file_name && *file_name);

  g_free (self->file_name);
  self->file_name = g_strdup (file_name);

  file = g_file_new_for_path (file_name);
  file_info = g_file_query_info (file,
                                 G_FILE_ATTRIBUTE_STANDARD_ICON ","
                                 G_FILE_ATTRIBUTE_THUMBNAIL_PATH ","
                                 G_FILE_ATTRIBUTE_THUMBNAIL_IS_VALID ","
                                 G_FILE_ATTRIBUTE_THUMBNAILING_FAILED,
                                 G_FILE_QUERY_INFO_NONE,
                                 NULL, &error);
  if (error)
    g_warning ("Error querying info: %s", error->message);

  image = gtk_image_new ();
  gtk_widget_set_tooltip_text (image, self->file_name);
  gtk_widget_set_size_request (image, -1, 96);
  thumbnail = g_file_info_get_attribute_byte_string (file_info, G_FILE_ATTRIBUTE_THUMBNAIL_PATH);
  thumbnail_failed = g_file_info_get_attribute_boolean (file_info, G_FILE_ATTRIBUTE_THUMBNAILING_FAILED);
  thumbnail_valid = g_file_info_get_attribute_boolean (file_info, G_FILE_ATTRIBUTE_THUMBNAIL_IS_VALID);

  CHATTY_TRACE_MSG ("has thumbnail: %d, failed thumbnail: %d, valid thumbnail: %d",
                    !!thumbnail, thumbnail_failed, thumbnail_valid);

  if (thumbnail || (thumbnail_failed && thumbnail_valid)) {
    file_item_update_image (self, image, file);
  } else {
    FileItemData *data;

    data = g_new0 (FileItemData, 1);
    data->self = g_object_ref (self);
    data->image = g_object_ref (image);
    data->file = g_object_ref (file);
    chatty_utils_create_thumbnail_async (self->file_name,
                                         file_create_thumbnail_cb,
                                         data);
  }
}

static void
chatty_file_item_finalize (GObject *object)
{
  ChattyFileItem *self = (ChattyFileItem *)object;

  g_free (self->file_name);

  G_OBJECT_CLASS (chatty_file_item_parent_class)->finalize (object);
}

static void
chatty_file_item_class_init (ChattyFileItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = chatty_file_item_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-file-item.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyFileItem, overlay);
  gtk_widget_class_bind_template_child (widget_class, ChattyFileItem, remove_button);
}

static void
chatty_file_item_init (ChattyFileItem *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
chatty_file_item_new (const char *file_name)
{
  ChattyFileItem *self;

  self = g_object_new (CHATTY_TYPE_FILE_ITEM, NULL);
  chatty_file_item_set_file (self, file_name);

  return GTK_WIDGET (self);
}

const char *
chatty_file_item_get_file (ChattyFileItem *self)
{
  g_return_val_if_fail (CHATTY_IS_FILE_ITEM (self), NULL);

  return self->file_name;
}
