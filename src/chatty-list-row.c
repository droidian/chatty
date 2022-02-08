/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-list-row.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *   Andrea Schäfer <mosibasu@me.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>

#include "chatty-purple.h"
#include "chatty-contact.h"
#include "chatty-contact-list.h"
#include "chatty-chat.h"
#include "chatty-avatar.h"
#include "chatty-clock.h"
#include "chatty-list-row.h"
#include "chatty-contact-provider.h"

struct _ChattyListRow
{
  GtkListBoxRow  parent_instance;

  GtkWidget     *avatar;
  GtkWidget     *title;
  GtkWidget     *subtitle;
  GtkWidget     *last_modified;
  GtkWidget     *unread_message_count;
  GtkWidget     *checkbox;
  GtkWidget     *close_button;
  GtkWidget     *add_contact_button;
  GtkWidget     *call_button;

  ChattyItem    *item;
  gboolean       hide_chat_details;
  gulong         clock_id;
};

G_DEFINE_TYPE (ChattyListRow, chatty_list_row, GTK_TYPE_LIST_BOX_ROW)

static gboolean
chatty_list_row_item_is_valid (ChattyItem *item)
{
  return CHATTY_IS_CONTACT (item) ||
#ifdef PURPLE_ENABLED
    CHATTY_IS_PP_BUDDY (item) ||
#endif
    CHATTY_IS_CHAT (item);
}

static const char *
list_row_get_clock_signal (time_t time_stamp)
{
  double diff;

  diff = difftime (time (NULL), time_stamp);

  if (diff >= - SECONDS_PER_MINUTE &&
      diff < SECONDS_PER_WEEK)
    return g_intern_static_string ("day-changed");

  return NULL;
}

static gboolean
chatty_list_row_update_last_modified (ChattyListRow *self)
{
  ChattyChat *item;
  g_autofree char *str = NULL;
  const char *time_signal;
  time_t last_message_time;

  item = CHATTY_CHAT (self->item);
  last_message_time = chatty_chat_get_last_msg_time (item);
  if (!last_message_time) {
    g_clear_signal_handler (&self->clock_id, chatty_clock_get_default ());

    return G_SOURCE_REMOVE;
  }

  str = chatty_clock_get_human_time (chatty_clock_get_default (),
                                     last_message_time, FALSE);
  if (str)
    gtk_label_set_label (GTK_LABEL (self->last_modified), str);

  time_signal = list_row_get_clock_signal (last_message_time);

  if (time_signal != g_object_get_data (G_OBJECT (self), "time-signal")) {
    g_clear_signal_handler (&self->clock_id, chatty_clock_get_default ());
    g_object_set_data (G_OBJECT (self), "time-signal", (gpointer)time_signal);

    if (time_signal)
      self->clock_id = g_signal_connect_object (chatty_clock_get_default (), time_signal,
                                                G_CALLBACK (chatty_list_row_update_last_modified),
                                                self, G_CONNECT_SWAPPED);
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

#ifdef PURPLE_ENABLED
static char *
list_row_user_flag_to_str (ChattyUserFlag flags)
{
  const char *color_tag;
  const char *status;

  if (flags & CHATTY_USER_FLAG_OWNER) {
    status = _("Owner");
    color_tag = "<span color='#4d86ff'>";
  } else if (flags & CHATTY_USER_FLAG_MODERATOR) {
    status = _("Moderator");
    color_tag = "<span color='#66e6ff'>";
  } else if (flags & CHATTY_USER_FLAG_MEMBER) {
    status = _("Member");
    color_tag = "<span color='#c0c0c0'>";
  } else {
    color_tag = "<span color='#000000'>";
    status = "";
  }

  return g_strconcat (color_tag, status, "</span>", NULL);
}
#endif

static void
chatty_list_row_update (ChattyListRow *self)
{
  const char *subtitle = NULL;

  g_assert (CHATTY_IS_LIST_ROW (self));
  g_assert (CHATTY_IS_ITEM (self->item));

#ifdef PURPLE_ENABLED
  if (CHATTY_IS_PP_BUDDY (self->item)) {
    if (chatty_pp_buddy_get_buddy (CHATTY_PP_BUDDY (self->item))) { /* Buddy in contact list */
      ChattyAccount *account;

      account = chatty_pp_buddy_get_account (CHATTY_PP_BUDDY (self->item));
      subtitle = chatty_item_get_username (CHATTY_ITEM (account));
    } else { /* Buddy in chat list */
      g_autofree char *markup = NULL;
      ChattyUserFlag flag;

      flag = chatty_pp_buddy_get_flags (CHATTY_PP_BUDDY (self->item));
      markup = list_row_user_flag_to_str (flag);
      gtk_label_set_markup (GTK_LABEL (self->subtitle), markup);
      gtk_widget_show (self->subtitle);
    }
  } else
#endif

  if (CHATTY_IS_CONTACT (self->item)) {
    g_autofree gchar *type = NULL;
    const gchar *number;

    number = chatty_item_get_username (self->item);

    if (chatty_contact_is_dummy (CHATTY_CONTACT (self->item)))
      type = g_strdup (number);
    else
      type = g_strconcat (chatty_contact_get_value_type (CHATTY_CONTACT (self->item)), number, NULL);
    gtk_label_set_label (GTK_LABEL (self->subtitle), type);
    chatty_item_get_avatar (self->item);
  } else if (CHATTY_IS_CHAT (self->item) && !self->hide_chat_details) {
    g_autofree char *unread = NULL;
    const char *last_message;
    ChattyChat *item;
    guint unread_count;
    time_t last_message_time;

    item = CHATTY_CHAT (self->item);
    last_message = chatty_chat_get_last_message (item);

    gtk_widget_set_visible (self->subtitle, last_message && *last_message);
    if (last_message && *last_message) {
      g_autofree char *message_stripped = NULL;

#ifdef PURPLE_ENABLED
      message_stripped = purple_markup_strip_html (last_message);
#else
      message_stripped = g_strdup (last_message);
#endif
      g_strstrip (message_stripped);

      gtk_label_set_label (GTK_LABEL (self->subtitle), message_stripped);
    }

    unread_count = chatty_chat_get_unread_count (item);
    gtk_widget_set_visible (self->unread_message_count, unread_count > 0);

    if (unread_count) {
      unread = g_strdup_printf ("%d", unread_count);
      gtk_label_set_text (GTK_LABEL (self->unread_message_count), unread);
    }

    last_message_time = chatty_chat_get_last_msg_time (item);
    gtk_widget_set_visible (self->last_modified, last_message_time > 0);

    chatty_list_row_update_last_modified (self);
  }

  if (subtitle)
    gtk_label_set_label (GTK_LABEL (self->subtitle), subtitle);
}

static void
write_eds_contact_cb (GObject      *object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  ChattyListRow *self = user_data;
  g_autoptr(GError) error = NULL;

  if (chatty_eds_write_contact_finish (result, &error)) {
    gtk_widget_hide (self->add_contact_button);
    return;
  }
}

static void
chatty_list_row_delete_clicked_cb (ChattyListRow *self)
{
  GtkWidget *scrolled, *list;

  g_assert (CHATTY_IS_LIST_ROW (self));

  /* We can't directly use CHATTY_TYPE_CONTACT_LIST as it's not linked with the shared library */
  scrolled = gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_SCROLLED_WINDOW);

  if (!scrolled)
    return;

  list = gtk_widget_get_parent (scrolled);
  if (list)
    g_signal_emit_by_name (list, "delete-row", self);
}

static void
chatty_list_row_add_contact_clicked_cb (ChattyListRow *self)
{
  const char *phone;

  g_return_if_fail (CHATTY_IS_CONTACT (self->item));

  phone = gtk_label_get_text (GTK_LABEL (self->subtitle));
  chatty_eds_write_contact_async ("", phone,
                                  write_eds_contact_cb,
                                  g_object_ref (self));
}

static void
chatty_list_row_call_button_clicked_cb (ChattyListRow *self)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *uri = NULL;

  g_return_if_fail (CHATTY_IS_CONTACT (self->item));

  uri = g_strconcat ("tel://", chatty_item_get_username (self->item), NULL);

  g_debug ("Calling uri: %s", uri);
  if (!gtk_show_uri_on_window (NULL, uri, GDK_CURRENT_TIME, &error))
    g_warning ("Failed to launch call: %s", error->message);
}

static void
chatty_list_row_finalize (GObject *object)
{
  ChattyListRow *self = (ChattyListRow *)object;

  g_clear_object (&self->item);

  G_OBJECT_CLASS (chatty_list_row_parent_class)->finalize (object);
}

static void
chatty_list_row_class_init (ChattyListRowClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = chatty_list_row_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-list-row.ui");
  gtk_widget_class_bind_template_child (widget_class, ChattyListRow, avatar);
  gtk_widget_class_bind_template_child (widget_class, ChattyListRow, checkbox);
  gtk_widget_class_bind_template_child (widget_class, ChattyListRow, close_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyListRow, title);
  gtk_widget_class_bind_template_child (widget_class, ChattyListRow, subtitle);
  gtk_widget_class_bind_template_child (widget_class, ChattyListRow, last_modified);
  gtk_widget_class_bind_template_child (widget_class, ChattyListRow, unread_message_count);
  gtk_widget_class_bind_template_child (widget_class, ChattyListRow, add_contact_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyListRow, call_button);

  gtk_widget_class_bind_template_callback (widget_class, chatty_list_row_delete_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, chatty_list_row_add_contact_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, chatty_list_row_call_button_clicked_cb);
}

static void
chatty_list_row_init (ChattyListRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
chatty_list_row_new (ChattyItem *item)
{
  ChattyListRow *self;

  g_return_val_if_fail (chatty_list_row_item_is_valid (item), NULL);

  self = g_object_new (CHATTY_TYPE_LIST_ROW, NULL);
  chatty_list_row_set_item (self, item);

  return GTK_WIDGET (self);
}

/**
 * chatty_list_contact_row_new:
 * @item: A #ChattyItem
 *
 * Create and return a new list row to be used in contact
 * list.  If the @item is a #ChattyChat no chat details
 * will be shown (like unread count, time, etc.)
 *
 * Returns: (transfer full): A #ChattyListRow
 */
GtkWidget *
chatty_list_contact_row_new (ChattyItem *item)
{
  ChattyListRow *self;

  g_return_val_if_fail (chatty_list_row_item_is_valid (item), NULL);

  self = g_object_new (CHATTY_TYPE_LIST_ROW, NULL);
  self->hide_chat_details = TRUE;
  chatty_list_row_set_item (self, item);

  return GTK_WIDGET (self);
}

ChattyItem *
chatty_list_row_get_item (ChattyListRow *self)
{
  g_return_val_if_fail (CHATTY_IS_LIST_ROW (self), NULL);

  return self->item;
}

void
chatty_list_row_set_item (ChattyListRow *self,
                          ChattyItem    *item)
{
  g_return_if_fail (CHATTY_IS_LIST_ROW (self));
  g_return_if_fail (chatty_list_row_item_is_valid (item));

  g_set_object (&self->item, item);
  chatty_avatar_set_item (CHATTY_AVATAR (self->avatar), item);
  g_object_bind_property (item, "name",
                          self->title, "label",
                          G_BINDING_SYNC_CREATE);

  if (CHATTY_IS_CHAT (item))
    g_signal_connect_object (item, "changed",
                             G_CALLBACK (chatty_list_row_update),
                             self, G_CONNECT_SWAPPED);
  chatty_list_row_update (self);
}

void
chatty_list_row_set_selectable (ChattyListRow *self, gboolean enable)
{
  g_return_if_fail (CHATTY_IS_LIST_ROW (self));

  gtk_widget_set_visible (self->checkbox, enable);
}

void
chatty_list_row_select (ChattyListRow *self, gboolean enable)
{
  g_return_if_fail (CHATTY_IS_LIST_ROW (self));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->checkbox),
                                enable);
}

void
chatty_list_row_set_contact (ChattyListRow *self, gboolean enable)
{
  g_return_if_fail (CHATTY_IS_LIST_ROW (self));

  gtk_widget_set_visible (self->add_contact_button, enable);
}

void
chatty_list_row_set_call (ChattyListRow *self, gboolean enable)
{
  g_autoptr(GAppInfo) app_info = NULL;

  g_return_if_fail (CHATTY_IS_LIST_ROW (self));

  app_info = g_app_info_get_default_for_uri_scheme ("tel");

  if (app_info) {
    gboolean user_valid;
    user_valid = CHATTY_IS_CONTACT (self->item) &&
                 chatty_utils_username_is_valid (chatty_item_get_username (self->item),
                                                 CHATTY_PROTOCOL_MMS_SMS);

    gtk_widget_set_visible (self->call_button, enable && user_valid);
  } else
    gtk_widget_hide (self->call_button);
}

void
chatty_list_row_show_delete_button (ChattyListRow *self)
{
  g_return_if_fail (CHATTY_IS_LIST_ROW (self));

  gtk_widget_show (self->close_button);
  gtk_widget_hide (self->checkbox);
}
