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
#include "users/chatty-contact.h"
#include "users/chatty-pp-buddy.h"
#include "chatty-message-row.h"
#include "chatty-chat-view.h"

struct _ChattyChatView
{
  GtkBox      parent_instance;

  GtkWidget  *message_list;
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
  GtkWidget  *empty_label2;
  GtkTextBuffer *message_input_buffer;
  GtkAdjustment *vadjustment;

  ChattyChat *chat;
  PurpleConversation *conv;
  char       *last_message_id;  /* id of last sent message, currently used only for SMS */
  guint       message_type;
  guint       refresh_typing_id;
  gboolean    first_scroll_to_bottom;
};

static GHashTable *ht_sms_id = NULL;

#define INDICATOR_WIDTH   60
#define INDICATOR_HEIGHT  40
#define INDICATOR_MARGIN   2
#define MSG_BUBBLE_MAX_RATIO .3

#define LAZY_LOAD_INITIAL_MSGS_LIMIT 20

G_DEFINE_TYPE (ChattyChatView, chatty_chat_view, GTK_TYPE_BOX)


const char *disclaimer_strings[][3] = {
  {
    N_("This is an IM conversation."),
    N_("Your messages are not encrypted,"),
    N_("ask your counterpart to use E2EE."),
  },
  {
    N_("This is an IM conversation."),
    N_("Your messages are secured"),
    N_("by end-to-end encryption."),
  },
  {
    N_("This is an SMS conversation."),
    N_("Your messages are not encrypted,"),
    N_("and carrier rates may apply."),
  },
};

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
chat_view_setup_file_upload (ChattyChatView *self)
{
  PurplePluginProtocolInfo *prpl_info;
  PurpleConnection         *gc;
  PurpleBlistNode          *node;
  g_autoptr(GList)          list = NULL;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  gtk_widget_show (self->send_file_button);

  gc = purple_conversation_get_gc (self->conv);
  node = chatty_utils_get_conv_blist_node (self->conv);
  prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO (gc->prpl);

  if (prpl_info->blist_node_menu)
    list = prpl_info->blist_node_menu (node);

  for (GList *l = list; l; l = l->next) {
    PurpleMenuAction *act = l->data;

    if (g_strcmp0 (act->label, "HTTP File Upload") == 0) {
      g_object_set_data (G_OBJECT (self->send_file_button),
                         "callback", act->callback);

      g_object_set_data (G_OBJECT (self->send_file_button),
                         "callback-data", act->data);
    }
    purple_menu_action_free (act);
  }
}

static void
chatty_chat_view_update (ChattyChatView *self)
{
  GtkStyleContext *context;
  int index = -1;


  g_assert (CHATTY_IS_CHAT_VIEW (self));

  if (chatty_chat_is_im (self->chat) &&
      chatty_item_get_protocols (CHATTY_ITEM (self->chat)) == CHATTY_PROTOCOL_SMS)
    self->message_type = CHATTY_MSG_TYPE_SMS;
  else if (chatty_chat_is_im (self->chat))
    self->message_type = CHATTY_MSG_TYPE_IM;
  else
    self->message_type = CHATTY_MSG_TYPE_MUC;

  gtk_widget_show (self->encrypt_icon);

  if (chatty_chat_is_im (self->chat) && CHATTY_IS_PP_CHAT (self->chat))
    chatty_pp_chat_load_encryption_status (CHATTY_PP_CHAT (self->chat));

  if (chatty_manager_has_file_upload_plugin (chatty_manager_get_default ()) &&
      chatty_item_get_protocols (CHATTY_ITEM (self->chat)) == CHATTY_PROTOCOL_XMPP)
    chat_view_setup_file_upload (self);

  if (self->message_type == CHATTY_MSG_TYPE_IM)
    index = 0;
  else if (self->message_type == CHATTY_MSG_TYPE_SMS)
    index = 2;

  /* XXX: This is temporary, strings put into different labels isn’t good for translation */
  if (index >= 0) {
    gtk_label_set_label (GTK_LABEL (self->empty_label0), disclaimer_strings[index][0]);
    gtk_label_set_label (GTK_LABEL (self->empty_label1), disclaimer_strings[index][1]);
    gtk_label_set_label (GTK_LABEL (self->empty_label2), disclaimer_strings[index][2]);
  }

  context = gtk_widget_get_style_context (self->send_message_button);

  if (self->message_type == CHATTY_MSG_TYPE_SMS)
    gtk_style_context_add_class (context, "button_send_green");
  else if (self->message_type == CHATTY_MSG_TYPE_IM)
    gtk_style_context_add_class (context, "suggested-action");
}

static void
chatty_update_typing_status (ChattyChatView *self)
{
  PurpleConversation     *conv;
  PurpleConvIm           *im;
  GtkTextIter             start, end;
  g_autofree char         *text = NULL;
  gboolean                empty;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  conv = self->conv;

  if (!chatty_chat_is_im (self->chat))
    return;

  gtk_text_buffer_get_bounds (self->message_input_buffer, &start, &end);
  text = gtk_text_buffer_get_text (self->message_input_buffer, &start, &end, FALSE);

  empty = !text || !*text || *text == '/';

  im = PURPLE_CONV_IM (conv);

  if (!empty) {
    gboolean send = (purple_conv_im_get_send_typed_timeout (im) == 0);

    purple_conv_im_stop_send_typed_timeout (im);
    purple_conv_im_start_send_typed_timeout (im);

    if (send || (purple_conv_im_get_type_again (im) != 0 &&
                 time (NULL) > purple_conv_im_get_type_again (im))) {
      unsigned int timeout;

      timeout = serv_send_typing (purple_conversation_get_gc (conv),
                                  purple_conversation_get_name (conv),
                                  PURPLE_TYPING);

      purple_conv_im_set_type_again (im, timeout);
    }
  } else {
    purple_conv_im_stop_send_typed_timeout (im);

    serv_send_typing (purple_conversation_get_gc (conv),
                      purple_conversation_get_name (conv),
                      PURPLE_NOT_TYPING);
  }
}

static void
chat_view_edge_overshot_cb (ChattyChatView  *self,
                            GtkPositionType  pos)
{
  g_assert (CHATTY_IS_CHAT_VIEW (self));

  if (pos == GTK_POS_TOP)
    chatty_manager_load_more_chat (chatty_manager_get_default (), self->chat, LAZY_LOAD_INITIAL_MSGS_LIMIT);
}


static GtkWidget *
chat_view_message_row_new (ChattyMessage  *message,
                           ChattyChatView *self)
{
  GtkWidget *row;
  ChattyProtocol protocol;
  gboolean is_im = TRUE;

  g_assert (CHATTY_IS_MESSAGE (message));
  g_assert (CHATTY_IS_CHAT_VIEW (self));

  if (self->message_type == CHATTY_MSG_TYPE_MUC)
    is_im = FALSE;

  protocol = chatty_item_get_protocols (CHATTY_ITEM (self->chat));
  row = chatty_message_row_new (message, protocol, is_im);
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
  if (self->message_type == CHATTY_MSG_TYPE_MUC)
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



static gboolean
chatty_conv_check_for_command (ChattyChatView *self)
{
  PurpleConversation *conv;
  gchar              *cmd;
  gboolean            retval = FALSE;
  GtkTextIter         start, end;
  PurpleMessageFlags  flags = 0;

  flags |= PURPLE_MESSAGE_NO_LOG | PURPLE_MESSAGE_SYSTEM;
  conv = self->conv;

  gtk_text_buffer_get_bounds (self->message_input_buffer, &start, &end);

  cmd = gtk_text_buffer_get_text (self->message_input_buffer, &start, &end, FALSE);

  if (cmd && *cmd == '/') {
    PurpleCmdStatus status;
    gchar *error, *cmdline;

    cmdline = cmd + strlen ("/");

    if (purple_strequal (cmdline, "xyzzy")) {
      purple_conversation_write (conv,
                                 "",
                                 "Nothing happens",
                                 flags,
                                 time(NULL));

      g_free (cmd);
      return TRUE;
    }

    purple_conversation_write (conv,
                               "",
                               cmdline,
                               flags,
                               time(NULL));

    status = purple_cmd_do_command (conv, cmdline, cmdline, &error);

    switch (status) {
    case PURPLE_CMD_STATUS_OK:
      retval = TRUE;
      break;
    case PURPLE_CMD_STATUS_NOT_FOUND:
      {
        PurplePluginProtocolInfo *prpl_info = NULL;
        PurpleConnection *gc;

        if ((gc = purple_conversation_get_gc (conv)))
          prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO(gc->prpl);

        if ((prpl_info != NULL) &&
            (prpl_info->options & OPT_PROTO_SLASH_COMMANDS_NATIVE)) {
          gchar *spaceslash;

          /* If the first word in the entered text has a '/' in it, then the user
           * probably didn't mean it as a command. So send the text as message. */
          spaceslash = cmdline;

          while (*spaceslash && *spaceslash != ' ' && *spaceslash != '/') {
            spaceslash++;
          }

          if (*spaceslash != '/') {
            purple_conversation_write (conv,
                                       "",
                                       "Unknown command. Get a list of available commands with '/chatty help'",
                                       flags,
                                       time(NULL));
            retval = TRUE;
          }
        }
        break;
      }
    case PURPLE_CMD_STATUS_WRONG_ARGS:
      purple_conversation_write (conv,
                                 "",
                                 "Wrong number of arguments for the command.",
                                 flags,
                                 time(NULL));
      retval = TRUE;
      break;
    case PURPLE_CMD_STATUS_FAILED:
      purple_conversation_write (conv,
                                 "",
                                 error ? error : "The command failed.",
                                 flags,
                                 time(NULL));
      g_free(error);
      retval = TRUE;
      break;
    case PURPLE_CMD_STATUS_WRONG_TYPE:
      if (purple_conversation_get_type (conv) == PURPLE_CONV_TYPE_IM)
        purple_conversation_write (conv,
                                   "",
                                   "That command only works in chats, not IMs.",
                                   flags,
                                   time(NULL));
      else
        purple_conversation_write (conv,
                                   "",
                                   "That command only works in IMs, not chats.",
                                   flags,
                                   time(NULL));
      retval = TRUE;
      break;
    case PURPLE_CMD_STATUS_WRONG_PRPL:
      purple_conversation_write (conv,
                                 "",
                                 "That command doesn't work on this protocol.",
                                 flags,
                                 time(NULL));
      retval = TRUE;
      break;
    default:
      break;
    }
  }

  g_free (cmd);
  return retval;
}


static void
chat_view_send_file_button_clicked_cb (ChattyChatView *self,
                                       GtkButton      *button)
{
  PurpleBlistNode *node;
  gpointer data;

  void (*callback)(gpointer, gpointer);

  g_assert (CHATTY_IS_CHAT_VIEW (self));
  g_assert (GTK_IS_BUTTON (button));

  callback = g_object_get_data (G_OBJECT (button), "callback");
  data = g_object_get_data (G_OBJECT (button), "callback-data");
  node = chatty_utils_get_conv_blist_node (self->conv);

  if (callback)
    callback (node, data);
}

static void
chat_view_send_message_button_clicked_cb (ChattyChatView *self)
{
  PurpleConversation  *conv;
  ChattyAccount *account;
  GtkTextIter    start, end;
  gchar         *message = NULL;
  gchar         *sms_id_str;
  guint          sms_id;

  g_assert (CHATTY_IS_CHAT_VIEW (self));

  conv = self->conv;

  gtk_text_buffer_get_bounds (self->message_input_buffer, &start, &end);

  if (chatty_conv_check_for_command (self)) {
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

  message = gtk_text_buffer_get_text (self->message_input_buffer, &start, &end, FALSE);

  if (gtk_text_buffer_get_char_count (self->message_input_buffer)) {
    g_autofree char *escaped = NULL;
    ChattyProtocol protocol;

    protocol = chatty_item_get_protocols (CHATTY_ITEM (self->chat));

    /* provide a msg-id to the sms-plugin for send-receipts */
    if (chatty_item_get_protocols (CHATTY_ITEM (self->chat)) == CHATTY_PROTOCOL_SMS) {
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
        protocol == CHATTY_PROTOCOL_XMPP)
      escaped = purple_markup_escape_text (message, -1);

    if (conv && purple_conversation_get_type (conv) == PURPLE_CONV_TYPE_IM) {
      purple_conv_im_send (PURPLE_CONV_IM(conv), escaped ? escaped : message);
    } else if (conv && purple_conversation_get_type (conv) == PURPLE_CONV_TYPE_CHAT) {
      purple_conv_chat_send (PURPLE_CONV_CHAT (conv), escaped ? escaped : message);
    }

    gtk_widget_hide (self->send_message_button);
  }

  gtk_text_buffer_delete (self->message_input_buffer, &start, &end);

  g_free (message);
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

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/chatty/"
                                               "ui/chatty-chat-view.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, message_list);
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
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, empty_label2);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, message_input_buffer);
  gtk_widget_class_bind_template_child (widget_class, ChattyChatView, vadjustment);

  gtk_widget_class_bind_template_callback (widget_class, chat_view_edge_overshot_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_typing_indicator_draw_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_input_focus_in_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_input_focus_out_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_send_file_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_send_message_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_input_key_pressed_cb);
  gtk_widget_class_bind_template_callback (widget_class, chat_view_message_input_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, list_page_size_changed_cb);
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

  if (CHATTY_IS_PP_CHAT (chat))
    self->conv = chatty_pp_chat_get_purple_conv (CHATTY_PP_CHAT (chat));

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

  chat_encrypt_changed_cb (self);
  chat_buddy_typing_changed_cb (self);
  chatty_chat_view_update (self);
}

ChattyChat *
chatty_chat_view_get_chat (ChattyChatView *self)
{
  g_return_val_if_fail (CHATTY_IS_CHAT_VIEW (self), NULL);

  return self->chat;
}
