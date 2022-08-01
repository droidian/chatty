/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-utils"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib.h>
#ifdef PURPLE_ENABLED
# include <purple.h>
#endif

#include "chatty-manager.h"
#include "chatty-settings.h"
#include "chatty-phone-utils.h"
#include "chatty-utils.h"
#include <libebook-contacts/libebook-contacts.h>
#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-desktop-thumbnail.h>

#include "chatty-log.h"

#define DIGITS      "0123456789"
#define ASCII_CAPS  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define ASCII_SMALL "abcdefghijklmnopqrstuvwxyz"

/*
 * matrix_id_is_valid:
 * @name: A string
 * @prefix: The allowed prefix
 *
 * Check if @name is a valid username
 * or channel name
 *
 * @prefix should be one of ‘#’ or ‘@’.
 *
 * See https://matrix.org/docs/spec/appendices#id12
 */
static gboolean
matrix_id_is_valid (const char *name,
                    char        prefix)
{
  guint len;

  if (!name || !*name)
    return FALSE;

  if (prefix != '@' && prefix != '#')
    return FALSE;

  if (*(name + 1) == ':')
    return FALSE;

  if (prefix == '@' && *name != '@')
    return FALSE;

  /* Group name can have '#' or '!' (Group id) as prefix */
  if (prefix == '#' && *name != '#' && *name != '!')
    return FALSE;

  len = strlen (name);

  if (len > 255)
    return FALSE;

  if (strspn (name + 1, DIGITS ASCII_CAPS ASCII_SMALL ":._=/-") != len - 1)
    return FALSE;

  if (len >= 4 &&
      *(name + len - 1) != ':' &&
      !strchr (name + 1, prefix) &&
      strchr (name, ':'))
    return TRUE;

  return FALSE;
}

char *
chatty_utils_check_phonenumber (const char *phone_number,
                                const char *country)
{
  EPhoneNumber      *number;
  g_autofree char   *raw = NULL;
  char              *stripped;
  char              *result;
  g_autoptr(GError)  err = NULL;

  CHATTY_DEBUG (phone_number, "checking number");

  if (!phone_number || !*phone_number)
    return NULL;

  raw = g_uri_unescape_string (phone_number, NULL);

  if (g_str_has_prefix (raw, "sms:"))
    stripped = raw + strlen ("sms:");
  else
    stripped = raw;

  /* Skip sms:// */
  while (*stripped == '/')
    stripped++;

  if (strspn (stripped, "+()- 0123456789") != strlen (stripped))
    return NULL;

  if (!e_phone_number_is_supported ()) {
    g_warning ("evolution-data-server built without libphonenumber support");
    return NULL;
  }

  number = e_phone_number_from_string (stripped, country, &err);

  if (!number) {
    g_debug ("Error parsing ‘%s’ for country ‘%s’: %s", phone_number, country, err->message);

    return NULL;
  }

  if (*phone_number != '+' &&
      !chatty_phone_utils_is_valid (phone_number, country))
    result = e_phone_number_to_string (number, E_PHONE_NUMBER_FORMAT_NATIONAL);
  else
    result = e_phone_number_to_string (number, E_PHONE_NUMBER_FORMAT_E164);

  e_phone_number_free (number);

  return result;
}

/**
 * chatty_utils_username_is_valid:
 * @name: A string
 * @protocol: A #ChattyProtocol flag
 *
 * Check if @name is a valid username for the given
 * @protocol(s). Please note that only rudimentary
 * checks are done for the validation process.
 *
 * Currently, %CHATTY_PROTOCOL_XMPP, %CHATTY_PROTOCOL_SMS
 * and %CHATTY_PROTOCOL_MATRIX or their combinations are
 * supported for @protocol.
 *
 * Returns: A #ChattyProtocol with all valid protocols
 * set.
 */
ChattyProtocol
chatty_utils_username_is_valid (const char     *name,
                                ChattyProtocol  protocol)
{
  ChattyProtocol valid = 0;
  guint len;

  if (!name)
    return valid;

  len = strlen (name);
  if (len < 3)
    return valid;

  if (protocol & (CHATTY_PROTOCOL_XMPP | CHATTY_PROTOCOL_EMAIL)) {
    const char *at_char, *at_char_end;

    at_char = strchr (name, '@');
    at_char_end = strrchr (name, '@');

    /* Consider valid if @name has only one ‘@’ and @name
     * doesn’t start nor end with a ‘@’
     * See https://xmpp.org/rfcs/rfc3920.html#addressing
     */
    /* XXX: We are ignoring one valid case.  ie, domain alone
     * or domain/resource */
    if (at_char &&
        /* Should not begin with ‘@’ */
        *name != '@' &&
        /* should not end with ‘@’ */
        *(at_char + 1) &&
        /* We require exact one ‘@’ */
        at_char == at_char_end)
      valid |= ((CHATTY_PROTOCOL_XMPP | CHATTY_PROTOCOL_EMAIL) & protocol);
  }

  if (protocol & CHATTY_PROTOCOL_MATRIX) {
    if (matrix_id_is_valid (name, '@'))
      valid |= CHATTY_PROTOCOL_MATRIX;
  }

  if (protocol & CHATTY_PROTOCOL_TELEGRAM && *name == '+') {
    /* country code doesn't matter as we use international format numbers */
    if (chatty_phone_utils_is_valid (name, "US"))
      valid |= CHATTY_PROTOCOL_TELEGRAM;
  }

  if (protocol & CHATTY_PROTOCOL_MMS_SMS && len < 20) {
    const char *end;
    guint end_len;

    end = name;
    if (*end == '+')
      end++;

    end_len = strspn (end, "0123456789- ()");

    if (*name == '+')
      end_len++;

    if (end_len == len)
      valid |= CHATTY_PROTOCOL_MMS_SMS;
  }

  return valid;
}

/**
 * chatty_utils_groupname_is_valid:
 * @name: A string
 * @protocol: A #ChattyProtocol flag
 *
 * Check if @name is a valid group name for the given
 * @protocol(s).  Please note that only rudimentary checks
 * are done for the validation process.
 *
 * Currently %CHATTY_PROTOCOL_XMPP and %CHATTY_PROTOCOL_MATRIX
 * or their combinations are supported for @protocol.
 *
 * Returns: A #ChattyProtocol with all valid protocols
 * set.
 */
ChattyProtocol
chatty_utils_groupname_is_valid (const char     *name,
                                 ChattyProtocol  protocol)
{
  ChattyProtocol valid = 0;
  guint len;

  if (!name)
    return valid;

  len = strlen (name);
  if (len < 3)
    return valid;

  if (protocol & CHATTY_PROTOCOL_XMPP) {
    if (chatty_utils_username_is_valid (name, CHATTY_PROTOCOL_XMPP))
      valid |= CHATTY_PROTOCOL_XMPP;
  }

  if (protocol & CHATTY_PROTOCOL_MATRIX) {
    /* Consider valid if @name starts with ‘#’ and has only one
     * ‘#’, has ‘:’, and has atleast 4 chars*/
    if (matrix_id_is_valid (name, '#'))
      valid |= CHATTY_PROTOCOL_MATRIX;
  }

  return valid;
}

const char *
chatty_utils_get_purple_dir (void)
{
#ifdef PURPLE_ENABLED
  return purple_user_dir ();
#endif
  return g_build_filename (g_get_home_dir (), ".purple", NULL);
}

char *
chatty_utils_jabber_id_strip (const char *name)
{
  char ** split;
  char *  stripped;

  split = g_strsplit (name, "/", -1);
  stripped = g_strdup (split[0]);

  g_strfreev (split);

  return stripped;
}
/*
 * chatty_utils_sanitize_filename:
 * @filename: the filename to sanitize
 *
 * Sanitizes a filename by modifying the passed in string in place.
 * It's basically the reverse of `ChattyTextItem.find_url()`.
 */
void
chatty_utils_sanitize_filename (char *filename)
{
  g_strdelimit (filename, " ()[],", '_');
}


gboolean
chatty_utils_get_item_position (GListModel *list,
                                gpointer    item,
                                guint      *position)
{
  guint n_items;

  g_return_val_if_fail (G_IS_LIST_MODEL (list), FALSE);
  g_return_val_if_fail (item != NULL, FALSE);

  n_items = g_list_model_get_n_items (list);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(GObject) object = NULL;

      object = g_list_model_get_item (list, i);

      if (object == item)
        {
          if (position)
            *position = i;

          return TRUE;
        }
    }

  return FALSE;
}

/**
 * chatty_utils_remove_list_item:
 * @store: a #GListStore
 * @item: A #GObject derived object
 *
 * Remove first found @item from @store.
 *
 * Returns: %TRUE if found and removed. %FALSE otherwise.
 */
gboolean
chatty_utils_remove_list_item (GListStore *store,
                               gpointer    item)
{
  GListModel *model;
  guint n_items;

  g_return_val_if_fail (G_IS_LIST_STORE (store), FALSE);
  g_return_val_if_fail (item, FALSE);

  model = G_LIST_MODEL (store);
  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(GObject) object = NULL;

      object = g_list_model_get_item (model, i);

      if (object == item)
        {
          g_list_store_remove (store, i);

          return TRUE;
        }
    }

  return FALSE;
}

static void
pixbuf_size_prepared_cb (GdkPixbufLoader *loader,
                         int              width,
                         int              height)
{
  int scale;

  g_assert (GDK_IS_PIXBUF_LOADER (loader));

  scale = width / 384;

  /* Scale down the image as we show them only as thumbnails */
  if (scale)
    gdk_pixbuf_loader_set_size (loader, width / scale, height / scale);
}

GdkPixbuf *
chatty_utils_get_pixbuf_from_data (const guchar *buf,
                                   gsize         count)
{
  g_autoptr(GdkPixbufLoader) loader = NULL;
  g_autoptr(GError) error = NULL;
  GdkPixbuf *pixbuf;

  if (!buf || !count)
    return NULL;

  loader = gdk_pixbuf_loader_new ();
  g_signal_connect_object (loader, "size-prepared",
                           G_CALLBACK (pixbuf_size_prepared_cb),
                           loader, 0);
  gdk_pixbuf_loader_write (loader, buf, count, &error);

  if (!error)
    gdk_pixbuf_loader_close (loader, &error);

  if (error) {
    CHATTY_TRACE_MSG ("%s", error->message);
    return NULL;
  }

  pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);

  if (!pixbuf) {
    CHATTY_TRACE_MSG ("pixbuf creation failed");
    return NULL;
  }

  return g_object_ref (pixbuf);
}

void
chatty_file_info_free (ChattyFileInfo *file_info)
{
  if (!file_info)
    return;

  g_free (file_info->file_name);
  g_free (file_info->url);
  g_free (file_info->path);
  g_free (file_info->mime_type);
  g_free (file_info);
}

/**
 * chatty_file_info_new_for_path:
 * A @path: A path string
 *
 * Takes an absolute path and creates a ChattyFileInfo Object
 * with MIME Type, path, URI, and size. Returned object must be freed
 * with chatty_file_info_free ()
 *
 * Returns: (transfer full) (nullable): A ChattyFileInfo with MIME Type, path,
 * URI, and size
 */

ChattyFileInfo *
chatty_file_info_new_for_path (const char *path)
{
  g_autoptr(GError) error = NULL;
  GFile *file;
  GFileInfo *file_info;
  ChattyFileInfo *attachment = NULL;

  attachment = g_try_new0 (ChattyFileInfo, 1);
  g_debug ("Attachment Path: %s", path);
  file = g_file_new_for_path (path);

  file_info = g_file_query_info (file,
                                 G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE "," G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                 G_FILE_QUERY_INFO_NONE,
                                 NULL,
                                 &error);



  if (error != NULL) {
    g_warning ("Error getting file info: %s", error->message);
    chatty_file_info_free (attachment);
    return NULL;
  }
  /*
   *  https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types
   */
  attachment->file_name = g_file_get_basename (file);
  if (g_file_info_get_content_type (file_info) == NULL)
    attachment->mime_type = g_strdup ("application/octet-stream");
  else
    attachment->mime_type = g_content_type_get_mime_type (g_file_info_get_content_type (file_info));

  if (attachment->mime_type == NULL) {
    g_warning ("Could not get MIME type! Trying Content Type instead");
    if (g_file_info_get_content_type (file_info) != NULL) {
      attachment->mime_type = g_strdup (g_file_info_get_content_type (file_info));
    } else {
      g_warning ("Could not figure out Content Type! Using a Generic one");
      attachment->mime_type = g_strdup ("application/octet-stream");
    }
  }
  attachment->size = g_file_info_get_size (file_info);
  attachment->path = g_file_get_path (file);
  attachment->url  = g_file_get_uri (file);

  return attachment;
}

static void
utils_create_thumbnail (GTask        *task,
                        gpointer      source_object,
                        gpointer      task_data,
                        GCancellable *cancellable)
{
  g_autoptr(GnomeDesktopThumbnailFactory) factory = NULL;
  g_autoptr(GdkPixbuf) thumbnail = NULL;
  g_autoptr(GFileInfo) file_info = NULL;
  g_autoptr(GFile) file = NULL;
  g_autofree char *uri = NULL;
  const char *file_name = task_data;
  const char *content_type;
  GError *error = NULL;
  gboolean thumbnail_valid, thumbnail_failed;
  time_t mtime;

  file = g_file_new_for_path (file_name);
  file_info = g_file_query_info (file,
                                 G_FILE_ATTRIBUTE_THUMBNAIL_IS_VALID ","
                                 G_FILE_ATTRIBUTE_THUMBNAILING_FAILED ","
                                 G_FILE_ATTRIBUTE_TIME_MODIFIED ","
                                 G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE,
                                 G_FILE_QUERY_INFO_NONE,
                                 NULL, &error);
  if (error) {
    g_task_return_error (task, error);
    return;
  }

  thumbnail_valid = g_file_info_get_attribute_boolean (file_info, G_FILE_ATTRIBUTE_THUMBNAIL_IS_VALID);
  thumbnail_failed = g_file_info_get_attribute_boolean (file_info, G_FILE_ATTRIBUTE_THUMBNAILING_FAILED);

  if (thumbnail_valid && thumbnail_failed) {
    g_task_return_boolean (task, TRUE);
    return;
  }

  uri = g_file_get_uri (file);
  factory = gnome_desktop_thumbnail_factory_new (GNOME_DESKTOP_THUMBNAIL_SIZE_LARGE);
  content_type = g_file_info_get_attribute_string (file_info, G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE);
  mtime = g_file_info_get_attribute_uint64 (file_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);

  if (!gnome_desktop_thumbnail_factory_can_thumbnail (factory, uri, content_type, mtime)) {
    g_task_return_boolean (task, FALSE);
    return;
  }

  if (gnome_desktop_thumbnail_factory_has_valid_failed_thumbnail (factory, uri, mtime)) {
    g_task_return_boolean (task, FALSE);
    return;
  }

  if (!content_type) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "NULL content type");
    return;
  }

#if defined(GNOME_DESKTOP_PLATFORM_VERSION) && GNOME_DESKTOP_PLATFORM_VERSION >= 43
  thumbnail = gnome_desktop_thumbnail_factory_generate_thumbnail (factory, uri, content_type, NULL, &error);
  if (!thumbnail) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to create thumbnail for file: %s (%s)", uri, error->message);

    g_warning ("Failed to create thumbnail for file: %s", uri);

    g_error_free (error);
    return;
  }

  gnome_desktop_thumbnail_factory_save_thumbnail (factory, thumbnail, uri, mtime, NULL, &error);
  if (error) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to create thumbnail for file: %s (%s)", uri, error->message);
    g_error_free (error);
    return;
  }

  g_task_return_boolean (task, TRUE);
#else
  thumbnail = gnome_desktop_thumbnail_factory_generate_thumbnail (factory, uri, content_type);

  if (thumbnail) {
    gnome_desktop_thumbnail_factory_save_thumbnail (factory, thumbnail, uri, mtime);
  } else {
    /* TODO: seems to fail always on Librem5/pinephone.  So fix it instead of
     * creating a failed thumbnail */
    /* gnome_desktop_thumbnail_factory_create_failed_thumbnail (factory, uri, mtime); */
    g_warning ("Failed to create thumbnail for file: %s", uri);
  }

  g_task_return_boolean (task, TRUE);
#endif
}

void
chatty_utils_create_thumbnail_async (const char          *file,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (file && *file);
  g_return_if_fail (callback);

  task = g_task_new (NULL, NULL, callback, user_data);
  g_task_set_task_data (task, g_strdup (file), g_free);

  g_task_run_in_thread (task, utils_create_thumbnail);
}

gboolean
chatty_utils_create_thumbnail_finish (GAsyncResult  *result,
                                      GError       **error)
{
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
