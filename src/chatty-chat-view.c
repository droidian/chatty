/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-chat-view.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib/gi18n.h>

#include "chatty-avatar.h"
#include "chatty-chat.h"
#include "chatty-pp-chat.h"
#include "chatty-history.h"
#include "chatty-icons.h"
#include "chatty-manager.h"
#include "chatty-utils.h"
#include "chatty-window.h"
#include "matrix/chatty-ma-chat.h"
#include "users/chatty-contact.h"
#include "users/chatty-pp-buddy.h"
#include "chatty-message-row.h"
#include "chatty-chat-view.h"

struct _ChattyChatView
{
  GtkBox      parent_instance;

  GtkWidget  *message_list;
  GtkWidget  *loading_spinner;
  GtkWidget  *typing_revealer;
  GtkWidget  *typing_indicator;
  GtkWidget  *chatty_message_list;
  GtkWidget  *input_frame;
  GtkWidget  *scrolled_window;
  GtkWidget  *message_input;
  GtkWidget  *send_file_button;
  GtkWidget  *encrypt_icon;
  GtkWidget  *send_message_button;
  GtkWidget  *empty_view;
  GtkWidget  *empty_label0;
  GtkWidget  *empty_label1;
  GtkWidget  *scroll_down_button;
  GtkTextBuffer *message_input_buffer;
  GtkAdjustment *vadjustment;

  ChattyChat *chat;
  char       *last_message_id;  /* id of last sent message, currently used only for SMS */
  guint       refresh_typing_id;
  gboolean    first_scroll_to_bottom;
};

static GHashTable *ht_sms_id = NULL;

#define INDICATOR_WIDTH   60
#define INDICATOR_HEIGHT  40
#define INDICATOR_MARGIN   2
#define MSG_BUBBLE_MAX_RATIO .3

G_DEFINE_TYPE (ChattyChatView, chatty_chat_view, GTK_TYPE_BOX)


const char *emoticons[][15] = {
  {":)", "🙂"},
  {";)", "😉"},
  {":(", "🙁"},
  {":'(", "😢"},
  {":/", "😕"},
  {":D", "😀"},
  {":'D", "😂"},
  {";P", "😜"},
  {":P", "😛"},
  {";p", "😜"},
  {":p", "😛"},
  {":o", "😮"},
  {"B)", "😎 "},
  {"SANTA", "🎅"},
  {"FROSTY", "⛄"},
};

enum {
  FILE_REQUESTED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static gboolean
chat_view_time_is_same_day (time_t time_a,
                            time_t time_b)
{
  struct tm *tm;
  int day_a, day_b;

  if (difftime (time_a, time_b) > SECONDS_PER_DAY)
    return FALSE;

  tm = localtime (&time_a);
  day_a = tm->tm_yday;

  tm = localtime (&time_b);
  day_b = tm->tm_yday;

  if (day_a == day_b)
    return TRUE;

  return FALSE;
}

static gboolean
chat_view_hash_table_match_item (gpointer key,
                                 gpointer value,
                                 gpointer user_data)
{
  return value == user_data;
}

static void
chatty_draw_typing_indicator (cairo_t *cr)
{
  double dot_pattern [3][3]= {{0.5, 0.9, 0.9},
                              {0.7, 0.5, 0.9},
                              {0.9, 0.7, 0.5}};
  guint  dot_origins [3] = {15, 30, 45};
  double grey_lev,
    x, y,
    width, height,
    rad, deg;

  static guint i;

  deg = G_PI / 180.0;

  rad = INDICATOR_MARGIN * 5;
  x = y = INDICATOR_MARGIN;
  width = INDICATOR_WIDTH - INDICATOR_MARGIN * 2;
  height = INDICATOR_HEIGHT - INDICATOR_MARGIN * 2;

  if (i > 2)
    i = 0;

  cairo_new_sub_path (cr);
  cairo_arc (cr, x + width - rad, y + rad, rad, -90 * deg, 0 * deg);
  cairo_arc (cr, x + width - rad, y + height - rad, rad, 0 * deg, 90 * deg);
  cairo_arc (cr, x + rad, y + height - rad, rad, 90 * deg, 180 * deg);
  cairo_arc (cr, x + rad, y + rad, rad, 180 * deg, 270 * deg);
  cairo_close_path (cr);

  cairo_set_source_rgb (cr, 0.7, 0.7, 0.7);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);

  for (guint n = 0; n < 3; n++) {
    cairo_arc (cr, dot_origins[n], 20, 5, 0, 2 * G_PI);
    grey_lev = dot_pattern[i][n];
    cairo_set_source_rgb (cr, grey_lev, grey_lev, grey_lev);
    cairo_fill (cr);
  }

  i++;
}


static gboolean
chat_view_typing_indicator_draw_cb (ChattyChatView *self,
                                    cairo_t        *cr)
{
  g_assert (CHATTY_IS_CHAT_VIEW (self));

  if (self->refresh_typing_id > 0)
    chatty_draw_typing_indicator (cr);

  return TRUE;
}

static gboolean
chat_view_indicator_refresh_cb (ChattyChatView *self)
{
  g_assert (CHATTY_IS_CHAT_VIEW (self));

  gtk_widget_queue_draw (self->typing_indicator);

  return G_SOURCE_CONTINUE;
}

static void
chatty_check_for_emoticon (ChattyChatView *self)
{
  GtkTextIter start, end, position;
  g_autofree char *text = NULL;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  gtk_text_buffer_get_bounds (self->message_input_buffer, &start, &end);
  text = gtk_text_buffer_get_text (self->message_input_buffer, &start, &end, FALSE);

  for (guint i = 0; i < G_N_ELEMENTS (emoticons); i++)
    if (g_str_has_suffix (text, emoticons[i][0])) {
      position = end;

      gtk_text_iter_backward_chars (&position, strlen (emoticons[i][0]));
      gtk_text_buffer_delete (self->message_input_buffer, &position, &end);
      gtk_text_buffer_insert (self->message_input_buffer, &position, emoticons[i][1], -1);

      break;
    }
}

static void
chatty_chat_view_update (ChattyChatView *self)
{
  GtkStyleContext *context;
  ChattyProtocol protocol;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  protocol = chatty_item_get_protocols (CHATTY_ITEM (self->chat));

  gtk_widget_show (self->encrypt_icon);

  if (chatty_chat_is_im (self->chat) && CHATTY_IS_PP_CHAT (self->chat))
    chatty_pp_chat_load_encryption_status (CHATTY_PP_CHAT (self->chat));

  gtk_widget_set_visible (self->send_file_button, chatty_chat_has_file_upload (self->chat));

  if (protocol == CHATTY_PROTOCOL_SMS) {
    gtk_label_set_label (GTK_LABEL (self->empty_label0),
                         _("This is an SMS conversation"));
    gtk_label_set_label (GTK_LABEL (self->empty_label1),
                         _("Your messages are not encrypted, "
                           "and carrier rates may apply"));
  } else if (chatty_chat_is_im (self->chat)) {
    gtk_label_set_label (GTK_LABEL (self->empty_label0),
                         _("This is an IM conversation"));
    if (chatty_chat_get_encryption (self->chat) == CHATTY_ENCRYPTION_ENABLED)
      gtk_label_set_label (GTK_LABEL (self->empty_label1),
                           _("Your messages are encrypted"));
    else
      gtk_label_set_label (GTK_LABEL (self->empty_label1),
                           _("Your messages are not encrypted"));
  }

  context = gtk_widget_get_style_context (self->send_message_button);

  if (protocol == CHATTY_PROTOCOL_SMS)
    gtk_style_context_add_class (context, "button_send_green");
  else if (chatty_chat_is_im (self->chat))
    gtk_style_context_add_class (context, "suggested-action");
}

static void
chatty_update_typing_status (ChattyChatView *self)
{
  GtkTextIter             start, end;
  g_autofree char         *text = NULL;
  gboolean                empty;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  gtk_text_buffer_get_bounds (self->message_input_buffer, &start, &end);
  text = gtk_text_buffer_get_text (self->message_input_buffer, &start, &end, FALSE);

  empty = !text || !*text || *text == '/';
  chatty_chat_set_typing (self->chat, !empty);
}

static void
chat_view_scroll_down_clicked_cb (ChattyChatView *self)
{
  g_assert (CHATTY_IS_CHAT_VIEW (self));

  gtk_adjustment_set_value (self->vadjustment,
                            gtk_adjustment_get_upper (self->vadjustment));
}

static void
chat_view_edge_overshot_cb (ChattyChatView  *self,
                            GtkPositionType  pos)
{
  g_assert (CHATTY_IS_CHAT_VIEW (self));

  if (pos == GTK_POS_TOP)
    chatty_chat_load_past_messages (self->chat, -1);
}


static GtkWidget *
chat_view_message_row_new (ChattyMessage  *message,
                           ChattyChatView *self)
{
  GtkWidget *row;
  ChattyProtocol protocol;

  g_assert (CHATTY_IS_MESSAGE (message));
  g_assert (CHATTY_IS_CHAT_VIEW (self));

  protocol = chatty_item_get_protocols (CHATTY_ITEM (self->chat));
  row = chatty_message_row_new (message, protocol, chatty_chat_is_im (self->chat));
  chatty_message_row_set_alias (CHATTY_MESSAGE_ROW (row),
                                chatty_message_get_user_alias (message));

  return GTK_WIDGET (row);
}

static void
messages_items_changed_cb (ChattyChatView *self,
                           guint           position,
                           guint           removed,
                           guint           added)
{
  ChattyMessage *next_msg, *msg;
  GtkListBoxRow *row, *next_row;
  GtkListBox *list;
  time_t next_time, time;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  if (added == 0)
    return;

  /* Don't hide footers in group chats */
  if (!chatty_chat_is_im (self->chat))
    return;

  list = GTK_LIST_BOX (self->message_list);

  for (gint i = position; i < position + added; i++) {
    next_row = gtk_list_box_get_row_at_index (list, i + 1);

    if (!next_row)
      break;

    row = gtk_list_box_get_row_at_index (list, i);
    msg = chatty_message_row_get_item (CHATTY_MESSAGE_ROW (row));
    time = chatty_message_get_time (msg);

    next_msg = chatty_message_row_get_item (CHATTY_MESSAGE_ROW (next_row));
    next_time = chatty_message_get_time (next_msg);

    /*
     * Hide time if the message following the current one belong to the same
     * day
     */
    if (chat_view_time_is_same_day (time, next_time))
      chatty_message_row_hide_footer (CHATTY_MESSAGE_ROW (row));
  }
}

static void
chat_encrypt_changed_cb (ChattyChatView *self)
{
  GtkStyleContext *context;
  const char *icon_name;
  ChattyEncryption encryption;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  context = gtk_widget_get_style_context (self->encrypt_icon);
  encryption = chatty_chat_get_encryption (self->chat);

  if (encryption == CHATTY_ENCRYPTION_ENABLED) {
    icon_name = "changes-prevent-symbolic";
    gtk_style_context_remove_class (context, "dim-label");
    gtk_style_context_add_class (context, "encrypt");
  } else {
    icon_name = "changes-allow-symbolic";
    gtk_style_context_add_class (context, "dim-label");
    gtk_style_context_remove_class (context, "encrypt");
  }

  gtk_image_set_from_icon_name (GTK_IMAGE (self->encrypt_icon), icon_name, 1);
}

static void
chat_buddy_typing_changed_cb (ChattyChatView *self)
{
  g_assert (CHATTY_IS_CHAT_VIEW (self));

  if (chatty_chat_get_buddy_typing (self->chat)) {
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->typing_revealer), TRUE);
    self->refresh_typing_id = g_timeout_add (300,
                                             (GSourceFunc)chat_view_indicator_refresh_cb,
                                             self);
  } else {
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->typing_revealer), FALSE);
    g_clear_handle_id (&self->refresh_typing_id, g_source_remove);
  }
}

static gboolean
chat_view_input_focus_in_cb (ChattyChatView *self)
{
  GtkStyleContext *context;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  context = gtk_widget_get_style_context (self->input_frame);
  gtk_style_context_remove_class (context, "msg_entry_defocused");
  gtk_style_context_add_class (context, "msg_entry_focused");

  return FALSE;
}


static gboolean
chat_view_input_focus_out_cb (ChattyChatView *self)
{
  GtkStyleContext *context;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  context = gtk_widget_get_style_context (self->input_frame);
  gtk_style_context_remove_class (context, "msg_entry_focused");
  gtk_style_context_add_class (context, "msg_entry_defocused");

  return FALSE;
}

static void
chat_view_send_file_button_clicked_cb (ChattyChatView *self,
                                       GtkButton      *button)
{
  g_assert (CHATTY_IS_CHAT_VIEW (self));
  g_assert (GTK_IS_BUTTON (button));
  g_return_if_fail (chatty_chat_has_file_upload (self->chat));

  if (CHATTY_IS_MA_CHAT (self->chat)) {
    /* TODO */

  } else if (CHATTY_IS_PP_CHAT (self->chat)) {
    chatty_pp_chat_show_file_upload (CHATTY_PP_CHAT (self->chat));
  }
}

static void
chat_view_send_message_button_clicked_cb (ChattyChatView *self)
{
  PurpleConversation *conv = NULL;
  ChattyAccount *account;
  g_autoptr(ChattyMessage) msg = NULL;
  g_autofree char *message = NULL;
  GtkTextIter    start, end;
  gchar         *sms_id_str;
  guint          sms_id;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  if (CHATTY_IS_PP_CHAT (self->chat))
    conv = chatty_pp_chat_get_purple_conv (CHATTY_PP_CHAT (self->chat));

  gtk_text_buffer_get_bounds (self->message_input_buffer, &start, &end);
  message = gtk_text_buffer_get_text (self->message_input_buffer, &start, &end, FALSE);

  if (CHATTY_IS_PP_CHAT (self->chat) &&
      chatty_pp_chat_run_command (CHATTY_PP_CHAT (self->chat), message)) {
    gtk_widget_hide (self->send_message_button);
    gtk_text_buffer_delete (self->message_input_buffer, &start, &end);

    return;
  }

  account = chatty_chat_get_account (self->chat);
  if (chatty_account_get_status (account) != CHATTY_CONNECTED)
    return;

  gtk_widget_grab_focus (self->message_input);

  if (conv)
    purple_idle_touch ();

  if (gtk_text_buffer_get_char_count (self->message_input_buffer)) {
    g_autofree char *escaped = NULL;
    ChattyProtocol protocol;

    protocol = chatty_item_get_protocols (CHATTY_ITEM (self->chat));

    /* provide a msg-id to the sms-plugin for send-receipts */
    if (conv && chatty_item_get_protocols (CHATTY_ITEM (self->chat)) == CHATTY_PROTOCOL_SMS) {
      sms_id = g_random_int ();

      sms_id_str = g_strdup_printf ("%i", sms_id);

      g_hash_table_insert (ht_sms_id, sms_id_str, g_object_ref (self->chat));

      g_debug ("hash table insert sms_id_str: %s  ht_size: %i",
               sms_id_str, g_hash_table_size (ht_sms_id));

      purple_conv_im_send_with_flags (PURPLE_CONV_IM (conv),
                                      sms_id_str,
                                      PURPLE_MESSAGE_NO_LOG |
                                      PURPLE_MESSAGE_NOTIFY |
                                      PURPLE_MESSAGE_INVISIBLE);
    }

    if (protocol == CHATTY_PROTOCOL_MATRIX ||
        protocol == CHATTY_PROTOCOL_XMPP ||
        protocol == CHATTY_PROTOCOL_TELEGRAM)
      escaped = purple_markup_escape_text (message, -1);

    msg = chatty_message_new (NULL, NULL, escaped ? escaped : message,
                              NULL, time (NULL),
                              escaped ? CHATTY_MESSAGE_HTML_ESCAPED : CHATTY_MESSAGE_TEXT,
                              CHATTY_DIRECTION_OUT, 0);
    chatty_chat_send_message_async (self->chat, msg, NULL, NULL);

    gtk_widget_hide (self->send_message_button);
  }

  gtk_text_buffer_delete (self->message_input_buffer, &start, &end);
}

static gboolean
chat_view_input_key_pressed_cb (ChattyChatView *self,
                                GdkEventKey    *event_key)
{
  g_assert (CHATTY_IS_CHAT_VIEW (self));

  if (!(event_key->state & GDK_SHIFT_MASK) && event_key->keyval == GDK_KEY_Return &&
      chatty_settings_get_return_sends_message (chatty_settings_get_default ())) {
    if (gtk_text_buffer_get_char_count (self->message_input_buffer) > 0)
      chat_view_send_message_button_clicked_cb (self);
    else
      gtk_widget_error_bell (self->message_input);

    return TRUE;
  }

  return FALSE;
}


static void
chat_view_message_input_changed_cb (ChattyChatView *self)
{
  gboolean has_text;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  has_text = gtk_text_buffer_get_char_count (self->message_input_buffer) > 0;
  gtk_widget_set_visible (self->send_message_button, has_text);

  if (chatty_settings_get_send_typing (chatty_settings_get_default ()))
    chatty_update_typing_status (self);

  if (chatty_settings_get_convert_emoticons (chatty_settings_get_default ()) &&
      chatty_item_get_protocols (CHATTY_ITEM (self->chat)) != CHATTY_PROTOCOL_SMS)
    chatty_check_for_emoticon (self);
}

static void chat_view_adjustment_value_changed_cb (ChattyChatView *self);
static void
list_page_size_changed_cb (ChattyChatView *self)
{
  gdouble size, upper, value;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  size  = gtk_adjustment_get_page_size (self->vadjustment);
  value = gtk_adjustment_get_value (self->vadjustment);
  upper = gtk_adjustment_get_upper (self->vadjustment);

  if (upper - size <= DBL_EPSILON)
    return;

  /* If close to bottom, scroll to bottom */
  if (!self->first_scroll_to_bottom || upper - value < (size * 1.75))
    gtk_adjustment_set_value (self->vadjustment, upper);

  self->first_scroll_to_bottom = TRUE;
  chat_view_adjustment_value_changed_cb (self);
}

static void
chat_view_adjustment_value_changed_cb (ChattyChatView *self)
{
  gdouble value, upper, page_size;

  upper = gtk_adjustment_get_upper (self->vadjustment);
  value = gtk_adjustment_get_value (self->vadjustment);
  page_size = gtk_adjustment_get_page_size (self->vadjustment);

  gtk_widget_set_visible (self->scroll_down_button,
                          (upper - value) > page_size + 1.0);
}

static void
chat_view_adjustment_changed_cb (GtkAdjustment  *adjustment,
                                 GParamSpec     *pspec,
                                 ChattyChatView *self)
{
  GtkAdjustment *vadjust;
  GtkWidget     *vscroll;
  gdouble        upper;
  gdouble        page_size;
  gint           max_height;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  vadjust = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolled_window));
  vscroll = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (self->scrolled_window));
  upper = gtk_adjustment_get_upper (GTK_ADJUSTMENT (vadjust));
  page_size = gtk_adjustment_get_page_size (GTK_ADJUSTMENT (vadjust));
  max_height = gtk_scrolled_window_get_max_content_height (GTK_SCROLLED_WINDOW (self->scrolled_window));

  gtk_adjustment_set_value (vadjust, upper - page_size);

  if (upper > (gdouble)max_height) {
    gtk_widget_set_visible (vscroll, TRUE);
    gtk_widget_hide (self->encrypt_icon);
  } else {
    gtk_widget_set_visible (vscroll, FALSE);
    gtk_widget_show (self->encrypt_icon);
  }

  chat_view_adjustment_value_changed_cb (self);
}

static void
chat_view_get_files_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  g_autoptr(ChattyChatView) self = user_data;
}

static void
chat_view_file_requested_cb (ChattyChatView *self,
                             ChattyMessage  *message)
{
  chatty_chat_get_files_async (self->chat, message,
                               chat_view_get_files_cb,
                               g_object_ref (self));
}

static void
chat_view_sms_sent_cb (const char *sms_id,
                       int         status)
{
  ChattyChat    *chat;
  ChattyMessage *message;
  GListModel    *message_list;
  const gchar *message_id;
  ChattyMsgStatus sent_status;
  time_t       time_now;
  guint        n_items;

  if (sms_id == NULL)
    return;

  if (status == CHATTY_SMS_RECEIPT_NONE)
    sent_status = CHATTY_STATUS_SENDING_FAILED;
  else if (status == CHATTY_SMS_RECEIPT_MM_ACKN)
    sent_status = CHATTY_STATUS_SENT;
  else if (status == CHATTY_SMS_RECEIPT_SMSC_ACKN)
    sent_status = CHATTY_STATUS_DELIVERED;
  else
    return;

  chat = g_hash_table_lookup (ht_sms_id, sms_id);

  if (!chat)
    return;

  message_list = chatty_chat_get_messages (chat);
  n_items = g_list_model_get_n_items (message_list);
  message = g_list_model_get_item (message_list, n_items - 1);
  message_id = chatty_message_get_id (message);
  time_now = time (NULL);

  if (message_id == NULL)
    chatty_message_set_id (message, sms_id);

  if (g_strcmp0 (message_id, sms_id) == 0) {
    chatty_message_set_status (message, sent_status, time_now);
    g_object_unref (message);
    return;
  }

  message = chatty_pp_chat_find_message_with_id (CHATTY_PP_CHAT (chat), sms_id);

  if (message)
    chatty_message_set_status (message, sent_status, time_now);
}

static void
chatty_chat_view_map (GtkWidget *widget)
{
  ChattyChatView *self = (ChattyChatView *)widget;

  GTK_WIDGET_CLASS (chatty_chat_view_parent_class)->map (widget);

  gtk_widget_grab_focus (self->message_input);
}

static void
chatty_chat_view_finalize (GObject *object)
{
  ChattyChatView *self = (ChattyChatView *)object;

  g_hash_table_foreach_remove (ht_sms_id,
                               chat_view_hash_table_match_item,
                               self);
  g_clear_object (&self->chat);

  G_OBJECT_CLASS (chatty_chat_view_parent_class)->finalize (object);
}

static void
chatty_chat_view_class_init (ChattyChatViewClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = chatty_chat_view_finalize;

  widget_class->map = chatty_chat_view_map;

  signals [FILE_REQUESTED] =
    g_signal_new ("file-requested",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, CHATTY_TYPE_MESSAGE);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-chat-view.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, scroll_down_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, message_list);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, loading_spinner);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, typing_revealer);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, typing_indicator);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, input_frame);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, scrolled_window);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, message_input);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, send_file_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, encrypt_icon);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, send_message_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, empty_view);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, empty_label0);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, empty_label1);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, message_input_buffer);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, vadjustment);

  gtk_widget_class_bind_template_callback (widget_class, chat_view_scroll_down_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_edge_overshot_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_typing_indicator_draw_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_input_focus_in_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_input_focus_out_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_send_file_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_send_message_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_input_key_pressed_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_message_input_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, list_page_size_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_adjustment_value_changed_cb);
}

static void
chatty_chat_view_init (ChattyChatView *self)
{
  GtkAdjustment *vadjustment;

  gtk_widget_init_template (GTK_WIDGET (self));
  gtk_list_box_set_placeholder(GTK_LIST_BOX (self->message_list), self->empty_view);

  vadjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolled_window));
  g_signal_connect_after (G_OBJECT (vadjustment), "notify::upper",
                          G_CALLBACK (chat_view_adjustment_changed_cb),
                          self);
  g_signal_connect_after (G_OBJECT (self), "file-requested",
                          G_CALLBACK (chat_view_file_requested_cb), self);
}

GtkWidget *
chatty_chat_view_new (void)
{
  return g_object_new (CHATTY_TYPE_CHAT_VIEW, NULL);
}


void
chatty_chat_view_purple_init (void)
{
  ht_sms_id = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

  purple_signal_connect (purple_conversations_get_handle (),
                         "sms-sent", ht_sms_id,
                         PURPLE_CALLBACK (chat_view_sms_sent_cb), NULL);
}

void
chatty_chat_view_purple_uninit (void)
{
  purple_signals_disconnect_by_handle (ht_sms_id);

  g_hash_table_destroy (ht_sms_id);
}

void
chatty_chat_view_set_chat (ChattyChatView *self,
                           ChattyChat     *chat)
{
  g_return_if_fail (CHATTY_IS_CHAT_VIEW (self));
  g_return_if_fail (CHATTY_IS_CHAT (chat));

  if (!g_set_object (&self->chat, chat))
    return;

  if (!chat)
    return;

  gtk_list_box_bind_model (GTK_LIST_BOX (self->message_list),
                           chatty_chat_get_messages (self->chat),
                           (GtkListBoxCreateWidgetFunc)chat_view_message_row_new,
                           self, NULL);
  g_signal_connect_object (chatty_chat_get_messages (self->chat),
                           "items-changed",
                           G_CALLBACK (messages_items_changed_cb),
                           self,
                           G_CONNECT_SWAPPED | G_CONNECT_AFTER);
  g_signal_connect_object (self->chat, "notify::encrypt",
                           G_CALLBACK (chat_encrypt_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->chat, "notify::buddy-typing",
                           G_CALLBACK (chat_buddy_typing_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_object_bind_property (self->chat, "loading-history",
                          self->loading_spinner, "active",
                          G_BINDING_SYNC_CREATE);

  chat_encrypt_changed_cb (self);
  chat_buddy_typing_changed_cb (self);
  chatty_chat_view_update (self);
  chat_view_adjustment_value_changed_cb (self);
}

ChattyChat *
chatty_chat_view_get_chat (ChattyChatView *self)
{
  g_return_val_if_fail (CHATTY_IS_CHAT_VIEW (self), NULL);

  return self->chat;
}
