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
#include "chatty-image-item.h"
#include "chatty-text-item.h"
#include "chatty-clock.h"
#include "chatty-message-row.h"


struct _ChattyMessageRow
{
  GtkListBoxRow  parent_instance;

  GtkWidget  *content_grid;
  GtkWidget  *avatar_image;
  GtkWidget  *hidden_box;
  GtkWidget  *author_label;
  GtkWidget  *message_event_box;
  GtkWidget  *footer_label;

  GtkWidget  *content;

  GtkWidget  *popover;
  GtkGesture *multipress_gesture;
  GtkGesture *longpress_gesture;
  GtkGesture *activate_gesture;

  GBinding   *name_binding;

  ChattyMessage *message;
  ChattyProtocol protocol;
  gulong         clock_id;
  gboolean       is_im;
  gboolean       force_hide_footer;
};

G_DEFINE_TYPE (ChattyMessageRow, chatty_message_row, GTK_TYPE_LIST_BOX_ROW)


static void
copy_clicked_cb (ChattyMessageRow *self)
{
  GtkClipboard *clipboard;
  const char *text;

  g_assert (CHATTY_IS_MESSAGE_ROW (self));

  clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
  text = chatty_text_item_get_text (CHATTY_TEXT_ITEM (self->content));

  if (text && *text)
    gtk_clipboard_set_text (clipboard, text, -1);
}

static void
message_row_show_popover (ChattyMessageRow *self)
{
  g_assert (CHATTY_IS_MESSAGE_ROW (self));

  if (chatty_message_get_msg_type (self->message) != CHATTY_MESSAGE_TEXT)
    return;

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
message_activate_gesture_cb (ChattyMessageRow *self)
{
  g_autoptr(GFile) file = NULL;
  g_autofree char *uri = NULL;
  ChattyFileInfo *info = NULL;
  GList *file_list;

  g_assert (CHATTY_IS_MESSAGE_ROW (self));

  if (chatty_message_get_msg_type (self->message) != CHATTY_MESSAGE_IMAGE)
    return;

  file_list = chatty_message_get_files (self->message);

  if (file_list)
    info = file_list->data;

  if (!info || !info->path)
    return;

  if (self->protocol == CHATTY_PROTOCOL_MMS_SMS || self->protocol == CHATTY_PROTOCOL_MMS)
    file = g_file_new_build_filename (g_get_user_data_dir (), "chatty", info->path, NULL);
  else
    file = g_file_new_build_filename (g_get_user_cache_dir (), "chatty", info->path, NULL);

  uri = g_file_get_uri (file);

  gtk_show_uri_on_window (NULL, uri, GDK_CURRENT_TIME, NULL);
}

static const char *
message_row_get_clock_signal (time_t time_stamp)
{
  double diff;

  diff = difftime (time (NULL), time_stamp);

  if (diff >= - SECONDS_PER_MINUTE &&
      diff <= SECONDS_PER_HOUR)
    return g_intern_static_string ("changed");

  if (diff < 0)
    return NULL;

  if (diff < SECONDS_PER_WEEK)
    return g_intern_static_string ("day-changed");

  return NULL;
}

static gboolean
chatty_message_row_update_footer (ChattyMessageRow *self)
{
  g_autofree char *time_str = NULL;
  g_autofree char *footer = NULL;
  const char *status_str = "", *time_signal;
  ChattyMsgStatus status;
  time_t time_stamp;

  g_assert (CHATTY_IS_MESSAGE_ROW (self));
  g_assert (self->message);

  if (self->force_hide_footer) {
    g_clear_signal_handler (&self->clock_id, chatty_clock_get_default ());
    g_object_set_data (G_OBJECT (self), "time-signal", NULL);

    return G_SOURCE_REMOVE;
  }

  status = chatty_message_get_status (self->message);

  if (status == CHATTY_STATUS_SENDING_FAILED)
    status_str = "<span color='red'> x</span>";
  else if (status == CHATTY_STATUS_SENT)
    status_str = " ✓";
  else if (status == CHATTY_STATUS_DELIVERED)
    status_str = "<span color='#6cba3d'> ✓</span>";

  time_stamp = chatty_message_get_time (self->message);
  time_str = chatty_clock_get_human_time (chatty_clock_get_default (),
                                          time_stamp, TRUE);

  footer = g_strconcat (time_str, status_str, NULL);
  gtk_label_set_markup (GTK_LABEL (self->footer_label), footer);
  gtk_widget_set_visible (self->footer_label, footer && *footer);

  time_signal = message_row_get_clock_signal (time_stamp);

  if (time_signal != g_object_get_data (G_OBJECT (self), "time-signal")) {
    g_clear_signal_handler (&self->clock_id, chatty_clock_get_default ());
    g_object_set_data (G_OBJECT (self), "time-signal", (gpointer)time_signal);

    if (time_signal)
      self->clock_id = g_signal_connect_object (chatty_clock_get_default (), time_signal,
                                                G_CALLBACK (chatty_message_row_update_footer),
                                                self, G_CONNECT_SWAPPED);
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

static void
message_row_update_message (ChattyMessageRow *self)
{
  g_autofree char *message = NULL;

  g_assert (CHATTY_IS_MESSAGE_ROW (self));
  g_assert (self->message);

  chatty_message_row_update_footer (self);

  /* Don't show author label for outgoing SMS/MMS */
  if (chatty_message_get_msg_direction (self->message) == CHATTY_DIRECTION_OUT &&
      self->protocol & (CHATTY_PROTOCOL_MMS | CHATTY_PROTOCOL_MMS_SMS))
    return;

  if (!self->is_im || self->protocol == CHATTY_PROTOCOL_MATRIX) {
    const char *alias;

    alias = chatty_message_get_user_alias (self->message);

    if (alias)
      gtk_label_set_label (GTK_LABEL (self->author_label), alias);

    g_clear_object (&self->name_binding);

    if (chatty_message_get_user (self->message))
      self->name_binding = g_object_bind_property (chatty_message_get_user (self->message), "name",
                                                   self->author_label, "label",
                                                   G_BINDING_SYNC_CREATE);

    alias = gtk_label_get_label (GTK_LABEL (self->author_label));
    gtk_widget_set_visible (self->author_label, alias && *alias);
  }
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
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-message-row.ui");
  gtk_widget_class_bind_template_child (widget_class, ChattyMessageRow, content_grid);
  gtk_widget_class_bind_template_child (widget_class, ChattyMessageRow, avatar_image);
  gtk_widget_class_bind_template_child (widget_class, ChattyMessageRow, hidden_box);
  gtk_widget_class_bind_template_child (widget_class, ChattyMessageRow, author_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyMessageRow, message_event_box);
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

  self->activate_gesture = gtk_gesture_multi_press_new (self->message_event_box);
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (self->activate_gesture), GDK_BUTTON_PRIMARY);
  g_signal_connect_swapped (self->activate_gesture, "pressed",
                            G_CALLBACK (message_activate_gesture_cb), self);
}

GtkWidget *
chatty_message_row_new (ChattyMessage  *message,
                        ChattyProtocol  protocol,
                        gboolean        is_im)
{
  ChattyMessageRow *self;
  GtkStyleContext *sc;
  ChattyMsgDirection direction;
  ChattyMsgType type;

  g_return_val_if_fail (CHATTY_IS_MESSAGE (message), NULL);

  self = g_object_new (CHATTY_TYPE_MESSAGE_ROW, NULL);
  self->protocol = protocol;

  self->message = g_object_ref (message);
  self->is_im = !!is_im;
  direction = chatty_message_get_msg_direction (message);
  type = chatty_message_get_msg_type (message);

  if (type == CHATTY_MESSAGE_IMAGE) {
    self->content = chatty_image_item_new (message, protocol);
    sc = chatty_image_item_get_style (CHATTY_IMAGE_ITEM (self->content));
  } else {
    self->content = chatty_text_item_new (message, protocol);
    sc = chatty_text_item_get_style (CHATTY_TEXT_ITEM (self->content));
  }

  gtk_style_context_add_class (sc, "message_bubble");
  gtk_container_add (GTK_CONTAINER (self->message_event_box), self->content);
  gtk_widget_show (self->content);

  if (direction == CHATTY_DIRECTION_IN) {
    gtk_style_context_add_class (sc, "bubble_white");
    gtk_widget_set_halign (self->content_grid, GTK_ALIGN_START);
    gtk_widget_set_halign (self->message_event_box, GTK_ALIGN_START);
    gtk_widget_set_halign (self->author_label, GTK_ALIGN_START);
  } else if (direction == CHATTY_DIRECTION_OUT && protocol == CHATTY_PROTOCOL_MMS_SMS) {
    gtk_style_context_add_class (sc, "bubble_green");
  } else if (direction == CHATTY_DIRECTION_OUT) {
    gtk_style_context_add_class (sc, "bubble_blue");
  } else { /* System message */
    gtk_style_context_add_class (sc, "bubble_purple");
    gtk_widget_set_hexpand (self->message_event_box, TRUE);
  }

  if (direction == CHATTY_DIRECTION_OUT) {
    gtk_label_set_xalign (GTK_LABEL (self->footer_label), 1);
    gtk_widget_set_halign (self->content_grid, GTK_ALIGN_END);
    gtk_widget_set_halign (self->message_event_box, GTK_ALIGN_END);
    gtk_widget_set_halign (self->author_label, GTK_ALIGN_END);
  } else {
    gtk_label_set_xalign (GTK_LABEL (self->footer_label), 0);
  }

  if ((is_im && protocol != CHATTY_PROTOCOL_MATRIX) || direction == CHATTY_DIRECTION_SYSTEM ||
      direction == CHATTY_DIRECTION_OUT)
    gtk_widget_hide (self->avatar_image);
  else
    chatty_avatar_set_item (CHATTY_AVATAR (self->avatar_image),
                            chatty_message_get_user (message));

  g_signal_connect_object (message, "updated",
                           G_CALLBACK (message_row_update_message),
                           self, G_CONNECT_SWAPPED);
  message_row_update_message (self);

  return GTK_WIDGET (self);
}

ChattyMessage *
chatty_message_row_get_item (ChattyMessageRow *self)
{
  g_return_val_if_fail (CHATTY_IS_MESSAGE_ROW (self), NULL);

  return self->message;
}

void
chatty_message_row_hide_footer (ChattyMessageRow *self)
{
  g_return_if_fail (CHATTY_IS_MESSAGE_ROW (self));

  self->force_hide_footer = TRUE;
  gtk_widget_hide (self->footer_label);
}

void
chatty_message_row_set_alias (ChattyMessageRow *self,
                              const char       *alias)
{
  g_return_if_fail (CHATTY_IS_MESSAGE_ROW (self));

  chatty_avatar_set_title (CHATTY_AVATAR (self->avatar_image), alias);
}

void
chatty_message_row_hide_user_detail (ChattyMessageRow *self)
{
  g_return_if_fail (CHATTY_IS_MESSAGE_ROW (self));

  gtk_widget_hide (self->author_label);
  if (gtk_widget_get_visible (self->avatar_image)) {
    gtk_widget_hide (self->avatar_image);
    gtk_widget_show (self->hidden_box);
  }
}
