/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-media.c
 *
 * Copyright 2020, 2021 Purism SPC
 *           2021, Chris Talbot
 *
 * Author(s):
 *   Chris Talbot
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define G_LOG_DOMAIN "chatty-media"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/stat.h>
#include <errno.h>

#include "chatty-media.h"
#include "chatty-log.h"

/**
 * SECTION: chatty-media
 * @title: ChattyMultmedia
 * @short_description: Functions to transform multimedia
 * @include: "chatty-media.h"
 *
 */


/**
 * chatty_media_scale_image_to_size_sync:
 * @name: A string
 * @protocol: A #ChattyProtocol flag
 *
 * This function takes in a ChattyFileInfo, and scales the image in a new file
 * to be a size below the original_desired_size. It then creates a new
 * ChattyFileInfo to pass back. the original ChattyFileInfo is untouched.
 * original_desired_size is in bytes
 *
 * Returns: A newly allowcated #ChattyFileInfo with all valid protocols
 * set.
 */

ChattyFileInfo *
chatty_media_scale_image_to_size_sync (ChattyFileInfo *input_file,
                                       gsize           desired_size,
                                       gboolean        use_temp_file)
{
  g_autoptr(GFileInfo) file_info = NULL;
  g_autoptr(GFile) resized_file = NULL;
  g_autoptr(GdkPixbuf) dest = NULL;
  g_autoptr(GdkPixbuf) src = NULL;
  g_autoptr(GString) path = NULL;
  g_autoptr(GError) error = NULL;
  ChattyFileInfo *new_attachment;
  g_autofree char *basename = NULL;
  char *file_extension = NULL;
  int width = -1, height = -1;
  const char *qualities[] = {"80", "70", "60", "40", NULL};
  gsize new_size;

  if (!input_file->mime_type || !g_str_has_prefix (input_file->mime_type, "image")) {
    g_warning ("File is not an image! Cannot Resize");
    return NULL;
  }

  /* Most gifs are animated, so this cannot resize them */
  if (strstr (input_file->mime_type, "gif")) {
    g_warning ("File is a gif! Cannot resize");
    return NULL;
  }

  /*
   * https://developer.gnome.org/gdk-pixbuf/stable/
   * https://developer.gnome.org/gdk-pixbuf/stable/gdk-pixbuf-File-saving.html#gdk-pixbuf-save
   * https://developer.gnome.org/gdk-pixbuf/stable/gdk-pixbuf-File-Loading.html#gdk-pixbuf-new-from-file-at-scale
   */

  src = gdk_pixbuf_new_from_file (input_file->path, &error);
  if (error) {
    g_warning ("Error in loading: %s\n", error->message);
    return NULL;
  }

  {
    float aspect_ratio;
    int size;

    /* We don't have to apply the embedded orientation here
     * as we care only the largest of the width/height */
    width = gdk_pixbuf_get_width (src);
    height = gdk_pixbuf_get_height (src);
    aspect_ratio = MAX (width, height) / (float)(MIN (width, height));

    /*
     * Image size scales about linearly with either width or height changes
     * Some (conservative) experimental figures for jpeg quality 80%:
     * 2560 by 2560: ~750000 Bytes
     * 2048 by 2048: ~500000 Bytes
     * 1600 by 1600: ~300000 Bytes
     * 1080 by 1080: ~150000 Bytes
     * 720 by 720:   ~ 80000 Bytes
     * 480 by 480:   ~ 50000 Bytes
     * 320 by 320:   ~ 25000 Bytes
     */

    if (desired_size < 25000 * aspect_ratio) {
      g_warning ("Requested size is too small!\n");
      return NULL;
    }

    if (desired_size < 50000 * aspect_ratio)
      size = 320;
    else if (desired_size < 80000 * aspect_ratio)
      size = 480;
    else if (desired_size < 150000 * aspect_ratio)
      size = 720;
    else if (desired_size < 300000 * aspect_ratio)
      size = 1080;
    else if (desired_size < 500000 * aspect_ratio)
      size = 1600;
    else if (desired_size < 750000 * aspect_ratio)
      size = 2048;
    else
      size = 2560;

    /* Don't grow image more than the available size */
    if (width > height) {
      height = CLAMP (size, 0, height);
      width = -1;
    } else {
      width = CLAMP (size, 0, width);
      height = -1;
    }

    g_debug ("New width: %d, New height: %d", width, height);
  }

  /* Try qualities in descending order until one works. If the last one isn't
   * small enough, let it try anyway */
  for (const char **quality = qualities; *quality != NULL; quality++) {
    g_clear_object (&src);

    src = gdk_pixbuf_new_from_file_at_size (input_file->path, width, height, &error);

    /* Make sure the pixbuf is in the correct orientation */
    dest = gdk_pixbuf_apply_embedded_orientation (src);

    new_attachment = g_try_new0 (ChattyFileInfo, 1);
    if (new_attachment == NULL) {
      g_warning ("Error in creating new attachment\n");
      return NULL;
    }

    if (use_temp_file) {
      path = g_string_new (g_build_filename (g_get_tmp_dir (), "chatty/", NULL));
    } else {
      path = g_string_new (g_build_filename (g_get_user_cache_dir (), "chatty/", NULL));
    }

    CHATTY_TRACE_MSG ("New Directory Path: %s", path->str);

    if (g_mkdir_with_parents (path->str, S_IRWXU | S_IRWXG | S_IRWXO) == -1) {
      g_warning ("Error creating directory: %s", strerror (errno));
      return NULL;
    }

    basename = g_path_get_basename (input_file->path);
    file_extension = strrchr (basename, '.');
    if (file_extension) {
      g_string_append_len (path, basename, file_extension - basename);
      g_string_append (path, "-resized.jpg");
    } else {
      g_string_append_printf (path, "%s-resized.jpg", basename);
    }

    CHATTY_TRACE_MSG ("New File Path: %s", path->str);
    resized_file = g_file_new_for_path (path->str);

    /* Putting the quality at 80 seems to work well experimentally */
    gdk_pixbuf_save (dest, path->str, "jpeg", &error, "quality", *quality, NULL);

    if (error) {
      g_warning ("Error in saving: %s\n", error->message);
      return NULL;
    }

    /* Debug: Figure out size of images */
    file_info = g_file_query_info (resized_file,
                                   G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                   G_FILE_QUERY_INFO_NONE,
                                   NULL,
                                   &error);

    if (error) {
      g_warning ("Error getting file info: %s", error->message);
      return NULL;
    }
    new_size = g_file_info_get_size (file_info);
    if (new_size <= desired_size) {
      g_debug ("Resized at quality %s to size %" G_GSIZE_FORMAT, *quality, new_size);
      break;
    }
  }

  /*
   * https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types
   */
  new_attachment->file_name = g_file_get_basename (resized_file);

  /* We are saving it as a jpeg, so we know the MIME type */
  new_attachment->mime_type = g_strdup ("image/jpeg");

  new_attachment->size = g_file_info_get_size (file_info);
  new_attachment->path = g_file_get_path (resized_file);
  new_attachment->url  = g_file_get_uri (resized_file);

  return new_attachment;
}

typedef struct {
  ChattyFileInfo *input_file;
  gsize           desired_size;
  gboolean        use_temp_file;
} ChattyMediaScaleData;

static void
scale_image_thread (GTask        *task,
                    gpointer      source_object,
                    gpointer      task_data,
                    GCancellable *cancellable)
{
  ChattyMediaScaleData *scale_data = task_data;
  ChattyFileInfo *new_file;

  new_file = chatty_media_scale_image_to_size_sync (scale_data->input_file,
                                                    scale_data->desired_size,
                                                    scale_data->use_temp_file);

  if (new_file) {
    g_task_return_pointer (task, new_file, (GDestroyNotify)chatty_file_info_free);
  } else {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Error in creating scaled image");
    return;
  }

}

void
chatty_media_scale_image_to_size_async (ChattyFileInfo      *input_file,
                                        gsize                desired_size,
                                        gboolean             use_temp_file,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  ChattyMediaScaleData *scale_data;

  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_return_if_fail (callback);

  task = g_task_new (NULL, cancellable, callback, user_data);
  if (!input_file->mime_type || !g_str_has_prefix (input_file->mime_type, "image")) {
    g_warning ("File is not an image! Cannot Resize");
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                             "File is not an image! Cannot Resize");
    return;
  }

  /* Most gifs are animated, so this cannot resize them */
  if (strstr (input_file->mime_type, "gif")) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                             "File is a gif! Cannot resize");
    return;
  }

  scale_data = g_try_new0 (ChattyMediaScaleData, 1);
  if (scale_data == NULL) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                             "Error in creating new attachment");
    return;
  }

  scale_data->input_file = input_file;
  scale_data->desired_size = desired_size;
  scale_data->use_temp_file = use_temp_file;

  g_task_set_task_data (task, scale_data, g_free);
  g_task_run_in_thread (task, scale_image_thread);
}

ChattyFileInfo *
chatty_media_scale_image_to_size_finish (GAsyncResult  *result,
                                         GError       **error)
{
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}
