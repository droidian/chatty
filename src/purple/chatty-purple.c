/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-purple.c
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-purple"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <purple.h>
#include "contrib/gtk.h"
#include "xeps/xeps.h"

#include "chatty-settings.h"
#include "chatty-pp-buddy.h"
#include "chatty-pp-account.h"
#include "chatty-purple-request.h"
#include "chatty-purple-notify.h"
#include "chatty-pp-chat.h"
#include "chatty-pp-utils.h"
#include "chatty-application.h"
#include "chatty-manager.h"
#include "chatty-purple.h"
#include "chatty-log.h"

struct _ChattyPurple
{
  GObject              parent_instance;

  ChattyHistory       *history;

  PurplePlugin        *sms_plugin;
  PurplePlugin        *lurch_plugin;
  PurplePlugin        *carbon_plugin;
  PurplePlugin        *file_upload_plugin;

  GListStore          *accounts;
  GtkFlattenListModel *users_list;
  GListStore          *list_of_user_list;
  GListStore          *chat_list;

  ChattyProtocol       active_protocols;

  gboolean             disable_auto_login;
  gboolean             network_available;
  gboolean             is_loaded;
  gboolean             enabled;
};

#define CHATTY_PREFS_ROOT   "/chatty"
#define CHATTY_UI           "chatty-ui"

#define PURPLE_GLIB_READ_COND  (G_IO_IN | G_IO_HUP | G_IO_ERR)
#define PURPLE_GLIB_WRITE_COND (G_IO_OUT | G_IO_HUP | G_IO_ERR | G_IO_NVAL)

G_DEFINE_TYPE (ChattyPurple, chatty_purple, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_ENABLED,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];
static GHashTable *ui_info = NULL;
static gboolean enable_debug;

static void
chatty_purple_emit_updated (void)
{
  ChattyPurple *self;
  ChattyManager *manager;
  GListModel *model;
  ChattyProtocol protocol;
  ChattyStatus status;
  guint n_items;

  manager = chatty_manager_get_default ();
  self = chatty_purple_get_default ();
  model = G_LIST_MODEL (self->accounts);
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

  g_object_notify (G_OBJECT (manager), "active-protocols");
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

  if (condition & PURPLE_GLIB_READ_COND)
    purple_cond |= PURPLE_INPUT_READ;

  if (condition & PURPLE_GLIB_WRITE_COND)
    purple_cond |= PURPLE_INPUT_WRITE;

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
  GIOChannel *channel;
  GIOCondition cond = 0;

  closure = g_new0 (PurpleGLibIOClosure, 1);

  closure->function = function;
  closure->data = data;

  if (condition & PURPLE_INPUT_READ)
    cond |= PURPLE_GLIB_READ_COND;

  if (condition & PURPLE_INPUT_WRITE)
    cond |= PURPLE_GLIB_WRITE_COND;

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

static int
run_dialog_and_destroy (GtkDialog *dialog)
{
  int response;

  gtk_dialog_set_default_response (dialog, GTK_RESPONSE_CANCEL);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ON_PARENT);

  response = gtk_dialog_run (dialog);

  gtk_widget_destroy (GTK_WIDGET (dialog));

  return response;
}

static GtkDialog *
message_dialog_new (GtkMessageType  type,
                    GtkButtonsType  buttons,
                    const gchar    *message_format,
                    ...) G_GNUC_PRINTF (3, 4);
static GtkDialog *
message_dialog_new (GtkMessageType  type,
                    GtkButtonsType  buttons,
                    const gchar    *message_format,
                    ...)
{
  GtkApplication *app;
  GtkWidget *dialog;
  GtkWindow *window;
  g_autofree char *message = NULL;
  va_list args;

  va_start (args, message_format);
  message = g_strdup_vprintf (message_format, args);
  va_end (args);

  app = GTK_APPLICATION (g_application_get_default ());
  window = gtk_application_get_active_window (app);
  dialog = gtk_message_dialog_new (window,
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   type, buttons, "%s", message);

  return GTK_DIALOG (dialog);
}

static void
chatty_purple_account_notify_added (PurpleAccount *pp_account,
                                    const char    *remote_user,
                                    const char    *id,
                                    const char    *alias,
                                    const char    *msg)
{
  GtkDialog *dialog;

  dialog = message_dialog_new (GTK_MESSAGE_INFO, GTK_BUTTONS_OK, _("Contact added"));
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("User %s has added %s to the contacts"),
                                            remote_user, id);
  run_dialog_and_destroy (dialog);
}


static void *
chatty_purple_account_request_authorization (PurpleAccount                       *pp_account,
                                             const char                          *remote_user,
                                             const char                          *id,
                                             const char                          *alias,
                                             const char                          *message,
                                             gboolean                             on_list,
                                             PurpleAccountRequestAuthorizationCb  auth_cb,
                                             PurpleAccountRequestAuthorizationCb  deny_cb,
                                             void                                *user_data)
{
  GtkDialog *dialog;

  dialog = message_dialog_new (GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                               _("Authorize %s?"), alias ? alias : remote_user);
  gtk_dialog_add_buttons ((dialog),
                          _("Reject"), GTK_RESPONSE_REJECT,
                          _("Accept"), GTK_RESPONSE_ACCEPT,
                          NULL);
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("Add %s to contact list"),
                                            remote_user);

  if (run_dialog_and_destroy (dialog) == GTK_RESPONSE_ACCEPT) {
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
chatty_purple_account_request_add (PurpleAccount *account,
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
  chatty_purple_account_notify_added,
  NULL,
  chatty_purple_account_request_add,
  chatty_purple_account_request_authorization,
};

static ChattyPpChat *
chatty_purple_find_pp_chat (GListModel *model,
                            PurpleChat *pp_chat)
{
  guint n_items;

  if (!pp_chat)
    return NULL;

  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyPpChat) chat = NULL;

    chat = g_list_model_get_item (model, i);

    if (chatty_pp_chat_get_purple_chat (chat) == pp_chat)
      return chat;
  }

  return NULL;
}

static ChattyPpChat *
chatty_purple_find_chat (GListModel   *model,
                         ChattyPpChat *chat)
{
  guint n_items;

  if (!chat)
    return NULL;

  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyPpChat) item = NULL;

    item = g_list_model_get_item (model, i);

    if (chatty_pp_chat_are_same (chat, item))
      return item;
  }

  return NULL;
}

static ChattyPpChat *
chatty_purple_add_chat (ChattyPurple *self,
                        ChattyPpChat *chat)
{
  ChattyPpChat *item;
  GListModel *model;

  g_return_val_if_fail (CHATTY_IS_PURPLE (self), NULL);
  g_return_val_if_fail (CHATTY_IS_PP_CHAT (chat), NULL);

  model = G_LIST_MODEL (self->chat_list);

  if (chatty_utils_get_item_position (model, chat, NULL))
    item = chat;
  else
    item = chatty_purple_find_pp_chat (model, chatty_pp_chat_get_purple_chat (chat));

  if (!item) {
    CHATTY_DEBUG (chatty_chat_get_chat_name (CHATTY_CHAT (chat)), "Added chat:");
    chatty_chat_set_data (CHATTY_CHAT (chat), NULL, self->history);
    g_list_store_append (self->chat_list, chat);
  }

  /* gtk_sorter_changed (self->chat_sorter, GTK_SORTER_CHANGE_DIFFERENT); */

  return item ? item : chat;
}

static void
chatty_purple_update_node (ChattyPurple    *self,
                           PurpleBlistNode *node)
{
  g_autoptr(ChattyPpChat) chat = NULL;
  PurpleChat *pp_chat;

  g_assert (CHATTY_IS_PURPLE (self));

  if (!PURPLE_BLIST_NODE_IS_CHAT (node))
    return;

  pp_chat = (PurpleChat*)node;

  if(!purple_account_is_connected (pp_chat->account))
    return;

  chat = chatty_purple_find_pp_chat (G_LIST_MODEL (self->chat_list), pp_chat);

  if (chat) {
    ChattyManager *manager;

    manager = chatty_manager_get_default ();

    g_object_notify (G_OBJECT (manager), "active-protocols");
    chat = NULL;

    return;
  }

  chat = chatty_pp_chat_new_purple_chat (pp_chat,
                                         chatty_purple_has_encryption (self));

  chatty_purple_add_chat (self, chat);
  chatty_chat_set_data (CHATTY_CHAT (chat), NULL, self->history);
}

static void
chatty_blist_update (PurpleBuddyList *list,
                     PurpleBlistNode *node)
{
  if (!node)
    return;

  switch (node->type) {
  case PURPLE_BLIST_CHAT_NODE:
    chatty_purple_update_node (chatty_purple_get_default (), node);
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
chatty_blist_request_add_buddy (PurpleAccount *account,
                                const char    *username,
                                const char    *group,
                                const char    *alias)
{
  PurpleBuddy *buddy;
  const char *account_name;

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
purple_account_added_cb (PurpleAccount *pp_account,
                         ChattyPurple  *self)
{
  g_autoptr(ChattyPpAccount) account = NULL;
  const char *protocol_id;

  g_assert (CHATTY_IS_PURPLE (self));

  protocol_id = purple_account_get_protocol_id (pp_account);

  /* We handles matrix accounts native. */
  if (chatty_settings_get_experimental_features (chatty_settings_get_default ()) &&
      g_strcmp0 (protocol_id, "prpl-matrix") == 0) {
    return;
  }

  if (g_strcmp0 (protocol_id, "prpl-mm-sms") == 0) {
    purple_account_set_enabled (pp_account, purple_core_get_ui (), FALSE);
    return;
  }

  account = chatty_pp_account_get_object (pp_account);

  if (account)
    g_object_ref (account);
  else
    account = chatty_pp_account_new_purple (pp_account,
                                            chatty_purple_has_encryption (self));

  g_object_notify (G_OBJECT (account), "status");
  g_list_store_append (self->accounts, account);
  g_list_store_append (self->list_of_user_list,
                       chatty_account_get_buddies (CHATTY_ACCOUNT (account)));
  CHATTY_DEBUG (chatty_item_get_username (CHATTY_ITEM (account)), "Added account: ");

  if (self->disable_auto_login)
    chatty_account_set_enabled (CHATTY_ACCOUNT (account), FALSE);
}

static void
purple_account_removed_cb (PurpleAccount *pp_account,
                           ChattyPurple  *self)
{
  ChattyPpAccount *account;

  g_assert (CHATTY_IS_PURPLE (self));

  account = chatty_pp_account_get_object (pp_account);

  if (!account)
    return;

  chatty_utils_remove_list_item (self->list_of_user_list,
                                 chatty_account_get_buddies (CHATTY_ACCOUNT (account)));
  g_object_notify (G_OBJECT (account), "status");
  g_signal_emit_by_name (account, "deleted");
  chatty_utils_remove_list_item (self->accounts, account);
}

static void
purple_account_changed_cb (PurpleAccount *pp_account,
                           ChattyPurple  *self)
{
  ChattyPpAccount *account;

  account = chatty_pp_account_get_object (pp_account);

  if (account)
    g_object_notify (G_OBJECT (account), "enabled");
}

static void
purple_account_connection_failed_cb (PurpleAccount         *pp_account,
                                     PurpleConnectionError  error,
                                     const char            *error_msg,
                                     ChattyPurple          *self)
{
  ChattyPpAccount *account;

  g_assert (CHATTY_IS_PURPLE (self));

  /* account should exist in the store */
  account = chatty_pp_account_get_object (pp_account);
  g_return_if_fail (account);

  if (error == PURPLE_CONNECTION_ERROR_NETWORK_ERROR &&
      self->network_available)
    chatty_account_connect (CHATTY_ACCOUNT (account), TRUE);

  if (purple_connection_error_is_fatal (error)) {
    GtkDialog *dialog;

    dialog = message_dialog_new (GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, _("Login failed"));
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                              "%s: %s\n\n%s",
                                              error_msg,
                                              chatty_item_get_username (CHATTY_ITEM (account)),
                                              _("Please check ID and password"));
    run_dialog_and_destroy (dialog);
  }
}

static void
purple_conversation_updated_cb (PurpleConversation   *conv,
                                PurpleConvUpdateType  type,
                                ChattyManager        *self)
{
  if (type == PURPLE_CONV_UPDATE_ICON &&
      conv->ui_data)
    g_signal_emit_by_name (conv->ui_data, "avatar-changed");
}

static void
purple_deleting_conversation_cb (PurpleConversation *conv,
                                 ChattyPurple       *self)
{
  PurpleBuddy *pp_buddy;
  ChattyPpChat *chat;

  g_return_if_fail (CHATTY_IS_PURPLE (self));
  g_return_if_fail (conv);

  chat = conv->ui_data;

  if (!chat)
    return;

  pp_buddy = chatty_pp_chat_get_purple_buddy (chat);
  if (pp_buddy) {
    ChattyPpBuddy *buddy;

    buddy = chatty_pp_buddy_get_object (pp_buddy);

    if (buddy) {
      g_object_set_data (G_OBJECT (buddy), "chat", NULL);
      chatty_pp_buddy_set_chat (buddy, NULL);
    }
  }

  if (chat) {
    ChattyManager *manager;
    gboolean removed;

    manager = chatty_manager_get_default ();
    g_signal_emit_by_name (manager, "chat-deleted", chat);
    removed = chatty_utils_remove_list_item (self->chat_list, chat);
    g_warn_if_fail (removed);
  }
}

static void
purple_buddy_set_typing (PurpleAccount *account,
                         const char    *name,
                         ChattyPurple  *self,
                         gboolean       is_typing)
{
  PurpleConversation *conv;

  g_assert (CHATTY_IS_PURPLE (self));

  conv = purple_find_conversation_with_account (PURPLE_CONV_TYPE_IM,
                                                name,
                                                account);
  if (!conv || !conv->ui_data)
    return;

  chatty_pp_chat_set_buddy_typing (conv->ui_data, is_typing);
}

static void
purple_buddy_typing_cb (PurpleAccount *account,
                        const char    *name,
                        ChattyPurple  *self)
{
  purple_buddy_set_typing (account, name, self, TRUE);
}

static void
purple_buddy_typing_stopped_cb (PurpleAccount *account,
                                const char    *name,
                                ChattyPurple  *self)
{
  purple_buddy_set_typing (account, name, self, FALSE);
}

static gboolean
purple_conversation_buddy_leaving_cb (PurpleConversation *conv,
                                      const char         *user,
                                      const char         *reason,
                                      ChattyPurple       *self)
{
  g_return_val_if_fail (conv->ui_data, TRUE);

  chatty_pp_chat_remove_user (conv->ui_data, user);

  return TRUE;
}

static void
chatty_conv_add_history_since_component (GHashTable *components,
                                         const char *account,
                                         const char *room)
{
  time_t mtime;
  struct tm * timeinfo;

  g_autofree gchar *iso_timestamp = g_malloc0(MAX_GMT_ISO_SIZE * sizeof(char));

  mtime = chatty_history_get_last_message_time (chatty_manager_get_history (chatty_manager_get_default ()),
                                                account, room);
  mtime += 1; // Use the next epoch to exclude the last stored message(s)
  timeinfo = gmtime (&mtime);
  g_return_if_fail (strftime (iso_timestamp,
                              MAX_GMT_ISO_SIZE * sizeof(char),
                              "%Y-%m-%dT%H:%M:%SZ",
                              timeinfo));

  g_hash_table_steal (components, "history_since");
  g_hash_table_insert (components, "history_since", g_steal_pointer(&iso_timestamp));
}

static gboolean
auto_join_chat_cb (gpointer data)
{
  PurpleBlistNode *node;
  PurpleConnection *pc = data;
  GHashTable *components;
  PurplePluginProtocolInfo *prpl_info;
  PurpleAccount *account = purple_connection_get_account (pc);

  for (node = purple_blist_get_root (); node;
       node = purple_blist_node_next (node, FALSE)) {

    if (PURPLE_BLIST_NODE_IS_CHAT(node)) {
      PurpleChat *chat = (PurpleChat*)node;

      if (purple_chat_get_account (chat) == account &&
          purple_blist_node_get_bool (node, "chatty-autojoin")) {
        g_autofree char *chat_name = NULL;

        prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO(purple_find_prpl (purple_account_get_protocol_id (account)));
        components = purple_chat_get_components (chat);
        chat_name = prpl_info->get_chat_name(components);

        if (!chat_name || !*chat_name)
          continue;

        chatty_conv_add_history_since_component(components, account->username, chat_name);

        serv_join_chat (purple_account_get_connection (account),
                        purple_chat_get_components (chat));
      }
    }
  }

  return FALSE;
}

static gboolean
purple_connection_autojoin_cb (PurpleConnection *gc,
                               ChattyPurple     *self)
{
  g_idle_add (auto_join_chat_cb, gc);

  return TRUE;
}

static void
purple_connection_changed_cb (PurpleConnection *gc,
                              ChattyPurple     *self)
{
  PurpleAccount *pp_account;
  ChattyPpAccount *account;

  g_assert (CHATTY_IS_PURPLE (self));

  pp_account = purple_connection_get_account (gc);
  account = chatty_pp_account_get_object (pp_account);

  if (account)
    g_object_notify (G_OBJECT (account), "status");
  else
    g_return_if_reached ();
}

static void
purple_connection_signed_on_cb (PurpleConnection *gc,
                                ChattyPurple     *self)
{
  ChattyManager *manager;
  PurpleAccount *pp_account;
  ChattyPpAccount *account;
  ChattyProtocol protocol;

  g_assert (CHATTY_IS_PURPLE (self));

  pp_account = purple_connection_get_account (gc);
  account = chatty_pp_account_get_object (pp_account);
  g_return_if_fail (account);

  protocol = chatty_item_get_protocols (CHATTY_ITEM (account));
  self->active_protocols |= protocol;

  manager = chatty_manager_get_default ();
  g_object_notify (G_OBJECT (manager), "active-protocols");
  /* g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_PROTOCOLS]); */

  g_object_notify (G_OBJECT (account), "status");
}

static void
purple_connection_signed_off_cb (PurpleConnection *gc,
                                 ChattyPurple     *self)
{
  PurpleAccount *pp_account;
  ChattyPpAccount *account;

  g_assert (CHATTY_IS_PURPLE (self));

  pp_account = purple_connection_get_account (gc);
  account = chatty_pp_account_get_object (pp_account);

  if (!account)
    return;

  chatty_purple_emit_updated ();

  g_object_notify (G_OBJECT (account), "status");
}

static void
purple_network_changed_cb (GNetworkMonitor *network_monitor,
                           gboolean         network_available,
                           ChattyPurple    *self)
{
  GListModel *list;
  guint n_items;

  g_assert (G_IS_NETWORK_MONITOR (network_monitor));
  g_assert (CHATTY_IS_PURPLE (self));

  if (network_available == self->network_available)
    return;

  self->network_available = network_available;
  list = G_LIST_MODEL (self->accounts);
  n_items = g_list_model_get_n_items (list);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(ChattyAccount) account = NULL;

      account = g_list_model_get_item (list, i);

      if (network_available)
        chatty_account_connect (account, FALSE);
      else
        chatty_account_disconnect (account);
    }
}

static ChattyPpChat *
chatty_conv_find_chat (PurpleConversation *conv)
{
  PurpleBlistNode *contact_node, *buddy_node;
  PurpleContact *contact;
  PurpleBuddy *buddy;

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
    if (c && c->ui_data)
      return c->ui_data;
  }

  return NULL;
}

static void
chatty_conv_new (PurpleConversation *conv)
{
  ChattyPurple *self;
  ChattyPpChat *chat = NULL;
  PurpleAccount *account;
  PurpleBuddy *buddy;
  PurpleValue *value;
  PurpleBlistNode *conv_node;
  const char *conv_name;

  PurpleConversationType conv_type = purple_conversation_get_type (conv);

  if (conv_type == PURPLE_CONV_TYPE_IM &&
      (chat = chatty_conv_find_chat (conv))) {
    conv->ui_data = chat;
    g_set_weak_pointer (&conv->ui_data, chat);

    return;
  }

  self = chatty_purple_get_default ();
  conv_node = chatty_pp_utils_get_conv_blist_node (conv);

  if (conv_node && conv_node->ui_data) {
    if (conv_type == PURPLE_CONV_TYPE_CHAT)
      chat = conv_node->ui_data;
    else
      chat = g_object_get_data (conv_node->ui_data, "chat");

    if (chat)
      chatty_pp_chat_set_purple_conv (chat, conv);
  }

  if (!chat)
    chat = chatty_pp_chat_new_purple_conv (conv,
                                           chatty_purple_has_encryption (self));

  chat = chatty_purple_add_chat (self, chat);
  chatty_chat_set_data (CHATTY_CHAT (chat), NULL, self->history);
  account = purple_conversation_get_account (conv);

  if (conv_type == PURPLE_CONV_TYPE_IM) {
    // Add SMS and IMs from unknown contacts to the chats-list,
    // but do not add them to the contacts-list and in case of
    // instant messages do not sync contacts with the server
    conv_name = purple_conversation_get_name (conv);
    buddy = purple_find_buddy (account, conv_name);

    if (buddy == NULL) {
      buddy = purple_buddy_new (account, conv_name, NULL);
      purple_blist_add_buddy (buddy, NULL, NULL, NULL);
      // flag the node in the blist so it can be set off in the chats-list
      purple_blist_node_set_bool (PURPLE_BLIST_NODE(buddy), "chatty-unknown-contact", TRUE);
      purple_blist_node_set_bool (PURPLE_BLIST_NODE (buddy), "chatty-notifications", TRUE);

      g_debug ("Unknown contact %s added to blist", purple_buddy_get_name (buddy));
    }
  }

  if (conv_node != NULL &&
      (value = g_hash_table_lookup (conv_node->settings, "enable-logging")) &&
      purple_value_get_type (value) == PURPLE_TYPE_BOOLEAN) {
    purple_conversation_set_logging (conv, purple_value_get_boolean (value));
  }

  {
    GListModel *messages;

    messages = chatty_chat_get_messages (CHATTY_CHAT (chat));

    if (g_list_model_get_n_items (messages) == 0)
      chatty_chat_load_past_messages (CHATTY_CHAT (chat), 1);
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

static void
chatty_conv_write_conversation (PurpleConversation *conv,
                                const char         *who,
                                const char         *alias,
                                const char         *message,
                                PurpleMessageFlags  flags,
                                time_t              mtime)
{
  ChattyPpChat *chat;
  g_autoptr(ChattyMessage) chat_message = NULL;
  ChattyPurple *self;
  PurpleConversationType type;
  PurpleConnection *gc;
  PurpleAccount *account;
  PurpleBuddy *buddy = NULL;
  PurpleBlistNode *node;
  const char *buddy_name;
  g_autofree char *uuid = NULL;
  PurpleConvMessage pcm = {NULL,
                           NULL,
                           flags,
                           mtime,
                           conv,
                           NULL};
  ChattyProtocol protocol;

  if ((flags & PURPLE_MESSAGE_SYSTEM) && !(flags & PURPLE_MESSAGE_NOTIFY)) {
    flags &= ~(PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_RECV);
  }

  node = chatty_pp_utils_get_conv_blist_node (conv);
  chat = conv->ui_data;
  self = chatty_purple_get_default ();
  protocol = chatty_item_get_protocols (CHATTY_ITEM (chat));

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

    pcm.who = chatty_utils_jabber_id_strip (who);
  } else {
    if (protocol == CHATTY_PROTOCOL_MATRIX ||
        protocol == CHATTY_PROTOCOL_XMPP)
      pcm.who = chatty_pp_chat_get_buddy_name (chat, who);
    else
      pcm.who = g_strdup (who);

    if (protocol == CHATTY_PROTOCOL_XMPP &&
        !g_str_has_prefix (pcm.who, conv->name)) {
      pcm.who = chatty_utils_jabber_id_strip (pcm.who);
    }
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
                      "conversation-write", conv, &pcm, &uuid, type);
  CHATTY_DEBUG (pcm.who, "Posting message id:%s flags:%d type:%d from:",
                uuid, pcm.flags, type);

  if (!uuid)
    uuid = g_uuid_string_random ();

  if (*message != '\0') {

    if (pcm.flags & (PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_ERROR)) {
      // System is usually also RECV so should be first to catch
      chat_message = chatty_message_new (NULL, message, uuid, 0, CHATTY_MESSAGE_HTML_ESCAPED,
                                         CHATTY_DIRECTION_SYSTEM, 0);
      chatty_pp_chat_append_message (chat, chat_message);
      g_signal_emit_by_name (chat, "message-added");
    } else if (pcm.flags & PURPLE_MESSAGE_RECV) {
      g_autoptr(ChattyContact) contact = NULL;
      ChattyPpChat *active_chat;

      active_chat = CHATTY_PP_CHAT (chatty_application_get_active_chat (CHATTY_APPLICATION_DEFAULT ()));
      contact = g_object_new (CHATTY_TYPE_CONTACT, NULL);
      chatty_contact_set_name (contact, pcm.who);
      chatty_contact_set_value (contact, pcm.who);

      chat_message = chatty_message_new (CHATTY_ITEM (contact), message, uuid, mtime,
                                         CHATTY_MESSAGE_HTML_ESCAPED, CHATTY_DIRECTION_IN, 0);
      chatty_pp_chat_append_message (chat, chat_message);
      g_signal_emit_by_name (chat, "message-added");

      if (buddy && purple_blist_node_get_bool (node, "chatty-notifications") &&
          active_chat != chat) {
        buddy_name = purple_buddy_get_alias (buddy);
        chatty_chat_show_notification (CHATTY_CHAT (chat), buddy_name);
      }
    } else if (flags & PURPLE_MESSAGE_SEND && pcm.flags & PURPLE_MESSAGE_SEND) {
      // normal send
      chat_message = chatty_message_new (NULL, message, uuid, 0, CHATTY_MESSAGE_HTML_ESCAPED,
                                         CHATTY_DIRECTION_OUT, 0);
      chatty_message_set_status (chat_message, CHATTY_STATUS_SENT, 0);
      chatty_pp_chat_append_message (chat, chat_message);
      g_signal_emit_by_name (chat, "message-added");
    } else if (pcm.flags & PURPLE_MESSAGE_SEND) {
      // offline send (from MAM)
      // FIXME: current list_box does not allow ordering rows by timestamp
      // TODO: Needs proper sort function and timestamp as user_data for rows
      // FIXME: Alternatively may need to reload history to re-populate rows
      chat_message = chatty_message_new (NULL, message, uuid, mtime, CHATTY_MESSAGE_HTML_ESCAPED,
                                         CHATTY_DIRECTION_OUT, 0);
      chatty_message_set_status (chat_message, CHATTY_STATUS_SENT, 0);
      chatty_pp_chat_append_message (chat, chat_message);
      g_signal_emit_by_name (chat, "message-added");
    }

    /*
     * This is default fallback history handler.  Other plugins may
     * intercept “conversation-write” and suppress it if they handle
     * history on their own (eg. MAM).  If %PURPLE_MESSAGE_NO_LOG is
     * set in @flags, it won't be saved to database.
     */
    if (!(pcm.flags & PURPLE_MESSAGE_NO_LOG) && chat_message)
      chatty_history_add_message (self->history, CHATTY_CHAT (chat), chat_message);

    chatty_chat_set_unread_count (CHATTY_CHAT (chat),
                                  chatty_chat_get_unread_count (CHATTY_CHAT (chat)) + 1);
  }

  if (chat) {
    GListModel *messages;

    messages = chatty_chat_get_messages (CHATTY_CHAT (chat));

    /* The first message was added, notify so that chat list in main window updates */
    if (g_list_model_get_n_items (messages) == 1)
      chatty_purple_emit_updated ();
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
  ChattyManager *manager;
  ChattyChat *chat;

  manager = chatty_manager_get_default ();
  chat = conv->ui_data;
  g_return_if_fail (chat);

  CHATTY_DEBUG (chatty_chat_get_chat_name (chat), "conversation:");

  g_signal_emit_by_name (manager, "open-chat", chat);
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

static void
chatty_purple_initialize (ChattyPurple *self)
{
  GNetworkMonitor *network_monitor;

  g_assert (CHATTY_IS_PURPLE (self));

  network_monitor = g_network_monitor_get_default ();
  self->network_available = g_network_monitor_get_network_available (network_monitor);

  purple_signal_register (chatty_manager_get_default (), "conversation-write",
                          purple_marshal_VOID__POINTER_POINTER_POINTER_UINT,
                          NULL, 4,
                          purple_value_new(PURPLE_TYPE_SUBTYPE,
                                           PURPLE_SUBTYPE_CONVERSATION),
                          purple_value_new (PURPLE_TYPE_BOXED,
                                            "PurpleConvMessage *"),
                          purple_value_new(PURPLE_TYPE_POINTER),
                          purple_value_new(PURPLE_TYPE_ENUM));

  purple_signal_connect (purple_accounts_get_handle(),
                         "account-added", self,
                         PURPLE_CALLBACK (purple_account_added_cb), self);

  purple_signal_connect (purple_accounts_get_handle(),
                         "account-removed", self,
                         PURPLE_CALLBACK (purple_account_removed_cb), self);

  purple_signal_connect (purple_accounts_get_handle(),
                         "account-enabled", self,
                         PURPLE_CALLBACK (purple_account_changed_cb), self);
  purple_signal_connect (purple_accounts_get_handle(),
                         "account-disabled", self,
                         PURPLE_CALLBACK (purple_account_changed_cb), self);
  purple_signal_connect (purple_accounts_get_handle(),
                         "account-connection-error", self,
                         PURPLE_CALLBACK (purple_account_connection_failed_cb), self);

  purple_signal_connect (purple_conversations_get_handle (),
                         "conversation-updated", self,
                         PURPLE_CALLBACK (purple_conversation_updated_cb), self);
  purple_signal_connect (purple_conversations_get_handle (),
                         "deleting-conversation", self,
                         PURPLE_CALLBACK (purple_deleting_conversation_cb), self);

  purple_signal_connect (purple_conversations_get_handle (),
                         "buddy-typing", self,
                         PURPLE_CALLBACK (purple_buddy_typing_cb), self);
  purple_signal_connect (purple_conversations_get_handle (),
                         "buddy-typed", self,
                         PURPLE_CALLBACK (purple_buddy_typing_stopped_cb), self);
  purple_signal_connect (purple_conversations_get_handle (),
                         "buddy-typing-stopped", self,
                         PURPLE_CALLBACK (purple_buddy_typing_stopped_cb), self);

  /**
   * This is default fallback history handler which is called last,
   * other plugins may intercept and suppress it if they handle history
   * on their own (eg. MAM)
   */

  purple_signal_connect (purple_conversations_get_handle (),
                         "chat-buddy-leaving", self,
                         PURPLE_CALLBACK (purple_conversation_buddy_leaving_cb), self);

  purple_signal_connect_priority (purple_connections_get_handle (),
                                  "autojoin", self,
                                  PURPLE_CALLBACK (purple_connection_autojoin_cb), self,
                                  PURPLE_SIGNAL_PRIORITY_HIGHEST);
  purple_signal_connect (purple_connections_get_handle(),
                         "signing-on", self,
                         PURPLE_CALLBACK (purple_connection_changed_cb), self);
  purple_signal_connect (purple_connections_get_handle(),
                         "signed-on", self,
                         PURPLE_CALLBACK (purple_connection_signed_on_cb), self);
  purple_signal_connect (purple_connections_get_handle(),
                         "signed-off", self,
                         PURPLE_CALLBACK (purple_connection_signed_off_cb), self);

  g_signal_connect_object (network_monitor, "network-changed",
                           G_CALLBACK (purple_network_changed_cb), self,
                           G_CONNECT_AFTER);
}

static PurpleCmdRet
chatty_purple_handle_chatty_cmd (PurpleConversation  *conv,
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
                    " - '/chatty blur_idle [on; off]': Blur idle-contacts icons\n"
                    " - '/chatty typing_info [on; off]': Send typing notifications\n"
                    " - '/chatty msg_receipts [on; off]': Send message receipts\n"
                    " - '/chatty msg_carbons [on; off]': Share chat history\n");
  } else if (!g_strcmp0 (args[1], "on")) {
    if (!g_strcmp0 (args[0], "return_sends")) {
      g_object_set (settings, "return-sends-message", TRUE, NULL);
      msg = g_strdup ("Return key sends messages");
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

  g_debug ("%s", G_STRFUNC);
  g_debug ("%s", args[0]);

  if (msg) {
    purple_conversation_write (conv,
                               "chatty",
                               msg,
                               PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG,
                               time (NULL));
  }

  return PURPLE_CMD_RET_OK;
}

static void
chatty_purple_setup (ChattyPurple *self)
{
  g_assert (CHATTY_IS_PURPLE (self));

  if (!self->disable_auto_login)
    purple_savedstatus_activate (purple_savedstatus_new (NULL, PURPLE_STATUS_AVAILABLE));

  /* chatty_manager_initialize_libpurple (self); */
  chatty_purple_initialize (self);
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
                       chatty_purple_handle_chatty_cmd,
                       "chatty &lt;help&gt;:  "
                       "For a list of commands use the 'help' argument.",
                       self);
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

static void
chatty_purple_ui_init (void)
{
  chatty_purple_setup (chatty_purple_get_default ());
}

static void
chatty_purple_quit (void)
{
  chatty_xeps_close ();
  purple_signals_disconnect_by_handle (chatty_purple_get_default ());
  purple_signals_disconnect_by_handle (purple_connections_get_handle());
  purple_signals_disconnect_by_handle (purple_conversations_get_handle ());
  purple_signals_disconnect_by_handle (chatty_manager_get_default ());

  purple_conversations_set_ui_ops (NULL);
  purple_connections_set_ui_ops (NULL);
  purple_blist_set_ui_ops (NULL);
  purple_accounts_set_ui_ops (NULL);
}

static GHashTable *
chatty_purple_ui_get_info (void)
{
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

static ChattyPpBuddy *
chatty_purple_find_buddy (GListModel  *model,
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
purple_load_messages_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  ChattyHistory *history = (ChattyHistory *)object;
  g_autoptr(ChattyPurple) self = user_data;
  g_autoptr(GPtrArray) messages = NULL;
  g_autoptr(GError) error = NULL;
  ChattyPpChat *chat;

  g_assert (CHATTY_IS_PURPLE (self));
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
      ChattyPpChat *item;

      item = chatty_purple_add_chat (self, chat);
      model = chatty_chat_get_messages (CHATTY_CHAT (item));

      /* If at least one message is loaded, don’t add again. */
      if (g_list_model_get_n_items (model) == 0) {
        chatty_pp_chat_prepend_messages (item, messages);
        chatty_purple_emit_updated ();
      }
    }

  } else if (error &&
             !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
             !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
    g_warning ("Error fetching messages: %s,", error->message);
  }
}

static void
purple_buddy_added_cb (PurpleBuddy  *pp_buddy,
                       ChattyPurple *self)
{
  g_autoptr(ChattyPpChat) chat = NULL;
  PurpleConversation *conv;
  ChattyPpAccount *account;
  ChattyPpBuddy *buddy;
  PurpleAccount *pp_account;
  GListModel *model;

  g_assert (CHATTY_IS_PURPLE (self));

  pp_account = purple_buddy_get_account (pp_buddy);
  if (g_strcmp0 (purple_account_get_protocol_id (pp_account), "prpl-mm-sms") == 0)
    return;

  account = chatty_pp_account_get_object (pp_account);
  g_return_if_fail (account);

  model = chatty_account_get_buddies (CHATTY_ACCOUNT (account));
  buddy = chatty_purple_find_buddy (model, pp_buddy);

  if (!buddy)
    buddy = chatty_pp_account_add_purple_buddy (account, pp_buddy);

  conv = purple_find_conversation_with_account (PURPLE_CONV_TYPE_IM,
                                                pp_buddy->name,
                                                pp_account);
  if (conv)
    chat = conv->ui_data;

  if (chat) {
    g_object_ref (chat);
    chatty_pp_chat_set_purple_conv (chat, conv);
  } else {
    chat = chatty_pp_chat_new_im_chat (pp_account, pp_buddy,
                                       !!self->lurch_plugin);
  }

  chatty_history_get_messages_async (self->history, CHATTY_CHAT (chat), NULL, 1,
                                     purple_load_messages_cb,
                                     g_object_ref (self));
}

static void
purple_buddy_removed_cb (PurpleBuddy  *pp_buddy,
                         ChattyPurple *self)
{
  ChattyPpAccount *account;
  PurpleAccount *pp_account;
  ChattyPpBuddy *buddy;
  GListModel *model;

  g_assert (CHATTY_IS_PURPLE (self));

  pp_account = purple_buddy_get_account (pp_buddy);
  account = chatty_pp_account_get_object (pp_account);

  /*
   * If account is NULL, the account has gotten deleted, and so
   * the buddy object is also deleted along it.
   */
  if (!account)
    return;

  model = chatty_account_get_buddies (CHATTY_ACCOUNT (account));
  buddy = chatty_purple_find_buddy (model, pp_buddy);
  g_return_if_fail (buddy);

  if (g_object_get_data (G_OBJECT (buddy), "chat"))
    chatty_pp_chat_remove_purple_buddy (g_object_get_data (G_OBJECT (buddy), "chat"));

  g_signal_emit_by_name (buddy, "deleted");
  chatty_utils_remove_list_item (G_LIST_STORE (model), buddy);
}


static void
purple_buddy_privacy_chaged_cb (PurpleBuddy *buddy)
{
  if (!PURPLE_BLIST_NODE(buddy)->ui_data)
    return;

  chatty_blist_update (purple_get_blist (), PURPLE_BLIST_NODE(buddy));
}

static void
purple_buddy_signed_on_off_cb (PurpleBuddy *buddy)
{
  ChattyPpBuddy *pp_buddy;

  chatty_blist_update (purple_get_blist(), (PurpleBlistNode*)buddy);

  pp_buddy = chatty_pp_buddy_get_object (buddy);

  /* As avatar depends on online status, emit ::avatar-changed */
  if (pp_buddy)
    g_signal_emit_by_name (pp_buddy, "avatar-changed");

  g_debug ("Buddy \"%s\" (%s) signed on/off", purple_buddy_get_name (buddy),
           purple_account_get_protocol_id (purple_buddy_get_account (buddy)));
}

static void
purple_buddy_icon_chaged_cb (PurpleBuddy *buddy)
{
  PurpleConversation *conv;

  conv = purple_find_conversation_with_account (PURPLE_CONV_TYPE_IM,
                                                buddy->name,
                                                buddy->account);

  if (conv && conv->ui_data)
    g_signal_emit_by_name (conv->ui_data, "avatar-changed");
}

static void
chatty_purple_load_buddies (ChattyPurple *self)
{
  g_autoptr(GSList) buddies = NULL;

  g_return_if_fail (CHATTY_IS_PURPLE (self));

  purple_signal_connect (purple_blist_get_handle (),
                         "buddy-added", self,
                         PURPLE_CALLBACK (purple_buddy_added_cb), self);
  purple_signal_connect (purple_blist_get_handle (),
                         "buddy-removed", self,
                         PURPLE_CALLBACK (purple_buddy_removed_cb), self);
  purple_signal_connect (purple_blist_get_handle (),
                         "buddy-privacy-changed", self,
                         PURPLE_CALLBACK (purple_buddy_privacy_chaged_cb), self);
  purple_signal_connect (purple_blist_get_handle (),
                         "buddy-signed-on", self,
                         PURPLE_CALLBACK (purple_buddy_signed_on_off_cb), self);
  purple_signal_connect (purple_blist_get_handle (),
                         "buddy-signed-off", self,
                         PURPLE_CALLBACK (purple_buddy_signed_on_off_cb), self);
  purple_signal_connect (purple_blist_get_handle (),
                         "buddy-icon-changed", self,
                         PURPLE_CALLBACK (purple_buddy_icon_chaged_cb), self);

  buddies = purple_blist_get_buddies ();

  for (GSList *node = buddies; node; node = node->next)
    purple_buddy_added_cb (node->data, self);
}

static gboolean
chatty_purple_load_plugin (PurplePlugin *plugin)
{
  gboolean loaded;

  if (!plugin || purple_plugin_is_loaded (plugin))
    return TRUE;

  loaded = purple_plugin_load (plugin);
  purple_plugins_save_loaded (CHATTY_PREFS_ROOT "/plugins/loaded");
  g_debug ("plugin: %s, Loading %s", purple_plugin_get_name (plugin),
           CHATTY_LOG_SUCESS (loaded));

  return loaded;
}

static void
chatty_purple_unload_plugin (PurplePlugin *plugin)
{
  gboolean unloaded;

  if (!plugin || !purple_plugin_is_loaded (plugin))
    return;

  unloaded = purple_plugin_unload (plugin);
  purple_plugin_disable (plugin);
  purple_plugins_save_loaded (CHATTY_PREFS_ROOT "/plugins/loaded");
  /* Failing to unload may mean that the application require restart to do so. */
  g_debug ("plugin: %s, Unloading %s", purple_plugin_get_name (plugin),
           CHATTY_LOG_SUCESS (unloaded));
}

static void
chatty_purple_message_carbons_changed_cb (ChattyPurple *self)
{
  g_assert (CHATTY_IS_PURPLE (self));

  if (!self->carbon_plugin)
    return;

  if (chatty_settings_get_message_carbons (chatty_settings_get_default ()))
    chatty_purple_load_plugin (self->carbon_plugin);
  else
    chatty_purple_unload_plugin (self->carbon_plugin);
}

static void
chatty_purple_load_plugins (ChattyPurple *self)
{
  ChattySettings *settings;

  g_assert (CHATTY_IS_PURPLE (self));

  purple_plugins_load_saved (CHATTY_PREFS_ROOT "/plugins/loaded");
  purple_plugins_probe (G_MODULE_SUFFIX);

  self->lurch_plugin = purple_plugins_find_with_id ("core-riba-lurch");
  self->carbon_plugin = purple_plugins_find_with_id ("core-riba-carbons");
  self->file_upload_plugin = purple_plugins_find_with_id ("xep-http-file-upload");

  chatty_purple_load_plugin (self->lurch_plugin);
  chatty_purple_load_plugin (self->file_upload_plugin);

  purple_plugins_init ();
  purple_network_force_online();
  purple_pounces_load ();

  chatty_xeps_init ();
  settings = chatty_settings_get_default ();

  if (chatty_settings_get_experimental_features (settings))
    chatty_purple_unload_plugin (purple_plugins_find_with_id ("prpl-matrix"));

  /* We now have native SMS */
  chatty_purple_unload_plugin (purple_plugins_find_with_id ("prpl-mm-sms"));

  g_signal_connect_object (settings, "notify::message-carbons",
                           G_CALLBACK (chatty_purple_message_carbons_changed_cb), self,
                           G_CONNECT_SWAPPED);
  chatty_purple_message_carbons_changed_cb (self);

  {
    g_autoptr(GString) plugins_str = NULL;
    GList *plugins;

    plugins_str = g_string_new (NULL);
    plugins = purple_plugins_get_loaded ();

    for (GList *item = plugins; item; item = item->next)
      g_string_append_printf (plugins_str, "%s:%s ",
                              purple_plugin_get_id (item->data),
                              purple_plugin_get_version (item->data));

    g_debug ("Loaded purple plugins: %s", plugins_str->str);
  }
}

static void
chatty_purple_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  ChattyPurple *self = (ChattyPurple *)object;

  switch (prop_id)
    {
    case PROP_ENABLED:
      g_value_set_boolean (value, self->enabled);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
chatty_purple_finalize (GObject *object)
{
  ChattyPurple *self = (ChattyPurple *)object;

  g_clear_object (&self->accounts);
  g_clear_object (&self->list_of_user_list);
  g_clear_object (&self->history);
  g_clear_pointer (&ui_info, g_hash_table_unref);

  G_OBJECT_CLASS (chatty_purple_parent_class)->finalize (object);
}

static void
chatty_purple_class_init (ChattyPurpleClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = chatty_purple_finalize;
  object_class->get_property = chatty_purple_get_property;

  /**
   * ChattyPurple:enabled:
   *
   * Whether purple is enabled/disabled
   */
  properties[PROP_ENABLED] =
    g_param_spec_boolean ("enabled",
                          "purple enabled",
                          "Purple enabled",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
chatty_purple_init (ChattyPurple *self)
{
  purple_core_set_ui_ops (&core_ui_ops);
  purple_eventloop_set_ui_ops (&eventloop_ui_ops);

  ui_info = g_hash_table_new (g_str_hash, g_str_equal);
  g_hash_table_insert (ui_info, "name", (char *)g_get_application_name ());
  g_hash_table_insert (ui_info, "version", PACKAGE_VERSION);
  g_hash_table_insert (ui_info, "dev_website", "https://source.puri.sm/Librem5/chatty");
  g_hash_table_insert (ui_info, "client_type", "phone");

  self->accounts = g_list_store_new (CHATTY_TYPE_ACCOUNT);
  self->list_of_user_list = g_list_store_new (G_TYPE_LIST_MODEL);
  self->users_list = gtk_flatten_list_model_new (CHATTY_TYPE_ITEM,
                                                 G_LIST_MODEL (self->list_of_user_list));
  self->chat_list = g_list_store_new (CHATTY_TYPE_CHAT);
}

ChattyPurple *
chatty_purple_get_default (void)
{
  static ChattyPurple *self;

  if (!self) {
    self = g_object_new (CHATTY_TYPE_PURPLE, NULL);
    g_set_weak_pointer (&self, self);
  }

  return self;
}

void
chatty_purple_enable_debug (void)
{
  enable_debug = TRUE;

  purple_debug_set_enabled (enable_debug);

  if (chatty_log_get_verbosity () > 3)
    purple_debug_set_verbose (TRUE);
}

gboolean
chatty_purple_is_loaded (ChattyPurple *self)
{
  g_return_val_if_fail (CHATTY_IS_PURPLE (self), FALSE);

  return self->enabled;
}

static ChattyChat *
chatty_purple_start_buddy_chat (ChattyPurple  *self,
                                ChattyPpBuddy *buddy)
{
  ChattyPpChat *item, *chat;
  GListModel *model;

  g_return_val_if_fail (CHATTY_IS_PURPLE (self), NULL);
  g_return_val_if_fail (CHATTY_IS_PP_BUDDY (buddy), NULL);

  model = G_LIST_MODEL (self->chat_list);
  chat = chatty_pp_chat_new_buddy_chat (buddy, chatty_purple_has_encryption (self));

  if (chatty_utils_get_item_position (model, chat, NULL))
    item = chat;
  else
    item = chatty_purple_find_chat (model, chat);

  CHATTY_DEBUG (chatty_item_get_username (CHATTY_ITEM (chat)),
                "Starting buddy chat, pre-existing: %d, buddy:", !!item);

  if (!item) {
    item = chat;
    chatty_chat_set_data (CHATTY_CHAT (chat), NULL, self->history);
    g_list_store_append (self->chat_list, chat);
  }

  return CHATTY_CHAT (item);
}

void
chatty_purple_start_chat (ChattyPurple *self,
                          ChattyItem   *item)
{
  ChattyChat *chat;

  g_return_if_fail (CHATTY_IS_PURPLE (self));
  g_return_if_fail (CHATTY_IS_PP_CHAT (item) || CHATTY_IS_PP_BUDDY (item));

  if (CHATTY_IS_PP_BUDDY (item)) {
    chat = g_object_get_data (G_OBJECT (item), "chat");

    if (!chat)
      chat = chatty_purple_start_buddy_chat (self, CHATTY_PP_BUDDY (item));
  } else {
    chat = CHATTY_CHAT (item);
  }

  chatty_pp_chat_join (CHATTY_PP_CHAT (chat));
}

GListModel *
chatty_purple_get_accounts (ChattyPurple *self)
{
  g_return_val_if_fail (CHATTY_IS_PURPLE (self), NULL);

  return G_LIST_MODEL (self->accounts);
}

GListModel *
chatty_purple_get_chat_list (ChattyPurple *self)
{
  g_return_val_if_fail (CHATTY_IS_PURPLE (self), NULL);

  return G_LIST_MODEL (self->chat_list);
}

GListModel *
chatty_purple_get_user_list (ChattyPurple *self)
{
  g_return_val_if_fail (CHATTY_IS_PURPLE (self), NULL);

  return G_LIST_MODEL (self->users_list);
}

ChattyAccount *
chatty_purple_find_account_with_name (ChattyPurple   *self,
                                      ChattyProtocol  protocol,
                                      const char     *account_id)
{
  GListModel *account_list;
  guint n_items;

  g_return_val_if_fail (CHATTY_IS_PURPLE (self), NULL);

  account_list = G_LIST_MODEL (self->accounts);
  n_items = g_list_model_get_n_items (account_list);

  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyAccount) account = NULL;

    account = g_list_model_get_item (account_list, i);

    if (protocol & chatty_item_get_protocols (CHATTY_ITEM (account)) &&
        g_strcmp0 (account_id, chatty_item_get_username (CHATTY_ITEM (account))) == 0)
      return account;
  }

  return NULL;
}

ChattyChat *
chatty_purple_find_chat_with_name (ChattyPurple   *self,
                                   ChattyProtocol  protocol,
                                   const char     *account_id,
                                   const char     *chat_id)
{
  GListModel *chat_list;
  guint n_items;

  g_return_val_if_fail (CHATTY_IS_PURPLE (self), NULL);

  chat_list = chatty_purple_get_chat_list (self);
  n_items = g_list_model_get_n_items (chat_list);

  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyChat) chat = NULL;

    chat = g_list_model_get_item (chat_list, i);

    if (!(protocol & chatty_item_get_protocols (CHATTY_ITEM (chat))))
      continue;

    if (g_strcmp0 (account_id, chatty_item_get_username (CHATTY_ITEM (chat))) != 0)
      continue;

    if (g_strcmp0 (chat_id, chatty_chat_get_chat_name (chat)) == 0)
      return chat;
  }

  return NULL;
}

void
chatty_purple_delete_account_async (ChattyPurple        *self,
                                    ChattyAccount       *account,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (CHATTY_IS_PURPLE (self));
  g_return_if_fail (CHATTY_IS_PP_ACCOUNT (account));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_task_data (task, g_object_ref (account), g_object_unref);

  chatty_utils_remove_list_item (self->accounts, account);
  chatty_account_delete (account);

  g_task_return_boolean (task, TRUE);
}

gboolean
chatty_purple_delete_account_finish (ChattyPurple  *self,
                                     GAsyncResult  *result,
                                     GError       **error)
{
  g_return_val_if_fail (CHATTY_IS_PURPLE (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

ChattyProtocol
chatty_purple_get_protocols (ChattyPurple *self)
{
  g_return_val_if_fail (CHATTY_IS_PURPLE (self), CHATTY_PROTOCOL_NONE);

  if (!chatty_purple_is_loaded (self))
    return CHATTY_PROTOCOL_NONE;

  return self->active_protocols;
}

void
chatty_purple_set_history_db (ChattyPurple  *self,
                              ChattyHistory *db)
{
  g_return_if_fail (CHATTY_IS_PURPLE (self));
  g_return_if_fail (!self->history);

  g_set_object (&self->history, db);
}

#if 0
static void
chatty_purple_disable (ChattyPurple *self)
{
  g_assert (CHATTY_IS_PURPLE (self));

  if (!self->enabled)
    return;

  CHATTY_DEBUG_MSG ("Disabling purple");
  self->enabled = FALSE;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);

  g_list_store_remove_all (self->accounts);
  g_list_store_remove_all (self->list_of_user_list);
  g_list_store_remove_all (self->chat_list);

  purple_core_quit ();
  chatty_purple_emit_updated ();
}
#endif

static void
chatty_purple_enable (ChattyPurple *self)
{
  g_autofree char *search_path = NULL;

  g_assert (CHATTY_IS_PURPLE (self));

  if (self->enabled)
    return;

  search_path = g_build_filename (purple_user_dir (), "plugins", NULL);
  purple_plugins_add_search_path (search_path);

  self->enabled = TRUE;
  CHATTY_DEBUG_MSG ("Enabling purple");
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);

  if (!purple_core_init (CHATTY_UI)) {
    g_warning ("libpurple initialization failed");

    return;
  }

  if (!purple_core_ensure_single_instance ()) {
    g_warning ("Another libpurple client is already running");
    purple_core_quit ();

    return;
  }

  purple_set_blist (purple_blist_new ());
  purple_prefs_load ();
  purple_blist_load ();
  purple_plugins_load_saved (CHATTY_PREFS_ROOT "/plugins/loaded");
  chatty_purple_load_plugins (self);

  for (GList *node = purple_accounts_get_all (); node; node = node->next)
    purple_account_added_cb (node->data, self);

  chatty_purple_load_buddies (self);

  purple_savedstatus_activate (purple_savedstatus_get_startup());
  purple_accounts_restore_current_statuses ();
  purple_blist_show ();

  g_debug ("libpurple initialized. Running version %s.",
           purple_core_get_version ());
}

static void
purple_enable_changed_cb (ChattyPurple *self)
{
  ChattySettings *settings;
  gboolean enabled;

  g_assert (CHATTY_IS_PURPLE (self));

  settings = chatty_settings_get_default ();
  enabled = chatty_settings_get_purple_enabled (settings);

  /* We only enable, disabling requires chatty restart */
  if (enabled)
    chatty_purple_enable (self);
}

void
chatty_purple_load (ChattyPurple *self,
                    gboolean      disable_auto_login)
{
  g_return_if_fail (CHATTY_IS_PURPLE (self));
  g_return_if_fail (self->history);

  if (self->is_loaded)
    return;

  g_info ("Loading purple, auto login: %d", !disable_auto_login);
  purple_debug_set_enabled (enable_debug);

  if (chatty_log_get_verbosity () > 3)
    purple_debug_set_verbose (TRUE);

  self->is_loaded = TRUE;
  self->disable_auto_login = !!disable_auto_login;

  g_signal_connect_object (chatty_settings_get_default (),
                           "notify::purple-enabled",
                           G_CALLBACK (purple_enable_changed_cb),
                           self, G_CONNECT_SWAPPED);
  purple_enable_changed_cb (self);
}

gboolean
chatty_purple_has_encryption (ChattyPurple *self)
{
  g_return_val_if_fail (CHATTY_IS_PURPLE (self), FALSE);

  if (!self->lurch_plugin)
    return FALSE;

  return purple_plugin_is_loaded (self->lurch_plugin);
}

gboolean
chatty_purple_has_carbon_plugin (ChattyPurple *self)
{
  g_return_val_if_fail (CHATTY_IS_PURPLE (self), FALSE);

  return self->carbon_plugin != NULL;
}

gboolean
chatty_purple_has_telegram_loaded (ChattyPurple *self)
{
  PurplePlugin *plugin;

  g_return_val_if_fail (CHATTY_IS_PURPLE (self), FALSE);

  if (!self->enabled)
    return FALSE;

  plugin = purple_plugins_find_with_id ("prpl-telegram");

  if (plugin)
    return purple_plugin_is_loaded (plugin);

  return FALSE;
}
