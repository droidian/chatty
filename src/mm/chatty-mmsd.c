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
  guint             mmsd_service_proxy_watch_id;
  guint             mmsd_manager_proxy_add_watch_id;
  guint             mmsd_manager_proxy_remove_watch_id;
  guint             modemmanager_watch_id;
  guint             modemmanager_bearer_handler_watch_id;
  guint             modemmanager_settings_changed_watch_id;
  GDBusProxy       *modemmanager_proxy;
  char             *modem_number;
  char             *default_modem_number;
  GPtrArray        *mms_arr;
  GHashTable       *mms_hash_table;
  int               max_attach_size;
  int               max_num_attach;
  char             *carrier_mmsc;
  char             *mms_apn;
  char             *carrier_proxy;
  gboolean          auto_create_smil;
  gboolean          is_ready;
  gulong            mmsc_signal_id;
  gulong            apn_signal_id;
  gulong            proxy_signal_id;
  gulong            smil_signal_id;
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
static void chatty_mmsd_delete_mms (ChattyMmsd *self, char *objectpath);

static void
chatty_mmsd_delete_mms_cb (GObject      *interface,
                           GAsyncResult *result,
                           gpointer     *user_data)
{
  g_autoptr(GError) error = NULL;

  if (g_dbus_proxy_call_finish (G_DBUS_PROXY (interface),
                                result,
                                &error)) {
    g_debug ("MMS delete finished");
  } else {
    g_warning ("Couldn't delete MMS - error: %s", error ? error->message : "unknown");
  }
  g_object_unref (interface);
}

static void
chatty_mmsd_get_message_proxy_cb (GObject      *service,
                                  GAsyncResult *res,
                                  gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  GDBusProxy *message_proxy;

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

static void
chatty_mmsd_delete_mms (ChattyMmsd *self,
                        char       *objectpath)
{
  g_debug ("Deleting MMS with Object Path: %s", objectpath);

  /*
   *  I see you thinking you can move this, DONT! the objectpath is unique
   *  based on the message.
   */

  g_dbus_proxy_new (self->connection,
                    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                    NULL,
                    MMSD_SERVICE,
                    objectpath,
                    MMSD_MESSAGE_INTERFACE,
                    NULL,
                    chatty_mmsd_get_message_proxy_cb,
                    self);

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
chatty_mmsd_delete_payload (ChattyMmsd  *self,
                            mms_payload *payload)
{
  g_hash_table_remove (self->mms_hash_table, payload->objectpath);

  g_free (payload->objectpath);
  g_free (payload->sender);
  g_free (payload->chat);
  g_object_unref (payload->message);
  g_free (payload);
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

  chatty_mm_account_recieve_mms_cb (self->mm_account,
                                    message,
                                    sender,
                                    recipientlist);

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
    if (payload->mmsd_message_proxy_watch_id != 0) {
      g_debug ("Unsubscribing from MMS watch");
      g_dbus_connection_signal_unsubscribe (self->connection,
                                            payload->mmsd_message_proxy_watch_id);
    }
    chatty_mmsd_delete_mms (self, payload->objectpath);
    chatty_mmsd_delete_payload (self, payload);
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
    gulong attachments_size = size;
    gulong image_attachments_size = 0;
    gulong video_attachments_size = 0;
    gulong other_attachments_size = 0;

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
      g_warning ("Size of attachments that can't be resized %ld greater then maximum attachment size %d",
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
            g_debug ("Total Attachment Size %ld, Image size reduction needed: %ld",
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
  g_autoptr(GError) error = NULL;

  g_debug ("%s", __func__);

  g_dbus_proxy_call_finish (self->service_proxy,
                            res,
                            &error);

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
chatty_mmsd_process_mms_message_attachments (GList *files, char *subject)
{
  GString *message_contents;
  GFile *text_file = NULL;
  gboolean first_attachment = FALSE;

  message_contents = g_string_new (NULL);
  for (GList *l = files; l != NULL; l = l->next) {
    ChattyFileInfo *attachment = l->data;

    if (g_strcmp0(attachment->mime_type, "application/smil") == 0) {
      continue;
    }

    if (g_str_match_string ("text/plain", attachment->mime_type, TRUE)) {
      /* If an MMS has a message, it tends to be the first text/plain attachment */
      if (text_file == NULL) {
        g_autoptr(GError) error = NULL;
        char *contents;
        gulong length;

        text_file = g_file_new_build_filename (g_get_user_data_dir (),
                                               "chatty", attachment->path, NULL);
        g_file_load_contents (text_file,
                              NULL,
                              &contents,
                              &length,
                              NULL,
                              &error);
        message_contents = g_string_prepend (message_contents, "\n\n");
        message_contents = g_string_prepend (message_contents, contents);
        if (subject != NULL) {
          message_contents = g_string_prepend (message_contents, "Message: ");
        }
        g_object_unref (text_file);
      } else {
        g_debug ("Already found text file, skipping....");
        if (first_attachment)
          message_contents = g_string_append (message_contents, "\n\n");
        else
          first_attachment = TRUE;

        message_contents = g_string_append (message_contents, attachment->url);
      }
    } else {
      if (first_attachment)
        message_contents = g_string_append (message_contents, "\n\n");
      else
        first_attachment = TRUE;

      message_contents = g_string_append (message_contents, attachment->url);
    }
  }

  if (subject != NULL) {
    message_contents = g_string_prepend (message_contents, "\n\n");
    message_contents = g_string_prepend (message_contents, subject);
    message_contents = g_string_prepend (message_contents, "Subject: ");
  }

  return g_string_free (message_contents, FALSE);
}

static char *
chatty_mmsd_process_mms_message_text (GList *files, char *subject)
{
  GString *message_contents;
  GFile *text_file = NULL;

  message_contents = g_string_new (NULL);
  for (GList *l = files; l != NULL; l = l->next) {
    ChattyFileInfo *attachment = l->data;

    if (g_strcmp0(attachment->mime_type, "application/smil") == 0) {
      continue;
    }

    if (g_str_match_string ("text/plain", attachment->mime_type, TRUE)) {
      /* If an MMS has a message, it tends to be the first text/plain attachment */
      if (text_file == NULL) {
        g_autoptr(GError) error = NULL;
        char *contents;
        gulong length;

        g_debug ("Found Text file!");
        text_file = g_file_new_build_filename (g_get_user_data_dir (),
                                               "chatty", attachment->path, NULL);
        g_file_load_contents (text_file,
                              NULL,
                              &contents,
                              &length,
                              NULL,
                              &error);
        message_contents = g_string_prepend (message_contents, contents);
        if (subject != NULL) {
          message_contents = g_string_prepend (message_contents, "Message: ");
        }
        g_object_unref (text_file);
      } else {
        g_debug ("Already found text file, skipping....");
      }
    }
  }

  if (subject != NULL) {
    message_contents = g_string_prepend (message_contents, "\n\n");
    message_contents = g_string_prepend (message_contents, subject);
    message_contents = g_string_prepend (message_contents, "Subject: ");
  }

  g_debug ("MMS Message: %s", message_contents->str);

  return g_string_free (message_contents, FALSE);
}

static mms_payload *
chatty_mmsd_receive_message (ChattyMmsd *self,
                             GVariant   *message_t)
{
  g_autoptr(GFile) parent = NULL;
  g_autoptr(GVariant) recipients = NULL;
  g_autoptr(GVariant) attachments = NULL;
  g_autoptr(GDateTime) date_time = NULL;
  ChattyMsgDirection direction = CHATTY_DIRECTION_UNKNOWN;
  ChattyMsgStatus mms_status = CHATTY_STATUS_UNKNOWN;
  ChattyMsgType chatty_msg_type = CHATTY_MESSAGE_TEXT;
  GVariant *properties, *reciever, *attach;
  GVariantDict dict;
  GVariantIter iter;
  GFile *container = NULL;
  GList *files = NULL;
  GFile *savepath;
  char *objectpath = NULL, *date = NULL, *sender = NULL, *rx_modem_number = NULL;
  char *smil = NULL, *status = NULL, *subject = NULL, *mms_message = NULL;
  GString *who;
  GVariantIter recipientiter;
  mms_payload *payload;
  gint64 unix_time = 0;
  int delivery_report = FALSE;
  guint num_files;

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
    subject = NULL;
  else if (strlen (subject) < 1)
    subject = NULL;

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
    mms_status = CHATTY_STATUS_RECIEVED;
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
    mms_status = CHATTY_STATUS_RECIEVED;
  } else {
    chatty_mmsd_delete_mms (self, objectpath);
    g_return_val_if_reached (NULL);
    return NULL;
  }

  payload = g_try_new0 (mms_payload, 1);
  payload->delivery_report = delivery_report;
  payload->objectpath = g_strdup (objectpath);
  payload->mmsd_message_proxy_watch_id = 0;

  if (delivery_report) {
    char *delivery_status;
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

  if (rx_modem_number != NULL) {
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
  g_variant_iter_init (&recipientiter, recipients);
  while ((reciever = g_variant_iter_next_value (&recipientiter))) {
    char *temp, *temp2;
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
  payload->chat = g_string_free (who, FALSE);

  /* Go through the attachments */
  attachments = g_variant_dict_lookup_value (&dict, "Attachments", G_VARIANT_TYPE_ARRAY);
  g_variant_iter_init (&iter, attachments);

  while ((attach = g_variant_iter_next_value (&iter))) {
    ChattyFileInfo *attachment = NULL;
    GFile *new;
    GFileOutputStream *out;
    char *filenode, *containerpath, *mimetype, *filename, *contents;
    gulong size, data;
    gulong length, written = 0;
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
        chatty_mmsd_delete_payload (self, payload);
        return NULL;
      } else if (error) {
        g_warning ("Error loading MMSD Payload: %s", error->message);
        return NULL;
      }

      tag = g_strconcat (date, payload->sender, NULL);
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
        int smil_size = strlen (smil);

        new = g_file_get_child (savepath, "mms.smil");
        out = g_file_create (new, G_FILE_CREATE_PRIVATE, NULL, &error);
        if (out == NULL) {
          if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
            g_debug ("%s Exists, Skipping Error....", g_file_peek_path (new));
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
          } else {
            attachment->file_name = g_strdup ("mms.smil");
            attachment->mime_type = g_strdup ("application/smil");
            attachment->size      = written;
            attachment->path      = g_file_get_relative_path (parent, new);
            attachment->url       = g_file_get_uri (new);
            attachment->status    = CHATTY_FILE_DOWNLOADED;


            files            = g_list_append (files, attachment);
            attachment = NULL;
            attachment = g_try_new0 (ChattyFileInfo, 1);
          }
        }
      }
    }
    filename = g_strdup (filenode);
    g_strdelimit (filename, "<>", ' ');
    g_strstrip (filename);
    filename = g_path_get_basename (filename);
    new = g_file_get_child (savepath, filename);
    out = g_file_create (new, G_FILE_CREATE_PRIVATE, NULL, &error);
    if (error) {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
        g_debug ("%s Exists, Skipping Error....",
                 g_file_peek_path (new));
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
    g_output_stream_close (G_OUTPUT_STREAM (out), NULL, NULL);

    attachment->file_name = g_strdup (filename);
    attachment->mime_type = g_strdup (mimetype);
    attachment->size      = written;
    attachment->path      = g_file_get_relative_path (parent, new);
    attachment->url       = g_file_get_uri (new);
    attachment->status    = CHATTY_FILE_DOWNLOADED;

    files = g_list_prepend (files, attachment);
    attachment = NULL;
  }
  g_object_unref (container);

  num_files = g_list_length (files);

  if (num_files == 2) {
    GList *l = files->next;
    ChattyFileInfo *smil_file = l->data;
    /*
     * If there is only one file and SMIL, there is no reason to keep the SMIL
     * treat it just like there is only one attachment
     */
    if (g_strcmp0(smil_file->mime_type, "application/smil") == 0) {
      files = g_list_delete_link (files, l);
      num_files = g_list_length (files);
      chatty_file_info_free (smil_file);
    }
  }

  if (num_files == 1) {
    ChattyFileInfo *attachment = files->data;
    if (g_str_has_prefix (attachment->mime_type, "image")) {
      chatty_msg_type = CHATTY_MESSAGE_IMAGE;
    } else if (g_str_has_prefix (attachment->mime_type, "video")) {
      /* TODO: Support for inline video */
      //chatty_msg_type = CHATTY_MESSAGE_VIDEO;
      mms_message = chatty_mmsd_process_mms_message_attachments (files, subject);
    } else if (g_str_has_prefix (attachment->mime_type, "audio")) {
      /* TODO: Support for inline audio */
      //chatty_msg_type = CHATTY_MESSAGE_AUDIO;
      mms_message = chatty_mmsd_process_mms_message_attachments (files, subject);
    } else if (g_str_match_string ("text/plain", attachment->mime_type, TRUE)) {
      mms_message = chatty_mmsd_process_mms_message_text (files, subject);
    } else {
      /* TODO: Support for inline file */
      //chatty_msg_type = CHATTY_MESSAGE_FILE;
      mms_message = chatty_mmsd_process_mms_message_attachments (files, subject);
    }
  } else {
    /*
     * TODO: Support for inline multiple attachments. There may not necessarily
     *       be SMIL to depend on for formatting.
     */
    mms_message = chatty_mmsd_process_mms_message_attachments (files, subject);
  }

  date_time = g_date_time_new_from_iso8601 (date, NULL);
  if (date_time)
    unix_time = g_date_time_to_unix (date_time);
  if (!unix_time)
    unix_time = time (NULL);

  payload->message = chatty_message_new (NULL,
                                         mms_message,
                                         g_path_get_basename (objectpath),
                                         unix_time,
                                         chatty_msg_type,
                                         direction,
                                         mms_status);

  chatty_message_set_id (payload->message, g_path_get_basename (objectpath));
  chatty_message_set_files (payload->message, files);

  return payload;
}

static void
chatty_mmsd_get_new_mms_cb (GDBusConnection *connection,
                            const char      *sender_name,
                            const char      *object_path,
                            const char      *interface_name,
                            const char      *signal_name,
                            GVariant        *parameters,
                            gpointer         user_data)
{
  ChattyMmsd *self = user_data;
  mms_payload *payload;

  g_debug ("%s", __func__);
  payload = chatty_mmsd_receive_message (self, parameters);
  if (payload == NULL) {
    g_autofree char *objectpath = NULL;
    GVariant *properties;
    g_variant_get (parameters, "(o@a{?*})", &objectpath, &properties);
    g_warning ("There was an error with decoding the MMS %s",
               objectpath);
  } else {
    if (!g_hash_table_insert (self->mms_hash_table, payload->objectpath, payload)) {
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
  GVariant *ret;

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
        mms_payload *payload;
        GVariant *properties;
        char *objectpath;
        g_variant_get (message_t, "(o@a{?*})", &objectpath, &properties);
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
          if (!g_hash_table_insert (self->mms_hash_table, objectpath, payload)) {
            g_warning ("g_hash_table:MMS Already exists! This should not happe");
          }
          chatty_mmsd_process_mms (self, payload);
        }
      }
      g_variant_unref (msg_pack);
    } else {
      g_debug ("Have 0 MMS messages to process");
    }
  }
}

static gboolean
chatty_mmsd_sync_settings (ChattyMmsd *self)
{
  ChattySettings *settings;
  gboolean sync_mmsd = FALSE;
  g_autofree char *carrier_mmsc = NULL;
  g_autofree char *carrier_apn = NULL;
  g_autofree char *carrier_proxy = NULL;

  g_debug ("Syncing settings");
  settings = chatty_settings_get_default ();
  carrier_mmsc = chatty_settings_get_mms_carrier_mmsc (settings);
  carrier_apn = chatty_settings_get_mms_carrier_apn (settings);
  carrier_proxy = chatty_settings_get_mms_carrier_proxy (settings);
  if (g_strcmp0 (self->carrier_mmsc, carrier_mmsc) != 0) {
    g_debug ("Changing MMSC from %s to %s", self->carrier_mmsc, carrier_mmsc);
    g_free (self->carrier_mmsc);
    self->carrier_mmsc = g_strdup (carrier_mmsc);
    if (!carrier_mmsc)
      carrier_mmsc = "";

    g_dbus_proxy_call_sync (self->modemmanager_proxy,
                            "ChangeSettings",
                            g_variant_new_parsed ("('CarrierMMSC', <%s>)", carrier_mmsc),
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            NULL);
    sync_mmsd = TRUE;
  }
  if (g_strcmp0 (self->mms_apn, carrier_apn) != 0) {
    g_debug ("Changing APN from %s to %s", self->mms_apn, carrier_apn);
    g_free (self->mms_apn);
    self->mms_apn = g_strdup (carrier_apn);
    if (!carrier_apn)
      carrier_apn = "";

    g_dbus_proxy_call_sync (self->modemmanager_proxy,
                            "ChangeSettings",
                            g_variant_new_parsed ("('MMS_APN', <%s>)", carrier_apn),
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            NULL);
    sync_mmsd = TRUE;
  }
  if (g_strcmp0 (self->carrier_proxy, carrier_proxy) != 0) {
    g_debug ("Changing Proxy from %s to %s", self->carrier_proxy, carrier_proxy);
    g_free (self->carrier_proxy);
    self->carrier_proxy = g_strdup (carrier_proxy);
    /* mmsd-tng prefers "NULL" to "" */
    if (!carrier_proxy || !*carrier_proxy) {
      g_free (carrier_proxy);
      carrier_proxy = g_strdup("NULL");
    }

    g_dbus_proxy_call_sync (self->modemmanager_proxy,
                            "ChangeSettings",
                            g_variant_new_parsed ("('CarrierMMSProxy', <%s>)", carrier_proxy),
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            NULL);
    sync_mmsd = TRUE;
  }

  return sync_mmsd;
}

static void
chatty_mmsd_get_message_queue_cb (GObject      *service,
                                  GAsyncResult *res,
                                  gpointer      user_data)
{
  ChattyMmsd *self = user_data;
  g_autoptr(GError) error = NULL;

  g_debug ("%s", __func__);

  g_dbus_proxy_call_finish (self->modemmanager_proxy,
                            res,
                            &error);

  if (error != NULL) {
    g_warning ("Error syncing messages: %s", error->message);
  } else {
    g_debug ("Finished syncing messages!");
  }
}

static void
chatty_mmsd_settings_changed_cb (ChattyMmsd *self)
{
  if (chatty_mmsd_sync_settings (self)) {
    g_dbus_proxy_call (self->modemmanager_proxy,
                       "ProcessMessageQueue",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       (GAsyncReadyCallback)chatty_mmsd_get_message_queue_cb,
                       self);
  }
}

static void
chatty_mmsd_smil_changed_cb (ChattyMmsd *self)
{
  ChattySettings *settings;
  gboolean create_smil_setting;

  settings = chatty_settings_get_default ();
  create_smil_setting = chatty_settings_request_mmsd_tng_smil (settings);
  if (create_smil_setting != self->auto_create_smil) {
    self->auto_create_smil = create_smil_setting;
    g_debug ("Changing AutoCreateSMIL to %d", create_smil_setting);
    g_dbus_proxy_call_sync (self->service_proxy,
                            "SetProperty",
                            g_variant_new_parsed ("('AutoCreateSMIL', <%b>)", self->auto_create_smil),
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            NULL);
  }
}

static void
chatty_mmsd_settings_signal_changed_cb (GDBusConnection *connection,
                                        const char      *sender_name,
                                        const char      *object_path,
                                        const char      *interface_name,
                                        const char      *signal_name,
                                        GVariant        *parameters,
                                        gpointer        user_data)
{
  ChattyMmsd *self = user_data;
  char *apn, *mmsc, *proxy;
  ChattySettings *settings;

  g_variant_get (parameters, "(sss)", &apn, &mmsc, &proxy);
  g_debug("Settings Changed: apn %s, mmsc %s, proxy %s", apn, mmsc, proxy);

  g_free (self->mms_apn);
  self->mms_apn = g_strdup(apn);
  g_free (self->carrier_mmsc);
  self->carrier_mmsc = g_strdup(mmsc);
  g_free (self->carrier_proxy);
  if (g_strcmp0 (proxy, "NULL") == 0)
    self->carrier_proxy = NULL;
  else
    self->carrier_proxy = g_strdup(proxy);

  settings = chatty_settings_get_default ();
  g_object_freeze_notify (G_OBJECT (settings));
  chatty_settings_set_mms_carrier_mmsc (settings, self->carrier_mmsc);
  chatty_settings_set_mms_carrier_apn (settings, self->mms_apn);
  chatty_settings_set_mms_carrier_proxy (settings, self->carrier_proxy);
  g_object_thaw_notify (G_OBJECT (settings));
}


static void
chatty_mmsd_get_mmsd_service_settings_cb (GObject      *service,
                                          GAsyncResult *res,
                                          gpointer      user_data)
{
  ChattyMmsd *self = user_data;
  g_autoptr(GError) error = NULL;
  GVariant *ret;

  g_debug ("%s", __func__);

  ret = g_dbus_proxy_call_finish (self->service_proxy,
                                  res,
                                  &error);

  if (error != NULL) {
    g_warning ("Error in Proxy call: %s\n", error->message);
  } else {

    GVariantDict dict;
    GVariant *all_settings;
    ChattySettings *settings;
    gboolean request_report;
    gboolean use_delivery_reports, auto_create_smil;
    int max_attach_total_size, max_attachments;
    g_autofree char *carrier_mmsc = NULL;

    g_variant_get (ret, "(@a{?*})", &all_settings);

    settings = chatty_settings_get_default ();
    carrier_mmsc = chatty_settings_get_mms_carrier_mmsc(settings);
    request_report = chatty_settings_request_sms_delivery_reports (settings);

    self->mmsc_signal_id = g_signal_connect_swapped (settings,
                                                     "notify::mmsd-carrier-mmsc",
                                                     G_CALLBACK (chatty_mmsd_settings_changed_cb),
                                                     self);
    self->apn_signal_id = g_signal_connect_swapped (settings,
                                                    "notify::mmsd-carrier-mms-apn",
                                                    G_CALLBACK (chatty_mmsd_settings_changed_cb),
                                                    self);
    self->proxy_signal_id = g_signal_connect_swapped (settings,
                                                      "notify::mmsd-carrier-mms-proxy",
                                                      G_CALLBACK (chatty_mmsd_settings_changed_cb),
                                                      self);
    self->smil_signal_id = g_signal_connect_swapped (settings,
                                                  "notify::request-mmsd-smil",
                                                  G_CALLBACK (chatty_mmsd_smil_changed_cb),
                                                  self);

    g_variant_dict_init (&dict, all_settings);
    if (g_variant_dict_lookup (&dict, "UseDeliveryReports", "b", &use_delivery_reports)) {
      g_debug ("UseDeliveryReports is set to %d", use_delivery_reports);
      if (use_delivery_reports != request_report) {
        g_debug ("Changing UseDeliveryReports to %d", request_report);
        g_dbus_proxy_call_sync (self->service_proxy,
                                "SetProperty",
                                g_variant_new_parsed ("('UseDeliveryReports', <%b>)", request_report),
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                NULL,
                                NULL);
      }
    }
    if (g_variant_dict_lookup (&dict, "AutoCreateSMIL", "b", &auto_create_smil)) {
      gboolean chatty_smil_setting = chatty_settings_request_mmsd_tng_smil (settings);
      self->auto_create_smil = auto_create_smil;
      g_debug ("AutoCreateSMIL is set to %d", self->auto_create_smil);
      if (chatty_smil_setting != self->auto_create_smil) {
        g_debug ("Changing AutoCreateSMIL to %d", chatty_smil_setting);
        g_dbus_proxy_call_sync (self->service_proxy,
                                "SetProperty",
                                g_variant_new_parsed ("('AutoCreateSMIL', <%b>)", chatty_smil_setting),
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                NULL,
                                NULL);
      }
    }
    if (g_variant_dict_lookup (&dict, "TotalMaxAttachmentSize", "i", &max_attach_total_size))
      self->max_attach_size = max_attach_total_size;
    else
      self->max_attach_size = DEFAULT_MAXIMUM_ATTACHMENT_SIZE;

    g_debug ("TotalMaxAttachmentSize is set to %d", self->max_attach_size);

    if (g_variant_dict_lookup (&dict, "MaxAttachments", "i", &max_attachments))
      self->max_num_attach = max_attachments;
    else
      self->max_num_attach = DEFAULT_MAXIMUM_ATTACHMENTS;

    g_debug ("MaxAttachments is set to %d", self->max_num_attach);

    /* If carrier MMSC is blank, assume no settings are valid */
    if (!carrier_mmsc || !*carrier_mmsc) {
      /* mmsd-tng default for MMSC is http://mms.invalid */
      if (g_strcmp0 (self->carrier_mmsc, "http://mms.invalid") != 0) {
        g_object_freeze_notify (G_OBJECT (settings));
        chatty_settings_set_mms_carrier_mmsc (settings, self->carrier_mmsc);
        chatty_settings_set_mms_carrier_apn (settings, self->mms_apn);
        chatty_settings_set_mms_carrier_proxy (settings, self->carrier_proxy);
        g_object_thaw_notify (G_OBJECT (settings));
      }
    } else {
      chatty_mmsd_sync_settings (self);
    }
    self->modemmanager_settings_changed_watch_id =
      g_dbus_connection_signal_subscribe (self->connection,
                                          MMSD_SERVICE,
                                          MMSD_MODEMMANAGER_INTERFACE,
                                          "SettingsChanged",
                                          MMSD_PATH,
                                          NULL,
                                          G_DBUS_SIGNAL_FLAGS_NONE,
                                          (GDBusSignalCallback)chatty_mmsd_settings_signal_changed_cb,
                                          self,
                                          NULL);
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
    self->mmsd_service_proxy_watch_id =
      g_dbus_connection_signal_subscribe (self->connection,
                                          MMSD_SERVICE,
                                          MMSD_SERVICE_INTERFACE,
                                          "MessageAdded",
                                          MMSD_MODEMMANAGER_PATH,
                                          NULL,
                                          G_DBUS_SIGNAL_FLAGS_NONE,
                                          (GDBusSignalCallback)chatty_mmsd_get_new_mms_cb,
                                          self,
                                          NULL);

    if (self->mmsd_service_proxy_watch_id) {
      g_debug ("Listening for new MMS messages");
    } else {
      g_warning ("Failed to connect 'MessageAdded' signal");
    }

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

  if (self->mmsd_watch_id) {
    g_debug ("Unwatching MMSD");
    g_bus_unwatch_name (self->mmsd_watch_id);
    self->mmsd_watch_id = 0;
  }
}

static void
chatty_mmsd_connect_to_service (ChattyMmsd *self,
                                GVariant   *service)
{
  char *servicepath, *serviceidentity;
  GVariant *properties;
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
chatty_mmsd_service_added_cb (GDBusConnection *connection,
                              const char      *sender_name,
                              const char      *object_path,
                              const char      *interface_name,
                              const char      *signal_name,
                              GVariant        *parameters,
                              gpointer         user_data)
{
  ChattyMmsd *self = user_data;
  g_autofree char *param = NULL;

  param = g_variant_print (parameters, TRUE);
  CHATTY_DEBUG_MSG ("Service Added g_variant: %s", param);

  chatty_mmsd_connect_to_service (self, parameters);
}

static void
chatty_mmsd_remove_service (ChattyMmsd *self)
{
  if (G_IS_OBJECT (self->service_proxy)) {
    g_debug ("Removing Service!");
    g_object_unref (self->service_proxy);
    g_dbus_connection_signal_unsubscribe (self->connection,
                                          self->mmsd_service_proxy_watch_id);

  } else {
    g_warning ("No Service to remove!");
  }
}

static void
chatty_mmsd_service_removed_cb (GDBusConnection *connection,
                                const char      *sender_name,
                                const char      *object_path,
                                const char      *interface_name,
                                const char      *signal_name,
                                GVariant        *parameters,
                                gpointer         user_data)
{
  ChattyMmsd *self = user_data;
  g_autofree char *param = NULL;

  param = g_variant_print (parameters, TRUE);
  CHATTY_DEBUG_MSG ("Service Removed g_variant: %s", param);

  chatty_mmsd_remove_service (self);
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

    self->mmsd_manager_proxy_add_watch_id =
      g_dbus_connection_signal_subscribe (self->connection,
                                          MMSD_SERVICE,
                                          MMSD_MANAGER_INTERFACE,
                                          "ServiceAdded",
                                          MMSD_PATH,
                                          NULL,
                                          G_DBUS_SIGNAL_FLAGS_NONE,
                                          (GDBusSignalCallback)chatty_mmsd_service_added_cb,
                                          self,
                                          NULL);

    self->mmsd_manager_proxy_remove_watch_id =
      g_dbus_connection_signal_subscribe (self->connection,
                                          MMSD_SERVICE,
                                          MMSD_MANAGER_INTERFACE,
                                          "ServiceRemoved",
                                          MMSD_PATH,
                                          NULL,
                                          G_DBUS_SIGNAL_FLAGS_NONE,
                                          (GDBusSignalCallback)chatty_mmsd_service_removed_cb,
                                          self,
                                          NULL);

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
  g_autoptr(GError) error = NULL;
  GVariant *ret;

  g_debug ("%s", __func__);

  ret = g_dbus_proxy_call_finish (self->modemmanager_proxy,
                                  res,
                                  &error);

  if (error != NULL) {
    g_warning ("Error in Proxy call: %s\n", error->message);
  } else {

    GVariantDict dict;
    GVariant *all_settings;
    char *message_center, *mms_apn, *CarrierMMSProxy, *default_modem_number;
    int auto_process_on_connection, autoprocess_sms_wap;

    g_variant_get (ret, "(@a{?*})", &all_settings);

    g_variant_dict_init (&dict, all_settings);
    if (g_variant_dict_lookup (&dict, "CarrierMMSC", "s", &message_center))
      self->carrier_mmsc = g_strdup(message_center);
    else
      self->carrier_mmsc = NULL;

    g_debug ("CarrierMMSC is set to %s", self->carrier_mmsc);
    if (g_variant_dict_lookup (&dict, "MMS_APN", "s", &mms_apn))
      self->mms_apn = g_strdup(mms_apn);
    else
      self->mms_apn = NULL;

    g_debug ("MMS APN is set to %s", self->mms_apn);
    if (g_variant_dict_lookup (&dict, "CarrierMMSProxy", "s", &CarrierMMSProxy)) {
      if (g_strcmp0 (CarrierMMSProxy, "NULL") == 0)
        self->carrier_proxy = NULL;
      else
        self->carrier_proxy = g_strdup(CarrierMMSProxy);

    } else
      self->carrier_proxy = NULL;

    g_debug ("CarrierMMSProxy is set to %s", self->carrier_proxy);
    if (g_variant_dict_lookup (&dict, "default_modem_number", "s", &default_modem_number)) {
      if (g_strcmp0 (self->default_modem_number, "NULL") == 0)
        self->default_modem_number = g_strdup (default_modem_number);
      else
        self->default_modem_number = NULL;

      g_debug ("Default Modem Number is set to %s", self->default_modem_number);
    }
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
chatty_mmsd_bearer_handler_error_cb (GDBusConnection *connection,
                                     const char      *sender_name,
                                     const char      *object_path,
                                     const char      *interface_name,
                                     const char      *signal_name,
                                     GVariant        *parameters,
                                     gpointer        user_data)
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

    self->modemmanager_bearer_handler_watch_id =
      g_dbus_connection_signal_subscribe (self->connection,
                                          MMSD_SERVICE,
                                          MMSD_MODEMMANAGER_INTERFACE,
                                          "BearerHandlerError",
                                          MMSD_PATH,
                                          NULL,
                                          G_DBUS_SIGNAL_FLAGS_NONE,
                                          (GDBusSignalCallback)chatty_mmsd_bearer_handler_error_cb,
                                          self,
                                          NULL);

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
mmsd_appeared_cb (GDBusConnection *connection,
                  const char      *name,
                  const char      *name_owner,
                  gpointer         user_data)
{
  ChattyMmsd *self = user_data;
  g_assert (G_IS_DBUS_CONNECTION (connection));
  self->connection = connection;
  g_debug ("MMSD appeared");

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

  if (G_IS_OBJECT (self->service_proxy)) {
    ChattySettings *settings;
    settings = chatty_settings_get_default ();
    chatty_mmsd_remove_service (self);
    g_clear_signal_handler (&self->mmsc_signal_id, settings);
    g_clear_signal_handler (&self->apn_signal_id, settings);
    g_clear_signal_handler (&self->proxy_signal_id, settings);
    g_clear_signal_handler (&self->smil_signal_id, settings);
  }
  if (G_IS_OBJECT (self->manager_proxy)) {
    g_object_unref (self->manager_proxy);
    g_dbus_connection_signal_unsubscribe (self->connection,
                                          self->mmsd_manager_proxy_add_watch_id);
    g_dbus_connection_signal_unsubscribe (self->connection,
                                          self->mmsd_manager_proxy_remove_watch_id);
  }

  if (G_IS_OBJECT (self->modemmanager_proxy)) {
    g_object_unref (self->modemmanager_proxy);
    g_dbus_connection_signal_unsubscribe (self->connection,
                                          self->modemmanager_bearer_handler_watch_id);

    g_dbus_connection_signal_unsubscribe (self->connection,
                                          self->modemmanager_settings_changed_watch_id);
  }

  if (G_IS_DBUS_CONNECTION (self->connection)) {
    g_dbus_connection_unregister_object (self->connection,
                                         self->mmsd_watch_id);
  }
}

static void
chatty_mmsd_reload (ChattyMmsd *self)
{
  const char *const *own_numbers;
  GListModel *devices;
  MMObject *mm_object;
  MMModem *mm_modem;

  g_assert (CHATTY_IS_MMSD (self));
  g_assert (!self->mm_device);
  g_assert (!self->mmsd_watch_id);

  g_hash_table_remove_all (self->mms_hash_table);
  g_clear_pointer (&self->modem_number, g_free);

  devices = chatty_mm_account_get_devices (self->mm_account);
  /* We can handle only one device, get the first one */
  self->mm_device = g_list_model_get_item (devices, 0);
  g_return_if_fail (self->mm_device);

  mm_object = chatty_mm_device_get_object (self->mm_device);
  mm_modem = mm_object_peek_modem (MM_OBJECT (mm_object));

  /* Figure out what number the modem is on. */
  own_numbers = mm_modem_get_own_numbers (mm_modem);

  for (guint i = 0; own_numbers && own_numbers[i]; i++) {
    const char *number, *country_code;

    number = own_numbers[i];
    country_code = chatty_settings_get_country_iso_code (chatty_settings_get_default ());
    self->modem_number = chatty_utils_check_phonenumber (number, country_code);

    if (self->modem_number)
      break;
  }

  if (!self->modem_number) {
    g_warning ("Your SIM or Modem does not support modem manger's number! Please file a bug report");
    self->modem_number = g_strdup ("");
    g_debug ("Making Dummy modem number: %s", self->modem_number);
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
  self->mm_account = NULL;
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
  self->mms_hash_table = g_hash_table_new (g_str_hash, g_str_equal);
}

ChattyMmsd *
chatty_mmsd_new (ChattyMmAccount *account)
{
  ChattyMmsd *self;

  g_return_val_if_fail (CHATTY_IS_MM_ACCOUNT (account), NULL);

  self = g_object_new (CHATTY_TYPE_MMSD, NULL);
  self->mm_account = account;
  g_set_weak_pointer (&self->mm_account, account);

  return self;
}

gboolean
chatty_mmsd_is_ready (ChattyMmsd *self)
{
  g_return_val_if_fail (CHATTY_IS_MMSD (self), FALSE);

  return self->is_ready;
}

void
chatty_mmsd_load (ChattyMmsd *self)
{
  GListModel *devices;

  g_return_if_fail (CHATTY_IS_MMSD (self));

  devices = chatty_mm_account_get_devices (self->mm_account);
  g_signal_connect_object (devices, "items-changed",
                           G_CALLBACK (mmsd_device_list_changed_cb),
                           self, G_CONNECT_SWAPPED);
  mmsd_device_list_changed_cb (self);
}
