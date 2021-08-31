/*
 * Copyright (C) 2018 Purism SPC
 * Copyright (C) 2019 Ruslan N. Marchenko
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-xeps"

#include <glib.h>
#include <libpurple/prpl.h>
#include <libpurple/version.h>
#include <libpurple/xmlnode.h>
#include "jabber.h"
#include "xeps.h"
#include "chatty-xep-0184.h"
#include "chatty-xep-0313.h"
#include "chatty-xep-0352.h"
#include "chatty-log.h"

static PurplePlugin *jabber = NULL;

static void
jabber_disco_bare_info_result_cb(JabberStream *js, const char *from,
                                 JabberIqType type, const char *id,
                                 xmlnode *packet, gpointer data)
{
  xmlnode *query, *child;

  if (type == JABBER_IQ_ERROR)
    return;

  query = xmlnode_get_child(packet, "query");

  for(child = xmlnode_get_child(query, "feature"); child;
      child = xmlnode_get_next_twin(child)) {
    const char *var;

    if(!(var = xmlnode_get_attrib(child, "var")))
      continue;

    g_debug ("Discovered feature %s on own bare", var);
    purple_signal_emit (jabber, "jabber-bare-info", js->gc, from, var);
  }
}


static void
jabber_disco_bare_items_result_cb(JabberStream *js, const char *from,
                                  JabberIqType type, const char *id,
                                  xmlnode *packet, gpointer data)
{
  xmlnode *query, *child;

  if (type == JABBER_IQ_ERROR)
    return;

  query = xmlnode_get_child(packet, "query");

  for(child = xmlnode_get_child(query, "item"); child;
      child = xmlnode_get_next_twin(child)) {
    const char *jid, *node;

    if(!(jid = xmlnode_get_attrib(child, "jid")))
      continue;

    if(!(node = xmlnode_get_attrib(child, "node")))
      continue;

    // find out some useful nodes perhaps? Like, eh... ava, nick
    CHATTY_DEBUG (jid, "Discovered node %s on bare", node);
    purple_signal_emit (jabber, "jabber-bare-items", js->gc, jid, node);
  }
}


/**
 * jabber_disco_items_bare:
 * @js: a JabberStream
 *
 * This function is called via the
 * "signed-on" signal
 *
 */
static void
jabber_disco_items_bare (JabberStream *js, const char *bare)
{
  // Upstream does not discover bare, need to fill the gap
  JabberIq *iq = jabber_iq_new_query(js, JABBER_IQ_GET, NS_DISCO_ITEMS);

  if(bare)
    xmlnode_set_attrib(iq->node, "to", bare);

  jabber_iq_set_callback(iq, jabber_disco_bare_items_result_cb, NULL);
  jabber_iq_send(iq);

  iq = jabber_iq_new_query(js, JABBER_IQ_GET, NS_DISCO_INFO);
  if(bare)
    xmlnode_set_attrib(iq->node, "to", bare);

  jabber_iq_set_callback(iq, jabber_disco_bare_info_result_cb, NULL);
  jabber_iq_send(iq);
}


static void
cb_chatty_xeps_signed_on (PurpleConnection *pc, void *data)
{
  PurpleAccount *pa = purple_connection_get_account(pc);
  JabberStream *js;
  // Skip non-jabber sign-on
  if(g_strcmp0 ("prpl-jabber", purple_account_get_protocol_id (pa)))
    return;

  js = purple_connection_get_protocol_data(pc);
  jabber_disco_items_bare (js, NULL);
}

static void
cb_chatty_xeps_chat_joined (PurpleConversation *pv, void *data)
{
  PurpleAccount *pa = purple_conversation_get_account(pv);
  PurpleConnection *pc = purple_account_get_connection(pa);
  JabberStream *js;

  g_return_if_fail(pa != NULL);
  g_return_if_fail(pc != NULL);

  // Skip non-jabber chats
  if(g_strcmp0 ("prpl-jabber", purple_account_get_protocol_id (pa)))
    return;

  js = purple_connection_get_protocol_data(pc);
  jabber_disco_items_bare (js, purple_conversation_get_name(pv));
}


/**
 * chatty_xeps_close:
 *
 * Unref node hashtable
 */
void
chatty_xeps_close (void)
{
  if(!jabber)
    return;

  chatty_0184_close ();
  chatty_0313_close ();
  chatty_0352_close ();
  // garbage collection
  purple_signals_disconnect_by_handle(chatty_xeps_get_handle());
  purple_signal_unregister(jabber, "jabber-bare-items");
  purple_signal_unregister(jabber, "jabber-bare-info");
}


/**
 * chatty_xeps_init:
 *
 * Sets purple XEP functions
 * and defines libpurple signal callbacks
 *
 */
void
chatty_xeps_init (void)
{
  jabber = purple_find_prpl ("prpl-jabber");

  if (!jabber) {
    g_debug ("plugin prpl-jabber not loaded");

    return;
  }

  purple_signal_register (jabber, "jabber-bare-items",
                          purple_marshal_VOID__POINTER_POINTER_POINTER, NULL, 2,
			  purple_value_new(PURPLE_TYPE_SUBTYPE, PURPLE_SUBTYPE_CONNECTION),
			  purple_value_new(PURPLE_TYPE_STRING), /* bare */
			  purple_value_new(PURPLE_TYPE_STRING));/* node */

  purple_signal_register (jabber, "jabber-bare-info",
                          purple_marshal_VOID__POINTER_POINTER_POINTER, NULL, 2,
			  purple_value_new(PURPLE_TYPE_SUBTYPE, PURPLE_SUBTYPE_CONNECTION),
			  purple_value_new(PURPLE_TYPE_STRING), /* bare */
			  purple_value_new(PURPLE_TYPE_STRING));/* var */

  purple_signal_connect(purple_connections_get_handle(),
                        "signed-on",
                        chatty_xeps_get_handle(),
                        PURPLE_CALLBACK(cb_chatty_xeps_signed_on),
                        NULL);

  purple_signal_connect(purple_conversations_get_handle(),
                        "chat-joined",
                        chatty_xeps_get_handle(),
                        PURPLE_CALLBACK(cb_chatty_xeps_chat_joined),
                        NULL);
  chatty_0184_init();
  chatty_0313_init();
  chatty_0352_init();
}

inline void *
chatty_xeps_get_handle (void)
{
  static int handle;

  return &handle;
}

inline PurplePlugin *
chatty_xeps_get_jabber (void)
{
  return jabber;
}

/* vim: set sts=2 et: */
