/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-manager.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-manager"

#include "chatty-config.h"

#define LIBFEEDBACK_USE_UNSTABLE_API
#include <libfeedback.h>
#include <glib/gi18n.h>
#include <purple.h>

#include "xeps/xeps.h"
#include "chatty-settings.h"
#include "contrib/gtk.h"
#include "chatty-contact-provider.h"
#include "chatty-utils.h"
#include "chatty-application.h"
#include "chatty-window.h"
#include "chatty-chat-view.h"
#include "users/chatty-pp-account.h"
#include "chatty-chat.h"
#include "chatty-pp-chat.h"
#include "chatty-icons.h"
#include "chatty-notify.h"
#include "chatty-purple-request.h"
#include "chatty-purple-notify.h"
#include "chatty-history.h"
#include "chatty-manager.h"

/**
 * SECTION: chatty-manager
 * @title: ChattyManager
 * @short_description: A class to manage various providers and accounts
 * @include: "chatty-manager.h"
 *
 * libpurple doesn’t have a nice OOP interface for managing anything.
 * This class hides all the complexities surrounding it.
 */

#define LAZY_LOAD_MSGS_LIMIT 12
#define LAZY_LOAD_INITIAL_MSGS_LIMIT 20
#define MAX_TIMESTAMP_SIZE 256
#define CHATTY_UI          "chatty-ui"

struct _ChattyManager
{
  GObject          parent_instance;

  ChattyEds       *chatty_eds;
  GListStore      *account_list;
  GListStore      *chat_list;
  GListStore      *list_of_user_list;
  GtkFlattenListModel *contact_list;
  GtkSortListModel    *sorted_chat_list;
  GtkSorter           *chat_sorter;

  PurplePlugin    *sms_plugin;
  PurplePlugin    *lurch_plugin;
  PurplePlugin    *carbon_plugin;
  PurplePlugin    *file_upload_plugin;

  gboolean         disable_auto_login;
  gboolean         network_available;

  gboolean         has_modem;
  ChattyProtocol   active_protocols;
};

G_DEFINE_TYPE (ChattyManager, chatty_manager, G_TYPE_OBJECT)

/* XXX: A copy from purple-mm-sms */
enum {
  PUR_MM_STATE_NO_MANAGER,
  PUR_MM_STATE_MANAGER_FOUND,
  PUR_MM_STATE_NO_MODEM,
  PUR_MM_STATE_MODEM_FOUND,
  PUR_MM_STATE_NO_MESSAGING_MODEM,
  PUR_MM_STATE_MODEM_DISABLED,
  PUR_MM_STATE_MODEM_UNLOCK_ERROR,
  PUR_MM_STATE_READY
} e_purple_connection;

enum {
  PROP_0,
  PROP_ACTIVE_PROTOCOLS,
  N_PROPS
};

enum {
  AUTHORIZE_BUDDY,
  CHAT_DELETED,
  NOTIFY_ADDED,
  CONNECTION_ERROR,
  OPEN_CHAT,
  N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];
static GHashTable *ui_info = NULL;

#define PURPLE_GLIB_READ_COND  (G_IO_IN | G_IO_HUP | G_IO_ERR)
#define PURPLE_GLIB_WRITE_COND (G_IO_OUT | G_IO_HUP | G_IO_ERR | G_IO_NVAL)

static int
manager_sort_chat_item (ChattyChat *a,
                        ChattyChat *b,
                        gpointer    user_data)
{
  time_t a_time, b_time;

  g_assert (CHATTY_IS_CHAT (a));
  g_assert (CHATTY_IS_CHAT (b));

  a_time = chatty_chat_get_last_msg_time (a);
  b_time = chatty_chat_get_last_msg_time (b);

  return difftime (b_time, a_time);
}

static void
chatty_conv_add_history_since_component (GHashTable *components,
                                         const char *account,
                                         const char *room)
{
  time_t mtime;
  struct tm * timeinfo;

  g_autofree gchar *iso_timestamp = g_malloc0(MAX_GMT_ISO_SIZE * sizeof(char));

  mtime = chatty_history_get_last_message_time (account, room);
  mtime += 1; // Use the next epoch to exclude the last stored message(s)
  timeinfo = gmtime (&mtime);
  g_return_if_fail (strftime (iso_timestamp,
                              MAX_GMT_ISO_SIZE * sizeof(char),
                              "%Y-%m-%dT%H:%M:%SZ",
                              timeinfo));

  g_hash_table_steal (components, "history_since");
  g_hash_table_insert (components, "history_since", g_steal_pointer(&iso_timestamp));
}

static void
manager_get_messages_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  ChattyHistory *history = (ChattyHistory *)object;
  g_autoptr(ChattyManager) self = user_data;
  g_autoptr(GPtrArray) messages = NULL;
  g_autoptr(GError) error = NULL;
  ChattyChat *chat;

  g_assert (CHATTY_IS_MANAGER (self));
  g_assert (CHATTY_IS_HISTORY (history));

  messages = chatty_history_get_messages_finish (history, result, &error);

  if (messages) {
    chat = g_object_get_data (G_OBJECT (result), "chat");
    g_assert (CHATTY_IS_CHAT (chat));

    if (CHATTY_IS_PP_CHAT (chat))
      chatty_pp_chat_prepend_messages (CHATTY_PP_CHAT (chat), messages);
  } else if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    g_warning ("Error fetching messages: %s,", error->message);
  }
}

static void
manager_load_messages_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  ChattyHistory *history = (ChattyHistory *)object;
  g_autoptr(ChattyManager) self = user_data;
  g_autoptr(GPtrArray) messages = NULL;
  g_autoptr(GError) error = NULL;
  ChattyPpChat *chat;

  g_assert (CHATTY_IS_MANAGER (self));
  g_assert (CHATTY_IS_HISTORY (history));

  messages = chatty_history_get_messages_finish (history, result, &error);

  chat = g_object_get_data (G_OBJECT (result), "chat");

  if (!CHATTY_IS_PP_CHAT (chat))
    return;

  if (!messages)
    chatty_pp_chat_set_show_notifications (chat, TRUE);

  if (messages) {
    if (chatty_pp_chat_get_auto_join (chat)) {
      GListModel *model;
      ChattyChat *item;

      item = chatty_manager_add_chat (chatty_manager_get_default (), CHATTY_CHAT (chat));
      model = chatty_chat_get_messages (item);

      /* If at least one message is loaded, don’t add again. */
      if (g_list_model_get_n_items (model) == 0)
        chatty_pp_chat_prepend_messages (CHATTY_PP_CHAT (item), messages);
    }

  } else if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    g_warning ("Error fetching messages: %s,", error->message);
  }
}


static void
manager_eds_is_ready (ChattyManager *self)
{
  GListModel *accounts, *model;
  ChattyContact *contact;
  const char *id;
  ChattyProtocol protocol;
  guint n_accounts, n_buddies;

  g_assert (CHATTY_IS_MANAGER (self));

  accounts = chatty_manager_get_accounts (self);
  n_accounts = g_list_model_get_n_items (accounts);

  /* TODO: Optimize */
  for (guint i = 0; i < n_accounts; i++) {
    g_autoptr(ChattyPpAccount) account = NULL;

    account  = g_list_model_get_item (accounts, i);
    protocol = chatty_item_get_protocols (CHATTY_ITEM (account));

    if (protocol != CHATTY_PROTOCOL_SMS)
      continue;

    model = chatty_pp_account_get_buddy_list (account);
    n_buddies = g_list_model_get_n_items (model);
    for (guint j = 0; j < n_buddies; j++) {
      g_autoptr(ChattyPpBuddy) buddy = NULL;

      buddy = g_list_model_get_item (model, j);
      id = chatty_pp_buddy_get_id (buddy);

      if (chatty_pp_buddy_get_contact (buddy))
        continue;

      contact = chatty_eds_find_by_number (self->chatty_eds, id);

      chatty_pp_buddy_set_contact (buddy, contact);
    }
  }
}

typedef struct _PurpleGLibIOClosure
{
  PurpleInputFunction function;
  guint               result;
  gpointer            data;
} PurpleGLibIOClosure;


static void
purple_glib_io_destroy (gpointer data)
{
  g_free (data);
}


static gboolean
purple_glib_io_invoke (GIOChannel   *source,
                       GIOCondition  condition,
                       gpointer      data)
{
  PurpleGLibIOClosure *closure = data;
  PurpleInputCondition purple_cond = 0;

  if (condition & PURPLE_GLIB_READ_COND) {
    purple_cond |= PURPLE_INPUT_READ;
  }

  if (condition & PURPLE_GLIB_WRITE_COND) {
    purple_cond |= PURPLE_INPUT_WRITE;
  }

  closure->function (closure->data, g_io_channel_unix_get_fd (source),
                     purple_cond);

  return TRUE;
}


static guint
glib_input_add (gint                 fd,
                PurpleInputCondition condition,
                PurpleInputFunction  function,
                gpointer             data)
{

  PurpleGLibIOClosure *closure;
  GIOChannel          *channel;
  GIOCondition         cond = 0;

  closure = g_new0 (PurpleGLibIOClosure, 1);

  closure->function = function;
  closure->data = data;

  if (condition & PURPLE_INPUT_READ) {
    cond |= PURPLE_GLIB_READ_COND;
  }

  if (condition & PURPLE_INPUT_WRITE) {
    cond |= PURPLE_GLIB_WRITE_COND;
  }

  channel = g_io_channel_unix_new (fd);

  closure->result = g_io_add_watch_full (channel,
                                         G_PRIORITY_DEFAULT,
                                         cond,
                                         purple_glib_io_invoke,
                                         closure,
                                         purple_glib_io_destroy);

  g_io_channel_unref (channel);
  return closure->result;
}


static
PurpleEventLoopUiOps eventloop_ui_ops =
{
  g_timeout_add,
  g_source_remove,
  glib_input_add,
  g_source_remove,
  NULL,
  g_timeout_add_seconds,
};


static void
chatty_purple_quit (void)
{
  chatty_chat_view_purple_uninit ();

  purple_conversations_set_ui_ops (NULL);
  purple_connections_set_ui_ops (NULL);
  purple_blist_set_ui_ops (NULL);
  purple_accounts_set_ui_ops (NULL);

  if (NULL != ui_info)
    g_hash_table_destroy (ui_info);

  chatty_xeps_close ();
}


static void
chatty_purple_ui_init (void)
{
  chatty_chat_view_purple_init ();
  chatty_manager_purple_init (chatty_manager_get_default ());
}


static void
chatty_purple_prefs_init (void)
{
  purple_prefs_add_none (CHATTY_PREFS_ROOT "");
  purple_prefs_add_none ("/plugins/chatty");

  purple_prefs_add_none (CHATTY_PREFS_ROOT "/plugins");
  purple_prefs_add_path_list (CHATTY_PREFS_ROOT "/plugins/loaded", NULL);

  purple_prefs_add_none (CHATTY_PREFS_ROOT "/filelocations");
  purple_prefs_add_path (CHATTY_PREFS_ROOT "/filelocations/last_save_folder", "");
  purple_prefs_add_path (CHATTY_PREFS_ROOT "/filelocations/last_open_folder", "");
  purple_prefs_add_path (CHATTY_PREFS_ROOT "/filelocations/last_icon_folder", "");
}


static GHashTable *
chatty_purple_ui_get_info (void)
{
  if (NULL == ui_info) {
    ui_info = g_hash_table_new (g_str_hash, g_str_equal);

    g_hash_table_insert (ui_info, "name", (char *)g_get_application_name ());
    g_hash_table_insert (ui_info, "version", PACKAGE_VERSION);
    g_hash_table_insert (ui_info, "dev_website", "https://source.puri.sm/Librem5/chatty");
    g_hash_table_insert (ui_info, "client_type", "phone");
  }

  return ui_info;
}

static
PurpleCoreUiOps core_ui_ops =
{
  chatty_purple_prefs_init,
  NULL,
  chatty_purple_ui_init,
  chatty_purple_quit,
  chatty_purple_ui_get_info,
};

static void
chatty_manager_account_notify_added (PurpleAccount *pp_account,
                                     const char    *remote_user,
                                     const char    *id,
                                     const char    *alias,
                                     const char    *msg)
{
  ChattyManager *self;
  ChattyPpAccount *account;

  self = chatty_manager_get_default ();
  account = chatty_pp_account_get_object (pp_account);
  g_signal_emit (self,  signals[NOTIFY_ADDED], 0, account, remote_user, id);
}


static void *
chatty_manager_account_request_authorization (PurpleAccount *pp_account,
                                              const char    *remote_user,
                                              const char    *id,
                                              const char    *alias,
                                              const char    *message,
                                              gboolean       on_list,
                                              PurpleAccountRequestAuthorizationCb auth_cb,
                                              PurpleAccountRequestAuthorizationCb deny_cb,
                                              void          *user_data)
{
  ChattyManager *self;
  ChattyPpAccount *account;
  GtkResponseType  response = GTK_RESPONSE_CANCEL;

  self = chatty_manager_get_default ();
  account = chatty_pp_account_get_object (pp_account);
  g_signal_emit (self,  signals[AUTHORIZE_BUDDY], 0, account, remote_user,
                 alias ? alias : remote_user, message, &response);

  if (response == GTK_RESPONSE_ACCEPT) {
    if (!on_list)
      purple_blist_request_add_buddy (pp_account, remote_user, NULL, alias);
    auth_cb (user_data);
  } else {
    deny_cb (user_data);
  }

  g_debug ("Request authorization user: %s alias: %s", remote_user, alias);

  return NULL;
}


static void
chatty_manager_account_request_add (PurpleAccount *account,
                                    const char    *remote_user,
                                    const char    *id,
                                    const char    *alias,
                                    const char    *msg)
{
  PurpleConnection *gc;

  gc = purple_account_get_connection (account);

  if (g_list_find (purple_connections_get_all (), gc))
    purple_blist_request_add_buddy (account, remote_user, NULL, alias);

  g_debug ("chatty_manager_account_request_add");
}


static PurpleAccountUiOps ui_ops =
{
  chatty_manager_account_notify_added,
  NULL,
  chatty_manager_account_request_add,
  chatty_manager_account_request_authorization,
};


static void
chatty_blist_remove (PurpleBuddyList *list,
                     PurpleBlistNode *node)
{
  if (CHATTY_IS_PP_BUDDY (node->ui_data))
    g_signal_emit_by_name (node->ui_data, "deleted");

  purple_request_close_with_handle (node);

  if (node->ui_data)
    purple_signals_disconnect_by_handle (node->ui_data);
}

static void
chatty_blist_update (PurpleBuddyList *list,
                     PurpleBlistNode *node)
{
  if (!node)
    return;

  switch (node->type) {
  case PURPLE_BLIST_CHAT_NODE:
    chatty_manager_update_node (chatty_manager_get_default (), node);
    break;

  case PURPLE_BLIST_BUDDY_NODE:
  case PURPLE_BLIST_CONTACT_NODE:
  case PURPLE_BLIST_GROUP_NODE:
  case PURPLE_BLIST_OTHER_NODE:
  default:
    return;
  }
}


static void
chatty_blist_request_add_buddy (PurpleAccount *account,
                                const char    *username,
                                const char    *group,
                                const char    *alias)
{
  PurpleBuddy     *buddy;
  const char      *account_name;

  buddy = purple_find_buddy (account, username);

  if (buddy == NULL) {
    buddy = purple_buddy_new (account, username, alias);

    purple_blist_add_buddy (buddy, NULL, NULL, NULL);
    purple_blist_node_set_bool (PURPLE_BLIST_NODE(buddy), "chatty-notifications", TRUE);
  }

  purple_account_add_buddy (account, buddy);

  account_name = purple_account_get_username (account);

  g_debug ("chatty_blist_request_add_buddy: %s  %s  %s",
           account_name, username, alias);
}


static PurpleBlistUiOps blist_ui_ops =
{
  NULL,
  NULL,
  NULL,
  chatty_blist_update,
  chatty_blist_remove,
  NULL,
  NULL,
  chatty_blist_request_add_buddy,
};


static void
on_feedback_triggered (LfbEvent      *event,
		       GAsyncResult  *res,
		       LfbEvent     **cmp)
{
  g_autoptr (GError) err = NULL;

  g_return_if_fail (LFB_IS_EVENT (event));

  if (!lfb_event_trigger_feedback_finish (event, res, &err)) {
    g_warning ("Failed to trigger feedback for %s",
	       lfb_event_get_event (event));
  }
}

static ChattyChat *
chatty_conv_find_chat (PurpleConversation *conv)
{
  PurpleBuddy     *buddy;
  PurpleContact   *contact;
  PurpleBlistNode *contact_node, *buddy_node;

  buddy = purple_find_buddy (conv->account, conv->name);

  if (!buddy)
    return NULL;

  if (!(contact = purple_buddy_get_contact (buddy)))
    return NULL;

  contact_node = PURPLE_BLIST_NODE (contact);

  for (buddy_node = purple_blist_node_get_first_child (contact_node);
       buddy_node;
       buddy_node = purple_blist_node_get_sibling_next (buddy_node)) {
    PurpleBuddy *b = PURPLE_BUDDY (buddy_node);
    PurpleConversation *c;

    c = purple_find_conversation_with_account (PURPLE_CONV_TYPE_IM,
                                               b->name,
                                               b->account);
    if (!c)
      continue;
    if (c->ui_data)
      return c->ui_data;
  }

  return NULL;
}

static void
chatty_conv_new (PurpleConversation *conv)
{
  ChattyChat         *chat = NULL;
  PurpleAccount      *account;
  PurpleBuddy        *buddy;
  PurpleValue        *value;
  PurpleBlistNode    *conv_node;
  const gchar        *conv_name;
  const gchar        *folks_name;

  PurpleConversationType conv_type = purple_conversation_get_type (conv);

  if (conv_type == PURPLE_CONV_TYPE_IM &&
      (chat = chatty_conv_find_chat (conv))) {
    conv->ui_data = chat;
    g_object_add_weak_pointer (G_OBJECT (chat), (gpointer *)&conv->ui_data);

    return;
  }

  conv_node = chatty_utils_get_conv_blist_node (conv);

  if (conv_node && conv_node->ui_data) {
    if (conv_type == PURPLE_CONV_TYPE_CHAT)
      chat = conv_node->ui_data;
    else
      chat = g_object_get_data (conv_node->ui_data, "chat");

    if (chat) {
      chatty_pp_chat_set_purple_conv (CHATTY_PP_CHAT (chat), conv);
    }
  }

  if (!chat)
    chat = (ChattyChat *)chatty_pp_chat_new_purple_conv (conv);

  chat = chatty_manager_add_chat (chatty_manager_get_default (), chat);
  account = purple_conversation_get_account (conv);

  if (conv_type == PURPLE_CONV_TYPE_IM) {
    // Add SMS and IMs from unknown contacts to the chats-list,
    // but do not add them to the contacts-list and in case of
    // instant messages do not sync contacts with the server
    conv_name = purple_conversation_get_name (conv);
    buddy = purple_find_buddy (account, conv_name);

    if (chatty_item_get_protocols (CHATTY_ITEM (chat)) == CHATTY_PROTOCOL_SMS) {
      if (buddy == NULL) {
        ChattyEds *chatty_eds;
        ChattyContact *contact;

        chatty_eds = chatty_manager_get_eds (chatty_manager_get_default ());
        contact = chatty_eds_find_by_number (chatty_eds, conv_name);

        if (contact) {
          folks_name = chatty_item_get_name (CHATTY_ITEM (contact));

          buddy = purple_buddy_new (account, conv_name, folks_name);

          purple_blist_add_buddy (buddy, NULL, NULL, NULL);
        }
      }
    }

    if (buddy == NULL) {
      buddy = purple_buddy_new (account, conv_name, NULL);
      purple_blist_add_buddy (buddy, NULL, NULL, NULL);
      // flag the node in the blist so it can be set off in the chats-list
      purple_blist_node_set_bool (PURPLE_BLIST_NODE(buddy), "chatty-unknown-contact", TRUE);

      g_debug ("Unknown contact %s added to blist", purple_buddy_get_name (buddy));
    }
  }

  if (conv_node != NULL &&
      (value = g_hash_table_lookup (conv_node->settings, "enable-logging")) &&
      purple_value_get_type (value) == PURPLE_TYPE_BOOLEAN)
    {
      purple_conversation_set_logging (conv, purple_value_get_boolean (value));
    }

  {
    GListModel *messages;

    messages = chatty_chat_get_messages (chat);

    if (g_list_model_get_n_items (messages) == 0)
      chatty_manager_load_more_chat (chatty_manager_get_default (), chat, 1);
  }
}

static void
chatty_conv_write_chat (PurpleConversation *conv,
                        const char         *who,
                        const char         *message,
                        PurpleMessageFlags  flags,
                        time_t              mtime)
{
  purple_conversation_write (conv, who, message, flags, mtime);
}


static void
chatty_conv_write_im (PurpleConversation *conv,
                      const char         *who,
                      const char         *message,
                      PurpleMessageFlags  flags,
                      time_t              mtime)
{
  if (conv->ui_data && conv != chatty_pp_chat_get_purple_conv (conv->ui_data) &&
      flags & PURPLE_MESSAGE_ACTIVE_ONLY)
    return;

  purple_conversation_write (conv, who, message, flags, mtime);
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
chatty_conv_write_conversation (PurpleConversation *conv,
                                const char         *who,
                                const char         *alias,
                                const char         *message,
                                PurpleMessageFlags  flags,
                                time_t              mtime)
{
  ChattyChat               *chat;
  g_autoptr(ChattyMessage)  chat_message = NULL;
  ChattyManager            *self;
  PurpleConversationType    type;
  PurpleConnection         *gc;
  PurpleAccount            *account;
  PurpleBuddy              *buddy = NULL;
  PurpleBlistNode          *node;
  GdkPixbuf                *avatar = NULL;
  const char               *buddy_name;
  gchar                    *titel;
  g_autofree char          *uuid = NULL;
  PurpleConvMessage        pcm = {
                                   NULL,
                                   NULL,
                                   flags,
                                   mtime,
                                   conv,
                                   NULL};
  g_autoptr(GError)         err = NULL;
  g_autoptr(LfbEvent)       event = NULL;

  if ((flags & PURPLE_MESSAGE_SYSTEM) && !(flags & PURPLE_MESSAGE_NOTIFY)) {
    flags &= ~(PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_RECV);
  }

  node = chatty_utils_get_conv_blist_node (conv);
  chat = conv->ui_data;
  self = chatty_manager_get_default ();

  account = purple_conversation_get_account (conv);
  g_return_if_fail (account != NULL);
  gc = purple_account_get_connection (account);
  g_return_if_fail (gc != NULL || !(flags & (PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_RECV)));

  type = purple_conversation_get_type (conv);

  if (type != PURPLE_CONV_TYPE_CHAT) {
    buddy = purple_find_buddy (account, who);
    node = (PurpleBlistNode*)buddy;

    if (node) {
      purple_blist_node_set_bool (node, "chatty-autojoin", TRUE);
    }

    pcm.who = chatty_utils_jabber_id_strip(who);
  } else {
    pcm.who = chatty_pp_chat_get_buddy_name (CHATTY_PP_CHAT (chat), who);
  }

  // No reason to go further if we ignore system/status
  if (flags & PURPLE_MESSAGE_SYSTEM &&
      type == PURPLE_CONV_TYPE_CHAT &&
      ! purple_blist_node_get_bool (node, "chatty-status-msg"))
    {
      g_debug("Skipping status[%d] message[%s] for %s <> %s", flags,
              message, purple_account_get_username(account), pcm.who);
      g_free(pcm.who);
      return;
    }

  pcm.what = g_strdup(message);
  pcm.alias = g_strdup(purple_conversation_get_name (conv));

  // If anyone wants to suppress archiving - feel free to set NO_LOG flag
  purple_signal_emit (chatty_manager_get_default (),
                      "conversation-write", account, &pcm, &uuid, type);
  g_debug("Posting message id:%s flags:%d type:%d from:%s",
          uuid, pcm.flags, type, pcm.who);

  /*
   * This is default fallback history handler.  Other plugins may
   * intercept “conversation-write” and suppress it if they handle
   * history on their own (eg. MAM).  If %PURPLE_MESSAGE_NO_LOG is
   * set in @flags, it won't be saved to database.
   */
  if (!(pcm.flags & PURPLE_MESSAGE_NO_LOG)) {
    const char *chat_name;

    chat_name = pcm.who;

    if (chatty_chat_is_im (chat))
      chat_name = chatty_chat_get_chat_name (chat);

    chatty_history_add_message (account->username, pcm.alias, chat_name,
                                pcm.what, &uuid, pcm.flags, pcm.when, type);
    }

  if (*message != '\0') {

    if (pcm.flags & (PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_ERROR)) {
      // System is usually also RECV so should be first to catch
      chat_message = chatty_message_new (NULL, NULL, message, uuid, 0, CHATTY_DIRECTION_SYSTEM, 0);
      chatty_pp_chat_append_message (CHATTY_PP_CHAT (chat), chat_message);
    } else if (pcm.flags & PURPLE_MESSAGE_RECV) {
      ChattyChat *active_chat;

      active_chat = chatty_application_get_active_chat (CHATTY_APPLICATION_DEFAULT ());

      if (buddy && purple_blist_node_get_bool (node, "chatty-notifications") &&
          active_chat != chat) {
        g_autoptr(GdkPixbuf) image = NULL;
        ChattyPpBuddy *pp_buddy;

        event = lfb_event_new ("message-new-instant");
        lfb_event_trigger_feedback_async (event, NULL,
                                          (GAsyncReadyCallback)on_feedback_triggered,
                                          NULL);

        pp_buddy = chatty_pp_buddy_get_object (buddy);
        buddy_name = purple_buddy_get_alias (buddy);

        titel = g_strdup_printf (_("New message from %s"), buddy_name);
        avatar = chatty_item_get_avatar (CHATTY_ITEM (pp_buddy));
        image = chatty_manager_round_pixbuf (avatar);

        chatty_notify_show_notification (titel, message, CHATTY_NOTIFY_MESSAGE_RECEIVED, conv, image);

        g_free (titel);
      }

      chat_message = chatty_message_new (NULL, who, message, uuid, mtime, CHATTY_DIRECTION_IN, 0);
      chatty_pp_chat_append_message (CHATTY_PP_CHAT (chat), chat_message);
    } else if (flags & PURPLE_MESSAGE_SEND && pcm.flags & PURPLE_MESSAGE_SEND) {
      // normal send
      chat_message = chatty_message_new (NULL, NULL, message, uuid, 0, CHATTY_DIRECTION_OUT, 0);
      chatty_message_set_status (chat_message, CHATTY_STATUS_SENT, 0);
      chatty_pp_chat_append_message (CHATTY_PP_CHAT (chat), chat_message);
    } else if (pcm.flags & PURPLE_MESSAGE_SEND) {
      // offline send (from MAM)
      // FIXME: current list_box does not allow ordering rows by timestamp
      // TODO: Needs proper sort function and timestamp as user_data for rows
      // FIXME: Alternatively may need to reload history to re-populate rows
      chat_message = chatty_message_new (NULL, NULL, message, uuid, mtime, CHATTY_DIRECTION_OUT, 0);
      chatty_message_set_status (chat_message, CHATTY_STATUS_SENT, 0);
      chatty_pp_chat_append_message (CHATTY_PP_CHAT (chat), chat_message);
    }

    chatty_chat_set_unread_count (chat, chatty_chat_get_unread_count (chat) + 1);
    gtk_sorter_changed (self->chat_sorter, GTK_SORTER_ORDER_TOTAL);
  }

  g_free (pcm.who);
  g_free (pcm.what);
  g_free (pcm.alias);
}


static void
chatty_conv_muc_list_add_users (PurpleConversation *conv,
                                GList              *users,
                                gboolean            new_arrivals)
{
  g_return_if_fail (conv->ui_data);

  chatty_pp_chat_add_users (conv->ui_data, users);
}


static void
chatty_conv_muc_list_update_user (PurpleConversation *conv,
                                  const char         *user)
{
  g_return_if_fail (conv->ui_data);

  chatty_pp_chat_emit_user_changed (conv->ui_data, user);
}


static void
chatty_conv_present_conversation (PurpleConversation *conv)
{
  ChattyManager *self;
  ChattyChat *chat;

  self = chatty_manager_get_default ();
  chat = conv->ui_data;
  g_return_if_fail (chat);

  g_debug ("%s conversation: %s", G_STRFUNC, chatty_chat_get_chat_name (chat));

  g_signal_emit (self,  signals[OPEN_CHAT], 0, chat);
}


static PurpleConversationUiOps conversation_ui_ops =
{
  chatty_conv_new,
  NULL,
  chatty_conv_write_chat,
  chatty_conv_write_im,
  chatty_conv_write_conversation,
  chatty_conv_muc_list_add_users,
  NULL,
  NULL,
  chatty_conv_muc_list_update_user,
  chatty_conv_present_conversation,
};


static gboolean
chatty_manager_load_plugin (PurplePlugin *plugin)
{
  gboolean loaded;

  if (!plugin || purple_plugin_is_loaded (plugin))
    return TRUE;

  loaded = purple_plugin_load (plugin);
  purple_plugins_save_loaded (CHATTY_PREFS_ROOT "/plugins/loaded");
  g_debug ("plugin %s%s Loaded",
           purple_plugin_get_name (plugin),
           loaded ? "" : " Not");

  return loaded;
}

static void
chatty_manager_unload_plugin (PurplePlugin *plugin)
{
  gboolean unloaded;

  if (!plugin || !purple_plugin_is_loaded (plugin))
    return;

  unloaded = purple_plugin_unload (plugin);
  purple_plugin_disable (plugin);
  purple_plugins_save_loaded (CHATTY_PREFS_ROOT "/plugins/loaded");
  /* Failing to unload may mean that the application require restart to do so. */
  g_debug ("plugin %s%s Unloaded",
           purple_plugin_get_name (plugin),
           unloaded ? "" : " Not");
}

static void
manager_message_carbons_changed (ChattyManager  *self,
                                 GParamSpec     *pspec,
                                 ChattySettings *settings)
{
  g_assert (CHATTY_IS_MANAGER (self));
  g_assert (CHATTY_IS_SETTINGS (settings));

  if (!self->carbon_plugin)
    return;

  if (chatty_settings_get_message_carbons (settings))
    chatty_manager_load_plugin (self->carbon_plugin);
  else
    chatty_manager_unload_plugin (self->carbon_plugin);
}

static void
chatty_manager_enable_sms_account (ChattyManager *self)
{
  g_autoptr(ChattyPpAccount) account = NULL;

  if (purple_accounts_find ("SMS", "prpl-mm-sms"))
    return;

  account = chatty_pp_account_new (CHATTY_PROTOCOL_SMS, "SMS", NULL);
  chatty_account_save (CHATTY_ACCOUNT (account));
}

static ChattyPpBuddy *
manager_find_buddy (GListModel  *model,
                    PurpleBuddy *pp_buddy)
{
  guint n_items;

  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyPpBuddy) buddy = NULL;

    buddy = g_list_model_get_item (model, i);

    if (chatty_pp_buddy_get_buddy (buddy) == pp_buddy)
      return buddy;
  }

  return NULL;
}

static void
manager_buddy_added_cb (PurpleBuddy   *pp_buddy,
                        ChattyManager *self)
{
  g_autoptr(ChattyChat) chat = NULL;
  ChattyPpAccount *account;
  ChattyPpBuddy *buddy;
  ChattyContact *contact;
  PurpleAccount *pp_account;
  GListModel *model;
  const char *id;

  g_assert (CHATTY_IS_MANAGER (self));

  pp_account = purple_buddy_get_account (pp_buddy);
  account = chatty_pp_account_get_object (pp_account);
  g_return_if_fail (account);

  model = chatty_pp_account_get_buddy_list (account);
  buddy = manager_find_buddy (model, pp_buddy);

  if (!buddy)
    buddy = chatty_pp_account_add_purple_buddy (account, pp_buddy);

  id = chatty_pp_buddy_get_id (buddy);
  contact = chatty_eds_find_by_number (self->chatty_eds, id);
  chatty_pp_buddy_set_contact (buddy, contact);

  chat = (ChattyChat *)chatty_pp_chat_new_im_chat (pp_account, pp_buddy);
  chatty_history_get_messages_async (chatty_history_get_default (), chat, NULL, 1,
                                     manager_load_messages_cb,
                                     g_object_ref (self));
}

static void
manager_buddy_removed_cb (PurpleBuddy   *pp_buddy,
                          ChattyManager *self)
{
  ChattyPpAccount *account;
  PurpleAccount *pp_account;
  ChattyPpBuddy *buddy;
  GListModel *model;

  g_assert (CHATTY_IS_MANAGER (self));

  pp_account = purple_buddy_get_account (pp_buddy);
  account = chatty_pp_account_get_object (pp_account);

  /*
   * If account is NULL, the account has gotten deleted, and so
   * the buddy object is also deleted along it.
   */
  if (!account)
    return;

  model = chatty_pp_account_get_buddy_list (account);
  buddy = manager_find_buddy (model, pp_buddy);

  g_return_if_fail (buddy);

  g_signal_emit_by_name (buddy, "deleted");
  chatty_utils_remove_list_item (G_LIST_STORE (model), buddy);
}


static void
manager_buddy_privacy_chaged_cb (PurpleBuddy *buddy)
{
  if (!PURPLE_BLIST_NODE(buddy)->ui_data)
    return;

  chatty_blist_update (purple_get_blist (), PURPLE_BLIST_NODE(buddy));
}


static void
manager_buddy_signed_on_off_cb (PurpleBuddy *buddy)
{
  ChattyPpBuddy *pp_buddy;

  chatty_blist_update (purple_get_blist(), (PurpleBlistNode*)buddy);

  pp_buddy = chatty_pp_buddy_get_object (buddy);

  /* As avatar depends on online status, emit ::avatar-changed */
  if (pp_buddy)
    g_signal_emit_by_name (pp_buddy, "avatar-changed");

  g_debug ("Buddy \"%s\" (%s) signed on/off", purple_buddy_get_name (buddy),
           purple_account_get_protocol_id (purple_buddy_get_account(buddy)));
}

static void
manager_buddy_icon_chaged_cb (PurpleBuddy   *buddy,
                              ChattyManager *self)
{
  PurpleConversation *conv;

  conv = purple_find_conversation_with_account (PURPLE_CONV_TYPE_IM,
                                                buddy->name,
                                                buddy->account);

  if (conv && conv->ui_data)
    g_signal_emit_by_name (conv->ui_data, "avatar-changed");
}

static void
manager_account_added_cb (PurpleAccount *pp_account,
                          ChattyManager *self)
{
  g_autoptr(ChattyPpAccount) account = NULL;

  g_assert (CHATTY_IS_MANAGER (self));

  account = chatty_pp_account_get_object (pp_account);

  if (account)
    g_object_ref (account);
  else
    account = chatty_pp_account_new_purple (pp_account);

  g_object_notify (G_OBJECT (account), "status");
  g_list_store_append (self->account_list, account);
  g_list_store_append (self->list_of_user_list,
                       chatty_pp_account_get_buddy_list (account));

  if (self->disable_auto_login)
    chatty_account_set_enabled (CHATTY_ACCOUNT (account), FALSE);

  if (chatty_item_is_sms (CHATTY_ITEM (account)))
    chatty_account_set_enabled (CHATTY_ACCOUNT (account), TRUE);
}

static void
manager_account_removed_cb (PurpleAccount *pp_account,
                            ChattyManager *self)
{
  ChattyPpAccount *account;

  g_assert (CHATTY_IS_MANAGER (self));

  /* account should exist in the store */
  account = chatty_pp_account_get_object (pp_account);
  g_return_if_fail (account);

  chatty_utils_remove_list_item (self->list_of_user_list,
                                 chatty_pp_account_get_buddy_list (account));
  g_object_notify (G_OBJECT (account), "status");
  g_signal_emit_by_name (account, "deleted");
  chatty_utils_remove_list_item (self->account_list, account);
}

static void
manager_account_changed_cb (PurpleAccount *pp_account,
                            ChattyManager *self)
{
  ChattyPpAccount *account;

  account = chatty_pp_account_get_object (pp_account);
  g_return_if_fail (account);

  g_object_notify (G_OBJECT (account), "enabled");
}

static void
manager_account_connection_failed_cb (PurpleAccount         *pp_account,
                                      PurpleConnectionError  error,
                                      const gchar           *error_msg,
                                      ChattyManager         *self)
{
  ChattyPpAccount *account;

  g_assert (CHATTY_IS_MANAGER (self));

  /* account should exist in the store */
  account = chatty_pp_account_get_object (pp_account);
  g_return_if_fail (account);

  if (error == PURPLE_CONNECTION_ERROR_NETWORK_ERROR &&
      self->network_available &&
      chatty_item_get_protocols (CHATTY_ITEM (account)) != CHATTY_PROTOCOL_SMS)
    chatty_pp_account_connect (account, TRUE);

  if (purple_connection_error_is_fatal (error))
    g_signal_emit (self,  signals[CONNECTION_ERROR], 0, account, error_msg);
}

static void
manager_conversation_updated_cb (PurpleConversation   *conv,
                                 PurpleConvUpdateType  type,
                                 ChattyManager        *self)
{
  if (type == PURPLE_CONV_UPDATE_ICON &&
      conv->ui_data)
    g_signal_emit_by_name (conv->ui_data, "avatar-changed");
}

static void
manager_deleting_conversation_cb (PurpleConversation *conv,
                                  ChattyManager      *self)
{
  chatty_manager_delete_conversation (self, conv);
}

static void
manager_buddy_set_typing (PurpleAccount *account,
                          const char    *name,
                          ChattyManager *self,
                          gboolean       is_typing)
{
  PurpleConversation *conv;

  g_assert (CHATTY_IS_MANAGER (self));

  conv = purple_find_conversation_with_account (PURPLE_CONV_TYPE_IM,
                                                name,
                                                account);
  if (!conv || !conv->ui_data)
    return;

  chatty_pp_chat_set_buddy_typing (conv->ui_data, is_typing);
}

static void
manager_buddy_typing_cb (PurpleAccount *account,
                         const char    *name,
                         ChattyManager *self)
{
  manager_buddy_set_typing (account, name, self, TRUE);
}

static void
manager_buddy_typing_stopped_cb (PurpleAccount *account,
                                 const char    *name,
                                 ChattyManager *self)
{
  manager_buddy_set_typing (account, name, self, FALSE);
}

static gboolean
manager_conversation_buddy_leaving_cb (PurpleConversation *conv,
                                       const char         *user,
                                       const char         *reason,
                                       ChattyManager      *self)
{
  g_return_val_if_fail (conv->ui_data, TRUE);

  chatty_pp_chat_remove_user (conv->ui_data, user);

  return TRUE;
}

static gboolean
auto_join_chat_cb (gpointer data)
{
  PurpleBlistNode  *node;
  PurpleConnection *pc = data;
  GHashTable               *components;
  PurplePluginProtocolInfo *prpl_info;
  PurpleAccount    *account = purple_connection_get_account (pc);

  for (node = purple_blist_get_root (); node;
       node = purple_blist_node_next (node, FALSE)) {

    if (PURPLE_BLIST_NODE_IS_CHAT(node)) {
      PurpleChat *chat = (PurpleChat*)node;

      if (purple_chat_get_account (chat) == account &&
          purple_blist_node_get_bool (node, "chatty-autojoin")) {
        g_autofree char *chat_name;

        prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO(purple_find_prpl (purple_account_get_protocol_id (account)));
        components = purple_chat_get_components (chat);
        chat_name = prpl_info->get_chat_name(components);
        chatty_conv_add_history_since_component(components, account->username, chat_name);

        serv_join_chat (purple_account_get_connection (account),
                        purple_chat_get_components (chat));
      }
    }
  }

  return FALSE;
}


static gboolean
manager_connection_autojoin_cb (PurpleConnection *gc,
                                gpointer          user_data)
{
  g_idle_add (auto_join_chat_cb, gc);

  return TRUE;
}


static void
manager_connection_changed_cb (PurpleConnection *gc,
                               ChattyManager    *self)
{
  PurpleAccount *pp_account;
  ChattyPpAccount *account;

  g_assert (CHATTY_IS_MANAGER (self));

  pp_account = purple_connection_get_account (gc);
  account = chatty_pp_account_get_object (pp_account);

  if (account)
    g_object_notify (G_OBJECT (account), "status");
  else
    g_return_if_reached ();
}

static void
manager_update_protocols (ChattyManager *self)
{
  GListModel *model;
  ChattyProtocol protocol;
  ChattyStatus status;
  guint n_items;

  g_assert (CHATTY_IS_MANAGER (self));

  model = G_LIST_MODEL (self->account_list);
  n_items = g_list_model_get_n_items (model);
  self->active_protocols = 0;

  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyAccount) account = NULL;

    account = g_list_model_get_item (model, i);
    status  = chatty_account_get_status (account);
    protocol = chatty_item_get_protocols (CHATTY_ITEM (account));

    if (status == CHATTY_CONNECTED)
      self->active_protocols |= protocol;
  }

  if (self->has_modem)
    self->active_protocols |= CHATTY_PROTOCOL_SMS;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_PROTOCOLS]);
}

static void
manager_connection_signed_on_cb (PurpleConnection *gc,
                                 ChattyManager    *self)
{
  PurpleAccount *pp_account;
  ChattyPpAccount *account;
  ChattyProtocol protocol;

  g_assert (CHATTY_IS_MANAGER (self));

  pp_account = purple_connection_get_account (gc);
  account = chatty_pp_account_get_object (pp_account);
  g_return_if_fail (account);

  /*
   * SMS plugin emits “signed-on” regardless of the true state
   * So it’s handled in “mm-sms-state” callback.
   */
  if (chatty_item_is_sms (CHATTY_ITEM (account)))
    return;

  protocol = chatty_item_get_protocols (CHATTY_ITEM (account));
  self->active_protocols |= protocol;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_PROTOCOLS]);

  g_object_notify (G_OBJECT (account), "status");
}

static void
manager_connection_signed_off_cb (PurpleConnection *gc,
                                  ChattyManager    *self)
{
  PurpleAccount *pp_account;
  ChattyPpAccount *account;

  g_assert (CHATTY_IS_MANAGER (self));

  pp_account = purple_connection_get_account (gc);
  account = chatty_pp_account_get_object (pp_account);
  g_return_if_fail (account);

  /*
   * SMS plugin emits “signed-off” regardless of the true state
   * So it’s handled in “mm-sms-state” callback.
   */
  if (chatty_item_is_sms (CHATTY_ITEM (account)))
    return;

  manager_update_protocols (self);

  g_object_notify (G_OBJECT (account), "status");
}

static ChattyChat *
manager_find_chat (GListModel *model,
                   PurpleChat *pp_chat)
{
  guint n_items;

  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyChat) chat = NULL;

    chat = g_list_model_get_item (model, i);

    if (chatty_pp_chat_get_purple_chat (CHATTY_PP_CHAT (chat)) == pp_chat)
      return chat;
  }

  return NULL;
}

static void
manager_sms_modem_added_cb (gint status)
{
  ChattyPpAccount *account;
  PurpleAccount   *pp_account;

  pp_account = purple_accounts_find ("SMS", "prpl-mm-sms");
  account = chatty_pp_account_get_object (pp_account);
  g_return_if_fail (CHATTY_IS_PP_ACCOUNT (account));

  chatty_pp_account_connect (account, TRUE);
}


/* XXX: works only with one modem */
static void
manager_sms_state_changed_cb (int            state,
                              ChattyManager *self)
{
  ChattyProtocol old_protocols;

  g_assert (CHATTY_IS_MANAGER (self));

  old_protocols = self->active_protocols;

  if (state == PUR_MM_STATE_READY) {
    self->has_modem = TRUE;
    self->active_protocols |= CHATTY_PROTOCOL_SMS;
  } else if (state != PUR_MM_STATE_MANAGER_FOUND && state != PUR_MM_STATE_MODEM_FOUND) {
    self->has_modem = FALSE;
    self->active_protocols &= ~CHATTY_PROTOCOL_SMS;
  }

  if (old_protocols != self->active_protocols)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_PROTOCOLS]);
}

static void
manager_sms_get_country_cb (const char    *country_code,
                            ChattyManager *self)
{
  ChattySettings *settings;

  g_assert (CHATTY_IS_MANAGER (self));

  settings = chatty_settings_get_default ();
  chatty_settings_set_country_iso_code (settings, country_code);
}

static void
manager_network_changed_cb (GNetworkMonitor *network_monitor,
                            gboolean         network_available,
                            ChattyManager   *self)
{
  GListModel *list;
  guint n_items;

  g_assert (G_IS_NETWORK_MONITOR (network_monitor));
  g_assert (CHATTY_IS_MANAGER (self));

  if (network_available == self->network_available)
    return;

  self->network_available = network_available;
  list = G_LIST_MODEL (self->account_list);
  n_items = g_list_model_get_n_items (list);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(ChattyPpAccount) account = NULL;

      account = g_list_model_get_item (list, i);

      if (network_available)
        chatty_pp_account_connect (account, FALSE);
      else
        chatty_pp_account_disconnect (account);
    }
}

static PurpleCmdRet
manager_handle_chatty_cmd (PurpleConversation  *conv,
                           const gchar         *cmd,
                           gchar              **args,
                           gchar              **error,
                           gpointer             user_data)
{
  ChattySettings *settings;
  g_autofree char *msg = NULL;

  settings = chatty_settings_get_default ();

  if (args[0] == NULL || !g_strcmp0 (args[0], "help")) {
    msg = g_strdup ("Commands for setting properties:\n\n"
                    "General settings:\n"
                    " - '/chatty help': Displays this message.\n"
                    " - '/chatty emoticons [on; off]': Convert emoticons\n"
                    " - '/chatty return_sends [on; off]': Return = send message\n"
                    "\n"
                    "XMPP settings:\n"
                    " - '/chatty grey_offline [on; off]': Greyout offline-contacts\n"
                    " - '/chatty blur_idle [on; off]': Blur idle-contacts icons\n"
                    " - '/chatty typing_info [on; off]': Send typing notifications\n"
                    " - '/chatty msg_receipts [on; off]': Send message receipts\n"
                    " - '/chatty msg_carbons [on; off]': Share chat history\n");
  } else if (!g_strcmp0 (args[1], "on")) {
    if (!g_strcmp0 (args[0], "return_sends")) {
      g_object_set (settings, "return-sends-message", TRUE, NULL);
      msg = g_strdup ("Return key sends messages");
    } else if (!g_strcmp0 (args[0], "grey_offline")) {
      g_object_set (settings, "greyout-offline-buddies", TRUE, NULL);
      msg = g_strdup ("Offline user avatars will be greyed out");
    } else if (!g_strcmp0 (args[0], "blur_idle")) {
      g_object_set (settings, "blur-idle-buddies", TRUE, NULL);
      msg = g_strdup ("Offline user avatars will be blurred");
    } else if (!g_strcmp0 (args[0], "typing_info")) {
      g_object_set (settings, "send-typing", TRUE, NULL);
      msg = g_strdup ("Typing messages will be sent");
    } else if (!g_strcmp0 (args[0], "msg_receipts")) {
      g_object_set (settings, "send-receipts", TRUE, NULL);
      msg = g_strdup ("Message receipts will be sent");
    } else if (!g_strcmp0 (args[0], "msg_carbons")) {
      g_object_set (settings, "message-carbons", TRUE, NULL);
      msg = g_strdup ("Chat history will be shared");
    } else if (!g_strcmp0 (args[0], "emoticons")) {
      g_object_set (settings, "convert-emoticons", TRUE, NULL);
      msg = g_strdup ("Emoticons will be converted");
    } else if (!g_strcmp0 (args[0], "welcome")) {
      g_object_set (settings, "first-start", TRUE, NULL);
      msg = g_strdup ("Welcome screen has been reset");
    }
  } else if (!g_strcmp0 (args[1], "off")) {
    if (!g_strcmp0 (args[0], "return_sends")) {
      g_object_set (settings, "return-sends-message", FALSE, NULL);
      msg = g_strdup ("Return key doesn't send messages");
    } else if (!g_strcmp0 (args[0], "grey_offline")) {
      g_object_set (settings, "greyout-offline-buddies", FALSE, NULL);
      msg = g_strdup ("Offline user avatars will not be greyed out");
    } else if (!g_strcmp0 (args[0], "blur_idle")) {
      g_object_set (settings, "blur-idle-buddies", FALSE, NULL);
      msg = g_strdup ("Offline user avatars will not be blurred");
    } else if (!g_strcmp0 (args[0], "typing_info")) {
      g_object_set (settings, "send-typing", FALSE, NULL);
      msg = g_strdup ("Typing messages will be hidden");
    } else if (!g_strcmp0 (args[0], "msg_receipts")) {
      g_object_set (settings, "send-receipts", FALSE, NULL);
      msg = g_strdup ("Message receipts won't be sent");
    } else if (!g_strcmp0 (args[0], "msg_carbons")) {
      g_object_set (settings, "message-carbons", FALSE, NULL);
      msg = g_strdup ("Chat history won't be shared");
    } else if (!g_strcmp0 (args[0], "emoticons")) {
      g_object_set (settings, "convert-emoticons", FALSE, NULL);
      msg = g_strdup ("emoticons will not be converted");
    }
  }

  g_debug("%s", G_STRFUNC);
  g_debug("%s", args[0]);

  if (msg) {
    purple_conversation_write (conv,
                               "chatty",
                               msg,
                               PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG,
                               time(NULL));
  }

  return PURPLE_CMD_RET_OK;
}


static void
chatty_manager_initialize_libpurple (ChattyManager *self)
{
  GNetworkMonitor *network_monitor;

  g_assert (CHATTY_IS_MANAGER (self));

  network_monitor = g_network_monitor_get_default ();
  self->network_available = g_network_monitor_get_network_available (network_monitor);

  purple_prefs_add_none (CHATTY_PREFS_ROOT "/conversations");
  purple_prefs_add_bool (CHATTY_PREFS_ROOT "/conversations/show_tabs", FALSE);

  purple_signal_register (self, "conversation-write",
                          purple_marshal_VOID__POINTER_POINTER_POINTER_UINT,
                          NULL, 4,
                          purple_value_new(PURPLE_TYPE_SUBTYPE,
                                           PURPLE_SUBTYPE_ACCOUNT),
                          purple_value_new (PURPLE_TYPE_BOXED,
                                            "PurpleConvMessage *"),
                          purple_value_new(PURPLE_TYPE_POINTER),
                          purple_value_new(PURPLE_TYPE_ENUM));

  purple_signal_connect (purple_accounts_get_handle(),
                         "account-added", self,
                         PURPLE_CALLBACK (manager_account_added_cb), self);

  purple_signal_connect (purple_accounts_get_handle(),
                         "account-removed", self,
                         PURPLE_CALLBACK (manager_account_removed_cb), self);

  for (GList *node = purple_accounts_get_all (); node; node = node->next)
    manager_account_added_cb (node->data, self);

  purple_signal_connect (purple_accounts_get_handle(),
                         "account-enabled", self,
                         PURPLE_CALLBACK (manager_account_changed_cb), self);
  purple_signal_connect (purple_accounts_get_handle(),
                         "account-disabled", self,
                         PURPLE_CALLBACK (manager_account_changed_cb), self);
  purple_signal_connect (purple_accounts_get_handle(),
                         "account-connection-error", self,
                         PURPLE_CALLBACK (manager_account_connection_failed_cb), self);

  purple_signal_connect (purple_conversations_get_handle (),
                         "conversation-updated", self,
                         PURPLE_CALLBACK (manager_conversation_updated_cb), self);
  purple_signal_connect (purple_conversations_get_handle (),
                         "deleting-conversation", self,
                         PURPLE_CALLBACK (manager_deleting_conversation_cb), self);

  purple_signal_connect (purple_conversations_get_handle (),
                         "buddy-typing", self,
                         PURPLE_CALLBACK (manager_buddy_typing_cb), self);
  purple_signal_connect (purple_conversations_get_handle (),
                         "buddy-typed", self,
                         PURPLE_CALLBACK (manager_buddy_typing_stopped_cb), self);
  purple_signal_connect (purple_conversations_get_handle (),
                         "buddy-typing-stopped", self,
                         PURPLE_CALLBACK (manager_buddy_typing_stopped_cb), self);

  /**
   * This is default fallback history handler which is called last,
   * other plugins may intercept and suppress it if they handle history
   * on their own (eg. MAM)
   */

  purple_signal_connect (purple_conversations_get_handle (),
                         "chat-buddy-leaving", self,
                         PURPLE_CALLBACK (manager_conversation_buddy_leaving_cb), self);

  purple_signal_connect_priority (purple_connections_get_handle (),
                                  "autojoin", self,
                                  PURPLE_CALLBACK (manager_connection_autojoin_cb), self,
                                  PURPLE_SIGNAL_PRIORITY_HIGHEST);
  purple_signal_connect (purple_connections_get_handle(),
                         "signing-on", self,
                         PURPLE_CALLBACK (manager_connection_changed_cb), self);
  purple_signal_connect (purple_connections_get_handle(),
                         "signed-on", self,
                         PURPLE_CALLBACK (manager_connection_signed_on_cb), self);
  purple_signal_connect (purple_connections_get_handle(),
                         "signed-off", self,
                         PURPLE_CALLBACK (manager_connection_signed_off_cb), self);

  purple_signal_connect (purple_plugins_get_handle (),
                         "mm-sms-modem-added", self,
                         PURPLE_CALLBACK (manager_sms_modem_added_cb), NULL);

  purple_signal_connect (purple_plugins_get_handle (),
                         "mm-sms-state", self,
                         PURPLE_CALLBACK (manager_sms_state_changed_cb), self);

  purple_signal_connect (purple_plugins_get_handle (),
                         "mm-sms-country-code", self,
                         PURPLE_CALLBACK (manager_sms_get_country_cb), self);

  g_signal_connect_object (network_monitor, "network-changed",
                           G_CALLBACK (manager_network_changed_cb), self,
                           G_CONNECT_AFTER);
}


static void
chatty_manager_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  ChattyManager *self = (ChattyManager *)object;

  switch (prop_id)
    {
    case PROP_ACTIVE_PROTOCOLS:
      g_value_set_int (value, chatty_manager_get_active_protocols (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}


static void
chatty_manager_dispose (GObject *object)
{
  ChattyManager *self = (ChattyManager *)object;

  purple_signals_disconnect_by_handle (self);
  g_clear_object (&self->contact_list);
  g_clear_object (&self->list_of_user_list);
  g_clear_object (&self->account_list);

  G_OBJECT_CLASS (chatty_manager_parent_class)->dispose (object);
}

static void
chatty_manager_finalize (GObject *object)
{
  chatty_purple_quit ();

  G_OBJECT_CLASS (chatty_manager_parent_class)->finalize (object);
}

static void
chatty_manager_class_init (ChattyManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = chatty_manager_get_property;
  object_class->dispose = chatty_manager_dispose;
  object_class->finalize = chatty_manager_finalize;

  /**
   * ChattyUser:active-protocols:
   *
   * Protocols currently available for use.  This is a
   * flag of protocols currently connected and available
   * for use.
   */
  properties[PROP_ACTIVE_PROTOCOLS] =
    g_param_spec_int ("active-protocols",
                      "Active protocols",
                      "Protocols currently active and connected",
                      CHATTY_PROTOCOL_NONE,
                      CHATTY_PROTOCOL_TELEGRAM,
                      CHATTY_PROTOCOL_NONE,
                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);


  /**
   * ChattyManager::authorize-buddy:
   * @self: a #ChattyManager
   * @account: A #ChattyPpAccount
   * @remote_user: username of the remote user
   * @name: The Alias of @remote_user
   * @message: The message sent by @remote_user
   *
   * Emitted when some one requests to add them to the
   * @account’s buddy list.
   *
   * Returns: %GTK_RESPONSE_ACCEPT if authorized to be
   * added to buddy list, any other value means unauthorized.
   */
  signals [AUTHORIZE_BUDDY] =
    g_signal_new ("authorize-buddy",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_INT,
                  4, CHATTY_TYPE_PP_ACCOUNT, G_TYPE_STRING,
                  G_TYPE_STRING, G_TYPE_STRING);

  /**
   * ChattyManager::chat-deleted:
   * @self: a #ChattyManager
   * @chat: A #ChattyChat
   *
   * Emitted when a chat is deleted.  ‘chat-deleted’ is
   * emitted just before the chat is actually deleted
   * and thus @chat will still point to a valid memory.
   */
  signals [CHAT_DELETED] =
    g_signal_new ("chat-deleted",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1, CHATTY_TYPE_CHAT);

  /**
   * ChattyManager::connection-error:
   * @self: a #ChattyManager
   * @account: A #ChattyPpAccount
   * @error: The error message
   *
   * Emitted when connection to @account failed
   */
  signals [CONNECTION_ERROR] =
    g_signal_new ("connection-error",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE,
                  2, CHATTY_TYPE_PP_ACCOUNT, G_TYPE_STRING);

  /**
   * ChattyManager::notify-added:
   * @self: a #ChattyManager
   * @account: A #ChattyPpAccount
   * @remote_user: username of the remote user
   * @id: The ID for @remote_user
   *
   * Emitted when some buddy added @account username to their
   * buddy list.
   */
  signals [NOTIFY_ADDED] =
    g_signal_new ("notify-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE,
                  3, CHATTY_TYPE_PP_ACCOUNT, G_TYPE_STRING, G_TYPE_STRING);

  /**
   * ChattyManager::open-chat:
   * @self: A #ChattyManager
   * @chat: A #ChattyChat
   *
   * Emitted when user requests to open a chat.  UI can hook
   * to this signal to do whatever appropriate.
   */
  signals [OPEN_CHAT] =
    g_signal_new ("open-chat",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1, CHATTY_TYPE_CHAT);
}

static void
chatty_manager_init (ChattyManager *self)
{
  self->chatty_eds = chatty_eds_new (CHATTY_PROTOCOL_SMS);

  self->account_list = g_list_store_new (CHATTY_TYPE_PP_ACCOUNT);

  self->chat_list = g_list_store_new (CHATTY_TYPE_CHAT);
  self->list_of_user_list = g_list_store_new (G_TYPE_LIST_MODEL);

  self->contact_list = gtk_flatten_list_model_new (G_TYPE_OBJECT,
                                                   G_LIST_MODEL (self->list_of_user_list));
  g_list_store_append (self->list_of_user_list, G_LIST_MODEL (self->chat_list));
  g_list_store_append (self->list_of_user_list,
                       chatty_eds_get_model (self->chatty_eds));

  self->chat_sorter = gtk_custom_sorter_new ((GCompareDataFunc)manager_sort_chat_item,
                                             NULL, NULL);
  self->sorted_chat_list = gtk_sort_list_model_new (G_LIST_MODEL (self->chat_list),
                                                    self->chat_sorter);

  g_signal_connect_object (self->chatty_eds, "notify::is-ready",
                           G_CALLBACK (manager_eds_is_ready), self,
                           G_CONNECT_SWAPPED);
}

ChattyManager *
chatty_manager_get_default (void)
{
  static ChattyManager *self;

  if (!self)
    {
      self = g_object_new (CHATTY_TYPE_MANAGER, NULL);
      g_object_add_weak_pointer (G_OBJECT (self), (gpointer *)&self);
    }

  return self;
}

/* XXX: Remove once the dust settles */
void
chatty_manager_purple_init (ChattyManager *self)
{
  g_return_if_fail (CHATTY_IS_MANAGER (self));

  if (!self->disable_auto_login)
    purple_savedstatus_activate (purple_savedstatus_new (NULL, PURPLE_STATUS_AVAILABLE));

  chatty_manager_initialize_libpurple (self);
  purple_accounts_set_ui_ops (&ui_ops);
  purple_request_set_ui_ops (chatty_request_get_ui_ops ());
  purple_notify_set_ui_ops (chatty_notify_get_ui_ops ());
  purple_blist_set_ui_ops (&blist_ui_ops);
  purple_conversations_set_ui_ops (&conversation_ui_ops);

  purple_cmd_register ("chatty",
                       "ww",
                       PURPLE_CMD_P_DEFAULT,
                       PURPLE_CMD_FLAG_IM | PURPLE_CMD_FLAG_ALLOW_WRONG_ARGS,
                       NULL,
                       manager_handle_chatty_cmd,
                       "chatty &lt;help&gt;:  "
                       "For a list of commands use the 'help' argument.",
                       self);

}

void
chatty_manager_purple (ChattyManager *self)
{
  g_autofree char *search_path = NULL;

  g_return_if_fail (CHATTY_IS_MANAGER (self));

  signal (SIGCHLD, SIG_IGN);
  signal (SIGPIPE, SIG_IGN);

  purple_core_set_ui_ops (&core_ui_ops);
  purple_eventloop_set_ui_ops (&eventloop_ui_ops);

  search_path = g_build_filename (purple_user_dir (), "plugins", NULL);
  purple_plugins_add_search_path (search_path);

  if (!purple_core_init (CHATTY_UI)) {
    g_printerr ("libpurple initialization failed\n");

    g_application_quit (g_application_get_default ());
  }

  if (!purple_core_ensure_single_instance ()) {
    g_printerr ("Another libpurple client is already running\n");

    g_application_quit (g_application_get_default ());
  }

  purple_set_blist (purple_blist_new ());
  purple_prefs_load ();
  purple_blist_load ();
  purple_plugins_load_saved (CHATTY_PREFS_ROOT "/plugins/loaded");

  chatty_manager_load_plugins (self);
  chatty_manager_load_buddies (self);

  purple_savedstatus_activate (purple_savedstatus_get_startup());
  purple_accounts_restore_current_statuses ();

  purple_blist_show ();

  g_debug ("libpurple initialized. Running version %s.",
           purple_core_get_version ());
}

GListModel *
chatty_manager_get_accounts (ChattyManager *self)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), NULL);

  return G_LIST_MODEL (self->account_list);
}

GListModel *
chatty_manager_get_contact_list (ChattyManager *self)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), NULL);

  return G_LIST_MODEL (self->contact_list);
}


GListModel *
chatty_manager_get_chat_list (ChattyManager *self)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), NULL);

  return G_LIST_MODEL (self->sorted_chat_list);
}

/**
 * chatty_manager_disable_auto_login:
 * @self: A #ChattyManager
 * @disable: whether to disable auto-login
 *
 * Set whether to disable automatic login when accounts are
 * loaded/added.  By default, auto-login is enabled if the
 * account is enabled with chatty_pp_account_set_enabled().
 *
 * This is not applicable to SMS accounts.
 */
void
chatty_manager_disable_auto_login (ChattyManager *self,
                                   gboolean       disable)
{
  g_return_if_fail (CHATTY_IS_MANAGER (self));

  self->disable_auto_login = !!disable;
}

gboolean
chatty_manager_get_disable_auto_login (ChattyManager *self)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), TRUE);

  return self->disable_auto_login;
}

void
chatty_manager_load_plugins (ChattyManager *self)
{
  ChattySettings *settings;

  g_return_if_fail (CHATTY_IS_MANAGER (self));

  purple_plugins_load_saved (CHATTY_PREFS_ROOT "/plugins/loaded");
  purple_plugins_probe (G_MODULE_SUFFIX);

  self->sms_plugin = purple_plugins_find_with_id ("prpl-mm-sms");
  self->lurch_plugin = purple_plugins_find_with_id ("core-riba-lurch");
  self->carbon_plugin = purple_plugins_find_with_id ("core-riba-carbons");
  self->file_upload_plugin = purple_plugins_find_with_id ("xep-http-file-upload");

  chatty_manager_load_plugin (self->lurch_plugin);
  chatty_manager_load_plugin (self->file_upload_plugin);

  purple_plugins_init ();
  purple_network_force_online();
  purple_pounces_load ();

  chatty_xeps_init ();

  if (chatty_manager_load_plugin (self->sms_plugin))
    chatty_manager_enable_sms_account (self);

  settings = chatty_settings_get_default ();
  g_signal_connect_object (settings, "notify::message-carbons",
                           G_CALLBACK (manager_message_carbons_changed), self,
                           G_CONNECT_SWAPPED);
  manager_message_carbons_changed (self, NULL, settings);
}

void
chatty_manager_load_buddies (ChattyManager *self)
{
  g_autoptr(GSList) buddies = NULL;

  g_return_if_fail (CHATTY_IS_MANAGER (self));

  purple_signal_connect (purple_blist_get_handle (),
                         "buddy-added", self,
                         PURPLE_CALLBACK (manager_buddy_added_cb), self);
  purple_signal_connect (purple_blist_get_handle (),
                         "buddy-removed", self,
                         PURPLE_CALLBACK (manager_buddy_removed_cb), self);
  purple_signal_connect (purple_blist_get_handle (),
                         "buddy-privacy-changed", self,
                         PURPLE_CALLBACK (manager_buddy_privacy_chaged_cb), self);
  purple_signal_connect (purple_blist_get_handle (),
                         "buddy-signed-on", self,
                         PURPLE_CALLBACK (manager_buddy_signed_on_off_cb), self);
  purple_signal_connect (purple_blist_get_handle (),
                         "buddy-signed-off", self,
                         PURPLE_CALLBACK (manager_buddy_signed_on_off_cb), self);
  purple_signal_connect (purple_blist_get_handle (),
                         "buddy-icon-changed", self,
                         PURPLE_CALLBACK (manager_buddy_icon_chaged_cb), self);

  buddies = purple_blist_get_buddies ();

  for (GSList *node = buddies; node; node = node->next)
    manager_buddy_added_cb (node->data, self);
}

gboolean
chatty_manager_has_carbons_plugin (ChattyManager *self)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), FALSE);

  return self->carbon_plugin != NULL;
}

gboolean
chatty_manager_has_file_upload_plugin (ChattyManager *self)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), FALSE);

  return self->file_upload_plugin != NULL;
}

gboolean
chatty_manager_lurch_plugin_is_loaded (ChattyManager *self)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), FALSE);

  if (!self->lurch_plugin)
    return FALSE;

  return purple_plugin_is_loaded (self->lurch_plugin);
}

ChattyProtocol
chatty_manager_get_active_protocols (ChattyManager *self)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), CHATTY_PROTOCOL_NONE);

  return self->active_protocols;
}


ChattyEds *
chatty_manager_get_eds (ChattyManager *self)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), NULL);

  return self->chatty_eds;
}


void
chatty_manager_update_node (ChattyManager   *self,
                            PurpleBlistNode *node)
{
  g_autoptr(ChattyChat) chat = NULL;
  PurpleChat *pp_chat;

  g_assert (CHATTY_IS_MANAGER (self));

  if (!PURPLE_BLIST_NODE_IS_CHAT (node))
    return;

  pp_chat = (PurpleChat*)node;

  if(!purple_account_is_connected (pp_chat->account))
    return;

  chat = manager_find_chat (G_LIST_MODEL (self->chat_list), pp_chat);

  if (chat) {
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_PROTOCOLS]);
    chat = NULL;

    return;
  }

  chat = (ChattyChat *)chatty_pp_chat_new_purple_chat (pp_chat);

  g_list_store_append (self->chat_list, chat);
}

void
chatty_manager_delete_conversation (ChattyManager      *self,
                                    PurpleConversation *conv)
{
  ChattyChat *chat;
  PurpleBuddy *pp_buddy;

  g_return_if_fail (CHATTY_IS_MANAGER (self));
  g_return_if_fail (conv);

  chat = conv->ui_data;

  if (!chat)
    return;

  pp_buddy = chatty_pp_chat_get_purple_buddy (CHATTY_PP_CHAT (chat));
  if (pp_buddy) {
    ChattyPpBuddy *buddy;

    buddy = chatty_pp_buddy_get_object (pp_buddy);

    if (buddy) {
      g_object_set_data (G_OBJECT (buddy), "chat", NULL);
      chatty_pp_buddy_set_chat (buddy, NULL);
    }
  }

  if (chat) {
    gboolean removed;

    g_signal_emit (self,  signals[CHAT_DELETED], 0, chat);
    removed = chatty_utils_remove_list_item (self->chat_list, chat);
    g_warn_if_fail (removed);
  }
}


static ChattyChat *
chatty_manager_find_chat (GListModel *model,
                          ChattyChat *item)
{
  guint n_items;

  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyChat) chat = NULL;

    chat = g_list_model_get_item (model, i);

    if (chatty_pp_chat_are_same (CHATTY_PP_CHAT (chat), CHATTY_PP_CHAT (item)))
      return chat;
  }

  return NULL;
}


ChattyChat *
chatty_manager_add_chat (ChattyManager *self,
                         ChattyChat    *chat)
{
  ChattyChat *item;
  GListModel *model;

  g_return_val_if_fail (CHATTY_IS_MANAGER (self), NULL);
  g_return_val_if_fail (CHATTY_IS_CHAT (chat), NULL);

  model = G_LIST_MODEL (self->chat_list);

  if (chatty_utils_get_item_position (model, chat, NULL))
    item = chat;
  else
    item = chatty_manager_find_chat (model, chat);

  if (!item) {
      g_list_store_append (self->chat_list, chat);
  }

  gtk_sorter_changed (self->chat_sorter, GTK_SORTER_ORDER_TOTAL);

  return item ? item : chat;
}

void
chatty_manager_load_more_chat (ChattyManager *self,
                               ChattyChat    *chat,
                               guint          limit)
{
  ChattyMessage *since;
  GListModel *model;

  g_return_if_fail (CHATTY_IS_MANAGER (self));
  g_return_if_fail (CHATTY_IS_CHAT (chat));
  g_return_if_fail (limit != 0);

  model = chatty_chat_get_messages (chat);
  since = g_list_model_get_item (model, 0);
  if (since)
    g_object_unref (since);

  chatty_history_get_messages_async (chatty_history_get_default (), chat, since, limit,
                                     manager_get_messages_cb,
                                     g_object_ref (self));
}

/**
 * chatty_conv_im_with_buddy:
 * @account: a PurpleAccount
 * @name: the buddy name
 *
 * Starts a new conversation with a buddy.
 * If there is already an instance of the conversation
 * the GUI presents it to the user.
 *
 */
void
chatty_conv_im_with_buddy (PurpleAccount *account,
                           const char    *name)
{
  PurpleConversation *conv;

  g_return_if_fail (purple_account_is_connected (account));
  g_return_if_fail (name != NULL);

  conv = purple_find_conversation_with_account (PURPLE_CONV_TYPE_IM,
                                                name,
                                                account);

  if (conv == NULL) {
    conv = purple_conversation_new (PURPLE_CONV_TYPE_IM,
                                    account,
                                    name);
  }

  purple_conversation_present (conv);
}

/**
 * chatty_conv_join_chat:
 * @chat: a PurpleChat
 *
 * Joins a group chat
 * If there is already an instance of the chat
 * the GUI presents it to the user.
 *
 */
void
chatty_conv_join_chat (PurpleChat *chat)
{
  PurpleAccount            *account;
  PurpleConversation       *conv;
  PurplePluginProtocolInfo *prpl_info;
  GHashTable               *components;
  const char               *name;
  char                     *chat_name;

  account = purple_chat_get_account(chat);
  prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO(purple_find_prpl (purple_account_get_protocol_id (account)));

  components = purple_chat_get_components (chat);

  if (prpl_info && prpl_info->get_chat_name) {
    chat_name = prpl_info->get_chat_name(components);
  } else {
    chat_name = NULL;
  }

  if (chat_name) {
    name = chat_name;
  } else {
    name = purple_chat_get_name(chat);
  }

  conv = purple_find_conversation_with_account (PURPLE_CONV_TYPE_CHAT,
                                                name,
                                                account);

  if (!conv || purple_conv_chat_has_left (PURPLE_CONV_CHAT(conv))) {
    chatty_conv_add_history_since_component(components, account->username, name);
    serv_join_chat (purple_account_get_connection (account), components);
  } else if (conv) {
    purple_conversation_present(conv);
  }

  g_free (chat_name);
}
