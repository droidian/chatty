/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-mmsd.c
 *
 * Copyright 2020, 2021 Purism SPC
 *           2021, Chris Talbot
 *           2020, Kyle Evans
 *
 * Author(s):
 *   Chris Talbot <chris@talbothome.com>
 *   Kyle Evans <kvans32@gmail.com>
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define G_LOG_DOMAIN "chatty-mmsd"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>
#include "chatty-settings.h"
#include "chatty-account.h"
#include "chatty-mm-chat.h"
#include "chatty-utils.h"
#include "chatty-mm-account.h"
#include "chatty-mm-account-private.h"
#include "chatty-media.h"
#include "chatty-mmsd.h"
#include "chatty-log.h"
#include "chatty-mmsd.h"

#define MMSD_SERVICE	                "org.ofono.mms"
#define MMSD_PATH	                "/org/ofono/mms"
#define MMSD_MODEMMANAGER_PATH          MMSD_PATH    "/modemmanager"
#define MMSD_MANAGER_INTERFACE	        MMSD_SERVICE ".Manager"
#define MMSD_SERVICE_INTERFACE	        MMSD_SERVICE ".Service"
#define MMSD_MESSAGE_INTERFACE	        MMSD_SERVICE ".Message"
#define MMSD_MODEMMANAGER_INTERFACE	MMSD_SERVICE ".ModemManager"

/**
 * SECTION: chatty-mmsd
 * @title: ChattyMmsd
 * @short_description: A handler for mmsd
 * @include: "chatty-mmsd.h"
 *
 */

/**
 * mmsd Context Connection:
 *
 * The enumerations if mmsd has a bearer handler error.
 */

enum {
  MMSD_MM_MODEM_MMSC_MISCONFIGURED,       //the MMSC is the default value
  MMSD_MM_MODEM_NO_BEARERS_ACTIVE,        //The Modem has no bearers
  MMSD_MM_MODEM_INTERFACE_DISCONNECTED,   //mmsd found the right bearer, but it is disconnected
  MMSD_MM_MODEM_INCORRECT_APN_CONNECTED,  //no APN is connected that matches the settings
  MMSD_MM_MODEM_FUTURE_CASE_DISCONNECTED, //Reserved for future case
  MMSD_MM_MODEM_CONTEXT_ACTIVE            //No error, context activated properly
} mmsd_context_connection;

struct _ChattyMmsd {
  GObject          parent_instance;

  ChattyMmAccount  *mm_account;
  ChattyMmDevice   *mm_device;
  GDBusConnection  *connection;
  guint             mmsd_watch_id;
  GDBusProxy       *manager_proxy;
  GDBusProxy       *service_proxy;
  GDBusProxy       *modemmanager_proxy;
  char             *modem_number;
  char             *default_modem_number;
  GPtrArray        *mms_arr;
  GHashTable       *mms_hash_table;
  gsize             max_attach_size;
  int               max_num_attach;
  char             *carrier_mmsc;
  char             *mms_apn;
  char             *carrier_proxy;
  gboolean          auto_create_smil;
  gboolean          is_ready;
  guint             mmsd_signal_id;
};

typedef struct _mms_payload mms_payload;

struct _mms_payload {
  ChattyMmsd     *self;
  ChattyMessage  *message;

  /* These are ephemeral values useful for chatty-mmsd only */
  char           *sender;
  char           *chat;
  char           *objectpath;
  int             delivery_report;
  guint           mmsd_message_proxy_watch_id;
};

#define DEFAULT_MAXIMUM_ATTACHMENT_SIZE 1100000
#define DEFAULT_MAXIMUM_ATTACHMENTS     25

G_DEFINE_TYPE (ChattyMmsd, chatty_mmsd, G_TYPE_OBJECT)

static void chatty_mmsd_process_mms (ChattyMmsd *self, mms_payload *payload);

static void
mms_payload_free (gpointer data)
{
  mms_payload *payload = data;

  if (!payload)
    return;

  if (payload->mmsd_message_proxy_watch_id != 0) {
    g_debug ("Unsubscribing from MMS watch");
    g_dbus_connection_signal_unsubscribe (payload->self->connection,
                                          payload->mmsd_message_proxy_watch_id);
  }

  g_free (payload->objectpath);
  g_free (payload->sender);
  g_free (payload->chat);
  g_clear_object (&payload->message);
  g_free (payload);
}

static void
mmsd_set_value (ChattyMmsd    *self,
                GVariantDict  *dict,
                const char    *key,
                char         **out)
{
  g_autofree char *value = NULL;

  g_clear_pointer (&*out, g_free);

  if (g_variant_dict_lookup (dict, key, "s", &value))
    *out = g_steal_pointer (&value);

  if ((*out == self->carrier_proxy ||
      *out == self->default_modem_number) &&
      ((g_strcmp0 (*out, "NULL") == 0) || !*out))
    g_clear_pointer (&*out, g_free);

  g_debug ("%s is set to %s", key, *out);
}

static void
chatty_mmsd_delete_mms_cb (GObject      *interface,
                           GAsyncResult *result,
                           gpointer     *user_data)
{
  g_autoptr(GVariant) ret = NULL;
  g_autoptr(GError) error = NULL;

  ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (interface), result, &error);

  if (ret) {
    g_debug ("MMS delete finished");
  } else {
    g_warning ("Couldn't delete MMS - error: %s", error ? error->message : "unknown");
  }
}

static void
chatty_mmsd_get_message_proxy_cb (GObject      *service,
                                  GAsyncResult *res,
                                  gpointer      user_data)
{
  g_autoptr(GDBusProxy) message_proxy = NULL;
  g_autoptr(GError) error = NULL;

  message_proxy = g_dbus_proxy_new_finish (res, &error);
  if (error != NULL) {
    g_warning ("Error in Message proxy call: %s\n", error->message);
  } else {
    g_dbus_proxy_call (message_proxy,
                       "Delete",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       (GAsyncReadyCallback) chatty_mmsd_delete_mms_cb,
                       NULL);
  }
}

void
chatty_mmsd_delete_mms (ChattyMmsd *self,
                        const char *uid)
{
  g_autofree char *objectpath = NULL;
  if (g_str_has_prefix (uid, MMSD_MODEMMANAGER_PATH))
    objectpath = g_strdup (uid);
  else
    objectpath = g_strdup_printf ("%s/%s", MMSD_MODEMMANAGER_PATH, uid);

  g_debug ("Deleting MMS with Object Path: %s", objectpath);

  if (g_hash_table_lookup (self->mms_hash_table, objectpath) == NULL) {
     g_debug ("MMS not found. Was it already deleted?");
     return;
  }

  g_dbus_proxy_new (self->connection,
                    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                    NULL,
                    MMSD_SERVICE,
                    objectpath,
                    MMSD_MESSAGE_INTERFACE,
                    NULL,
                    chatty_mmsd_get_message_proxy_cb,
                    self);

  g_hash_table_remove (self->mms_hash_table, objectpath);
}

static void
chatty_mmsd_process_delivery_status (const char *delivery_status)
{
  g_auto(GStrv) numbers = g_strsplit (delivery_status, ",", 0);
  guint len = g_strv_length (numbers);

  for (int i = 0; i < len; i++) {
    g_auto(GStrv) number = NULL;

    if (g_strcmp0 (numbers[i], "delivery_update") == 0)
      continue;

    number = g_strsplit (numbers[i], "=", 0);
    if ((g_strcmp0 (number[1], "none") != 0) && (g_strcmp0 (number[1], "retrieved") != 0)) {
      /* TODO: There was an error with delivery. Do we want to raise some sort of flag? */
      g_warning ("There was an error delivering message. Error: %s", number[1]);
    }
  }
}

static void
chatty_mmsd_message_status_changed_cb (GDBusConnection *connection,
                                       const char      *sender_name,
                                       const char      *object_path,
                                       const char      *interface_name,
                                       const char      *signal_name,
                                       GVariant        *parameters,
                                       gpointer         user_data)
{
  mms_payload *payload = user_data;
  g_autoptr (GVariant) variantstatus = NULL;
  const char *status;

  g_variant_get (parameters, "(sv)", NULL, &variantstatus);
  status = g_variant_get_string (variantstatus, NULL);

  if (g_strcmp0 (status, "sent") == 0) {
    g_debug ("Message was sent.");
    chatty_message_set_status (payload->message,
                               CHATTY_STATUS_SENT,
                               0);
    chatty_mmsd_process_mms (payload->self, payload);

  } else if (g_strcmp0 (status, "delivered") == 0) {
    g_debug ("Message was Delivered");
    chatty_message_set_status (payload->message,
                               CHATTY_STATUS_DELIVERED,
                               0);
    chatty_mmsd_process_mms (payload->self, payload);

  } else if (g_str_has_prefix (status, "delivery_update")) {
    g_debug ("Message has Delivery Update");
    chatty_mmsd_process_delivery_status (status);

  } else {
    g_debug ("Message is not sent! Leaving alone....");
  }
}

static void
chatty_mmsd_process_mms (ChattyMmsd  *self,
                         mms_payload *payload)
{
  g_autofree char *sender = NULL;
  g_autofree char *recipientlist = NULL;
  ChattyMsgStatus mms_status = chatty_message_get_status (payload->message);
  ChattyMessage *message = payload->message;

  sender = g_strdup (payload->sender);
  recipientlist = g_strdup (payload->chat);
  g_return_if_fail (recipientlist && *recipientlist);

  if (!chatty_mm_account_recieve_mms_cb (self->mm_account,
                                         message,
                                         sender,
                                         recipientlist)) {
     g_debug ("Message was deleted!");
     return;
  }

  /*
   * Message is still a draft in mmsd and hasn't been sent.
   * Monitor the status of the message until it is sent
   */
  if (mms_status == CHATTY_STATUS_SENDING ||
      (payload->delivery_report && mms_status == CHATTY_STATUS_SENT)) {
    if (payload->mmsd_message_proxy_watch_id == 0) {
      g_debug ("MMS not finished sending/delivering. Watching it for changes");
      payload->self = self;
      payload->mmsd_message_proxy_watch_id =
        g_dbus_connection_signal_subscribe (self->connection,
                                            MMSD_SERVICE,
                                            MMSD_MESSAGE_INTERFACE,
                                            "PropertyChanged",
                                            payload->objectpath,
                                            NULL,
                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                            (GDBusSignalCallback)chatty_mmsd_message_status_changed_cb,
                                            payload,
                                            NULL);
    } else {
      g_debug ("MMS already being watched for changes");
    }
  } else {
    g_debug ("MMS is finished sending/delivering/receiving. Deleting....");
    chatty_mmsd_delete_mms (self, payload->objectpath);
  }
}

static char **
chatty_mmsd_send_mms_create_sender (ChattyChat *chat)
{
  g_autoptr(GString) who = NULL;
  GListModel *users;
  char **send;
  guint items;

  /* Take the list of chat users, convert it into a string that mmsd will like */
  users = chatty_chat_get_users (chat);
  items = g_list_model_get_n_items (users);
  who = g_string_new (NULL);
  for (guint i = 0; i < items; i++) {
    g_autoptr (ChattyMmBuddy) tempbuddy = g_list_model_get_item (users, i);
    const char *country_code = chatty_settings_get_country_iso_code (chatty_settings_get_default ());
    const char *buddy_number = chatty_mm_buddy_get_number (tempbuddy);
    g_autofree char *temp = chatty_utils_check_phonenumber (buddy_number, country_code);

    if (temp == NULL)
      temp = g_strdup (buddy_number);

    who = g_string_append (who, temp);
    who = g_string_append (who, ",");
  }

  /* Convert the string *who into an array of strings **send */
  send = g_strsplit (who->str, ",", items);

  return send;
}

static GVariant *
chatty_mmsd_send_mms_create_options (void)
{
  g_autofree char *smil = NULL;
  ChattySettings *settings;
  GVariantBuilder options_builder;
  gboolean request_report;

  settings = chatty_settings_get_default ();
  request_report = chatty_settings_request_sms_delivery_reports (settings);

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_ARRAY);

  /*
   * SMIL is not required to have an MMS, but Android and iOS
   * add it. Android displays MMS just fine without SMIL.
   * Chris added (beta) functionality into mmsd to generate SMIL automatically
   * if mmsd is not passed SMIL by setting "AutoCreateSMIL" on "true".
   * Adding SMIL functionality into Chatty seems redundant if mmsd does it.
   * Right now Chatty leaves "AutoCreateSMIL" to default
   * and MMSD defaults to "false", but you can turn it on by turning
   * "AutoCreateSMIL" to "true".
   *
   */

  smil = g_strdup ("");

  g_variant_builder_add_parsed (&options_builder, "{'Smil', <%s>}", smil);
  g_variant_builder_add_parsed (&options_builder, "{'DeliveryReport', <%b>}", request_report);

  return g_variant_builder_end (&options_builder);

}

static GVariant *
chatty_mmsd_send_mms_create_attachments (ChattyMmsd    *self,
                                         ChattyMessage *message)
{
  const char *text = chatty_message_get_text (message);
  GVariantBuilder attachment_builder;
  g_autoptr(GError) error = NULL;
  GList *files;
  int size = 0;

  g_variant_builder_init (&attachment_builder, G_VARIANT_TYPE_ARRAY);

  if (text)
    size = strlen (text);

  /* If there is text in the ChattyMessage, convert it into a file for MMSD */
  if (size > 0) {
    g_autoptr(GFile) text_file = NULL;
    GFileIOStream *iostream;

    text_file = g_file_new_tmp ("chatty-mms-text.XXXXXX.txt", &iostream, &error);
    if (error) {
      g_warning ("Error creating Temp file: %s", error->message);
      return NULL;
    }

    if (!g_file_replace_contents (text_file, text, size, NULL, FALSE,
                                  G_FILE_CREATE_NONE, NULL, NULL, &error)) {
      g_warning ("Failed to write to file %s: %s",
                 g_file_peek_path (text_file), error->message);
      return NULL;
    }

    g_variant_builder_add_parsed (&attachment_builder, "('message-contents.txt','text/plain',%s)",
                                  g_file_peek_path (text_file));
  }

  /* Get attachments to process for MMSD */
  files = chatty_message_get_files (message);

  if (files) {
    int files_count = 0;
    int total_files_count = 0;
    int image_attachments = 0;
    int video_attachments = 0;
    gsize attachments_size = size;
    gsize image_attachments_size = 0;
    gsize video_attachments_size = 0;
    gsize other_attachments_size = 0;

    if (size > 0) {
      files_count = 1;
      total_files_count = 1;
    }

    /*
     *  Figure out the total size of images (excluding gifs), videos,
     *  and any other attachments
     */
    for (GList *l = files; l != NULL; l = l->next) {
      ChattyFileInfo *attachment = l->data;
      total_files_count++;
      attachments_size = attachments_size + attachment->size;

      if (g_str_match_string ("image", attachment->mime_type, FALSE)) {
        /*
         * gifs tend to be animated, and the scaler in chatty-media does not
         * handle animated images
         */
        if (!g_str_match_string ("gif", attachment->mime_type, FALSE)) {
          image_attachments_size = image_attachments_size + attachment->size;
          image_attachments++;
        }
      } else if (g_str_match_string ("video", attachment->mime_type, FALSE)) {
        video_attachments_size = video_attachments_size + attachment->size;
        video_attachments++;
      }

      if (total_files_count > self->max_num_attach) {
        g_warning ("Total Number of attachment %d greater then maximum number of attachments %d",
                   total_files_count,
                   self->max_num_attach);
        return NULL;
      }
    }

    g_debug ("Total Number of attachments %d", total_files_count);
    other_attachments_size = attachments_size-image_attachments_size;
    if (other_attachments_size > self->max_attach_size) {
      g_warning ("Size of attachments that can't be resized %" G_GSIZE_FORMAT
                 " greater then maximum attachment size %" G_GSIZE_FORMAT,
                 other_attachments_size, self->max_attach_size);
      return NULL;
    }
    /*
     * TODO: Add support for resizing Videos.
     *       Resize Libraries for Videos: gstreamer??
     */

    /*
     * Resize images if you need to
     * Android seems to scale images based on the number of images that
     * are sent (i.e. if there are 4 images, and it has 1 Megabyte of
     * room for attachments, Android will scale it to a max of 250 Kilobytes
     * each).
     *
     * Additionally, the scaling seems to be in the resolution for images,
     * so I will scale the image based on resolution.
     *
     * For lack of a better way to do this, I am matching Android's method
     * to scale images.
     */

    /* Figure out the average attachment size needed for the image */
    if (image_attachments)
      image_attachments_size = (self->max_attach_size - other_attachments_size) / image_attachments;
    for (GList *l = files; l != NULL && image_attachments; l = l->next) {
      ChattyFileInfo *attachment = l->data;

      if (g_str_match_string ("image", attachment->mime_type, FALSE)) {
        /*
         * gifs tend to be animated, and the scaler in chatty-media does not
         * handle animated images
         */
        if (!g_str_match_string ("gif", attachment->mime_type, FALSE)) {
          if (attachment->size > image_attachments_size) {
            ChattyFileInfo *new_attachment;
            g_debug ("Total Attachment Size %" G_GSIZE_FORMAT ", Image size reduction needed: %" G_GSIZE_FORMAT,
                     attachment->size,
                     attachment->size - image_attachments_size);

            new_attachment = chatty_media_scale_image_to_size_sync (attachment,
                                                                    image_attachments_size,
                                                                    TRUE);
            if (new_attachment == NULL) {
              g_warning ("Error Resizing!");
              return NULL;
            }
            chatty_file_info_free (l->data);
            l->data = new_attachment;
          }
        }
      }
    }

    for (GList *l = files; l != NULL; l = l->next) {
      GString *attachment_name_str;
      ChattyFileInfo *attachment = l->data;
      g_autofree char *attachment_name = NULL;
      char **attachment_name_builder;
      guint attachment_name_segments;

      files_count++;

      attachment_name = g_path_get_basename (attachment->path);

      attachment_name_str = g_string_new (NULL);
      attachment_name_builder = g_strsplit (attachment_name, ".", -1);
      g_free (attachment_name);
      attachment_name = NULL;
      attachment_name_segments = g_strv_length (attachment_name_builder);
      for (int i = 0; i < (attachment_name_segments-1); i++) {
        attachment_name_str = g_string_append (attachment_name_str,
                                                      attachment_name_builder[i]);
        if (i < (attachment_name_segments-2))
          attachment_name_str = g_string_append (attachment_name_str, ".");
      }
      attachment_name_str = g_string_append (attachment_name_str,
                                                    g_strdup_printf ("-%05d",
                                                                     files_count));
      if (attachment_name_segments > 1) {
        attachment_name_str = g_string_append (attachment_name_str, ".");
        attachment_name_str = g_string_append (attachment_name_str,
                                                      attachment_name_builder[attachment_name_segments-1]);
      } else {
        attachment_name_str = g_string_prepend (attachment_name_str,
                                                       attachment_name_builder[attachment_name_segments-1]);
      }

      attachment_name = g_string_free (attachment_name_str, FALSE);
      attachment_name_str = NULL;
      g_strfreev (attachment_name_builder);
      attachment_name_builder = NULL;

      g_variant_builder_add (&attachment_builder, "(sss)",
                             attachment_name,
                             attachment->mime_type,
                             attachment->path);
    }
  }

  return g_variant_builder_end (&attachment_builder);
}

static void
chatty_mmsd_send_mms_async_cb (GObject      *service,
                               GAsyncResult *res,
                               gpointer      user_data)
{
  ChattyMmsd *self = user_data;
  g_autoptr(GVariant) ret = NULL;
  g_autoptr(GError) error = NULL;

  g_debug ("%s", __func__);

  ret = g_dbus_proxy_call_finish (self->service_proxy, res, &error);

  if (error != NULL) {
    g_warning ("Error in Proxy call: %s\n", error->message);
  }
}

gboolean
chatty_mmsd_send_mms_async (ChattyMmsd    *self,
                            ChattyChat    *chat,
                            ChattyMessage *message,
                            gpointer       user_data)
{
  GVariant *parameters, *attachments, *options;
  g_autoptr(GTask) task = user_data;
  char **send;

  attachments = chatty_mmsd_send_mms_create_attachments (self, message);
  if (attachments == NULL) {
    g_warning ("Error making attachments!\n");
    g_task_return_boolean (task, FALSE);
    return FALSE;
  }

  send = chatty_mmsd_send_mms_create_sender (chat);

  options = chatty_mmsd_send_mms_create_options ();

  /*
   * Combine the three parameters to send to mmsd
   */
  parameters = g_variant_new ("(^asv@*)",
                              send,
                              options,
                              attachments);

  g_dbus_proxy_call (self->service_proxy,
                     "SendMessage",
                     parameters,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     (GAsyncReadyCallback)chatty_mmsd_send_mms_async_cb,
                     self);

  g_strfreev (send);

  g_task_return_boolean (task, TRUE);
  return TRUE;
}

/*
 * Until chatty has support for inline attachments, this present URI links for
 * all attachments along with the Message and Subject.
 */
static char *
chatty_mmsd_process_mms_message_attachments (GList **filesp)
{
  GString *message_contents;
  gboolean content_set = FALSE;

  message_contents = g_string_new (NULL);

  for (GList *l = *filesp; l != NULL;) {
    ChattyFileInfo *attachment = l->data;

    l = l->next;
    if (g_strcmp0(attachment->mime_type, "application/smil") == 0) {
      *filesp = g_list_remove (*filesp, attachment);
      if (attachment->url) {
        g_autoptr(GFile) file = NULL;
        g_autoptr(GError) error = NULL;

        file = g_file_new_for_uri (attachment->url);
        g_file_delete (file, NULL, &error);

        if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
          g_warning ("Deleting file failed: %s", error->message);
      }
      chatty_file_info_free (attachment);

      continue;
    }

    /* If an MMS has a message, it tends to be the first text/plain attachment */
    if (!content_set && g_str_match_string ("text/plain", attachment->mime_type, TRUE)) {
      g_autoptr(GFile) text_file = NULL;
      g_autofree char *contents = NULL;
      g_autoptr(GError) error = NULL;
      gsize length;

      text_file = g_file_new_build_filename (g_get_user_data_dir (),
                                             "chatty", attachment->path, NULL);
      g_file_load_contents (text_file,
                            NULL,
                            &contents,
                            &length,
                            NULL,
                            &error);
      if (error) {
        g_warning ("error opening file: %s", error->message);
        break;
      }

      if (contents && *contents)
        g_string_append (message_contents, contents);
      content_set = TRUE;

      /* We don't want the message content to be saved as a file */
      *filesp = g_list_remove (*filesp, attachment);
      if (attachment->url) {
        g_autoptr(GFile) file = NULL;

        file = g_file_new_for_uri (attachment->url);
        g_file_delete (file, NULL, &error);

        if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
          g_warning ("Deleting file failed: %s", error->message);
      }
      chatty_file_info_free (attachment);
    }
  }

  if (message_contents->len)
    return g_string_free (message_contents, FALSE);

  return NULL;
}

static mms_payload *
chatty_mmsd_receive_message (ChattyMmsd *self,
                             GVariant   *message_t)
{
  g_autoptr(GFile) parent = NULL;
  g_autoptr(GFile) savepath = NULL;
  g_autoptr(GVariant) recipients = NULL;
  g_autoptr(GVariant) attachments = NULL;
  g_autoptr(GDateTime) date_time = NULL;
  ChattyMsgDirection direction = CHATTY_DIRECTION_UNKNOWN;
  ChattyMsgStatus mms_status = CHATTY_STATUS_UNKNOWN;
  ChattyMsgType chatty_msg_type = CHATTY_MESSAGE_TEXT;
  g_autoptr(GVariant) properties = NULL;
  g_autoptr(GFileInfo) attachment_info = NULL;
  GVariant *reciever, *attach;
  GVariantDict dict;
  GVariantIter iter;
  GFile *container = NULL;
  GList *files = NULL;
  g_autofree char *objectpath = NULL;
  g_autofree char *date = NULL;
  g_autofree char *sender = NULL;
  g_autofree char *smil = NULL;
  g_autofree char *subject = NULL;
  g_autofree char *status = NULL;
  g_autofree char *rx_modem_number = NULL;
  g_autofree char *mms_message = NULL;
  g_autofree char *contents = NULL;
  g_autofree char *expire_time_string = NULL;
  GString *who;
  GVariantIter recipientiter;
  mms_payload *payload;
  gint64 unix_time = 0;
  int delivery_report = FALSE;

  parent = g_file_new_build_filename (g_get_user_data_dir (),
                                      "chatty", NULL);


  /* Parse through the MMS Payload. mmsd-tng has a parser, so we are using that */
  g_variant_get (message_t, "(o@a{?*})", &objectpath, &properties);

  g_variant_dict_init (&dict, properties);
  g_variant_dict_lookup (&dict, "Date", "s", &date);
  g_variant_dict_lookup (&dict, "Sender", "s", &sender);
  g_variant_dict_lookup (&dict, "Smil", "s", &smil);
  g_variant_dict_lookup (&dict, "Subject", "s", &subject);
  g_variant_dict_lookup (&dict, "Delivery Report", "b", &delivery_report);
  /* refer to mms_message_status_get_string () in MMSD mmsutil.c for a listing of statuses */
  g_variant_dict_lookup (&dict, "Status", "s", &status);
  g_variant_dict_lookup (&dict, "Modem Number", "s", &rx_modem_number);

  /* Android seems to put this on the subject */
  if (g_strcmp0 ("NoSubject", subject) == 0)
    g_clear_pointer (&subject, g_free);
  else if (subject && !*subject)
    g_clear_pointer (&subject, g_free);

  /* Determine what type of MMS we have */
  if (g_strcmp0 (status, "draft") == 0) {
    direction = CHATTY_DIRECTION_OUT;
    mms_status = CHATTY_STATUS_SENDING;
  } else if (g_strcmp0 (status, "sent") == 0) {
    direction = CHATTY_DIRECTION_OUT;
    mms_status = CHATTY_STATUS_SENT;
  } else if (g_strcmp0 (status, "delivered") == 0) {
    direction = CHATTY_DIRECTION_OUT;
    mms_status = CHATTY_STATUS_DELIVERED;
  } else if (g_strcmp0 (status, "received") == 0) {
    direction = CHATTY_DIRECTION_IN;
    mms_status = CHATTY_STATUS_RECEIVED;
  } else if (g_strcmp0 (status, "downloaded") == 0) {
    /* This is an internal mmsd-tng state, and shouldn't be shown */
    return NULL;
  } else if (g_strcmp0 (status, "read") == 0) {
    /*
     * This doesn't really mean anything except that some
     * program marked the MMS as "read". I will just process
     * it like a "received" message.
     */
    direction = CHATTY_DIRECTION_IN;
    mms_status = CHATTY_STATUS_RECEIVED;
  } else if (g_strcmp0 (status, "expired") == 0) {
    g_autoptr(GDateTime) expire_time = NULL;
    g_autofree char *expire_date = NULL;
    direction = CHATTY_DIRECTION_IN;
    mms_status = CHATTY_STATUS_RECEIVED;

    g_variant_dict_lookup (&dict, "Expire", "s", &expire_date);

    expire_time = g_date_time_new_from_iso8601 (expire_date, NULL);
    if (!expire_time)
      expire_time = g_date_time_new_now_local ();

  /* TRANSLATORS: Timestamp for minute accuracy, e.g. “2020-08-11 15:27”.
     See https://developer.gnome.org/glib/stable/glib-GDateTime.html#g-date-time-format
   */
    expire_time_string = g_date_time_format (expire_time, _("%Y-%m-%d %H∶%M"));
  } else {
    /* This is a state Chatty cannot support yet */
    return NULL;
  }

  payload = g_try_new0 (mms_payload, 1);
  payload->delivery_report = delivery_report;
  payload->objectpath = g_strdup (objectpath);
  payload->mmsd_message_proxy_watch_id = 0;

  if (delivery_report) {
    g_autofree char *delivery_status = NULL;

    if (!g_variant_dict_lookup (&dict, "Delivery Status", "s", &delivery_status)) {
      g_warning ("Something wrong happened with getting delivery status!");
      payload->delivery_report = FALSE;
    } else {
      chatty_mmsd_process_delivery_status (delivery_status);
    }
  }

  /* Fill out Sender and All Numbers */
  if (direction == CHATTY_DIRECTION_IN) {
    const char *country_code = chatty_settings_get_country_iso_code (chatty_settings_get_default ());

    payload->sender = chatty_utils_check_phonenumber (sender, country_code);
    if (payload->sender == NULL)
      payload->sender = g_strdup (sender);
  } else {
    payload->sender = g_strdup (self->modem_number);
  }

  recipients = g_variant_dict_lookup_value (&dict, "Recipients", G_VARIANT_TYPE_STRING_ARRAY);

  if (rx_modem_number && *rx_modem_number) {
    if (g_strcmp0 (self->modem_number, rx_modem_number) != 0) {
      g_warning ("Receieved Modem Number %s different than current modem number %s",
                 self->modem_number, rx_modem_number);
      return NULL;
    }
  }

  if (direction == CHATTY_DIRECTION_IN) {
    who = g_string_new (payload->sender);
  } else {
    who = g_string_new (NULL);
  }
  if (recipients)
    g_variant_iter_init (&recipientiter, recipients);

  while (recipients && (reciever = g_variant_iter_next_value (&recipientiter))) {
    g_autofree char *temp = NULL;
    g_autofree char *temp2 = NULL;
    const char *country_code = chatty_settings_get_country_iso_code (chatty_settings_get_default ());

    g_variant_get (reciever, "s", &temp2);
    temp = chatty_utils_check_phonenumber (temp2, country_code);
    if (temp == NULL)
      temp = g_strdup (temp2);
    if (g_strcmp0 (self->modem_number, temp) != 0) {
      if (who->len > 0) {
        who = g_string_append (who, ",");
      }
      who = g_string_append (who, temp);
      g_variant_unref (reciever);
    }
  }
  if (!who->len)
    who = g_string_append (who, self->modem_number);

  payload->chat = g_string_free (who, FALSE);

  /* Go through the attachments */
  attachments = g_variant_dict_lookup_value (&dict, "Attachments", G_VARIANT_TYPE_ARRAY);
  if (attachments)
    g_variant_iter_init (&iter, attachments);

  while (attachments && (attach = g_variant_iter_next_value (&iter))) {
    ChattyFileInfo *attachment = NULL;
    GFile *new;
    GFileOutputStream *out;
    g_autofree char *containerpath = NULL;
    g_autofree char *filenode = NULL;
    g_autofree char *filename = NULL;
    g_autofree char *mimetype = NULL;
    gulong size, data;
    gsize length, written = 0;
    g_autoptr(GError) error = NULL;

    attachment = g_try_new0 (ChattyFileInfo, 1);
    g_variant_get (attach, "(ssstt)", &filenode,
                   &mimetype,
                   &containerpath,
                   &data,
                   &size);
    g_variant_unref (attach);

    if (!container) {
      g_autofree char *tag = NULL;
      g_autofree char *uid = NULL;

      uid = g_path_get_basename (objectpath);
      container = g_file_new_for_path (containerpath);
      g_file_load_contents (container,
                            NULL,
                            &contents,
                            &length,
                            NULL,
                            &error);

      if (error && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
        g_debug ("MMS Payload does not exist, deleting...");
        chatty_mmsd_delete_mms (self, payload->objectpath);
        return NULL;
      } else if (error) {
        g_warning ("Error loading MMSD Payload: %s", error->message);
        return NULL;
      }

      tag = g_strconcat (date, payload->sender, uid, NULL);
      /* Save MMS in $XDG_DATA_HOME/chatty/mms/ */
      savepath = g_file_new_build_filename (g_get_user_data_dir (),
                                            "chatty", "mms", tag, NULL);
      if (!g_file_make_directory_with_parents (savepath, NULL, &error)) {
        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
          g_debug ("Directory exists, skipping error...");
          g_clear_error (&error);
        } else if (error) {
          g_warning ("Error creating Directory: %s", error->message);
          return NULL;
        }
      }
      /* create a file containing the smil */
      if (smil != NULL) {
        size_t smil_size = strlen (smil);

        new = g_file_get_child (savepath, "mms.smil");
        out = g_file_create (new, G_FILE_CREATE_PRIVATE, NULL, &error);
        if (error) {
          if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
            g_autoptr(GFileInfo) file_info = NULL;
            g_debug ("%s Exists, Skipping Error....", g_file_peek_path (new));
            g_clear_error (&error);
            file_info = g_file_query_info (new,
                                           G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                           G_FILE_QUERY_INFO_NONE,
                                           NULL,
                                           &error);
            if (error)
              g_warning ("Error getting file info: %s", error->message);
            else
              written = g_file_info_get_size (file_info);

          } else {
            g_warning ("Failed to create %s: %s",
                       g_file_peek_path (new), error->message);
          }
          g_clear_error (&error);
        } else {
          if (!g_output_stream_write_all ((GOutputStream *) out,
                                          smil,
                                          smil_size,
                                          &written,
                                          NULL,
                                          &error)) {
            g_warning ("Failed to write to file %s: %s",
                       g_file_peek_path (new), error->message);
            g_clear_error (&error);
          }
        }
        if (out)
          g_output_stream_close (G_OUTPUT_STREAM (out), NULL, NULL);

        attachment->file_name = g_strdup ("mms.smil");
        attachment->mime_type = g_strdup ("application/smil");
        attachment->size      = written;
        attachment->path      = g_file_get_relative_path (parent, new);
        attachment->url       = g_file_get_uri (new);
        attachment->status    = CHATTY_FILE_DOWNLOADED;

        files = g_list_append (files, attachment);
        attachment = NULL;
        attachment = g_try_new0 (ChattyFileInfo, 1);
        g_clear_object (&new);
      }
    }
    filename = g_strdup (filenode);
    g_strdelimit (filename, "<>", ' ');
    g_strstrip (filename);
    g_strdelimit (filename, " ", '_');
    filename = g_path_get_basename (filename);
    new = g_file_get_child (savepath, filename);
    out = g_file_create (new, G_FILE_CREATE_PRIVATE, NULL, &error);
    if (error) {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
        g_autoptr(GFileInfo) file_info = NULL;
        g_debug ("%s Exists, Skipping Error....", g_file_peek_path (new));
        g_clear_error (&error);
        file_info = g_file_query_info (new,
                                       G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                       G_FILE_QUERY_INFO_NONE,
                                       NULL,
                                       &error);
        if (error)
          g_warning ("Error getting file info: %s", error->message);
        else
          written = g_file_info_get_size (file_info);
      } else {
        g_warning ("Failed to create %s: %s",
                   g_file_peek_path (new), error->message);
      }
      g_clear_error (&error);
    } else {
      if (!g_output_stream_write_all (G_OUTPUT_STREAM (out),
                                      contents + data,
                                      size,
                                      &written,
                                      NULL,
                                      &error)) {
        g_warning ("Failed to write to file %s: %s",
                   g_file_peek_path (new), error->message);
        g_clear_error (&error);
      }
    }
    if (out)
      g_output_stream_close (G_OUTPUT_STREAM (out), NULL, NULL);

    attachment_info = g_file_query_info (new,
                                         G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                                         G_FILE_QUERY_INFO_NONE,
                                         NULL,
                                         &error);

    if (error != NULL) {
      g_clear_error (&error);
      attachment->mime_type = g_strdup (mimetype);
    } else if (g_file_info_get_content_type (attachment_info) == NULL) {
      /* If we can't figure out content type, do not trust what the MMS tells it is */
      attachment->mime_type = g_strdup ("application/octet-stream");
    } else {
      attachment->mime_type = g_content_type_get_mime_type (g_file_info_get_content_type (attachment_info));
      if (attachment->mime_type == NULL) {
        if (g_file_info_get_content_type (attachment_info) != NULL)
          attachment->mime_type = g_strdup (g_file_info_get_content_type (attachment_info));
        else
          attachment->mime_type = g_strdup ("application/octet-stream");
      }
    }

    attachment->file_name = g_strdup (filename);
    attachment->size      = written;
    attachment->path      = g_file_get_relative_path (parent, new);
    attachment->url       = g_file_get_uri (new);
    attachment->status    = CHATTY_FILE_DOWNLOADED;

    files = g_list_prepend (files, attachment);
    attachment = NULL;
    g_clear_object (&new);
  }
  if (attachments)
    g_object_unref (container);

  mms_message = chatty_mmsd_process_mms_message_attachments (&files);

  if ((!files || !files->data) && savepath) {
    g_autoptr(GError) error = NULL;

    g_file_delete (savepath, NULL, &error);

    if (error)
      g_warning ("Error deleting empty MMS directory: %s", error->message);
  }

  if (!subject && !mms_message && files && g_list_length (files) == 1) {
    ChattyFileInfo *attachment = files->data;

    if (attachment && attachment->mime_type &&
        g_str_has_prefix (attachment->mime_type, "image"))
      chatty_msg_type = CHATTY_MESSAGE_IMAGE;
  }

  if (!mms_message && !files) {
    if (g_strcmp0 (status, "expired") == 0)
      mms_message = g_strdup_printf (_("You received an MMS, but it expired on: %s"),
                                     expire_time_string);
    else
      mms_message = g_strdup (_("You received an empty MMS."));
  }

  date_time = g_date_time_new_from_iso8601 (date, NULL);
  if (date_time)
    unix_time = g_date_time_to_unix (date_time);
  if (!unix_time)
    unix_time = time (NULL);

  {
    g_autofree char *basename = NULL;

    basename = g_path_get_basename (objectpath);
    payload->message = chatty_message_new (NULL, mms_message, basename, unix_time,
                                           chatty_msg_type, direction, mms_status);
    chatty_message_set_subject (payload->message, subject);
    chatty_message_set_id (payload->message, basename);
    chatty_message_set_files (payload->message, files);
  }

  return payload;
}

static void
chatty_mmsd_get_new_mms_cb (ChattyMmsd *self,
                            GVariant   *parameters)
{
  mms_payload *payload;

  g_debug ("%s", __func__);
  payload = chatty_mmsd_receive_message (self, parameters);
  if (payload == NULL) {
    g_autoptr(GVariant) properties = NULL;
    g_autofree char *objectpath = NULL;

    g_variant_get (parameters, "(o@a{?*})", &objectpath, &properties);
    g_warning ("There was an error with decoding the MMS %s",
               objectpath);
  } else {
    if (!g_hash_table_insert (self->mms_hash_table, g_strdup (payload->objectpath), payload)) {
      g_warning ("g_hash_table:MMS Already exists! This should not happen");
    }
    chatty_mmsd_process_mms (self, payload);
  }
}

static void
chatty_mmsd_get_all_mms_cb (GObject      *service,
                            GAsyncResult *res,
                            gpointer      user_data)
{
  ChattyMmsd *self = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) ret = NULL;

  g_debug ("%s", __func__);

  ret = g_dbus_proxy_call_finish (self->service_proxy,
                                  res,
                                  &error);

  if (error != NULL) {
    g_warning ("Error in Proxy call: %s\n", error->message);
  } else {
    g_autoptr(GVariant) msg_pack = g_variant_get_child_value (ret, 0);
    GVariantIter iter;
    gulong num;

    if ((num = g_variant_iter_init (&iter, msg_pack))) {
      GVariant *message_t;
      g_debug ("Have %lu MMS message (s) to process", num);

      while ((message_t = g_variant_iter_next_value (&iter))) {
        g_autofree char *objectpath = NULL;
        mms_payload *payload;

        g_variant_get (message_t, "(o@a{?*})", &objectpath, NULL);
        if (g_hash_table_lookup (self->mms_hash_table, objectpath) != NULL) {
          g_debug ("MMS Already exists! skipping...");
          g_variant_unref (message_t);
          continue;
        }
        payload = chatty_mmsd_receive_message (self, message_t);
        g_variant_unref (message_t);
        if (payload == NULL) {
          g_warning ("There was an error with decoding the MMS Payload!");
        } else {
          if (!g_hash_table_insert (self->mms_hash_table, g_strdup (objectpath), payload)) {
            g_warning ("g_hash_table:MMS Already exists! This should not happe");
          }
          chatty_mmsd_process_mms (self, payload);
        }
      }
    } else {
      g_debug ("Have 0 MMS messages to process");
    }
  }
}

static void
mmsd_update_settings_cb (GTask        *task,
                         gpointer      source_object,
                         gpointer      task_data,
                         GCancellable *cancellable)
{
  ChattyMmsd *self = source_object;
  GDBusProxy *mm_proxy, *service_proxy;
  g_autoptr(GError) error = NULL;
  GObject *obj;
  char *apn, *mmsc, *proxy;
  gboolean use_smil;

  obj = G_OBJECT (task);
  apn = g_object_steal_data (obj, "apn");
  mmsc = g_object_steal_data (obj, "mmsc");
  proxy = g_object_steal_data (obj, "proxy");
  mm_proxy = g_object_get_data (obj, "mm-proxy");
  service_proxy = g_object_get_data (obj, "service-proxy");
  use_smil = GPOINTER_TO_INT (g_object_get_data (obj, "smil"));

  g_assert (mm_proxy);
  g_assert (service_proxy);

  if (mmsc) {
    g_autoptr(GVariant) ret = NULL;

    ret = g_dbus_proxy_call_sync (mm_proxy,
                                  "ChangeSettings",
                                  g_variant_new_parsed ("('CarrierMMSC', <%s>)", mmsc),
                                  G_DBUS_CALL_FLAGS_NONE,
                                  -1,
                                  cancellable, &error);
    g_debug ("Changing mmsc to '%s' %s", mmsc, CHATTY_LOG_SUCESS (!error));
  }

  if (!error && apn) {
    g_autoptr(GVariant) ret = NULL;

    ret = g_dbus_proxy_call_sync (mm_proxy,
                                  "ChangeSettings",
                                  g_variant_new_parsed ("('MMS_APN', <%s>)", apn),
                                  G_DBUS_CALL_FLAGS_NONE,
                                  -1,
                                  cancellable, &error);
    g_debug ("Changing apn to '%s' %s", apn, CHATTY_LOG_SUCESS (!error));
  }

  if (!error && proxy) {
    g_autoptr(GVariant) ret = NULL;

    ret = g_dbus_proxy_call_sync (mm_proxy,
                                  "ChangeSettings",
                                  g_variant_new_parsed ("('CarrierMMSProxy', <%s>)", *proxy ? proxy : "NULL"),
                                  G_DBUS_CALL_FLAGS_NONE,
                                  -1,
                                  cancellable, &error);
    g_debug ("Changing proxy to '%s' %s", proxy, CHATTY_LOG_SUCESS (!error));
  }

  if (use_smil != self->auto_create_smil) {
    g_autoptr(GVariant) ret = NULL;

    ret = g_dbus_proxy_call_sync (service_proxy,
                                  "SetProperty",
                                  g_variant_new_parsed ("('AutoCreateSMIL', <%b>)", use_smil),
                                  G_DBUS_CALL_FLAGS_NONE,
                                  -1,
                                  cancellable, &error);
    g_debug ("Changing AutoCreateSMIL to %d %s", use_smil, CHATTY_LOG_SUCESS (!error));
  }

  if (error) {
    g_task_return_error (task, g_steal_pointer (&error));
  } else {
    if (apn)
      g_atomic_pointer_set (&self->mms_apn, apn);
    if (mmsc)
      g_atomic_pointer_set (&self->carrier_mmsc, mmsc);
    if (proxy)
      g_atomic_pointer_set (&self->carrier_proxy, proxy);
    g_atomic_int_set (&self->auto_create_smil, use_smil);

    g_task_return_boolean (task, TRUE);
  }
}

static void
chatty_mmsd_get_message_queue_cb (GObject      *service,
                                  GAsyncResult *res,
                                  gpointer      user_data)
{
  ChattyMmsd *self = user_data;
  g_autoptr(GVariant) ret = NULL;
  g_autoptr(GError) error = NULL;

  g_debug ("%s", __func__);

  ret = g_dbus_proxy_call_finish (self->modemmanager_proxy, res, &error);

  if (error != NULL) {
    g_warning ("Error syncing messages: %s", error->message);
  } else {
    g_debug ("Finished syncing messages!");
  }
}

static void
chatty_mmsd_settings_signal_changed_cb (ChattyMmsd *self,
                                        GVariant   *parameters)
{
  char *apn, *mmsc, *proxy;

  g_variant_get (parameters, "(sss)", &apn, &mmsc, &proxy);
  g_debug("Settings Changed: apn %s, mmsc %s, proxy %s", apn, mmsc, proxy);

  g_free (self->mms_apn);
  self->mms_apn = g_strdup(apn);
  g_free (self->carrier_mmsc);
  self->carrier_mmsc = g_strdup(mmsc);
  g_free (self->carrier_proxy);
  if (g_strcmp0 (proxy, "NULL") == 0)
    self->carrier_proxy = g_strdup ("");
  else
    self->carrier_proxy = g_strdup(proxy);
}


static void
chatty_mmsd_get_mmsd_service_settings_cb (GObject      *service,
                                          GAsyncResult *res,
                                          gpointer      user_data)
{
  ChattyMmsd *self = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) ret = NULL;

  g_debug ("%s", __func__);

  ret = g_dbus_proxy_call_finish (self->service_proxy,
                                  res,
                                  &error);

  if (error != NULL) {
    g_warning ("Error in Proxy call: %s\n", error->message);
  } else {
    g_autoptr(GVariant) all_settings = NULL;
    GVariantDict dict;
    int max_attach_total_size;
    int max_attachments;
    gboolean autocreatesmil;

    g_variant_get (ret, "(@a{?*})", &all_settings);
    g_variant_dict_init (&dict, all_settings);

    if (g_variant_dict_lookup (&dict, "TotalMaxAttachmentSize", "i", &max_attach_total_size))
      self->max_attach_size = (gsize) max_attach_total_size;
    else
      self->max_attach_size = DEFAULT_MAXIMUM_ATTACHMENT_SIZE;

    g_debug ("TotalMaxAttachmentSize is set to %" G_GSIZE_FORMAT, self->max_attach_size);

    if (g_variant_dict_lookup (&dict, "MaxAttachments", "i", &max_attachments))
      self->max_num_attach = max_attachments;
    else
      self->max_num_attach = DEFAULT_MAXIMUM_ATTACHMENTS;

    g_debug ("MaxAttachments is set to %d", self->max_num_attach);

    if (g_variant_dict_lookup (&dict, "AutoCreateSMIL", "b", &autocreatesmil))
      self->auto_create_smil = autocreatesmil;

    g_debug ("AutoCreateSMIL is set to %d", self->auto_create_smil);
  }
}

static void
chatty_mmsd_get_service_cb (GObject      *service,
                            GAsyncResult *res,
                            gpointer      user_data)
{
  ChattyMmsd *self = user_data;
  g_autoptr(GError) error = NULL;

  self->service_proxy = g_dbus_proxy_new_finish (res, &error);
  if (error != NULL) {
    g_warning ("Error in MMSD Service Proxy call: %s\n", error->message);
  } else {
    g_debug ("Got MMSD Service");

    self->is_ready = TRUE;
    g_object_notify (G_OBJECT (self->mm_account), "status");

    g_dbus_proxy_call (self->service_proxy,
                       "GetMessages",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       (GAsyncReadyCallback)chatty_mmsd_get_all_mms_cb,
                       self);

    g_dbus_proxy_call (self->service_proxy,
                       "GetProperties",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       (GAsyncReadyCallback)chatty_mmsd_get_mmsd_service_settings_cb,
                       self);
  }
}

static void mmsd_vanished_cb (GDBusConnection *connection,
                              const char     *name,
                              gpointer         user_data);

static void
clear_chatty_mmsd (ChattyMmsd *self)
{
  self->is_ready = FALSE;
  g_clear_object (&self->mm_device);
  g_clear_pointer (&self->modem_number, g_free);
  g_clear_pointer (&self->carrier_mmsc, g_free);
  g_clear_pointer (&self->mms_apn, g_free);
  g_clear_pointer (&self->carrier_proxy, g_free);

  if (G_IS_DBUS_CONNECTION (self->connection)) {
    g_debug ("Removing any active MMSD connections");
    mmsd_vanished_cb (self->connection, MMSD_SERVICE, self);
  }

  g_clear_handle_id (&self->mmsd_watch_id, g_bus_unwatch_name);
}

static void
chatty_mmsd_connect_to_service (ChattyMmsd *self,
                                GVariant   *service)
{
  g_autofree char *servicepath = NULL;
  g_autofree char *serviceidentity = NULL;
  g_autoptr(GVariant) properties = NULL;
  GVariantDict dict;

  /*
   * MMSD only supports one modem at this time, and which modem it connects to
   * can be set through the dbus if desired by the "default_modem_number" setting.
   * If "default_modem_number" is set to NULL, mmsd will connect to the
   * first modem it connects to, which is fine for devices with only
   * one modem (which most of them are anyways).
   */

  if (self->default_modem_number != NULL) {
    if (g_strcmp0 (self->default_modem_number, self->modem_number) != 0) {
      g_warning ("Modem Number does not match MMSD Modem Number! Aborting");
      clear_chatty_mmsd (self);
      return;
    } else {
      g_debug ("Modem Number matches MMSD Modem Number!");
    }
  } else {
    g_debug ("No default MMSD Modem Number Set");
  }

  g_variant_get (service, "(o@a{?*})", &servicepath, &properties);
  g_debug ("Service Path: %s", servicepath);

  g_variant_dict_init (&dict, properties);
  if (!g_variant_dict_lookup (&dict, "Identity", "s", &serviceidentity)) {
    g_warning ("Could not get Service Identity!");
    serviceidentity = NULL;
    return;
  }
  g_debug ("Identity: %s", serviceidentity);
  if (g_strcmp0 (servicepath, MMSD_MODEMMANAGER_PATH) == 0)
    g_dbus_proxy_new (self->connection,
                      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                      NULL,
                      MMSD_SERVICE,
                      servicepath,
                      MMSD_SERVICE_INTERFACE,
                      NULL,
                      chatty_mmsd_get_service_cb,
                      self);
}

static void
chatty_mmsd_service_added_cb (ChattyMmsd *self,
                              GVariant   *parameters)
{
  g_autofree char *param = NULL;

  param = g_variant_print (parameters, TRUE);
  CHATTY_DEBUG_MSG ("Service Added g_variant: %s", param);

  chatty_mmsd_connect_to_service (self, parameters);
}

static void
chatty_mmsd_service_removed_cb (ChattyMmsd *self,
                                GVariant   *parameters)
{
  g_autofree char *param = NULL;

  param = g_variant_print (parameters, TRUE);
  CHATTY_DEBUG_MSG ("Service Removed g_variant: %s", param);

  g_clear_object (&self->service_proxy);
}

static void
chatty_mmsd_get_manager_cb (GObject      *manager,
                            GAsyncResult *res,
                            gpointer      user_data)
{
  ChattyMmsd *self = user_data;
  g_autoptr(GError) error = NULL;

  self->manager_proxy = g_dbus_proxy_new_finish (res, &error);
  if (error != NULL) {
    g_warning ("Error in MMSD Manager Proxy call: %s\n", error->message);
  } else {
    g_autoptr(GVariant) service_pack = NULL;
    g_autoptr(GVariant) all_services = NULL;
    GVariantIter iter;
    gulong num;
    g_debug ("Got MMSD Manager");

    all_services = g_dbus_proxy_call_sync (self->manager_proxy,
                                           "GetServices",
                                           NULL,
                                           G_DBUS_CALL_FLAGS_NONE,
                                           -1,
                                           NULL,
                                           NULL);

    service_pack = g_variant_get_child_value (all_services, 0);
    if ((num = g_variant_iter_init (&iter, service_pack))) {
      GVariant *service;

      while ((service = g_variant_iter_next_value (&iter))) {
        chatty_mmsd_connect_to_service (self, service);
        g_variant_unref (service);
      }
    }
  }
}

static void
chatty_mmsd_get_mmsd_modemmanager_settings_cb (GObject      *service,
                                               GAsyncResult *res,
                                               gpointer      user_data)
{
  ChattyMmsd *self = user_data;
  g_autoptr(GVariant) ret = NULL;
  g_autoptr(GError) error = NULL;

  g_debug ("%s", __func__);

  ret = g_dbus_proxy_call_finish (self->modemmanager_proxy,
                                  res,
                                  &error);

  if (error != NULL) {
    g_warning ("Error in Proxy call: %s\n", error->message);
  } else {
    g_autoptr(GVariant) all_settings = NULL;
    GVariantDict dict;
    int auto_process_on_connection, autoprocess_sms_wap;

    g_variant_get (ret, "(@a{?*})", &all_settings);

    g_variant_dict_init (&dict, all_settings);
    mmsd_set_value (self, &dict, "CarrierMMSC", &self->carrier_mmsc);
    mmsd_set_value (self, &dict, "MMS_APN", &self->mms_apn);
    mmsd_set_value (self, &dict, "CarrierMMSProxy", &self->carrier_proxy);
    mmsd_set_value (self, &dict, "default_modem_number", &self->default_modem_number);

    /*
     * MMSD will automatically manage sending/recieving MMSes
     * This is a lot easier to let MMSD manage
     */
    if (g_variant_dict_lookup (&dict, "AutoProcessOnConnection", "b", &auto_process_on_connection)) {
      if (auto_process_on_connection == FALSE) {
        g_debug ("AutoProcessOnConnection is set to false! Changing to True!");
        g_dbus_proxy_call_sync (self->modemmanager_proxy,
                                "ChangeSettings",
                                g_variant_new_parsed ("('AutoProcessOnConnection', <%b>)", TRUE),
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                NULL,
                                NULL);
      } else {
        g_debug ("AutoProcessOnConnection is set to True!");
      }
    }
    /*
     * MMSD will automatically manage SMS WAPs
     * This is a lot easier to let MMSD manage
     */
    if (g_variant_dict_lookup (&dict, "AutoProcessSMSWAP", "b", &autoprocess_sms_wap)) {
      if (autoprocess_sms_wap == FALSE) {
        g_debug ("AutoProcessSMSWAP is set to false! Changing to True!");
        g_dbus_proxy_call_sync (self->modemmanager_proxy,
                                "ChangeSettings",
                                g_variant_new_parsed ("('AutoProcessSMSWAP', <%b>)", TRUE),
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                NULL,
                                NULL);
      } else {
        g_debug ("AutoProcessSMSWAP is set to True!");
      }
    }
  }

  g_dbus_proxy_new (self->connection,
                    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                    NULL,
                    MMSD_SERVICE,
                    MMSD_PATH,
                    MMSD_MANAGER_INTERFACE,
                    NULL,
                    chatty_mmsd_get_manager_cb,
                    self);
}

/*
 * TODO: Is there anything we want to do with chatty_mmsd_bearer_handler_error_cb ()?
 *       This may be useful for user feedback.
 */
static void
chatty_mmsd_bearer_handler_error_cb (ChattyMmsd *self,
                                     GVariant   *parameters)
{
  guint error;

  g_variant_get (parameters, "(h)", &error);
  switch (error) {
  case MMSD_MM_MODEM_MMSC_MISCONFIGURED:
    g_warning ("Bearer Handler emitted an error, the mmsc is not configured right.");
    break;
  case MMSD_MM_MODEM_INCORRECT_APN_CONNECTED:
    g_warning ("Bearer Handler emitted an error, the APN set in mmsd's settings does not match any connected APNs");
    break;
  case MMSD_MM_MODEM_NO_BEARERS_ACTIVE:
    g_warning ("Bearer Handler emitted an error, no bearers are active");
    break;
  case MMSD_MM_MODEM_INTERFACE_DISCONNECTED:
    g_debug ("Bearer Handler emitted an error, the MMS bearer is disconnected");
    break;
  default:
    g_debug ("Bearer Handler emitted an error, but Chatty does not know how to handle it");
    break;
  }
}


static void
chatty_mmsd_get_modemmanager_cb (GObject      *simple,
                                 GAsyncResult *res,
                                 gpointer      user_data)
{
  ChattyMmsd *self = user_data;
  g_autoptr(GError) error = NULL;

  self->modemmanager_proxy = g_dbus_proxy_new_finish (res, &error);

  if (error != NULL) {
    g_warning ("Error in MMSD Modem Manager Proxy call: %s\n", error->message);
  } else {
    g_dbus_proxy_call (self->modemmanager_proxy,
                       "ViewSettings",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       (GAsyncReadyCallback)chatty_mmsd_get_mmsd_modemmanager_settings_cb,
                       self);
  }
}

static void
chatty_mmsd_signal_emitted_cb (GDBusConnection *connection,
                               const char      *sender_name,
                               const char      *object_path,
                               const char      *interface_name,
                               const char      *signal_name,
                               GVariant        *parameters,
                               gpointer         user_data)
{
  ChattyMmsd *self = user_data;

  g_assert (G_IS_DBUS_CONNECTION (connection));

  if (g_strcmp0 (signal_name, "BearerHandlerError") == 0 &&
      g_strcmp0 (interface_name, MMSD_MODEMMANAGER_INTERFACE) == 0 &&
      g_strcmp0 (object_path, MMSD_PATH) == 0)
    chatty_mmsd_bearer_handler_error_cb (self, parameters);
  else if (g_strcmp0 (signal_name, "ServiceAdded") == 0 &&
           g_strcmp0 (interface_name, MMSD_MANAGER_INTERFACE) == 0 &&
           g_strcmp0 (object_path, MMSD_PATH) == 0)
    chatty_mmsd_service_added_cb (self, parameters);
  else if (g_strcmp0 (signal_name, "ServiceRemoved") == 0 &&
           g_strcmp0 (interface_name, MMSD_MANAGER_INTERFACE) == 0 &&
           g_strcmp0 (object_path, MMSD_PATH) == 0)
    chatty_mmsd_service_removed_cb (self, parameters);
  else if (g_strcmp0 (signal_name, "MessageAdded") == 0 &&
           g_strcmp0 (interface_name, MMSD_SERVICE_INTERFACE) == 0 &&
           g_strcmp0 (object_path, MMSD_MODEMMANAGER_PATH) == 0)
    chatty_mmsd_get_new_mms_cb (self, parameters);
  else if (g_strcmp0 (signal_name, "SettingsChanged") == 0 &&
           g_strcmp0 (interface_name, MMSD_MODEMMANAGER_INTERFACE) == 0 &&
           g_strcmp0 (object_path, MMSD_PATH) == 0)
    chatty_mmsd_settings_signal_changed_cb (self, parameters);
}

static void
mmsd_appeared_cb (GDBusConnection *connection,
                  const char      *name,
                  const char      *name_owner,
                  gpointer         user_data)
{
  ChattyMmsd *self = user_data;
  g_assert (G_IS_DBUS_CONNECTION (connection));
  self->connection = connection;
  g_debug ("MMSD appeared");

  self->mmsd_signal_id =
    g_dbus_connection_signal_subscribe (self->connection,
                                        MMSD_SERVICE,
                                        NULL, NULL, NULL, NULL, /* Match everything */
                                        G_DBUS_SIGNAL_FLAGS_NONE,
                                        chatty_mmsd_signal_emitted_cb,
                                        self,
                                        NULL);

  g_dbus_proxy_new (self->connection,
                    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                    NULL,
                    MMSD_SERVICE,
                    MMSD_PATH,
                    MMSD_MODEMMANAGER_INTERFACE,
                    NULL,
                    chatty_mmsd_get_modemmanager_cb,
                    self);

}

static void
mmsd_vanished_cb (GDBusConnection *connection,
                  const char      *name,
                  gpointer        user_data)
{
  ChattyMmsd *self = user_data;
  g_assert (G_IS_DBUS_CONNECTION (connection));
  g_debug ("MMSD vanished");
  self->is_ready = FALSE;
  g_object_notify (G_OBJECT (self->mm_account), "status");

  g_clear_object (&self->service_proxy);
  g_clear_object (&self->manager_proxy);
  g_clear_object (&self->modemmanager_proxy);

  if (self->mmsd_signal_id && G_IS_DBUS_CONNECTION (self->connection)) {
    g_dbus_connection_signal_unsubscribe (self->connection, self->mmsd_signal_id);
    self->mmsd_signal_id = 0;
  }
}

static void
chatty_mmsd_reload (ChattyMmsd *self)
{
  GListModel *devices;

  g_assert (CHATTY_IS_MMSD (self));
  g_assert (!self->mm_device);
  g_assert (!self->mmsd_watch_id);

  g_hash_table_remove_all (self->mms_hash_table);
  g_clear_pointer (&self->modem_number, g_free);

  devices = chatty_mm_account_get_devices (self->mm_account);
  /* We can handle only one device, get the first one */
  self->mm_device = g_list_model_get_item (devices, 0);
  g_return_if_fail (self->mm_device);

  self->modem_number = chatty_mm_device_get_number (self->mm_device);

  /* TODO: Figure out a way to add back in modem number */
  if (!self->modem_number) {
    self->modem_number = g_strdup ("");
  }

  self->mmsd_watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                          MMSD_SERVICE,
                                          G_BUS_NAME_WATCHER_FLAGS_AUTO_START,
                                          mmsd_appeared_cb,
                                          mmsd_vanished_cb,
                                          self, NULL);
}

static void
mmsd_device_list_changed_cb (ChattyMmsd *self)
{
  GListModel *devices;
  guint n_items;

  g_assert (CHATTY_IS_MMSD (self));

  /* If mm_account is NULL, the object has been finalized */
  if (!self->mm_account) {
    clear_chatty_mmsd (self);
    return;
  }

  devices = chatty_mm_account_get_devices (self->mm_account);
  n_items = g_list_model_get_n_items (devices);

  if (n_items == 0) {
    clear_chatty_mmsd (self);
    return;
  }

  /* If the device list no longer have the device we track, clear mmsd */
  if (self->mm_device &&
      !chatty_utils_get_item_position (devices, self->mm_device, NULL))
    clear_chatty_mmsd (self);

  if (!self->mm_device)
    chatty_mmsd_reload (self);
}

static void
chatty_mmsd_finalize (GObject *object)
{
  ChattyMmsd *self = (ChattyMmsd *)object;

  clear_chatty_mmsd (self);
  g_hash_table_destroy (self->mms_hash_table);
  G_OBJECT_CLASS (chatty_mmsd_parent_class)->finalize (object);
}

static void
chatty_mmsd_class_init (ChattyMmsdClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = chatty_mmsd_finalize;
}

static void
chatty_mmsd_init (ChattyMmsd *self)
{
  self->mms_hash_table = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                g_free, mms_payload_free);
}

ChattyMmsd *
chatty_mmsd_new (ChattyMmAccount *account)
{
  ChattyMmsd *self;

  g_return_val_if_fail (CHATTY_IS_MM_ACCOUNT (account), NULL);

  self = g_object_new (CHATTY_TYPE_MMSD, NULL);
  g_set_weak_pointer (&self->mm_account, account);

  g_signal_connect_object (chatty_mm_account_get_devices (account),
                           "items-changed",
                           G_CALLBACK (mmsd_device_list_changed_cb),
                           self, G_CONNECT_SWAPPED);
  mmsd_device_list_changed_cb (self);

  return self;
}

gboolean
chatty_mmsd_is_ready (ChattyMmsd *self)
{
  g_return_val_if_fail (CHATTY_IS_MMSD (self), FALSE);

  return self->is_ready;
}

gboolean
chatty_mmsd_get_settings (ChattyMmsd  *self,
                          const char **apn,
                          const char **mmsc,
                          const char **proxy,
                          gboolean    *use_smil)
{
  g_return_val_if_fail (CHATTY_IS_MMSD (self), FALSE);
  g_return_val_if_fail (apn, FALSE);
  g_return_val_if_fail (mmsc, FALSE);
  g_return_val_if_fail (proxy, FALSE);
  g_return_val_if_fail (use_smil, FALSE);

  if (!chatty_mmsd_is_ready (self)) {
    *apn = *mmsc = *proxy = "";
    *use_smil = FALSE;

    return FALSE;
  }

  *apn = self->mms_apn ?: "";
  *mmsc = self->carrier_mmsc ?: "";
  *proxy = self->carrier_proxy ?: "";
  *use_smil = self->auto_create_smil;

  return TRUE;
}

static void
settings_task_completed_cb (GTask *task)
{
  ChattyMmsd *self;

  g_assert (G_IS_TASK (task));

  if (g_task_had_error (task))
    return;

  self = g_task_get_source_object (task);
  g_assert (CHATTY_IS_MMSD (self));

  g_dbus_proxy_call (self->modemmanager_proxy,
                     "ProcessMessageQueue",
                     NULL,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     (GAsyncReadyCallback)chatty_mmsd_get_message_queue_cb,
                     self);
}

void
chatty_mmsd_set_settings_async (ChattyMmsd          *self,
                                const char          *apn,
                                const char          *mmsc,
                                const char          *proxy,
                                gboolean             use_smil,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  GDBusProxy *mm_proxy = NULL, *service_proxy = NULL;
  g_autoptr(GTask) task = NULL;
  GObject *obj;

  g_return_if_fail (CHATTY_IS_MMSD (self));

  g_set_object (&mm_proxy, self->modemmanager_proxy);
  g_set_object (&service_proxy, self->service_proxy);

  task = g_task_new (self, cancellable, callback, user_data);

  if (!service_proxy || !mm_proxy || !chatty_mmsd_is_ready (self)) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED,
                             "mmsd not ready");
    return;
  }

  if (g_strcmp0 (apn ?: "", self->mms_apn ?: "") == 0)
    apn = NULL;

  if (g_strcmp0 (mmsc ?: "", self->carrier_mmsc ?: "") == 0)
    mmsc = NULL;

  if (g_strcmp0 (proxy ?: "", self->carrier_proxy ?: "") == 0)
    proxy = NULL;

  obj = G_OBJECT (task);
  g_object_set_data_full (obj, "mm-proxy", mm_proxy, g_object_unref);
  g_object_set_data_full (obj, "service-proxy", service_proxy, g_object_unref);
  g_object_set_data_full (obj, "proxy", g_strdup (proxy), g_free);
  g_object_set_data_full (obj, "mmsc", g_strdup (mmsc), g_free);
  g_object_set_data_full (obj, "apn", g_strdup (apn), g_free);
  g_object_set_data (obj, "smil", GINT_TO_POINTER (use_smil));

  g_signal_connect_object (task, "notify::completed",
                           G_CALLBACK (settings_task_completed_cb),
                           task, 0);

  g_task_run_in_thread (task, mmsd_update_settings_cb);
}

gboolean
chatty_mmsd_set_settings_finish (ChattyMmsd    *self,
                                 GAsyncResult  *result,
                                 GError       **error)
{
  g_return_val_if_fail (CHATTY_IS_MMSD (self), FALSE);

  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
