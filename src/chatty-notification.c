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

  GNotification  *notification;
  int             timeout_id;
};

G_DEFINE_TYPE (ChattyNotification, chatty_notification, G_TYPE_OBJECT)

static void
create_new_notification (ChattyNotification *self,
                         ChattyChat         *chat)
{
  g_assert (CHATTY_IS_NOTIFICATION (self));

  if (self->notification)
    return;

  self->notification = g_notification_new ("chatty");
  g_notification_set_default_action (self->notification, "app.show-window");
  g_notification_add_button_with_target (self->notification, _("Open Message"), "app.open-chat",
                                         "(ss)",
                                         chatty_chat_get_chat_name (chat),
                                         chatty_item_get_username (CHATTY_ITEM (chat)));
#if GLIB_CHECK_VERSION(2,70,0)
  g_notification_set_category (self->notification, "im.received");
#endif
}

static gboolean
show_notification (gpointer user_data)
{
  ChattyNotification *self = user_data;
  GApplication *app;

  g_assert (CHATTY_IS_NOTIFICATION (self));

  self->timeout_id = 0;
  app = g_application_get_default ();

  if (app)
    g_application_send_notification (app, "x-chatty.im.received",
                                     self->notification);

  /* We create new GNotification for each notification */
  g_clear_object (&self->notification);

  return G_SOURCE_REMOVE;
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
}

ChattyNotification *
chatty_notification_new (void)
{
  return g_object_new (CHATTY_TYPE_NOTIFICATION,
                       NULL);
}

void
chatty_notification_show_message (ChattyNotification *self,
                                  ChattyChat         *chat,
                                  ChattyMessage      *message,
                                  const char         *name)
{
  g_autofree char *title = NULL;
  g_autoptr(GdkPixbuf) image = NULL;
  g_autoptr(LfbEvent) event = NULL;
  GdkPixbuf *avatar;
  ChattyItem *item;

  g_return_if_fail (CHATTY_IS_NOTIFICATION (self));
  g_return_if_fail (CHATTY_IS_CHAT (chat));
  g_return_if_fail (CHATTY_IS_MESSAGE (message));

  create_new_notification (self, chat);

  if (name)
    title = g_strdup_printf (_("New message from %s"), name);
  else
    title = g_strdup (_("Message Received"));

  item = chatty_message_get_user (message);

  if (!item)
    item = CHATTY_ITEM (chat);

  avatar = chatty_item_get_avatar (item);
  image = chatty_manager_round_pixbuf (avatar);

  if (image)
    g_notification_set_icon (self->notification, G_ICON (image));

  g_notification_set_body (self->notification, chatty_message_get_text (message));
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
