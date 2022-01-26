/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-message.c
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-notification"

#define LIBFEEDBACK_USE_UNSTABLE_API
#include <libfeedback.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "chatty-notification.h"


#define NOTIFICATION_TIMEOUT 300 /* milliseconds */

struct _ChattyNotification
{
  GObject         parent_instance;

  ChattyChat     *chat;
  char           *chat_name;

  GNotification  *notification;
  int             timeout_id;
};

G_DEFINE_TYPE (ChattyNotification, chatty_notification, G_TYPE_OBJECT)

static gboolean
show_notification (gpointer user_data)
{
  ChattyNotification *self = user_data;
  GApplication *app;

  g_assert (CHATTY_IS_NOTIFICATION (self));

  self->timeout_id = 0;
  app = g_application_get_default ();

  if (app)
    g_application_send_notification (app, self->chat_name,
                                     self->notification);

  return G_SOURCE_REMOVE;
}

static void
notification_chat_changed_cb (ChattyNotification *self)
{
  GApplication *app;

  g_assert (CHATTY_IS_NOTIFICATION (self));

  if (chatty_chat_get_unread_count (self->chat) > 0)
    return;

  g_clear_handle_id (&self->timeout_id, g_source_remove);
  app = g_application_get_default ();

  if (app)
    g_application_withdraw_notification (app, self->chat_name);
}

static GdkPixbuf *
chatty_manager_round_pixbuf (GdkPixbuf *pixbuf)
{
  g_autoptr(GdkPixbuf) image = NULL;
  cairo_surface_t *surface;
  GdkPixbuf *round;
  cairo_t *cr;
  int width, height, size;

  if (!pixbuf)
    return NULL;

  g_assert (GDK_IS_PIXBUF (pixbuf));

  width  = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  size   = MIN (width, height);
  image  = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, size, size);

  gdk_pixbuf_scale (pixbuf, image, 0, 0,
                    size, size,
                    0, 0,
                    (double)size / width,
                    (double)size / height,
                    GDK_INTERP_BILINEAR);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, size, size);
  cr = cairo_create (surface);
  gdk_cairo_set_source_pixbuf (cr, image, 0, 0);

  cairo_arc (cr, size / 2.0, size / 2.0, size / 2.0, 0, 2 * G_PI);
  cairo_clip (cr);
  cairo_paint (cr);

  round = gdk_pixbuf_get_from_surface (surface, 0, 0, size, size);

  cairo_surface_destroy (surface);
  cairo_destroy (cr);

  return round;
}

static void
feedback_triggered_cb (GObject      *object,
		       GAsyncResult *result,
                       gpointer      user_data)
{
  g_autoptr (ChattyNotification) self = user_data;
  LfbEvent *event = (LfbEvent *)object;
  g_autoptr (GError) error = NULL;

  g_assert (LFB_IS_EVENT (event));
  g_assert (CHATTY_IS_NOTIFICATION (self));

  if (!lfb_event_trigger_feedback_finish (event, result, &error)) {
    g_warning ("Failed to trigger feedback for %s",
	       lfb_event_get_event (event));
  }
}

static void
chatty_notification_finalize (GObject *object)
{
  ChattyNotification *self = (ChattyNotification *)object;

  g_clear_handle_id (&self->timeout_id, g_source_remove);
  g_clear_object (&self->notification);

  g_free (self->chat_name);

  G_OBJECT_CLASS (chatty_notification_parent_class)->finalize (object);
}

static void
chatty_notification_class_init (ChattyNotificationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = chatty_notification_finalize;
}

static void
chatty_notification_init (ChattyNotification *self)
{
  self->notification = g_notification_new ("chatty");

  g_notification_set_default_action (self->notification, "app.show-window");

#if GLIB_CHECK_VERSION(2,70,0)
  g_notification_set_category (self->notification, "im.received");
#endif
}

ChattyNotification *
chatty_notification_new (ChattyChat *chat)
{
  ChattyNotification *self;

  g_return_val_if_fail (CHATTY_IS_CHAT (chat), NULL);

  self = g_object_new (CHATTY_TYPE_NOTIFICATION, NULL);
  self->chat = chat;
  self->chat_name = g_strdup (chatty_chat_get_chat_name (chat));
  g_set_weak_pointer (&self->chat, chat);

  g_signal_connect_object (self->chat, "changed",
                           G_CALLBACK (notification_chat_changed_cb),
                           self, G_CONNECT_SWAPPED);

  g_notification_add_button_with_target (self->notification, _("Open Message"), "app.open-chat",
                                         "(ssi)", self->chat_name,
                                         chatty_item_get_username (CHATTY_ITEM (chat)),
                                         chatty_item_get_protocols (CHATTY_ITEM (chat)));

  if (chatty_item_get_avatar (CHATTY_ITEM (chat))) {
    g_autoptr(GdkPixbuf) image = NULL;
    GdkPixbuf *avatar;

    avatar = chatty_item_get_avatar (CHATTY_ITEM (chat));
    image = chatty_manager_round_pixbuf (avatar);

    if (image)
      g_notification_set_icon (self->notification, G_ICON (image));
  }

  return self;
}

void
chatty_notification_show_message (ChattyNotification *self,
                                  ChattyMessage      *message,
                                  const char         *name)
{
  g_autofree char *title = NULL;
  g_autoptr(LfbEvent) event = NULL;
  ChattyItem *item;
  ChattyMsgType type;

  g_return_if_fail (CHATTY_IS_NOTIFICATION (self));
  g_return_if_fail (CHATTY_IS_MESSAGE (message));

  if (name)
    title = g_strdup_printf (_("New message from %s"), name);
  else
    title = g_strdup (_("Message Received"));

  item = chatty_message_get_user (message);

  if (item) {
    g_autoptr(GdkPixbuf) image = NULL;
    GdkPixbuf *avatar;

    avatar = chatty_item_get_avatar (item);
    image = chatty_manager_round_pixbuf (avatar);

    if (image)
      g_notification_set_icon (self->notification, G_ICON (image));
  }

  type = chatty_message_get_msg_type (message);

  switch (type) {
    case CHATTY_MESSAGE_UNKNOWN:
    case CHATTY_MESSAGE_TEXT:
    case CHATTY_MESSAGE_HTML:
    case CHATTY_MESSAGE_HTML_ESCAPED:
    case CHATTY_MESSAGE_MATRIX_HTML:
    case CHATTY_MESSAGE_LOCATION:
      g_notification_set_body (self->notification, chatty_message_get_text (message));
      break;

    case CHATTY_MESSAGE_FILE:
      g_notification_set_body (self->notification, "File");
      break;

    case CHATTY_MESSAGE_IMAGE:
      g_notification_set_body (self->notification, "Picture");
      break;

    case CHATTY_MESSAGE_VIDEO:
      g_notification_set_body (self->notification, "Video");
      break;

    case CHATTY_MESSAGE_AUDIO:
      g_notification_set_body (self->notification, "Audio File");
      break;

    case CHATTY_MESSAGE_MMS:
      if (chatty_message_get_text (message) && *chatty_message_get_text (message))
        g_notification_set_body (self->notification, chatty_message_get_text (message));
      else
        g_notification_set_body (self->notification, "MMS");
      break;

    default:
      g_notification_set_body (self->notification, chatty_message_get_text (message));
      break;
    }

  g_notification_set_title (self->notification, title);
  g_notification_set_priority (self->notification, G_NOTIFICATION_PRIORITY_HIGH);

  /* Delay the notification a bit so that we can squash multiple notifications
   * if we get them too fast */
  g_clear_handle_id (&self->timeout_id, g_source_remove);
  self->timeout_id = g_timeout_add (NOTIFICATION_TIMEOUT,
                                    show_notification, self);

  event = lfb_event_new ("message-new-instant");
  lfb_event_trigger_feedback_async (event, NULL,
                                    feedback_triggered_cb,
                                    g_object_ref (self));
}
