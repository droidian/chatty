/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-message-row.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *   Andrea Schäfer <mosibasu@me.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib/gi18n.h>

#include "chatty-avatar.h"
#include "chatty-utils.h"
#include "chatty-message-row.h"


#define MAX_GMT_ISO_SIZE 256

struct _ChattyMessageRow
{
  GtkListBoxRow  parent_instance;

  GtkWidget  *revealer;
  GtkWidget  *content_grid;
  GtkWidget  *avatar_image;
  GtkWidget  *message_event_box;
  GtkWidget  *message_label;
  GtkWidget  *footer_label;

  GtkWidget  *popover;
  GtkGesture *multipress_gesture;
  GtkGesture *longpress_gesture;

  ChattyMessage *message;
  ChattyProtocol protocol;
  gboolean       is_im;
};

G_DEFINE_TYPE (ChattyMessageRow, chatty_message_row, GTK_TYPE_LIST_BOX_ROW)


static gchar *
chatty_msg_list_escape_message (ChattyMessageRow *self,
                                const char       *message)
{
  g_autofree char *nl_2_br;
  g_autofree char *striped;
  g_autofree char *escaped;
  g_autofree char *linkified;
  char *result;

  nl_2_br = purple_strdup_withhtml (message);
  if (self->protocol == CHATTY_PROTOCOL_SMS)
    striped = g_strdup (message);
  else
    striped = purple_markup_strip_html (nl_2_br);
  escaped = purple_markup_escape_text (striped, -1);
  linkified = purple_markup_linkify (escaped);
  // convert all tags to lowercase for GtkLabel markup parser
  purple_markup_html_to_xhtml (linkified, &result, NULL);

  return result;
}

static void
copy_clicked_cb (ChattyMessageRow *self)
{
  GtkClipboard *clipboard;
  const char *text;

  g_assert (CHATTY_IS_MESSAGE_ROW (self));

  clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
  text = gtk_label_get_text (GTK_LABEL (self->message_label));

  if (text != NULL)
    gtk_clipboard_set_text (clipboard, text, -1);
}

static void
message_row_show_popover (ChattyMessageRow *self)
{
  g_assert (CHATTY_IS_MESSAGE_ROW (self));

  if (!self->popover) {
    GtkWidget *item;

    self->popover = gtk_popover_new (self->message_event_box);
    item = g_object_new (GTK_TYPE_MODEL_BUTTON,
                         "margin", 12,
                         "text", _("Copy"),
                         NULL);
    gtk_widget_show (item);
    g_signal_connect_swapped (item, "clicked",
                              G_CALLBACK (copy_clicked_cb), self);
    gtk_container_add (GTK_CONTAINER (self->popover), item);
  }

  gtk_popover_popup (GTK_POPOVER (self->popover));
}


static void
message_row_hold_cb (ChattyMessageRow *self)
{
  g_assert (CHATTY_IS_MESSAGE_ROW (self));

  message_row_show_popover (self);
  gtk_gesture_set_state (self->longpress_gesture, GTK_EVENT_SEQUENCE_CLAIMED);
}

static void
message_row_update_message (ChattyMessageRow *self)
{
  g_autofree char *message = NULL;
  g_autofree char *footer = NULL;
  const char *status_str = "";
  ChattyMsgStatus status;

  g_assert (CHATTY_IS_MESSAGE_ROW (self));
  g_assert (self->message);

  status = chatty_message_get_status (self->message);

  if (status == CHATTY_STATUS_SENDING_FAILED)
    status_str = "<span color='red'> x</span>";
  else if (status == CHATTY_STATUS_SENT)
    status_str = "<span color='grey'> ✓</span>";
  else if (status == CHATTY_STATUS_DELIVERED)
    status_str = "<span color='#6cba3d'> ✓</span>";

  if (self->is_im) {
    g_autofree char *time_str = NULL;
    time_t time_stamp;

    time_stamp = chatty_message_get_time (self->message);
    time_str = chatty_utils_get_human_time (time_stamp);

    footer = g_strconcat ("<span color='grey'>",
                          time_str,
                          "</span>",
                          status_str,
                          NULL);
  } else {
    const char *alias;

    alias = chatty_message_get_user_alias (self->message);

    if (alias)
      footer = g_strconcat ("<span color='grey'>",
                            alias,
                            "</span>",
                            NULL);
  }

  message = chatty_msg_list_escape_message (self, chatty_message_get_text (self->message));
  gtk_label_set_markup (GTK_LABEL (self->message_label), message);
  gtk_label_set_markup (GTK_LABEL (self->footer_label), footer);
  gtk_widget_set_visible (self->footer_label, footer && *footer);
}

static gboolean
message_row_show_revealer (ChattyMessageRow *self)
{
  g_assert (CHATTY_IS_MESSAGE_ROW (self));

  gtk_revealer_set_reveal_child (GTK_REVEALER (self->revealer), TRUE);

  return G_SOURCE_REMOVE;
}

static void
chatty_message_row_dispose (GObject *object)
{
  ChattyMessageRow *self = (ChattyMessageRow *)object;

  g_clear_object (&self->message);
  g_clear_object (&self->multipress_gesture);
  g_clear_object (&self->longpress_gesture);

  G_OBJECT_CLASS (chatty_message_row_parent_class)->dispose (object);
}

static void
chatty_message_row_class_init (ChattyMessageRowClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = chatty_message_row_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/chatty/"
                                               "ui/chatty-message-row.ui");
  gtk_widget_class_bind_template_child (widget_class, ChattyMessageRow, revealer);
  gtk_widget_class_bind_template_child (widget_class, ChattyMessageRow, content_grid);
  gtk_widget_class_bind_template_child (widget_class, ChattyMessageRow, avatar_image);
  gtk_widget_class_bind_template_child (widget_class, ChattyMessageRow, message_event_box);
  gtk_widget_class_bind_template_child (widget_class, ChattyMessageRow, message_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyMessageRow, footer_label);
}

static void
chatty_message_row_init (ChattyMessageRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->multipress_gesture = gtk_gesture_multi_press_new (self->message_event_box);
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (self->multipress_gesture), GDK_BUTTON_SECONDARY);
  g_signal_connect_swapped (self->multipress_gesture, "pressed",
                            G_CALLBACK (message_row_show_popover), self);

  self->longpress_gesture = gtk_gesture_long_press_new (self->message_event_box);
  gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (self->longpress_gesture), TRUE);
  g_signal_connect_swapped (self->longpress_gesture, "pressed",
                            G_CALLBACK (message_row_hold_cb), self);
}

GtkWidget *
chatty_message_row_new (ChattyMessage  *message,
                        ChattyProtocol  protocol,
                        gboolean        is_im)
{
  ChattyMessageRow *self;
  GtkStyleContext *sc;
  ChattyMsgDirection direction;

  self = g_object_new (CHATTY_TYPE_MESSAGE_ROW, NULL);
  sc = gtk_widget_get_style_context (self->message_label);
  self->protocol = protocol;

  if (!message)
    return GTK_WIDGET (self);

  self->message = g_object_ref (message);
  self->is_im = !!is_im;
  direction = chatty_message_get_msg_direction (message);

  if (direction == CHATTY_DIRECTION_IN) {
    gtk_style_context_add_class (sc, "bubble_white");
    gtk_widget_set_halign (self->content_grid, GTK_ALIGN_START);
    gtk_widget_set_halign (self->message_label, GTK_ALIGN_START);
  } else if (direction == CHATTY_DIRECTION_OUT && protocol == CHATTY_PROTOCOL_SMS) {
    gtk_style_context_add_class (sc, "bubble_green");
  } else if (direction == CHATTY_DIRECTION_OUT) {
    gtk_style_context_add_class (sc, "bubble_blue");
  } else { /* System message */
    gtk_style_context_add_class (sc, "bubble_purple");
    gtk_widget_set_hexpand (self->message_label, TRUE);
  }

  if (direction == CHATTY_DIRECTION_OUT) {
    gtk_label_set_xalign (GTK_LABEL (self->footer_label), 1);
    gtk_widget_set_halign (self->content_grid, GTK_ALIGN_END);
    gtk_widget_set_halign (self->message_label, GTK_ALIGN_END);
  } else {
    gtk_label_set_xalign (GTK_LABEL (self->footer_label), 0);
  }

  if (is_im || direction == CHATTY_DIRECTION_SYSTEM ||
      direction == CHATTY_DIRECTION_OUT)
    gtk_widget_hide (self->avatar_image);
  else
    chatty_avatar_set_item (CHATTY_AVATAR (self->avatar_image),
                            chatty_message_get_user (message));

  g_signal_connect_object (message, "updated",
                           G_CALLBACK (message_row_update_message),
                           self, G_CONNECT_SWAPPED);
  message_row_update_message (self);

  /*
   * HACK: This is to delay showing the revealer child, so that it
   * is revealed after @self is added to some container.  Otherwise,
   * the child revealing won’t be animated.
   */
  g_idle_add (G_SOURCE_FUNC (message_row_show_revealer), self);

  return GTK_WIDGET (self);
}

ChattyMessage *
chatty_message_row_get_item (ChattyMessageRow *self)
{
  g_return_val_if_fail (CHATTY_IS_MESSAGE_ROW (self), NULL);

  return self->message;
}


void
chatty_message_row_set_footer (ChattyMessageRow *self,
                               GtkWidget        *footer)
{
  g_return_if_fail (CHATTY_IS_MESSAGE_ROW (self));

  if (!footer || footer == self->footer_label)
    return;

  gtk_container_remove (GTK_CONTAINER (self->content_grid), self->footer_label);
  gtk_grid_attach (GTK_GRID (self->content_grid), footer, 1, 1, 1, 1);
  gtk_widget_show (footer);
  self->footer_label = footer;
}

void
chatty_message_row_hide_footer (ChattyMessageRow *self)
{
  g_return_if_fail (CHATTY_IS_MESSAGE_ROW (self));

  gtk_widget_hide (self->footer_label);
}

void
chatty_message_row_set_alias (ChattyMessageRow *self,
                              const char       *alias)
{
  g_return_if_fail (CHATTY_IS_MESSAGE_ROW (self));

  chatty_avatar_set_title (CHATTY_AVATAR (self->avatar_image), alias);
}
