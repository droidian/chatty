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
#include "chatty-list-row.h"
#include "chatty-contact-provider.h"

#define SECONDS_PER_MINUTE 60.0
#define SECONDS_PER_HOUR   3600.0
#define SECONDS_PER_MONTH  2592000.0
#define SECONDS_PER_YEAR   31536000.0


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
  int            last_modified_timeout;
};

G_DEFINE_TYPE (ChattyListRow, chatty_list_row, GTK_TYPE_LIST_BOX_ROW)


/* Copied from chatty-utils by Andrea Schäfer */
static char *
chatty_time_ago_in_words (time_t time_stamp)
{
  // based on the ruby on rails method 'distance_of_time_in_words'

  time_t time_now;
  struct tm  *timeinfo;

  g_autofree gchar *iso_timestamp = NULL;

  const char *unit;
  const char *prefix;

  const char *str_about, *str_less_than;
  const char *str_seconds, *str_minute, *str_minutes;
  const char *str_hour, *str_hours, *str_day, *str_days;
  const char *str_month, *str_months, *str_year, *str_years;

  int number, seconds, minutes, hours, days, months, years, offset, remainder;

  gboolean show_date = FALSE;

  double dist_in_seconds;

  str_about     =    "~";
  str_less_than =    "<";
  /* Translators: Timestamp seconds suffix */
  str_seconds   = C_("timestamp-suffix-seconds", "s");
  /* Translators: Timestamp minute suffix */
  str_minute    = C_("timestamp-suffix-minute", "m");
  /* Translators: Timestamp minutes suffix */
  str_minutes   = C_("timestamp-suffix-minutes", "m");
  /* Translators: Timestamp hour suffix */
  str_hour      = C_("timestamp-suffix-hour", "h");
  /* Translators: Timestamp hours suffix */
  str_hours     = C_("timestamp-suffix-hours", "h");
  /* Translators: Timestamp day suffix */
  str_day       = C_("timestamp-suffix-day", "d");
  /* Translators: Timestamp days suffix */
  str_days      = C_("timestamp-suffix-days", "d");
  /* Translators: Timestamp month suffix */
  str_month     = C_("timestamp-suffix-month", "mo");
  /* Translators: Timestamp months suffix */
  str_months    = C_("timestamp-suffix-months", "mos");
  /* Translators: Timestamp year suffix */
  str_year      = C_("timestamp-suffix-year", "y");
  /* Translators: Timestamp years suffix */
  str_years     = C_("timestamp-suffix-years", "y");

  time_now = time (NULL);

  timeinfo = localtime (&time_stamp);

  iso_timestamp = g_malloc0 (MAX_GMT_ISO_SIZE * sizeof(char));

  strftime (iso_timestamp,
            MAX_GMT_ISO_SIZE * sizeof(char),
            "%d.%m.%y",
            timeinfo);

  dist_in_seconds = difftime (time_now, time_stamp);

  seconds = (int)dist_in_seconds;
  minutes = (int)(dist_in_seconds / SECONDS_PER_MINUTE);
  hours   = (int)(dist_in_seconds / SECONDS_PER_HOUR);
  days    = (int)(dist_in_seconds / SECONDS_PER_DAY);
  months  = (int)(dist_in_seconds / SECONDS_PER_MONTH);
  years   = (int)(dist_in_seconds / SECONDS_PER_YEAR);

  switch (minutes) {
  case 0 ... 1:
    unit = str_seconds;

    switch (seconds) {
    case 0 ... 14:
      prefix = str_less_than;
      number = 15;
      break;
    case 15 ... 29:
      prefix = str_less_than;
      number = 30;
      break;
    case 30 ... 59:
      prefix = str_less_than;
      number = 1;
      unit = str_minute;
      break;
    default:
      prefix = str_about;
      number = 1;
      unit = str_minute;
      break;
    }
    break;

  case 2 ... 44:
    prefix = "";
    number = minutes;
    unit = str_minutes;
    break;
  case 45 ... 89:
    prefix = str_about;
    number = 1;
    unit = str_hour;
    break;
  case 90 ... 1439:
    prefix = str_about;
    number = hours;
    unit = str_hours;
    break;
  case 1440 ... 2529:
    prefix = str_about;
    number = 1;
    unit = str_day;
    show_date = TRUE;
    break;
  case 2530 ... 43199:
    prefix = "";
    number = days;
    unit = str_days;
    show_date = TRUE;
    break;
  case 43200 ... 86399:
    prefix = str_about;
    number = 1;
    unit = str_month;
    show_date = TRUE;
    break;
  case 86400 ... 525600:
    prefix = "";
    number = months;
    unit = str_months;
    show_date = TRUE;
    break;

  default:
    number = years;

    unit = (number == 1) ? str_year : str_years;

    offset = (int)((float)years / 4.0) * 1440.0;

    remainder = (minutes - offset) % 525600;
    show_date = TRUE;

    if (remainder < 131400) {
      prefix = str_about;
    } else if (remainder < 394200) {
      /* Translators: Timestamp prefix (e.g. Over 5h) */
      prefix = _("Over");
    } else {
      ++number;
      unit = str_years;
      /* Translators: Timestamp prefix (e.g. Almost 5h) */
      prefix = _("Almost");
    }
    break;
  }

  return show_date ? g_strdup_printf ("%s", iso_timestamp) :
    g_strdup_printf ("%s%d%s", prefix, number, unit);
}

static gboolean
chatty_list_row_item_is_valid (ChattyItem *item)
{
  return CHATTY_IS_CONTACT (item) ||
#ifdef PURPLE_ENABLED
    CHATTY_IS_PP_BUDDY (item) ||
#endif
    CHATTY_IS_CHAT (item);
}

static gboolean
chatty_list_row_update_last_modified (ChattyListRow *self)
{
  ChattyChat *item;
  g_autofree char *str = NULL;
  double delta_secs;
  time_t last_message_time;
  time_t time_now;

  self->last_modified_timeout = 0;

  item = CHATTY_CHAT (self->item);
  last_message_time = chatty_chat_get_last_msg_time (item);
  if (!last_message_time)
    return G_SOURCE_REMOVE;

  str = chatty_time_ago_in_words (last_message_time);
  if (str)
    gtk_label_set_label (GTK_LABEL (self->last_modified), str);

  time_now = time (NULL);
  delta_secs = difftime (time_now, last_message_time);

  if (delta_secs < SECONDS_PER_HOUR * 24) {
    /* Update the time when it's 20% older and at least 30 seconds have passed */
    int update_delay = MAX (delta_secs * 0.2, 30);

    self->last_modified_timeout =
      g_timeout_add_seconds (update_delay,
                             G_SOURCE_FUNC (chatty_list_row_update_last_modified),
                             self);
  }

  return G_SOURCE_REMOVE;
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

  g_clear_handle_id (&self->last_modified_timeout, g_source_remove);

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

  g_clear_handle_id (&self->last_modified_timeout, g_source_remove);
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
