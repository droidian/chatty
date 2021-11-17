/*
 * Copyright (C) 2018 Purism SPC
 * Copyright (C) 2019 Ruslan N. Marchenko
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-xeps-0313"

#include <glib.h>
#include <prpl.h>
#include <xmlnode.h>
#include <account.h>
#include <version.h>
#include "jabber.h"
#include "message.h"
#include "xeps.h"
#include "chatty-xep-0313.h"
#include "chatty-pp-utils.h"
#include "chatty-utils.h"
#include "chatty-history.h"
#include "chatty-pp-chat.h"
#include "chatty-manager.h"
#include "chatty-settings.h"
#include "chatty-log.h"

#define NS_FWDv0 "urn:xmpp:forward:0"
#define NS_SIDv0 "urn:xmpp:sid:0"
#define NS_MAMv2 "urn:xmpp:mam:2"
#define NS_DATA "jabber:x:data"
#define NS_RSM "http://jabber.org/protocol/rsm"

typedef struct {
  PurpleConversation *conv;
  PurpleConvMessage p;
  char *id;
  PurpleConversationType type;
} MamMsg;

typedef struct {
  JabberStream  *js;
  char          *id;
  char          *to;
  char        *with;
  char       *after;
  char      *before;
  char       *start;
  char         *end;
  int           max;
} MAMQuery;

/* FIXME: What if purple becomes multithreaded 8-O */
typedef struct {
  GHashTable *qs;
  time_t   last_ts;
  MamMsg  *cur_msg;
  char    *cur_oid;
  char    *ns;
} MamCtx;

static GHashTable *ht_mam_ctx = NULL;

/**
 * mamq_free:
 *
 * Free MAMQuery structure and its internals
 */
static void
mamq_free(void *ptr)
{
  MAMQuery *mamq = (MAMQuery*)ptr;
  if(ptr==NULL) return;
  g_free(mamq->id);
  g_free(mamq->to);
  g_free(mamq->with);
  g_free(mamq->after);
  g_free(mamq->before);
  g_free(mamq->start);
  g_free(mamq->end);
  g_free(mamq);
}

/**
 * mamm_free:
 *
 * Free MamMsg structure and internals
 */
static void
mamm_free(void *ptr)
{
  MamMsg *mm = (MamMsg*)ptr;
  if(ptr==NULL) return;
  g_free(mm->id);
  g_free(mm->p.who);
  g_free(mm->p.what);
  g_free(mm->p.alias);
  g_free(mm);
}

/**
 * MAM Context Management API
 */

/**
 * mamc_free:
 *
 * Free MamCtx context and its internals
 */
static void
mamc_free(void *ptr)
{
  MamCtx *mamc = (MamCtx*)ptr;
  if(ptr==NULL) return;
  g_free(mamc->ns);
  g_free(mamc->cur_oid);
  mamm_free(mamc->cur_msg);
  g_hash_table_destroy(mamc->qs);
  g_free(mamc);
}

/**
 * mamc_new:
 *
 * Create new empty MamCtx context.
 */
static MamCtx *
mamc_new(void)
{
  MamCtx *mamc = g_new0(MamCtx, 1);
  mamc->qs = g_hash_table_new_full(g_str_hash,
                                   g_str_equal,
                                   g_free,
                                   mamq_free);
  return mamc;
}

/**
 * chatty_mam_ctx_get:
 * @pa: PurpleAccount which username is used a key for context payload
 *
 * retrieve current MamCtx context for the @pa.
 * Returns NULL if it doesn't exist.
 */
static MamCtx *
chatty_mam_ctx_get(PurpleAccount *pa)
{
  g_return_val_if_fail(pa != NULL, NULL);
  return g_hash_table_lookup(ht_mam_ctx, purple_account_get_username(pa));
}

/**
 * chatty_mam_ctx_add:
 * @pa: PurpleAccount which username is used a key for context payload
 *
 * Creates (allocates and zerrores) new MamCtx context and registers it
 * in context hashtable for the given @pa.
 * If the context already exists simply returns current context.
 */
static inline MamCtx *
chatty_mam_ctx_add(PurpleAccount *pa)
{
  MamCtx *mamc = chatty_mam_ctx_get(pa);
  g_return_val_if_fail(pa != NULL, NULL);
  if(mamc == NULL) {
    mamc = mamc_new();
    g_hash_table_insert(ht_mam_ctx,
                        g_strdup(purple_account_get_username(pa)), mamc);
  }
  return mamc;
}

/**
 * chatty_mam_ctx_del:
 * @pa: PurpleAccount which username is used a key for context payload
 *
 * Removes MamCtx context for the account @pa, which is auto-freed.
 */
static void
chatty_mam_ctx_del(PurpleAccount *pa)
{
  g_return_if_fail(pa != NULL);
  CHATTY_DEBUG (purple_account_get_username (pa), "Cleaning context for ");
  g_hash_table_remove(ht_mam_ctx, purple_account_get_username(pa));
}

/**
 * MAM Query Handlers
 */

/**
 * chatty_mam_get_chat_prefs:
 * @pa: PurpleAccount to use for account or chat prefs
 * @room: jid string of MUC to get preference for
 * @def: default value which is returnd when not set
 *
 * The function returns either chat preference for the
 * given account, or, if @room is NULL, accounts prefs.
 * When disabled globally it would never be called.
 */
static const char *
chatty_mam_get_chat_prefs(PurpleAccount *pa, const char *room, const char *def)
{
  // Get per-room settings
  if(room && *room != '\0') {
    PurpleChat *chat = purple_blist_find_chat(pa, room);
    if(chat)
      return purple_blist_node_get_string(
                                    PURPLE_BLIST_NODE(chat), MAM_PREFS_DEF);
    return def;
  } else
    // Get per-account setting
    return purple_account_get_ui_string(pa, purple_core_get_ui (),
                                        MAM_PREFS_DEF, def);

}

/**
 * cb_mam_query_prefs:
 * @js: JabberStream of the current connection
 * @from: the jid string of the iq responder
 * @type: the type of the iq response - error or result
 * @id: the id of the iq query/response
 * @res: the xmlnode of the response
 *
 * The callback for chatty_mam_query_prefs which checks
 * server-side mam preferences and reset them if differ
 * from the client-side.
 */
static void
cb_mam_query_prefs(JabberStream *js, const char *from,
                    JabberIqType type, const char *id,
                    xmlnode *res, gpointer data)
{
  PurpleAccount *pa = purple_connection_get_account(js->gc);
  xmlnode *prefs = xmlnode_get_child_with_namespace(res, "prefs", NS_MAMv2);
  const char *to = data;

  if(type == JABBER_IQ_RESULT && prefs) {
    const char *srv_def = xmlnode_get_attrib(prefs, "default");
    const char *clt_def = chatty_mam_get_chat_prefs(pa, from, srv_def);
    if(g_strcmp0(clt_def, srv_def)) {
      JabberIq *iq = jabber_iq_new(js, JABBER_IQ_SET);
      prefs = xmlnode_new_child(iq->node, "prefs");
      xmlnode_set_namespace(prefs, NS_MAMv2);
      if(to != NULL)
        xmlnode_set_attrib(iq->node, "to", to);
      xmlnode_set_attrib(prefs, "default", clt_def);
      xmlnode_new_child(prefs,"always");
      xmlnode_new_child(prefs,"never");
      jabber_iq_send(iq);
    }
  }
}

/**
 * chatty_mam_query_prefs:
 * @pc: PurpleConnection with current JabberStream
 * @to: optional destination (if not own bare but say muc)
 *
 * Queries server-side preferences of the mam archiving node
 * to figure whether it is in sync with client-side. The iq
 * clabback is supposed to resync if it differs.
 */
static void
chatty_mam_query_prefs(PurpleConnection *pc, const char *to)
{
  JabberStream  *js = purple_connection_get_protocol_data (pc);
  JabberIq *iq = jabber_iq_new(js, JABBER_IQ_GET);
  xmlnode *prefs = xmlnode_new_child(iq->node, "prefs");
  xmlnode_set_namespace(prefs, NS_MAMv2);

  if(to != NULL)
    xmlnode_set_attrib(iq->node, "to", to);

  jabber_iq_set_callback(iq, cb_mam_query_prefs, (void*)to);

  jabber_iq_send(iq);
}

static void chatty_mam_query_archive (MAMQuery *mamq);

/**
 * cb_mam_query_result:
 * @js: JabberStream of the current connection
 * @from: jid string of the iq responder
 * @type: the type of the response - result or error
 * @id: id of the iq stanza
 * @res: the response xmlnode
 *
 * The callback for mam archive query set in chatty_mam_query_archive
 * Parses response and either finalizes it or pages through
 * remaining set.
 */
static void
cb_mam_query_result(JabberStream *js, const char *from,
                    JabberIqType type, const char *id,
                    xmlnode *res, gpointer data)
{
  xmlnode *fin = xmlnode_get_child_with_namespace(res, "fin", NS_MAMv2);
  PurpleAccount *pa = purple_connection_get_account(js->gc);
  MamCtx *mamc = chatty_mam_ctx_get(pa);
  MAMQuery *mamq = (MAMQuery*) data;

  if(type == JABBER_IQ_RESULT && fin != NULL) {
    const char *complete = xmlnode_get_attrib(fin, "complete");
    if(g_strcmp0(complete, "true")) {
      // not last page, need to continue
      xmlnode *set = xmlnode_get_child_with_namespace(fin, "set", NS_RSM);
      if(set) {
        xmlnode *last = xmlnode_get_child(set, "last");
        if(last) {
          g_free(mamq->after);
          mamq->after = xmlnode_get_data(last);
          chatty_mam_query_archive(mamq);
          return;
        }
      }
      fin = NULL; // Flag error state
    } else {
      g_debug("This is the last of them, standing down at %ld", mamc->last_ts);
      // Save ts but not for muc
      if(mamc->last_ts > 0 && mamq->to == NULL)
        purple_account_set_int(pa, "mam_last_ts", mamc->last_ts);
    }
  } else {
      fin = NULL; // Flag error state
      // FIXME: restart if it's just item-not-found
  }
  if(fin == NULL) {
    // Report error and give up
    int strl;
    char *xml = xmlnode_to_str(res, &strl);
    g_debug("Error for MAM Query: %s", xml);
    g_free(xml);
  }
  // No follow up, clean up the context
  g_hash_table_remove(mamc->qs, mamq->id);
}

/**
 * chatty_mam_query_archive:
 * @mamq: MAMQuery containing full query context
 *
 * Generates and sends iq query with mam archive query.
 * mamq structure is passed to the response callback to
 * page through RSM result set.
 * mamq is also supposed to be in the account's context
 * qs hashtable, it is freed on fin/error as part of hash
 * removal call.
 */
static void
chatty_mam_query_archive (MAMQuery *mamq)
{
  JabberIq  *iq;
  xmlnode   *mq;

  g_return_if_fail(mamq != NULL);
  g_return_if_fail(mamq->js != NULL);
  g_return_if_fail(mamq->id != NULL);

  iq = jabber_iq_new_query(mamq->js, JABBER_IQ_SET, NS_MAMv2);
  mq = xmlnode_get_child(iq->node, "query");

  xmlnode_set_attrib(mq, "queryid", mamq->id);
  if(mamq->to != NULL)
    xmlnode_set_attrib(iq->node, "to", mamq->to);
  jabber_iq_set_callback(iq, cb_mam_query_result, mamq);

  // Set search params
  if(mamq->with || mamq->start || mamq->end) {
    xmlnode *x = xmlnode_new_child(mq, "x");
    xmlnode *f = xmlnode_new_child(x, "field");
    xmlnode *v = xmlnode_new_child(f, "value");
    xmlnode_set_namespace(x, NS_DATA);
    xmlnode_set_attrib(x, "type", "submit");
    xmlnode_set_attrib(f, "type", "hidden");
    xmlnode_set_attrib(f, "var", "FORM_TYPE");
    xmlnode_insert_data(v, NS_MAMv2, -1);
    if(mamq->with) {
      f = xmlnode_new_child(x, "field");
      v = xmlnode_new_child(f, "value");
      xmlnode_set_attrib(f, "var", "with");
      xmlnode_insert_data(v, mamq->with, -1);
    }
    if(mamq->start) {
      f = xmlnode_new_child(x, "field");
      v = xmlnode_new_child(f, "value");
      xmlnode_set_attrib(f, "var", "start");
      xmlnode_insert_data(v, mamq->start, -1);
    }
    if(mamq->end) {
      f = xmlnode_new_child(x, "field");
      v = xmlnode_new_child(f, "value");
      xmlnode_set_attrib(f, "var", "end");
      xmlnode_insert_data(v, mamq->end, -1);
    }
  }

  if(mamq->before || mamq->after || (mamq->max > 0 && mamq->max < 1e9)) {
    xmlnode *rsm = xmlnode_new_child(mq, "set");
    xmlnode_set_namespace(rsm, NS_RSM);
    if(mamq->before) {
      xmlnode *v = xmlnode_new_child(rsm, "before");
      xmlnode_insert_data(v, mamq->before, -1);
    }
    if(mamq->after) {
      xmlnode *v = xmlnode_new_child(rsm, "after");
      xmlnode_insert_data(v, mamq->after, -1);
    }
    if(mamq->max > 0 && mamq->max < 1e9) {
      xmlnode *max = xmlnode_new_child(rsm, "max");
      char data[10];
      xmlnode_set_namespace(rsm, NS_RSM);
      snprintf(data, 10, "%d", mamq->max);
      xmlnode_insert_data(max, data, -1);
    }
  }

  jabber_iq_send(iq);
}

/**
 * chatty_mam_is_enabled:
 * @pa: PurpleAccount whose settings to consider
 * @room: the JID string of the MUC room to check settings
 *
 * The function aggregates various settings which may disable
 * mam processing globally, per-account and per-chat-room.
 * Returns aggregated decision on whether mam is enabled.
 */
static gboolean
chatty_mam_is_enabled(PurpleAccount *pa, const char *room)
{
  const char *pref;

  // Get global setting
  if(!chatty_settings_get_mam_enabled (chatty_settings_get_default ()))
    return FALSE;

  // Get local preference
  pref = chatty_mam_get_chat_prefs(pa, room, NULL);
  if(g_strcmp0(MAM_DEF_DISABLE, pref) == 0)
    return FALSE;

  return TRUE;
}

/**
 * cb_chatty_mam_bare_info:
 * @pc: PurpleConnection on which bare was discovered
 * @bare: the bare jid on which disco#info was queried
 * @var: the namespace/feature discovered on bare jid
 *
 * The callback for "jabber-bare-info" signal to discover whether
 * the bare jid (own jid or muc) is a mam v2 archiving entity. If
 * yes - query the archive.
 */
static void
cb_chatty_mam_bare_info(PurpleConnection *pc,
                        const char *bare,
                        const char *var)
{
  if(g_strcmp0(var, NS_MAMv2) == 0) {
    JabberStream  *js = purple_connection_get_protocol_data (pc);
    char *qid = jabber_get_next_id(js);
    GDateTime *dt = NULL;
    PurpleAccount *pa = purple_connection_get_account(pc);
    // Init CTX
    MamCtx *mamc = chatty_mam_ctx_add(pa);
    MAMQuery *mamq;

    if(mamc->ns == NULL)
      mamc->ns = g_strdup(var);

    if(!chatty_mam_is_enabled(pa, bare))
      return; // ok, if you say so

    mamq = g_new0(MAMQuery, 1);
    mamq->js = js;
    mamq->id = g_strdup(qid);
    if(g_strcmp0(bare, purple_account_get_username(pa))) {
      ChattyManager *manager = chatty_manager_get_default ();
      time_t ts;

      ts = chatty_history_get_last_message_time (chatty_manager_get_history (manager),
                                                 purple_account_get_username(pa), bare);
      // For MUC we're getting all messages so last history ts is ok
      if(ts>0)
        dt = g_date_time_new_from_unix_utc(ts);
      // This becomes indication of the foreign archive, eg MUC
      mamq->to = g_strdup(bare);
    } else
      // Get last stop point on the account
      mamc->last_ts = purple_account_get_int(pa, "mam_last_ts", 0);
    g_hash_table_insert(mamc->qs, qid, mamq);
    if(dt == NULL) {
      if(mamc->last_ts > 0 && mamq->to == NULL) {
        dt = g_date_time_new_from_unix_utc(mamc->last_ts);
      } else {
        // last week should be good enough for the start
        GDateTime *now = g_date_time_new_now_utc();
        dt = g_date_time_add_days(now, -7);
        g_date_time_unref(now);
      }
    }
    mamq->start = g_date_time_format(dt,"%FT%TZ");
    g_date_time_unref(dt);
    CHATTY_DEBUG (bare, "Server supports MAM %s; Querying by %s from %s after %s, id:",
                  var, qid, mamq->start, mamq->after);
    // Request MAM backlog
    chatty_mam_query_archive(mamq);
    // Also - request preferences and correct them if required
    chatty_mam_query_prefs(pc, mamq->to);
  }
}

/**
 * cb_chatty_mam_enabled_notify:
 * @obj: GObject which emitted notify signal
 * @param: GParamSpec which should contain mam-enabled prop
 * @null: unused
 *
 * The callback to notify::mam_enabled property of the
 * ChattySettings singleton to trigger mam sync when
 * the global setting was enabled.
 */
static void
cb_chatty_mam_enabled_notify (GObject *obj,
                              GParamSpec *param,
                              gpointer null)
{
  if(chatty_settings_get_mam_enabled((ChattySettings*)obj)) {
    // Cycle through context keys? Or better connected accounts
    GList *aacs = purple_accounts_get_all_active();
    g_debug("MAM is enabled, re-discover the bares");
    for(GList *la = aacs; la; la = la->next) {
      PurpleAccount *pa = la->data;
      MamCtx *mamc;

      // Skip non-jabber account
      if(g_strcmp0 ("prpl-jabber", purple_account_get_protocol_id (pa)))
        continue;

      mamc = chatty_mam_ctx_get(pa);

      if(!mamc || !mamc->ns) // no mam discovered here
        continue;

      // Pretend we rediscovered mam again here
      cb_chatty_mam_bare_info(
            purple_account_get_connection(pa),
            purple_account_get_username(pa),
            mamc->ns);
    }
    g_list_free(aacs);
  }
}

/**
 * cb_chatty_mam_msg_wrote:
 * @pa: PurpleAccount for the event
 * @pcm: PurpleConvMessage which has been writen to the Conversation
 * @uuid: a pointer to char* poiner which should contain message uuid
 * @type: PurpleConversationType enum with Conversation type (im or chat)
 *
 * The callback for message archive signal "conversation-write", the cb is
 * supposed to populate inflight message with body and flags, give back uuid
 * and have NO_LOG flag set to suppress archiving for incoming messages.
 *
 */
static void
cb_chatty_mam_msg_wrote(PurpleConversation *conv,
                        PurpleConvMessage *pcm,
                        char **uuid, PurpleConversationType type,
                        void *ctx)
{
  PurpleAccount *pa = conv->account;
  MamCtx *mamc = chatty_mam_ctx_get(pa);
  if(mamc == NULL)
    return;
  if(mamc->cur_oid && pcm->flags & PURPLE_MESSAGE_SEND) {
    // copy origin_id into uuid to be able to dedup outgoing messages
    *uuid = g_strdup(mamc->cur_oid);
    g_free(mamc->cur_oid);
    mamc->cur_oid = NULL;
    return;
  }

  if(mamc->cur_msg == NULL)
    return;

  // Skip non-jabber writes
  if(g_strcmp0 ("prpl-jabber", purple_account_get_protocol_id (pa)))
    return;

  mamc->cur_msg->p.what = g_strdup(pcm->what);
  // If flags are already set - enforce them, otherwise copy
  // Also always set NO_LOG flag to suppress in-app archiving
  if(mamc->cur_msg->p.flags) {
    pcm->flags = mamc->cur_msg->p.flags | PURPLE_MESSAGE_NO_LOG;
  } else {
    mamc->cur_msg->p.flags = pcm->flags;
    pcm->flags |= PURPLE_MESSAGE_NO_LOG;
  }
  mamc->cur_msg->conv = conv;
  mamc->cur_msg->type = type;
  mamc->cur_msg->p.alias = g_strdup(pcm->alias);
  mamc->cur_msg->p.when = pcm->when;
  mamc->cur_msg->p.who = g_strdup(pcm->who);
  if(mamc->cur_msg->id)
    *uuid = g_strdup(mamc->cur_msg->id);
  g_debug ("Received message on %s of type %d with flags %d",
            mamc->cur_msg->p.alias, mamc->cur_msg->type, mamc->cur_msg->p.flags);
}

static gboolean
cb_chatty_mam_msg_receiving(PurpleAccount *pa, char **who, char **msg,
                            PurpleConversation *conv, PurpleMessageFlags *flags)
{
  // This is a good chance to flip the flags early, not sure we need it though
  g_debug ("Receiving msg on %p from %s with flags %d", conv, *who, *flags);
  return FALSE;
}

/**
 * cb_chatty_mam_msg_received:
 * @pc: a PurpleConnection
 * @msg: a message xmlnode
 *
 * This function is called via the "jabber-receiving-message" signal
 * and is intended to intercept the parser (return TRUE)
 *
 */
static gboolean
cb_chatty_mam_msg_received (PurpleConnection *pc,
                            const char *type, const char *id,
                            const char *from, const char *to,
                            xmlnode *msg)
{
  xmlnode    *node_result;
  xmlnode    *node_sid;
  xmlnode    *message;
  const char *peer;
  const char *query_id;
  const char *stanza_id = NULL;
  const char *stamp = NULL;
  const char *user;
  PurpleMessageFlags flags = 0;
  JabberStream  *js = purple_connection_get_protocol_data (pc);
  PurpleAccount *pa = purple_connection_get_account (pc);
  MamCtx *mamc = chatty_mam_ctx_add(pa);
  MAMQuery *mamq = NULL;

  if (msg == NULL)
    return FALSE;

  node_result = xmlnode_get_child_with_namespace (msg, "result", NS_MAMv2);
  node_sid    = xmlnode_get_child_with_namespace (msg, "stanza-id", NS_SIDv0);

  if(mamc->cur_msg != NULL) {
    g_autofree char *str = NULL;

    str = g_strdup_printf ("%s %s", from, to);

    // Skip resubmission - break the loop
    CHATTY_DEBUG (str, "Received resubmission %s", id);
    return FALSE;
  }

  user = purple_account_get_username (pa);

  if(node_result != NULL || node_sid != NULL) {
    ChattyManager *manager = chatty_manager_get_default ();
    int dts;
    const char *msg_type;
    if(node_result != NULL) {
      xmlnode    *node_fwd;
      xmlnode    *node_delay;
      query_id = xmlnode_get_attrib (node_result, "queryid");
      stanza_id = xmlnode_get_attrib (node_result, "id");

      // Check result and query-id are valid
      if(query_id == NULL) {
        g_debug ("Malformed MAM result from %s missing queryid", from);
        return FALSE;
      }
      mamq = g_hash_table_lookup(mamc->qs, query_id);
      if(mamq == NULL) {
        // Fake result injection?
        g_debug ("Fake MAM result[%s] injection from %s", query_id, from);
        return FALSE;
      }

      node_fwd = xmlnode_get_child_with_namespace (node_result, "forwarded", NS_FWDv0);
      if(node_fwd == NULL) // this is rather unexpected, yield
        return FALSE;

      message = xmlnode_get_child (node_fwd, "message");
      if(message == NULL)
        return FALSE; // Now this is bizare

      node_delay = xmlnode_get_child (node_fwd, "delay");
      if(node_delay != NULL) {
        stamp = xmlnode_get_attrib (node_delay, "stamp");
        /* Copy delay down for the parser */
        xmlnode_insert_child (message, xmlnode_copy (node_delay));
      }
      g_debug ("Received result %s for query_id %s dated %s", stanza_id, query_id, stamp);
    } else {
      stanza_id = xmlnode_get_attrib (node_sid, "id");
      // If it's forward notification of the archive-id (SID) - we need to
      // store the SID in history at the least - to know where to start.
      message = msg;
      peer = from;
      CHATTY_DEBUG (peer, "Received forward id %s from", stanza_id);
    }
    // check history and drop the dup
    msg_type = xmlnode_get_attrib(message, "type");

    if(from && msg_type && g_strcmp0(msg_type, "groupchat") == 0) {
      dts = chatty_history_get_chat_timestamp (chatty_manager_get_history (manager),
                                               stanza_id, from);
    } else {
      dts = chatty_history_get_im_timestamp (chatty_manager_get_history (manager),
                                             stanza_id, user);
    }
    if(dts < INT_MAX) {
      g_debug ("Message id %s for acc %s is already stored on %d", stanza_id, user, dts);
      return TRUE; // note - true means stop processing
    }
    // Swap from/to for outgoing messages
    peer = xmlnode_get_attrib (message, "from");
    if(peer) {
      char *bare_peer = chatty_utils_jabber_id_strip(peer);
      if(g_strcmp0(user, bare_peer) == 0) {
        // FIXME: It could be communication between user's resources
        char *msg_to = g_strdup (xmlnode_get_attrib (message, "to"));
        xmlnode_set_attrib (message, "to", peer);
        xmlnode_set_attrib (message, "from", msg_to);
        g_free (msg_to);
        flags |= PURPLE_MESSAGE_SEND;
        peer = xmlnode_get_attrib (message, "from");
      }
      g_free (bare_peer);
    } else {
      xmlnode_set_attrib (message, "from", xmlnode_get_attrib (message, "to"));
      peer = xmlnode_get_attrib (message, "from");
      flags |= PURPLE_MESSAGE_SEND;
    }
    if(flags & PURPLE_MESSAGE_SEND) {
      // For sent messages need to attempt dedup based on origin-id
      xmlnode *node_oid = xmlnode_get_child_with_namespace (message, "origin-id", NS_SIDv0);
      if(node_oid) {
        const char *uuid = xmlnode_get_attrib (node_oid, "id");
        if(uuid) {
          dts = chatty_history_get_im_timestamp (chatty_manager_get_history (manager), uuid, user);
          if(dts < INT_MAX) {
            g_debug ("Message id %s for acc %s is already stored on %d", uuid, user, dts);
            return TRUE; // note - true means stop processing
          }
        }
      }
    }
  } else {
    // The server does not support MAM but we still need to handle history
    message = msg;
    peer = from;
  }
  CHATTY_DEBUG (peer, "Stealing parser for MAM, ID %s user:", stanza_id);
  /**
   * Before we resume message processing we need to pre-cook the message.
   * If there's no body the whole server_got_stuff is skipped, so we may never
   * see the conversation. On the other hand making full parsing with html and
   * oob here is an overkill. Let's just try to fish end-state message from
   * the parser using signals which will override our empty message.
   */
  mamc->cur_msg = g_new0(MamMsg, 1);
  mamc->cur_msg->id = (char*)stanza_id;
  mamc->cur_msg->p.who = (char*)peer;
  mamc->cur_msg->p.flags = flags;
  if(stamp)
    mamc->cur_msg->p.when = purple_str_to_time (stamp, TRUE, NULL, NULL, NULL);
  jabber_message_parse (js, message);
  if(stanza_id != NULL || mamc->cur_msg->p.what != NULL) {
    ChattyManager *manager = chatty_manager_get_default ();
    PurpleConvMessage *pcm = &(mamc->cur_msg->p);
    PurpleConversation *conv = mamc->cur_msg->conv;
    g_autoptr(ChattyMessage) chat_message = NULL;
    g_autofree char *who = NULL;

    if (!conv)
      conv = purple_find_conversation_with_account (PURPLE_CONV_TYPE_ANY,
                                                    pcm->alias ? pcm->alias : pcm->who,
                                                    pc->account);

    /* conv shall be missing only for IM chats */
    if (!conv && !pcm->alias)
        conv = purple_conversation_new (PURPLE_CONV_TYPE_IM,
                                        pc->account, pcm->who);
    if (pcm->who) {
      if (chatty_chat_is_im (conv->ui_data) ||
          !g_str_has_prefix (pcm->who, conv->name))
        who = chatty_utils_jabber_id_strip (pcm->who);
    }

    if (!stanza_id)
      stanza_id = g_uuid_string_random ();

    {
      g_autoptr(ChattyContact) contact = NULL;

      contact = g_object_new (CHATTY_TYPE_CONTACT, NULL);
      chatty_contact_set_name (contact, who);
      chatty_contact_set_value (contact, who);
      chat_message = chatty_message_new (CHATTY_ITEM (contact), pcm->what, stanza_id,
                                         pcm->when, CHATTY_MESSAGE_HTML_ESCAPED,
                                         chatty_pp_utils_direction_from_flag (pcm->flags), 0);
    }

    if (conv)
      chatty_history_add_message (chatty_manager_get_history (manager),
                                  conv->ui_data, chat_message);
    else
      g_warning ("NULL conversation for : who: %s, message: %s",
                  who ? who: pcm->who, pcm->what);

  }
  // Update last timestamp for account's archive
  if(mamq != NULL && mamq->to == NULL)
    mamc->last_ts = mamc->cur_msg->p.when;

  // Clear resubmission state
  if(peer == mamc->cur_msg->p.who)
    mamc->cur_msg->p.who = NULL;
  mamc->cur_msg->id = NULL;
  mamm_free(mamc->cur_msg);
  mamc->cur_msg = NULL;
  // Stop processing, we have done that already
  return TRUE;
}

/**
 * cb_chatty_mam_xmlnode_send:
 * @pc: a PurpleConnection
 * @packet: a xmlnode
 *
 * This function is called via the
 * "jabber-sending-xmlnode" signal
 *
 */
static void
cb_chatty_mam_xmlnode_send (PurpleConnection  *pc,
                            xmlnode          **packet,
                            gpointer           null)
{
  xmlnode    *node_body;
  xmlnode    *node_id;

  if (*packet && (*packet)->name && g_strcmp0((*packet)->name, "message") == 0) {
    node_body = xmlnode_get_child (*packet, "body");

    if (node_body) {
      MamCtx *mamc = chatty_mam_ctx_get(purple_connection_get_account(pc));
      if(mamc == NULL)
        return;
      node_id = xmlnode_new_child (*packet, "origin-id");
      xmlnode_set_namespace (node_id, NS_SIDv0);
      g_free(mamc->cur_oid);
      mamc->cur_oid = g_uuid_string_random ();
      xmlnode_set_attrib(node_id, "id", mamc->cur_oid);

      g_debug ("Set origin-id %s for outgoing message", mamc->cur_oid);
    }
  }
}

/**
 * cb_chatty_mam_disconnect:
 *
 * Clean up (remove/destroy) account's context.
 * Callback for connection close "signed-off" signal.
 */
static void
cb_chatty_mam_disconnect(PurpleConnection *pc, gpointer ptr)
{
  chatty_mam_ctx_del(purple_connection_get_account(pc));
}

/**
 * chatty_mam_close:
 *
 * Clean up context hashtable, the rest (signal cleanup)
 * is already done in xeps close.
 */
void
chatty_0313_close (void)
{
  g_hash_table_destroy (ht_mam_ctx);
}


/**
 * chatty_mam_init:
 *
 * Sets purple functions interception at appropriate signals
 * and initializes hashtable for per-account MAM context.
 *
 */
void
chatty_0313_init (void)
{
  PurplePlugin *jabber = chatty_xeps_get_jabber ();
  void *handle = chatty_xeps_get_handle ();

  // Initialize per-account context storage
  ht_mam_ctx = g_hash_table_new_full (g_str_hash,
                                       g_str_equal,
                                       g_free,
                                       mamc_free);

  // Perform MAM query if NS_MAMv2 is discovered
  purple_signal_connect(jabber,
                        "jabber-bare-info",
                        handle,
                        PURPLE_CALLBACK(cb_chatty_mam_bare_info),
                        NULL);

  // Intercept message send, inject origin-id
  purple_signal_connect(jabber,
                        "jabber-sending-xmlnode",
                        handle,
                        PURPLE_CALLBACK(cb_chatty_mam_xmlnode_send),
                        NULL);

  // Intercept message parsing and replace with own version
  purple_signal_connect(jabber,
                        "jabber-receiving-message",
                        handle,
                        PURPLE_CALLBACK(cb_chatty_mam_msg_received),
                        NULL);

  // Well, nothing really, just for debug/future use maybe
  purple_signal_connect(purple_conversations_get_handle(),
                        "receiving-im-msg",
                        handle,
                        PURPLE_CALLBACK(cb_chatty_mam_msg_receiving),
                        NULL);

  // Intercept history archiving and do it my way
  purple_signal_connect(chatty_manager_get_default (),
                        "conversation-write",
                        handle,
                        PURPLE_CALLBACK(cb_chatty_mam_msg_wrote),
                        NULL);

  // Clean up the context
  purple_signal_connect(purple_connections_get_handle(),
                        "signed-off",
                        handle,
                        PURPLE_CALLBACK(cb_chatty_mam_disconnect),
                        NULL);

  // Follow the settings
  g_signal_connect (chatty_settings_get_default(),
                    "notify::mam-enabled",
                    G_CALLBACK(cb_chatty_mam_enabled_notify),
                    NULL);
}
/* vim: set sts=2 et: */
