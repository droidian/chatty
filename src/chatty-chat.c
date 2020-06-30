/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-chat.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-chat"

#include <purple.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "contrib/gtk.h"
#include "chatty-settings.h"
#include "chatty-icons.h"
#include "chatty-utils.h"
#include "users/chatty-pp-buddy.h"
#include "users/chatty-pp-account.h"
#include "chatty-chat.h"

#define CHATTY_COLOR_BLUE "4A8FD9"

enum {
  LURCH_STATUS_DISABLED = 0,  /* manually disabled */
  LURCH_STATUS_NOT_SUPPORTED, /* no OMEMO support, i.e. there is no devicelist node */
  LURCH_STATUS_NO_SESSION,    /* OMEMO is supported, but there is no libsignal session yet */
  LURCH_STATUS_OK             /* OMEMO is supported and session exists */
};

/**
 * SECTION: chatty-chat
 * @title: ChattyChat
 * @short_description: An abstraction over #PurpleConversation
 * @include: "chatty-chat.h"
 *
 * libpurple doesn’t have a nice OOP interface for managing anything.
 * This class hides all the complexities surrounding it.
 */

struct _ChattyChat
{
  ChattyItem          parent_instance;

  PurpleAccount      *account;
  PurpleBuddy        *buddy;

  PurpleChat         *pp_chat;
  PurpleConversation *conv;
  GListStore         *chat_users;
  GtkSortListModel   *sorted_chat_users;
  GListStore         *message_store;

  char               *last_message;
  char               *chat_name;
  guint               unread_count;
  guint               last_msg_time;
  e_msg_dir           last_msg_direction;
  ChattyEncryption    encrypt;
};

G_DEFINE_TYPE (ChattyChat, chatty_chat, CHATTY_TYPE_ITEM)

enum {
  PROP_0,
  PROP_ENCRYPT,
  PROP_PURPLE_CHAT,
  N_PROPS
};

enum {
  CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];
static GParamSpec *properties[N_PROPS];

static char *
jabber_id_strip_resource (const char *name)
{
  g_auto(GStrv) split = NULL;
  char *stripped;

  split = g_strsplit (name, "/", -1);
  stripped = g_strdup (split[0]);

  return stripped;
}

static gboolean
chatty_chat_has_encryption_support (ChattyChat *self)
{
  PurpleConversationType type = PURPLE_CONV_TYPE_UNKNOWN;

  g_assert (CHATTY_IS_CHAT (self));

  if (self->conv)
    type = purple_conversation_get_type (self->conv);

  /* Currently we support only XMPP IM chats */
  if (self->conv &&
      type == PURPLE_CONV_TYPE_IM &&
      chatty_item_get_protocols (CHATTY_ITEM (self)) == CHATTY_PROTOCOL_XMPP)
    return TRUE;

  return FALSE;
}

static gint
sort_chat_buddy (ChattyPpBuddy *a,
                 ChattyPpBuddy *b)
{
  ChattyUserFlag flag_a, flag_b;

  flag_a = chatty_pp_buddy_get_flags (a);
  flag_b = chatty_pp_buddy_get_flags (b);

  flag_a = flag_a & (CHATTY_USER_FLAG_MEMBER | CHATTY_USER_FLAG_MODERATOR | CHATTY_USER_FLAG_OWNER);
  flag_b = flag_b & (CHATTY_USER_FLAG_MEMBER | CHATTY_USER_FLAG_MODERATOR | CHATTY_USER_FLAG_OWNER);

  if (flag_a == flag_b)
    return chatty_item_compare (CHATTY_ITEM (a), CHATTY_ITEM (b));

  if (flag_a > flag_b)
    return -1;

  /* @a should be after @b */
  return 1;
}

static void
chatty_chat_lurch_changed_cb (int      err,
                              gpointer user_data)
{
  g_autoptr(ChattyChat) self = user_data;

  g_assert (CHATTY_IS_CHAT (self));

  if (err) {
    g_warning ("Failed to change OMEMO encryption.");
    return;
  }

  chatty_chat_load_encryption_status (self);
}

static void
lurch_status_changed_cb (int      err,
                         int      status,
                         gpointer user_data)
{
  g_autoptr(ChattyChat) self = user_data;

  g_assert (CHATTY_IS_CHAT (self));

  if (err) {
    g_debug ("Failed to get the OMEMO status.");
    return;
  }

  switch (status) {
  case LURCH_STATUS_OK:
    self->encrypt = CHATTY_ENCRYPTION_ENABLED;
    break;

  case LURCH_STATUS_DISABLED:
  case LURCH_STATUS_NO_SESSION:
    self->encrypt = CHATTY_ENCRYPTION_DISABLED;
    break;

  default:
    self->encrypt = CHATTY_ENCRYPTION_UNSUPPORTED;
    break;
  }

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENCRYPT]);
}

static ChattyPpBuddy *
chat_find_user (ChattyChat *self,
                const char *user,
                guint      *index)
{
  guint n_items;

  g_assert (CHATTY_IS_CHAT (self));

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->chat_users));
  for (guint i = 0; i < n_items; i++) {
    g_autoptr(ChattyPpBuddy) buddy = NULL;

    buddy = g_list_model_get_item (G_LIST_MODEL (self->chat_users), i);
    if (chatty_pp_buddy_get_id (buddy) == user) {
      if (index)
        *index = i;

      return buddy;
    }
  }

  return NULL;
}

static void
emit_avatar_changed (ChattyChat *self)
{
  g_assert (CHATTY_IS_CHAT (self));

  g_signal_emit_by_name (self, "avatar-changed");
}

static const char *
chatty_chat_get_name (ChattyItem *item)
{
  ChattyChat *self = (ChattyChat *)item;
  const char *name = NULL;

  g_assert (CHATTY_IS_CHAT (self));

  if (self->pp_chat)
    name = purple_chat_get_name (self->pp_chat);
  else if (self->buddy)
    name = purple_buddy_get_alias_only (self->buddy);

  if (name)
    return name;

  /* If we have a cached name, return that */
  if (self->chat_name)
    return self->chat_name;

  if (self->buddy) {
    const char *name_end;

    name = purple_buddy_get_name (self->buddy);
    name_end = strchr (name, '/');

    /* Strip ‘/’ and following string from the username, if found */
    if (name_end)
      self->chat_name = g_strndup (name, name_end - name);
    else
      self->chat_name = g_strdup (name);

    return self->chat_name;
  }

  if (self->conv)
    name = purple_conversation_get_title (self->conv);

  if (!name)
    name = "Invalid user";

  return name;
}


static ChattyProtocol
chatty_chat_get_protocols (ChattyItem *item)
{
  ChattyChat *self = (ChattyChat *)item;
  PurpleAccount *pp_account = NULL;
  ChattyPpAccount *account = NULL;

  g_assert (CHATTY_IS_CHAT (self));

  if (self->buddy)
    pp_account = self->buddy->account;
  else if (self->pp_chat)
    pp_account = self->pp_chat->account;
  else if (self->conv)
    pp_account = self->conv->account;
  else
    return CHATTY_PROTOCOL_ANY;

  account = chatty_pp_account_get_object (pp_account);

  return chatty_item_get_protocols (CHATTY_ITEM (account));
}


static GdkPixbuf *
chatty_chat_get_avatar (ChattyItem *item)
{
  ChattyChat *self = (ChattyChat *)item;

  g_assert (CHATTY_IS_CHAT (self));

  if (self->buddy) {
    ChattyPpBuddy *buddy;

    buddy = chatty_pp_buddy_get_object (self->buddy);

    if (buddy)
      return chatty_item_get_avatar (CHATTY_ITEM (buddy));

    return NULL;
  }

  if (self->pp_chat)
    return chatty_icon_get_buddy_icon ((PurpleBlistNode *)self->pp_chat,
                                       NULL,
                                       CHATTY_ICON_SIZE_MEDIUM,
                                       CHATTY_COLOR_BLUE,
                                       FALSE);

  return NULL;
}

static void
chatty_chat_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  ChattyChat *self = (ChattyChat *)object;

  switch (prop_id)
    {
    case PROP_ENCRYPT:
      g_value_set_boolean (value, self->encrypt == CHATTY_ENCRYPTION_ENABLED);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
chatty_chat_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  ChattyChat *self = (ChattyChat *)object;

  switch (prop_id)
    {
    case PROP_ENCRYPT:
      chatty_chat_set_encryption (self, g_value_get_boolean (value));
      break;

    case PROP_PURPLE_CHAT:
      self->pp_chat = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}


static void
chatty_chat_finalize (GObject *object)
{
  ChattyChat *self = (ChattyChat *)object;

  g_list_store_remove_all (self->chat_users);
  g_list_store_remove_all (self->message_store);
  g_object_unref (self->message_store);
  g_object_unref (self->chat_users);
  g_object_unref (self->sorted_chat_users);
  g_free (self->last_message);
  g_free (self->chat_name);

  G_OBJECT_CLASS (chatty_chat_parent_class)->finalize (object);
}


static void
chatty_chat_class_init (ChattyChatClass *klass)
{
  GObjectClass *object_class  = G_OBJECT_CLASS (klass);
  ChattyItemClass *item_class = CHATTY_ITEM_CLASS (klass);

  object_class->get_property = chatty_chat_get_property;
  object_class->set_property = chatty_chat_set_property;
  object_class->finalize = chatty_chat_finalize;

  item_class->get_name = chatty_chat_get_name;
  item_class->get_protocols = chatty_chat_get_protocols;
  item_class->get_avatar = chatty_chat_get_avatar;

  properties[PROP_ENCRYPT] =
    g_param_spec_boolean ("encrypt",
                          "Encrypt",
                          "Whether the chat is encrypted or not",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  properties[PROP_PURPLE_CHAT] =
    g_param_spec_pointer ("purple-chat",
                          "Purple Chat",
                          "The PurpleChat to be used to create the object",
                          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * ChattyChat::changed:
   * @self: a #ChattyChat
   *
   * Emitted when chat changes
   */
  signals [CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}


static void
chatty_chat_init (ChattyChat *self)
{
  g_autoptr(GtkSorter) sorter = NULL;

  sorter = gtk_custom_sorter_new ((GCompareDataFunc)sort_chat_buddy, NULL, NULL);
  self->chat_users = g_list_store_new (CHATTY_TYPE_PP_BUDDY);
  self->sorted_chat_users = gtk_sort_list_model_new (G_LIST_MODEL (self->chat_users), sorter);

  self->message_store = g_list_store_new (CHATTY_TYPE_MESSAGE);
}


ChattyChat *
chatty_chat_new_im_chat (PurpleAccount *account,
                         PurpleBuddy   *buddy)
{
  ChattyChat *self;

  g_return_val_if_fail (account, NULL);
  g_return_val_if_fail (buddy, NULL);

  self = g_object_new (CHATTY_TYPE_CHAT, NULL);
  self->account = account;
  self->buddy = buddy;

  if (self->buddy) {
    ChattyPpBuddy *pp_buddy;

    pp_buddy = chatty_pp_buddy_get_object (self->buddy);

    if (pp_buddy)
      g_signal_connect_object (pp_buddy, "avatar-changed",
                               G_CALLBACK (emit_avatar_changed),
                               self,
                               G_CONNECT_SWAPPED);
  }

  return self;
}


ChattyChat *
chatty_chat_new_purple_chat (PurpleChat *pp_chat)
{
  return g_object_new (CHATTY_TYPE_CHAT,
                       "purple-chat", pp_chat,
                       NULL);
}


ChattyChat *
chatty_chat_new_purple_conv (PurpleConversation *conv)
{
  ChattyChat *self;
  PurpleBlistNode *node;

  self = g_object_new (CHATTY_TYPE_CHAT, NULL);
  self->conv = conv;

  node = chatty_utils_get_conv_blist_node (conv);

  if (node && PURPLE_BLIST_NODE_IS_CHAT (node))
    self->pp_chat = PURPLE_CHAT (node);
  else if (node && PURPLE_BLIST_NODE_IS_BUDDY (node))
    self->buddy = PURPLE_BUDDY (node);

  if (self->buddy) {
    ChattyPpBuddy *buddy;

    buddy = chatty_pp_buddy_get_object (self->buddy);

    if (buddy)
      g_signal_connect_object (buddy, "avatar-changed",
                               G_CALLBACK (emit_avatar_changed),
                               self,
                               G_CONNECT_SWAPPED);
  }

  return self;
}


void
chatty_chat_set_purple_conv (ChattyChat         *self,
                             PurpleConversation *conv)
{
  PurpleBlistNode *node;

  g_return_if_fail (CHATTY_IS_CHAT (self));

  self->conv = conv;

  if (self->pp_chat || self->buddy)
    return;

  if (!conv)
    return;

  node = chatty_utils_get_conv_blist_node (conv);

  if (node && PURPLE_BLIST_NODE_IS_CHAT (node))
    self->pp_chat = PURPLE_CHAT (node);
  else if (node && PURPLE_BLIST_NODE_IS_BUDDY (node))
    self->buddy = PURPLE_BUDDY (node);

  if (self->buddy) {
    ChattyPpBuddy *buddy;

    buddy = chatty_pp_buddy_get_object (self->buddy);

    if (buddy)
      g_signal_connect_object (buddy, "avatar-changed",
                               G_CALLBACK (emit_avatar_changed),
                               self,
                               G_CONNECT_SWAPPED);
  }
}

ChattyProtocol
chatty_chat_get_protocol (ChattyChat *self)
{
  ChattyPpAccount *account;
  PurpleAccount *pp_account;

  g_return_val_if_fail (CHATTY_IS_CHAT (self), CHATTY_PROTOCOL_NONE);

  if (self->account)
    pp_account = self->account;
  else if (self->conv)
    pp_account = self->conv->account;
  else if (self->pp_chat)
    pp_account = self->pp_chat->account;
  else
    return CHATTY_PROTOCOL_NONE;

  account = chatty_pp_account_get_object (pp_account);

  if (account)
    return chatty_item_get_protocols (CHATTY_ITEM (account));

  return CHATTY_PROTOCOL_NONE;
}

PurpleChat *
chatty_chat_get_purple_chat (ChattyChat *self)
{
  g_return_val_if_fail (CHATTY_IS_CHAT (self), NULL);

  return self->pp_chat;
}


PurpleBuddy *
chatty_chat_get_purple_buddy (ChattyChat *self)
{
  g_return_val_if_fail (CHATTY_IS_CHAT (self), NULL);

  return self->buddy;
}


PurpleConversation *
chatty_chat_get_purple_conv (ChattyChat *self)
{
  g_return_val_if_fail (CHATTY_IS_CHAT (self), NULL);

  return self->conv;
}

const char *
chatty_chat_get_username (ChattyChat *self)
{
  g_return_val_if_fail (CHATTY_IS_CHAT (self), "");

  if (self->pp_chat)
    return purple_account_get_username (self->pp_chat->account);

  if (self->buddy)
    return purple_account_get_username (self->buddy->account);

  if (self->conv)
    return purple_account_get_username (self->conv->account);

  return "";
}

gboolean
chatty_chat_are_same (ChattyChat *a,
                      ChattyChat *b)
{
  g_return_val_if_fail (CHATTY_IS_CHAT (a), FALSE);
  g_return_val_if_fail (CHATTY_IS_CHAT (b), FALSE);

  if (a == b)
    return TRUE;

  if (a->account && a->buddy &&
      a->account == b->account &&
      a->buddy == b->buddy)
    return TRUE;

  if (a->conv && a->conv == b->conv)
    return TRUE;

  if (a->pp_chat && a->pp_chat == b->pp_chat)
    return TRUE;

  if (a->conv &&
      chatty_chat_match_purple_conv (b, a->conv))
    return TRUE;

  if (b->conv &&
      chatty_chat_match_purple_conv (b, b->conv))
    return TRUE;

  return FALSE;
}

gboolean
chatty_chat_match_purple_conv (ChattyChat         *self,
                               PurpleConversation *conv)
{
  gpointer node;

  g_return_val_if_fail (CHATTY_IS_CHAT (self), FALSE);
  g_return_val_if_fail (conv, FALSE);

  if (self->conv && conv == self->conv)
    return TRUE;

  if (self->account && self->account != conv->account)
    return FALSE;

  node = chatty_utils_get_conv_blist_node (conv);

  if (!node)
    return FALSE;

  if (node == self->pp_chat ||
      node == self->buddy) {
    self->conv = conv;

    return TRUE;
  }

  return FALSE;
}

GListModel *
chatty_chat_get_messages (ChattyChat *self)
{
  g_return_val_if_fail (CHATTY_IS_CHAT (self), NULL);

  return G_LIST_MODEL (self->message_store);
}


ChattyMessage *
chatty_chat_find_message_with_id (ChattyChat *self,
                                  const char *id)
{
  guint n_items;

  g_return_val_if_fail (CHATTY_IS_CHAT (self), NULL);
  g_return_val_if_fail (id, NULL);

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->message_store));

  if (n_items == 0)
    return NULL;

  /* Search from end, the item is more likely to be at the end */
  for (guint i = n_items; i > 0; i--) {
    g_autoptr(ChattyMessage) message = NULL;
    const char *message_id;

    message = g_list_model_get_item (G_LIST_MODEL (self->message_store), i - 1);
    message_id = chatty_message_get_id (message);

    /*
     * Once we have a message with no id, all preceding items shall likely
     * have loaded from database, and thus no id, so don’t bother searching.
     */
    if (!message_id)
      break;

    if (g_str_equal (id, message_id))
      return message;
  }

  return NULL;
}

void
chatty_chat_append_message (ChattyChat    *self,
                            ChattyMessage *message)
{
  g_return_if_fail (CHATTY_IS_CHAT (self));
  g_return_if_fail (CHATTY_IS_MESSAGE (message));

  g_list_store_append (self->message_store, message);
  g_signal_emit (self, signals[CHANGED], 0);
}

void
chatty_chat_prepend_message (ChattyChat    *self,
                             ChattyMessage *message)
{
  g_return_if_fail (CHATTY_IS_CHAT (self));
  g_return_if_fail (CHATTY_IS_MESSAGE (message));

  g_list_store_insert (self->message_store, 0, message);
  g_signal_emit (self, signals[CHANGED], 0);
}

/**
 * chatty_chat_add_users:
 * @self: a #ChattyChat
 * @users: A #GList of added users
 *
 * Add a #GList of #PurpleConvChatBuddy users to
 * @self.  This function only adds the items to
 * the internal list model, so that it can be
 * used to create widgets.
 */
void
chatty_chat_add_users (ChattyChat *self,
                       GList      *users)
{
  ChattyPpBuddy *buddy;
  GPtrArray *users_array;

  g_return_if_fail (CHATTY_IS_CHAT (self));

  users_array = g_ptr_array_new_with_free_func (g_object_unref);

  for (GList *node = users; node; node = node->next) {
    buddy = g_object_new (CHATTY_TYPE_PP_BUDDY,
                          "chat-buddy", node->data, NULL);
    chatty_pp_buddy_set_chat (buddy, self->conv);
    g_ptr_array_add (users_array, buddy);
  }

  g_list_store_splice (self->chat_users, 0, 0,
                       users_array->pdata, users_array->len);

  g_ptr_array_free (users_array, TRUE);
}


/**
 * chatty_chat_remove_users:
 * @self: a #ChattyChat
 * @users: A #GList of removed users
 *
 * Remove a #GList of `const char*` users to
 * @self.  This function only remove the items
 * the internal list model, so that it can be
 * used to create widgets.
 */
void
chatty_chat_remove_user (ChattyChat *self,
                         const char *user)
{
  PurpleConvChatBuddy *cb = NULL;
  PurpleConvChat *chat;
  ChattyPpBuddy *buddy = NULL;
  guint index;

  g_return_if_fail (CHATTY_IS_CHAT (self));

  chat  = purple_conversation_get_chat_data (self->conv);

  if (chat)
    cb = purple_conv_chat_cb_find (chat, user);

  if (cb)
    buddy = chat_find_user (self, cb->name, &index);

  if (buddy)
    g_list_store_remove (self->chat_users, index);
}

GListModel *
chatty_chat_get_users (ChattyChat *self)
{
  g_return_val_if_fail (CHATTY_IS_CHAT (self), NULL);

  return G_LIST_MODEL (self->sorted_chat_users);
}


ChattyPpBuddy *
chatty_chat_find_user (ChattyChat *self,
                       const char *username)
{
  g_return_val_if_fail (CHATTY_IS_CHAT (self), NULL);
  g_return_val_if_fail (username, NULL);

  return chat_find_user (self, username, NULL);
}


void
chatty_chat_emit_user_changed (ChattyChat *self,
                               const char *user)
{
  ChattyPpBuddy *buddy;

  g_return_if_fail (CHATTY_IS_CHAT (self));
  g_return_if_fail (user);

  buddy = chat_find_user (self, user, NULL);

  if (buddy)
    g_signal_emit_by_name (buddy, "changed");
}


const char *
chatty_chat_get_last_message (ChattyChat *self)
{
  g_autoptr(ChattyMessage) message = NULL;
  GListModel *model;
  guint n_items;

  g_return_val_if_fail (CHATTY_IS_CHAT (self), "");

  model = G_LIST_MODEL (self->message_store);
  n_items = g_list_model_get_n_items (model);

  if (n_items == 0)
    return "";

  message = g_list_model_get_item (model, n_items - 1);

  return chatty_message_get_text (message);
}

guint
chatty_chat_get_unread_count (ChattyChat *self)
{
  g_return_val_if_fail (CHATTY_IS_CHAT (self), 0);

  return self->unread_count;
}

void
chatty_chat_set_unread_count (ChattyChat *self,
                              guint       unread_count)
{
  g_return_if_fail (CHATTY_IS_CHAT (self));

  if (self->unread_count == unread_count)
    return;

  self->unread_count = unread_count;
  g_signal_emit (self, signals[CHANGED], 0);
}

time_t
chatty_chat_get_last_msg_time (ChattyChat *self)
{
  g_autoptr(ChattyMessage) message = NULL;
  GListModel *model;
  guint n_items;

  g_return_val_if_fail (CHATTY_IS_CHAT (self), 0);

  model = G_LIST_MODEL (self->message_store);
  n_items = g_list_model_get_n_items (model);

  if (n_items == 0)
    return 0;

  message = g_list_model_get_item (model, n_items - 1);

  return chatty_message_get_time (message);
}

ChattyEncryption
chatty_chat_get_encryption_status (ChattyChat *self)
{
  g_return_val_if_fail (CHATTY_IS_CHAT (self), FALSE);

  return self->encrypt;
}

/**
 * chatty_chat_load_encryption_status:
 * @self: A #ChattyChat
 *
 * Load encryption status of the chat @self.
 * Once the status is loaded, notify::encrypt
 * is emitted.
 *
 * Currently only XMPP IM conversations are supported.
 * Otherwise, the function simply returns.
 */
void
chatty_chat_load_encryption_status (ChattyChat *self)
{
  PurpleAccount   *pp_account;
  const char      *name;
  g_autofree char *stripped = NULL;

  g_return_if_fail (CHATTY_IS_CHAT (self));

  if (!chatty_chat_has_encryption_support (self))
    return;

  name = purple_conversation_get_name (self->conv);
  pp_account = purple_conversation_get_account (self->conv);
  stripped = jabber_id_strip_resource (name);

  purple_signal_emit (purple_plugins_get_handle(),
                      "lurch-status-im",
                      pp_account,
                      stripped,
                      lurch_status_changed_cb,
                      g_object_ref (self));

}

void
chatty_chat_set_encryption (ChattyChat *self,
                            gboolean    enable)
{
  PurpleAccount   *pp_account;
  const char      *name;
  g_autofree char *stripped = NULL;

  if (!chatty_chat_has_encryption_support (self)) {
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENCRYPT]);

    return;
  }

  name = purple_conversation_get_name (self->conv);
  pp_account = purple_conversation_get_account (self->conv);
  stripped = jabber_id_strip_resource (name);

  purple_signal_emit (purple_plugins_get_handle (),
                      enable ? "lurch-enable-im" : "lurch-disable-im",
                      pp_account,
                      stripped,
                      chatty_chat_lurch_changed_cb,
                      g_object_ref (self));
}
