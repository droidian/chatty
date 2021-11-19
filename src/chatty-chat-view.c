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

#include "config.h"

#include <glib/gi18n.h>
#include <gspell/gspell.h>

#include "chatty-mm-chat.h"
#include "chatty-purple.h"
#include "chatty-settings.h"
#include "chatty-ma-chat.h"
#include "chatty-message-row.h"
#include "chatty-attachments-view.h"
#include "chatty-chat-view.h"

struct _ChattyChatView
{
  GtkStack    parent_instance;

  GtkWidget  *message_view;
  GtkWidget  *empty_view;

  GtkWidget  *message_list;
  GtkWidget  *loading_spinner;
  GtkWidget  *typing_revealer;
  GtkWidget  *typing_indicator;
  GtkWidget  *chatty_message_list;
  GtkWidget  *input_frame;
  GtkWidget  *scrolled_window;
  GtkWidget  *attachment_revealer;
  GtkWidget  *attachment_view;
  GtkWidget  *message_input;
  GtkWidget  *send_file_button;
  GtkWidget  *send_message_button;
  GtkWidget  *send_button_icon;
  GtkWidget  *no_message_status;
  GtkWidget  *scroll_down_button;
  GtkTextBuffer *message_input_buffer;
  GtkAdjustment *vadjustment;

  GDBusProxy *osk_proxy;

  ChattyChat *chat;
  gdouble     last_vadj_upper;
  guint       refresh_typing_id;
  guint       update_view_id;
  guint       osk_id;
  gboolean    first_scroll_to_bottom;
};

#define INDICATOR_WIDTH   60
#define INDICATOR_HEIGHT  40
#define INDICATOR_MARGIN   2
#define MSG_BUBBLE_MAX_RATIO .3

G_DEFINE_TYPE (ChattyChatView, chatty_chat_view, GTK_TYPE_STACK)


const char *emoticons[][2] = {
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

#ifdef PURPLE_ENABLED
  if (chatty_chat_is_im (self->chat) && CHATTY_IS_PP_CHAT (self->chat))
    chatty_pp_chat_load_encryption_status (CHATTY_PP_CHAT (self->chat));
#endif

  gtk_widget_set_visible (self->send_file_button, chatty_chat_has_file_upload (self->chat));

  if (protocol == CHATTY_PROTOCOL_MMS_SMS) {
    hdy_status_page_set_title (HDY_STATUS_PAGE (self->no_message_status),
                               _("This is an SMS conversation"));
    hdy_status_page_set_description (HDY_STATUS_PAGE (self->no_message_status),
                                     _("Your messages are not encrypted, "
                                       "and carrier rates may apply"));
  } else if (chatty_chat_is_im (self->chat)) {
    hdy_status_page_set_title (HDY_STATUS_PAGE (self->no_message_status),
                               _("This is an IM conversation"));
    hdy_status_page_set_description (HDY_STATUS_PAGE (self->no_message_status),
                                     _("Your messages are not encrypted, "
                                       "and carrier rates may apply"));
    if (chatty_chat_get_encryption (self->chat) == CHATTY_ENCRYPTION_ENABLED)
      hdy_status_page_set_description (HDY_STATUS_PAGE (self->no_message_status),
                                       _("Your messages are encrypted"));
    else
      hdy_status_page_set_description (HDY_STATUS_PAGE (self->no_message_status),
                                       _("Your messages are not encrypted"));
  }

  context = gtk_widget_get_style_context (self->send_message_button);

  if (protocol == CHATTY_PROTOCOL_MMS_SMS) {
    gtk_style_context_remove_class (context, "suggested-action");
    gtk_style_context_add_class (context, "button_send_green");
  } else if (chatty_chat_is_im (self->chat)) {
    gtk_style_context_remove_class (context, "button_send_green");
    gtk_style_context_add_class (context, "suggested-action");
  }
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
  gtk_widget_hide (self->scroll_down_button);
}

static void
chat_view_edge_overshot_cb (ChattyChatView  *self,
                            GtkPositionType  pos)
{
  g_assert (CHATTY_IS_CHAT_VIEW (self));

  if (pos == GTK_POS_TOP)
    chatty_chat_load_past_messages (self->chat, -1);
}

static void
chat_view_attachment_revealer_notify_cb (ChattyChatView *self)
{
  gboolean has_files, has_text;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  has_files = gtk_revealer_get_reveal_child (GTK_REVEALER (self->attachment_revealer));
  has_text = gtk_text_buffer_get_char_count (self->message_input_buffer) > 0;

  gtk_widget_set_visible (self->send_message_button, has_files || has_text);
}

static void
chat_account_status_changed_cb (ChattyChatView *self)
{
  ChattyAccount *account;
  gboolean enabled;

  account = chatty_chat_get_account (self->chat);
  g_return_if_fail (account);

  enabled = chatty_account_get_status (account) == CHATTY_CONNECTED;
  gtk_widget_set_sensitive (self->message_input, enabled);
  gtk_widget_set_sensitive (self->send_file_button, enabled);
  gtk_widget_set_sensitive (self->send_message_button, enabled);
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
chat_encrypt_changed_cb (ChattyChatView *self)
{
  const char *icon_name;
  ChattyEncryption encryption;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  encryption = chatty_chat_get_encryption (self->chat);

  if (encryption == CHATTY_ENCRYPTION_ENABLED)
    icon_name = "send-encrypted-symbolic";
   else
     icon_name = "send-symbolic";

  gtk_image_set_from_icon_name (GTK_IMAGE (self->send_button_icon), icon_name, 1);
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

static void
chat_view_loading_history_cb (ChattyChatView *self)
{
  gboolean loading;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  loading = chatty_chat_is_loading_history (self->chat);

  if (loading) {
    gtk_spinner_start (GTK_SPINNER (self->loading_spinner));
    gtk_widget_set_opacity (self->loading_spinner, 1.0);
  } else {
    gtk_spinner_stop (GTK_SPINNER (self->loading_spinner));
    gtk_widget_set_opacity (self->loading_spinner, 0.0);
  }
}

static void
chat_view_message_items_changed (ChattyChatView *self)
{
  GListModel *messages;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  if (!self->chat)
    return;

  messages = chatty_chat_get_messages (self->chat);

  if (g_list_model_get_n_items (messages) == 0)
    gtk_widget_set_valign (self->message_list, GTK_ALIGN_FILL);
  else
    gtk_widget_set_valign (self->message_list, GTK_ALIGN_END);
}

static void
chat_view_show_file_chooser (ChattyChatView *self)
{
  GtkWindow *window;
  GtkWidget *dialog;
  int response;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  window = gtk_application_get_active_window (GTK_APPLICATION (g_application_get_default ()));
  dialog = gtk_file_chooser_dialog_new (_("Select File..."),
                                        window,
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        _("Cancel"), GTK_RESPONSE_CANCEL,
                                        _("Open"), GTK_RESPONSE_ACCEPT,
                                        NULL);

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if (response == GTK_RESPONSE_ACCEPT) {
    g_autofree char *filename = NULL;

    filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
    chatty_attachments_view_add_file (CHATTY_ATTACHMENTS_VIEW (self->attachment_view), filename);
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->attachment_revealer), TRUE);

    /* Currently multiple files are allowed only for MMS chats */
    gtk_widget_set_sensitive (self->send_file_button, CHATTY_IS_MM_CHAT (self->chat));
  }

  gtk_widget_destroy (dialog);
}

static void
chat_view_send_file_button_clicked_cb (ChattyChatView *self,
                                       GtkButton      *button)
{
  g_assert (CHATTY_IS_CHAT_VIEW (self));
  g_assert (GTK_IS_BUTTON (button));
  g_return_if_fail (chatty_chat_has_file_upload (self->chat));

  if (CHATTY_IS_MM_CHAT (self->chat)) {
    chat_view_show_file_chooser (self);
  } if (CHATTY_IS_MA_CHAT (self->chat)) {
    /* TODO */

  } else {
#ifdef PURPLE_ENABLED
    chatty_pp_chat_show_file_upload (CHATTY_PP_CHAT (self->chat));
#endif
  }
}

static void
view_send_message_async_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  g_autoptr(ChattyChatView) self = user_data;

  chatty_chat_set_unread_count (self->chat, 0);
}

static void
chat_view_send_message_button_clicked_cb (ChattyChatView *self)
{
  ChattyAccount *account;
  g_autoptr(ChattyMessage) msg = NULL;
  g_autofree char *message = NULL;
  GtkTextIter    start, end;
  GList *files;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  files = chatty_attachments_view_get_files (CHATTY_ATTACHMENTS_VIEW (self->attachment_view));

  gtk_text_buffer_get_bounds (self->message_input_buffer, &start, &end);
  message = gtk_text_buffer_get_text (self->message_input_buffer, &start, &end, FALSE);

#ifdef PURPLE_ENABLED
  if (CHATTY_IS_PP_CHAT (self->chat) &&
      chatty_pp_chat_run_command (CHATTY_PP_CHAT (self->chat), message)) {
    gtk_widget_hide (self->send_message_button);
    gtk_text_buffer_delete (self->message_input_buffer, &start, &end);

    return;
  }
#endif

  account = chatty_chat_get_account (self->chat);
  if (chatty_account_get_status (account) != CHATTY_CONNECTED)
    return;

  gtk_widget_grab_focus (self->message_input);

  if (gtk_text_buffer_get_char_count (self->message_input_buffer) || files) {
    g_autofree char *escaped = NULL;

#ifdef PURPLE_ENABLED
    if (CHATTY_IS_PP_CHAT (self->chat))
      escaped = purple_markup_escape_text (message, -1);
#endif

    msg = chatty_message_new (NULL, escaped ? escaped : message,
                              NULL, time (NULL),
                              escaped ? CHATTY_MESSAGE_HTML_ESCAPED : CHATTY_MESSAGE_TEXT,
                              CHATTY_DIRECTION_OUT, 0);
    if (files) {
      chatty_message_set_files (msg, files);
    }
    chatty_chat_send_message_async (self->chat, msg,
                                    view_send_message_async_cb,
                                    g_object_ref (self));

    gtk_widget_hide (self->send_message_button);
  }
  chatty_attachments_view_reset (CHATTY_ATTACHMENTS_VIEW (self->attachment_view));

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
      chatty_item_get_protocols (CHATTY_ITEM (self->chat)) != CHATTY_PROTOCOL_MMS_SMS)
    chatty_check_for_emoticon (self);

  if (gtk_text_buffer_get_line_count (self->message_input_buffer) > 3)
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scrolled_window),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  else
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scrolled_window),
                                    GTK_POLICY_NEVER, GTK_POLICY_NEVER);
}

static void chat_view_adjustment_value_changed_cb (ChattyChatView *self);
static void
list_page_size_changed_cb (ChattyChatView *self)
{
  gdouble size, upper, value, old_upper;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  size  = gtk_adjustment_get_page_size (self->vadjustment);
  value = gtk_adjustment_get_value (self->vadjustment);
  upper = gtk_adjustment_get_upper (self->vadjustment);
  old_upper = self->last_vadj_upper;
  self->last_vadj_upper = upper;

  /* If the view grew in height, don't do anything. */
  if (old_upper < upper)
    return;

  if (upper - size <= DBL_EPSILON)
    return;

  /* If close to bottom, scroll to bottom */
  if (!self->first_scroll_to_bottom || upper - value < (size * 1.15))
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

  /* page_size sometimes reports itself as zero */
  if (page_size < 0.1)
    gtk_widget_hide (self->scroll_down_button);
}

static void
chat_view_update_header_func (ChattyMessageRow *row,
                              ChattyMessageRow *before,
                              gpointer          user_data)
{
  ChattyChatView *self = user_data;
  ChattyMessage *a, *b;
  time_t a_time, b_time;

  if (!before || !row)
    return;

  a = chatty_message_row_get_item (before);
  b = chatty_message_row_get_item (row);
  a_time = chatty_message_get_time (a);
  b_time = chatty_message_get_time (b);

  if (chatty_message_user_matches (a, b))
    chatty_message_row_hide_user_detail (row);

  /* Don't hide footers in outgoing SMS as it helps understanding
   * the delivery status of the message
   */
  if (CHATTY_IS_MM_CHAT (self->chat) &&
      chatty_message_get_msg_direction (a) == CHATTY_DIRECTION_OUT)
    return;

  /* Hide footer of the previous message if both have same time (in minutes) */
  if (a_time / 60 == b_time / 60)
    chatty_message_row_hide_footer (before);
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

static gboolean
update_view_scroll (gpointer user_data)
{
  ChattyChatView *self = user_data;
  gdouble size, upper, value;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  g_clear_handle_id (&self->update_view_id, g_source_remove);

  size  = gtk_adjustment_get_page_size (self->vadjustment);
  value = gtk_adjustment_get_value (self->vadjustment);
  upper = gtk_adjustment_get_upper (self->vadjustment);

  if (upper - size <= DBL_EPSILON)
    return G_SOURCE_REMOVE;

  /* If close to bottom, scroll to bottom */
  if (upper - value < (size * 1.75))
    gtk_adjustment_set_value (self->vadjustment, upper);

  return G_SOURCE_REMOVE;
}

static void
osk_properties_changed_cb (ChattyChatView *self,
                           GVariant       *changed_properties)
{
  g_autoptr(GVariant) value = NULL;
  GtkWindow *window;

  window = (GtkWindow *)gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
  if (!window || !gtk_window_has_toplevel_focus (window))
    return;

  value = g_variant_lookup_value (changed_properties, "Visible", NULL);

  if (value) {
    g_clear_handle_id (&self->update_view_id, g_source_remove);
    if (g_variant_get_boolean (value))
      self->update_view_id = g_timeout_add (60, update_view_scroll, self);
  }
}

static void
osk_proxy_new_cb (GObject      *service,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  g_autoptr(ChattyChatView) self = user_data;
  g_autoptr(GError) error = NULL;

  self->osk_proxy = g_dbus_proxy_new_finish (res, &error);

  if (error) {
    g_warning ("Error creating osk proxy: %s", error->message);
    return;
  }

  g_signal_connect_object (self->osk_proxy, "g-properties-changed",
                           G_CALLBACK (osk_properties_changed_cb), self,
                           G_CONNECT_SWAPPED | G_CONNECT_AFTER);
}

static void
osk_appeared_cb (GDBusConnection *connection,
                 const char      *name,
                 const char      *name_owner,
                 gpointer         user_data)
{
  ChattyChatView *self = user_data;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  g_dbus_proxy_new (connection,
                    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START_AT_CONSTRUCTION |
                    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START |
                    G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                    NULL,
                    "sm.puri.OSK0",
                    "/sm/puri/OSK0",
                    "sm.puri.OSK0",
                    NULL,
                    osk_proxy_new_cb,
                    g_object_ref (self));
}

static void
oks_vanished_cb (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  ChattyChatView *self = user_data;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  g_clear_object (&self->osk_proxy);
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

  g_clear_handle_id (&self->osk_id, g_bus_unwatch_name);
  g_clear_handle_id (&self->update_view_id, g_source_remove);
  g_clear_object (&self->osk_proxy);
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

  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, message_view);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, empty_view);

  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, scroll_down_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, message_list);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, loading_spinner);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, typing_revealer);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, typing_indicator);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, input_frame);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, scrolled_window);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, attachment_revealer);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, attachment_view);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, message_input);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, send_file_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, send_message_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, send_button_icon);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, no_message_status);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, message_input_buffer);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, vadjustment);

  gtk_widget_class_bind_template_callback (widget_class, chat_view_scroll_down_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_edge_overshot_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_attachment_revealer_notify_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_typing_indicator_draw_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_send_file_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_send_message_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_input_key_pressed_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_message_input_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, list_page_size_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_adjustment_value_changed_cb);

  g_type_ensure (CHATTY_TYPE_ATTACHMENTS_VIEW);
}

static void
chatty_chat_view_init (ChattyChatView *self)
{
  GspellTextView *gspell_view;

  gtk_widget_init_template (GTK_WIDGET (self));
  gtk_list_box_set_placeholder (GTK_LIST_BOX (self->message_list), self->no_message_status);
  gtk_stack_set_visible_child (GTK_STACK (self), self->empty_view);

  g_signal_connect_after (G_OBJECT (self), "file-requested",
                          G_CALLBACK (chat_view_file_requested_cb), self);
  gtk_list_box_set_header_func (GTK_LIST_BOX (self->message_list),
                                (GtkListBoxUpdateHeaderFunc)chat_view_update_header_func,
                                g_object_ref (self), g_object_unref);

  self->osk_id = g_bus_watch_name (G_BUS_TYPE_SESSION, "sm.puri.OSK0",
                                   G_BUS_NAME_WATCHER_FLAGS_NONE,
                                   osk_appeared_cb,
                                   oks_vanished_cb,
                                   g_object_ref (self),
                                   g_object_unref);

  gspell_view = gspell_text_view_get_from_gtk_text_view (GTK_TEXT_VIEW (self->message_input));
  gspell_text_view_basic_setup (gspell_view);
}

GtkWidget *
chatty_chat_view_new (void)
{
  return g_object_new (CHATTY_TYPE_CHAT_VIEW, NULL);
}

void
chatty_chat_view_set_chat (ChattyChatView *self,
                           ChattyChat     *chat)
{
  ChattyAccount *account;
  GListModel *messages;

  g_return_if_fail (CHATTY_IS_CHAT_VIEW (self));
  g_return_if_fail (!chat || CHATTY_IS_CHAT (chat));

  if (gtk_text_buffer_get_line_count (self->message_input_buffer) > 3)
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scrolled_window),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  else
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scrolled_window),
                                    GTK_POLICY_NEVER, GTK_POLICY_NEVER);

  if (self->chat && chat != self->chat) {
    gtk_image_set_from_icon_name (GTK_IMAGE (self->send_button_icon), "send-symbolic", 1);
    g_signal_handlers_disconnect_by_func (chatty_chat_get_account (self->chat),
                                          chat_account_status_changed_cb,
                                          self);
    g_signal_handlers_disconnect_by_func (self->chat,
                                          chat_encrypt_changed_cb,
                                          self);
    g_signal_handlers_disconnect_by_func (self->chat,
                                          chat_buddy_typing_changed_cb,
                                          self);
    g_signal_handlers_disconnect_by_func (self->chat,
                                          chat_view_loading_history_cb,
                                          self);
    g_signal_handlers_disconnect_by_func (chatty_chat_get_messages (self->chat),
                                          chat_view_message_items_changed,
                                          self);

    gtk_widget_hide (self->scroll_down_button);
    self->first_scroll_to_bottom = FALSE;
  }

  gtk_widget_set_sensitive (self->message_input, !!chat);
  gtk_widget_set_visible (self->no_message_status, !!chat);

  if (!g_set_object (&self->chat, chat))
    return;

  if (chat)
    gtk_stack_set_visible_child (GTK_STACK (self), self->message_view);
  else
    gtk_stack_set_visible_child (GTK_STACK (self), self->empty_view);

  if (!chat) {
    gtk_list_box_bind_model (GTK_LIST_BOX (self->message_list),
                             NULL, NULL, NULL, NULL);
    return;
  }

  chatty_attachments_view_reset (CHATTY_ATTACHMENTS_VIEW (self->attachment_view));
  messages = chatty_chat_get_messages (chat);
  account = chatty_chat_get_account (chat);

  g_signal_connect_object (messages, "items-changed",
                           G_CALLBACK (chat_view_message_items_changed),
                           self, G_CONNECT_SWAPPED);
  chat_view_message_items_changed (self);

  if (g_list_model_get_n_items (messages) <= 3)
    chatty_chat_load_past_messages (chat, -1);


  if (account)
    g_signal_connect_object (account, "notify::status",
                             G_CALLBACK (chat_account_status_changed_cb),
                             self,
                             G_CONNECT_SWAPPED);

  chat_account_status_changed_cb (self);

  gtk_list_box_bind_model (GTK_LIST_BOX (self->message_list),
                           chatty_chat_get_messages (self->chat),
                           (GtkListBoxCreateWidgetFunc)chat_view_message_row_new,
                           self, NULL);
  g_signal_connect_object (self->chat, "notify::encrypt",
                           G_CALLBACK (chat_encrypt_changed_cb),
                           self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->chat, "notify::buddy-typing",
                           G_CALLBACK (chat_buddy_typing_changed_cb),
                           self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->chat, "notify::loading-history",
                           G_CALLBACK (chat_view_loading_history_cb),
                           self, G_CONNECT_SWAPPED);

  chat_encrypt_changed_cb (self);
  chat_buddy_typing_changed_cb (self);
  chatty_chat_view_update (self);
  chat_view_loading_history_cb (self);
  chat_view_adjustment_value_changed_cb (self);

  gtk_widget_grab_focus (self->message_input);
}

ChattyChat *
chatty_chat_view_get_chat (ChattyChatView *self)
{
  g_return_val_if_fail (CHATTY_IS_CHAT_VIEW (self), NULL);

  return self->chat;
}
