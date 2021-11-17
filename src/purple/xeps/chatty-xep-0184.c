/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * 
 * Implementation based on:
 * https://github.com/noonien-d/pidgin-xmpp-receipts/blob/master/xmpp-receipts.c
 * 
 */

#define G_LOG_DOMAIN "chatty-xeps"

#include <glib.h>
#include <gtk/gtk.h>
#include "chatty-message.h"
#include "chatty-chat.h"
#include "chatty-manager.h"
#include "version.h"
#include "prpl.h"
#include "xmlnode.h"
#include "xeps.h"
#include "chatty-xep-0184.h"
#include "chatty-settings.h"


static GHashTable *ht_bubble_node = NULL;


static gboolean
cb_ht_bubble_node_check_items (gpointer key,
                               gpointer value,
                               gpointer user_data)
{
  return ((GtkWidget*)value == user_data) ? TRUE : FALSE;
}


/**
 * cb_chatty_xep_deleting_conversation:
 * @conv: a PurpleConversation
 *
 * Clear the hash-table when a conversation
 * was deleted
 *
 */
static void
cb_chatty_xep_deleting_conversation (PurpleConversation *conv)
{
  g_hash_table_foreach_remove (ht_bubble_node,
                               cb_ht_bubble_node_check_items,
                               conv->ui_data);

  g_debug ("conversation closed");
}


/**
 * chatty_xeps_display_received:
 * @node_id: a const char
 *
 * Search hash-table based on node_id
 * and add a check-mark to the corresponding
 * msg-bubble footer
 *
 */
static void
chatty_xeps_display_received (const char* node_id)
{
  ChattyChat *chat;

  if (node_id == NULL) {
    return;
  }

  chat = g_hash_table_lookup (ht_bubble_node, node_id);

  if (chat != NULL) {
    GListModel *message_list;
    g_autoptr(ChattyMessage) message = NULL;
    guint n_items;

    message_list = chatty_chat_get_messages (chat);
    n_items = g_list_model_get_n_items (message_list);
    message = g_list_model_get_item (message_list, n_items - 1);

    if (message)
      chatty_message_set_status (message, CHATTY_STATUS_DELIVERED, time (NULL));
    else if (n_items)
      g_warn_if_reached ();

    g_hash_table_remove (ht_bubble_node, node_id);
  }

}


/**
 * chatty_xeps_add_sent:
 * @node_to: a const char
 * @node_id: a const char
 *
 * Add nodes to a hash-table which
 * will be looked up for adding
 * receipt notifications to sent messages
 *
 */
static void
chatty_xeps_add_sent (PurpleConnection *gc,
                      const char       *node_to,
                      const char       *node_id)
{
  PurpleAccount       *account;
  PurpleConversation  *conv;

  account = purple_connection_get_account (gc);

  if (!account) {
    return;
  }

  conv = purple_find_conversation_with_account (PURPLE_CONV_TYPE_ANY,
                                                node_to,
                                                account);

  if (!conv || !conv->ui_data) {
    return;
  }

  g_hash_table_insert (ht_bubble_node, strdup (node_id), conv->ui_data);

  g_debug ("attached key: %s, table size %i \n",
           node_id,
           g_hash_table_size (ht_bubble_node));
}


/**
 * cb_chatty_xeps_xmlnode_received:
 * @gc: a PurpleConnection
 * @packet: a xmlnode
 *
 * This function is called via the
 * "jabber-received-xmlnode" signal
 *
 */
static void
cb_chatty_xeps_xmlnode_received (PurpleConnection  *gc,
                                 xmlnode          **packet,
                                 gpointer           null)
{
  xmlnode    *node_request;
  xmlnode    *node_received;
  xmlnode    *received;
  xmlnode    *message;
  const char *from;
  const char *node_id;
  const char *node_ns;

  if (*packet != NULL) {
    if (g_strcmp0 ((*packet)->name, "message") == 0) {

      node_request = xmlnode_get_child (*packet, "request");

      from = xmlnode_get_attrib (*packet , "from");

      if (node_request) {
        node_id = xmlnode_get_attrib (*packet , "id");
        node_ns = xmlnode_get_namespace (node_request);

        if (g_strcmp0 (node_ns, "urn:xmpp:receipts") == 0) {
          message = xmlnode_new ("message");
          xmlnode_set_attrib (message, "to", from);

          received = xmlnode_new_child (message, "received");

          xmlnode_set_namespace (received, "urn:xmpp:receipts");
          xmlnode_set_attrib (received, "id", node_id);

          purple_signal_emit (purple_connection_get_prpl (gc),
                              "jabber-sending-xmlnode",
                              gc,
                              &message);

          if (message != NULL) {
            xmlnode_free (message);
          }
        }
      }

      node_received = xmlnode_get_child (*packet, "received");

      if (node_received) {
        node_ns = xmlnode_get_namespace (node_received);
        node_id = xmlnode_get_attrib (node_received, "id");

        if (g_strcmp0 (node_ns, "urn:xmpp:receipts") == 0) {
          chatty_xeps_display_received (node_id);

          g_debug ("Received ackn for node_id: %s", node_id);
        }
      }
    }
  }
}


/**
 * cb_chatty_xeps_xmlnode_send:
 * @gc: a PurpleConnection
 * @packet: a xmlnode
 *
 * This function is called via the
 * "jabber-sending-xmlnode" signal
 *
 */
static void
cb_chatty_xeps_xmlnode_send (PurpleConnection  *gc,
                             xmlnode          **packet,
                             gpointer           null)
{
  xmlnode    *node_body;
  xmlnode    *child;
  const char *node_to;
  const char *node_id;

  if (!chatty_settings_get_send_receipts (chatty_settings_get_default ())) {
    return;
  }

  if (*packet != NULL && (*packet)->name) {

    if (g_strcmp0 ((*packet)->name, "message") == 0 &&
        g_strcmp0 (xmlnode_get_attrib(*packet, "type"), "groupchat") != 0) {
      node_body = xmlnode_get_child (*packet, "body");

      if (node_body) {
        child = xmlnode_new_child (*packet, "request");
        xmlnode_set_attrib (child, "xmlns", "urn:xmpp:receipts");

        node_to = xmlnode_get_attrib (*packet , "to");
        node_id = xmlnode_get_attrib (*packet , "id");

        g_debug ("Send ackn request for node_id: %s", node_id);

        chatty_xeps_add_sent (gc, node_to, node_id);
      }
    }
  }
}


/**
 * chatty_0184_close:
 *
 * Unref node hashtable
 */
void
chatty_0184_close (void)
{
  g_hash_table_destroy (ht_bubble_node);
}


/**
 * chatty_0184_init:
 *
 * Sets purple XEP functions
 * and defines libpurple signal callbacks
 *
 */
void
chatty_0184_init (void)
{
  PurplePlugin *jabber = chatty_xeps_get_jabber ();
  void *handle = chatty_xeps_get_handle ();
  gboolean      ok;


  void *conv_handle = purple_conversations_get_handle();

  jabber = purple_find_prpl ("prpl-jabber");

  purple_plugin_ipc_call (jabber, "add_feature", &ok, "urn:xmpp:receipts");

  if (ok) {
    g_debug ("xmpp receipt feature added");
  } else {
    g_debug ("xmpp receipt feature not added");
  }

  ht_bubble_node  = g_hash_table_new_full (g_str_hash,
                                           g_str_equal,
                                           g_free,
                                           NULL);

  purple_signal_connect (jabber,
                         "jabber-receiving-xmlnode",
                         &handle,
                         PURPLE_CALLBACK(cb_chatty_xeps_xmlnode_received),
                         NULL);

  purple_signal_connect_priority (jabber,
                                  "jabber-sending-xmlnode",
                                  &handle,
                                  PURPLE_CALLBACK(cb_chatty_xeps_xmlnode_send),
                                  NULL,
                                  -101);

  purple_signal_connect (conv_handle,
                         "deleting-conversation",
                         &handle,
                         PURPLE_CALLBACK(cb_chatty_xep_deleting_conversation),
                         NULL);
}
