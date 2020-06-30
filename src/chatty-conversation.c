/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#define G_LOG_DOMAIN "chatty-conversation"

#include <glib/gi18n.h>
#include "chatty-application.h"
#include "chatty-window.h"
#include "chatty-manager.h"
#include "chatty-icons.h"
#include "chatty-chat-view.h"
#include "chatty-conversation.h"
#include "chatty-history.h"
#include "chatty-utils.h"
#include "chatty-notify.h"
#include "chatty-chat-view.h"


// *** callbacks

static void
cb_buddy_typing (PurpleAccount *account,
                 const char    *name)
{
  PurpleConversation *conv;
  ChattyConversation *chatty_conv;

  conv = purple_find_conversation_with_account (PURPLE_CONV_TYPE_IM,
                                                name,
                                                account);
  if (!conv) {
    return;
  }

  chatty_conv = CHATTY_CONVERSATION(conv);

  if (chatty_conv && chatty_conv->conv == conv) {
    chatty_chat_view_show_typing_indicator (CHATTY_CHAT_VIEW (chatty_conv->chat_view));
  }
}


static void
cb_buddy_typed (PurpleAccount *account,
                const char    *name)
{
  PurpleConversation *conv;
  ChattyConversation *chatty_conv;

  conv = purple_find_conversation_with_account (PURPLE_CONV_TYPE_IM,
                                                name,
                                                account);
  if (!conv) {
    return;
  }

  chatty_conv = CHATTY_CONVERSATION(conv);

  if (chatty_conv && chatty_conv->conv == conv) {
    chatty_chat_view_hide_typing_indicator (CHATTY_CHAT_VIEW (chatty_conv->chat_view));
  }
}


static void
cb_update_buddy_icon (PurpleBuddy *buddy)
{
  PurpleConversation *conv;

  conv = purple_find_conversation_with_account (PURPLE_CONV_TYPE_IM, 
                                                buddy->name, 
                                                buddy->account);

  if (conv) {
    ChattyChat *chat;

    chat = chatty_manager_find_purple_conv (chatty_manager_get_default (), conv);

    if (chat)
      g_signal_emit_by_name (G_OBJECT (chat), "avatar-changed");
  }
}

// *** end callbacks

/**
 * chatty_conv_container_get_active_chatty_conv:
 * @notebook: a GtkNotebook
 *
 * Returns the chatty conversation that is
 * currently set active in the notebook
 *
 * Returns: ChattyConversation
 *
 */
ChattyConversation *
chatty_conv_container_get_active_chatty_conv (GtkNotebook *notebook)
{
  int       index;
  GtkWidget *tab_cont;

  index = gtk_notebook_get_current_page (GTK_NOTEBOOK(notebook));

  if (index == -1) {
    index = 0;
  }

  tab_cont = gtk_notebook_get_nth_page (GTK_NOTEBOOK(notebook), index);

  if (!tab_cont) {
    return NULL;
  }

  return g_object_get_data (G_OBJECT(tab_cont), "ChattyConversation");
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

  chatty_conv_show_conversation (conv);
}


/**
 * chatty_conv_show_conversation:
 * @conv: a PurpleConversation
 *
 * Shows a conversation after a notification
 *
 * Called from cb_open_message in chatty-notify.c
 *
 */
void
chatty_conv_show_conversation (PurpleConversation *conv)
{
  ChattyWindow       *window;

  if (!conv) {
    return;
  }

  window = chatty_application_get_main_window (CHATTY_APPLICATION_DEFAULT ());

  purple_conversation_present (conv);

  chatty_window_change_view (window, CHATTY_VIEW_MESSAGE_LIST);

  gtk_window_present (GTK_WINDOW(window));
}


void
chatty_conv_add_history_since_component (GHashTable *components,
                                         const char *account,
                                         const char *room){
  time_t mtime;
  struct tm * timeinfo;

  g_autofree gchar *iso_timestamp = g_malloc0(MAX_GMT_ISO_SIZE * sizeof(char));

  mtime = chatty_history_get_chat_last_message_time(account, room);
  mtime += 1; // Use the next epoch to exclude the last stored message(s)
  timeinfo = gmtime (&mtime);
  g_return_if_fail (strftime (iso_timestamp,
                              MAX_GMT_ISO_SIZE * sizeof(char),
                              "%Y-%m-%dT%H:%M:%SZ",
                              timeinfo));

  g_hash_table_steal (components, "history_since");
  g_hash_table_insert (components, "history_since", g_steal_pointer(&iso_timestamp));
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


void *
chatty_conversations_get_handle (void)
{
  static int handle;

  return &handle;
}


/**
 * chatty_init_conversations:
 *
 * Sets purple conversations preferenz values
 * and defines libpurple signal callbacks
 *
 */
void
chatty_conversations_init (void)
{
  void *handle = chatty_conversations_get_handle ();
  void *blist_handle = purple_blist_get_handle ();

  purple_signal_connect (blist_handle, "buddy-icon-changed",
                          handle, PURPLE_CALLBACK (cb_update_buddy_icon), NULL);

  purple_signal_connect (purple_conversations_get_handle (),
                         "buddy-typing", &handle,
                         PURPLE_CALLBACK (cb_buddy_typing), NULL);

  purple_signal_connect (purple_conversations_get_handle (),
                         "buddy-typed", &handle,
                         PURPLE_CALLBACK (cb_buddy_typed), NULL);

  purple_signal_connect (purple_conversations_get_handle (),
                         "buddy-typing-stopped", &handle,
                         PURPLE_CALLBACK (cb_buddy_typed), NULL);

  chatty_chat_view_purple_init ();
}


void
chatty_conversations_uninit (void)
{
  chatty_chat_view_purple_uninit ();
  purple_prefs_disconnect_by_handle (chatty_conversations_get_handle());
  purple_signals_disconnect_by_handle (chatty_conversations_get_handle());
  purple_signals_unregister_by_instance (chatty_conversations_get_handle());
}
