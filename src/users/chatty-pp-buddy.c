/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-pp-buddy.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-pp-buddy"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define _GNU_SOURCE
#include <string.h>
#include <purple.h>
#include <glib/gi18n.h>

#include "chatty-settings.h"
#include "chatty-account.h"
#include "chatty-pp-account.h"
#include "chatty-window.h"
#include "chatty-pp-buddy.h"

/**
 * SECTION: chatty-pp-buddy
 * @title: ChattyPpBuddy
 * @short_description: An abstraction over #PurpleBuddy
 * @include: "chatty-pp-buddy.h"
 */

struct _ChattyPpBuddy
{
  ChattyItem         parent_instance;

  char              *username;
  char              *name;
  PurpleAccount     *pp_account;
  PurpleBuddy       *pp_buddy;
  ChattyContact     *contact;
  PurpleConversation *conv;

  PurpleConvChatBuddy *chat_buddy;

  gpointer          *avatar_data; /* purple icon data */
  PurpleStoredImage *pp_avatar;
  GdkPixbuf         *avatar;
  ChattyProtocol     protocol;
};

G_DEFINE_TYPE (ChattyPpBuddy, chatty_pp_buddy, CHATTY_TYPE_ITEM)

enum {
  PROP_0,
  PROP_PURPLE_ACCOUNT,
  PROP_PURPLE_BUDDY,
  PROP_CHAT_BUDDY,
  PROP_USERNAME,
  N_PROPS
};

enum {
  CHANGED,
  N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

static void
chatty_pp_buddy_update_protocol (ChattyPpBuddy *self)
{
  PurpleAccount *pp_account;
  ChattyItem *item;

  g_assert (CHATTY_IS_PP_BUDDY (self));
  g_assert (self->pp_buddy);

  pp_account = purple_buddy_get_account (self->pp_buddy);
  item = pp_account->ui_data;
  g_return_if_fail (item);

  self->protocol = chatty_item_get_protocols (item);
}


static GdkPixbuf *
chatty_icon_from_data (const guchar *buf,
                       gsize         size)
{
  g_autoptr(GdkPixbufLoader) loader = NULL;
  g_autoptr(GError) error = NULL;
  GdkPixbuf *pixbuf = NULL;

  loader = gdk_pixbuf_loader_new ();
  gdk_pixbuf_loader_write (loader, buf, size, &error);

  if (!error)
    gdk_pixbuf_loader_close (loader, &error);

  if (error)
    g_warning ("Error: %s: %s", __func__, error->message);
  else
    pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);

  if (!pixbuf)
    g_warning ("%s: pixbuf creation failed", __func__);

  return g_object_ref (pixbuf);
}


/* copied and modified from chatty_blist_add_buddy */
static void
chatty_add_new_buddy (ChattyPpBuddy *self)
{
  PurpleConversation *conv;

  g_assert (CHATTY_IS_PP_BUDDY (self));

  /* buddy should be NULL and account should be non-NULL */
  g_return_if_fail (self->pp_account);

  purple_blist_add_buddy (self->pp_buddy, NULL, NULL, NULL);

  g_debug ("%s: %s ", __func__, purple_buddy_get_name (self->pp_buddy));

  conv = purple_find_conversation_with_account (PURPLE_CONV_TYPE_IM,
                                                self->username,
                                                self->pp_account);

  if (conv != NULL)
    {
      PurpleBuddyIcon *icon;

      icon = purple_conv_im_get_icon (PURPLE_CONV_IM (conv));

      if (icon != NULL)
        purple_buddy_icon_update (icon);
    }
}

static ChattyProtocol
chatty_pp_buddy_get_protocols (ChattyItem *item)
{
  ChattyPpBuddy *self = (ChattyPpBuddy *)item;

  g_assert (CHATTY_IS_PP_BUDDY (self));

  if (self->protocol != CHATTY_PROTOCOL_NONE)
    return self->protocol;

  return CHATTY_ITEM_CLASS (chatty_pp_buddy_parent_class)->get_protocols (item);
}

static gboolean
chatty_pp_buddy_matches (ChattyItem     *item,
                         const char     *needle,
                         ChattyProtocol  protocols,
                         gboolean        match_name)
{
  ChattyPpBuddy *self = (ChattyPpBuddy *)item;
  const char *item_id = NULL;

  if (self->chat_buddy && needle == self->chat_buddy->name)
    return TRUE;

  if (self->pp_buddy)
    item_id = purple_buddy_get_name (self->pp_buddy);
  else if (self->chat_buddy)
    item_id = self->chat_buddy->name;
  else
    return FALSE;

  return strcasestr (item_id, needle) != NULL;
}

static const char *
chatty_pp_buddy_get_name (ChattyItem *item)
{
  ChattyPpBuddy *self = (ChattyPpBuddy *)item;

  g_assert (CHATTY_IS_PP_BUDDY (self));

  if (self->contact)
    return chatty_item_get_name (CHATTY_ITEM (self->contact));

  if (self->chat_buddy) {
    const char *name;

    name = self->chat_buddy->alias;

    if (!name)
      name = self->chat_buddy->name;

    if (name)
      return name;
  }

  if (!self->pp_buddy && self->name)
    return self->name;
  else if (!self->pp_buddy)
    return "";

  return purple_buddy_get_alias (self->pp_buddy);
}

static void
chatty_pp_buddy_set_name (ChattyItem *item,
                          const char *name)
{
  ChattyPpBuddy *self = (ChattyPpBuddy *)item;
  PurpleContact *contact;

  g_assert (CHATTY_IS_PP_BUDDY (self));

  g_free (self->name);

  if (!name || !*name)
    self->name = NULL;
  else
    self->name = g_strdup (name);

  if (!self->pp_buddy)
    return;

  contact = purple_buddy_get_contact (self->pp_buddy);
  purple_blist_alias_contact (contact, self->name);
}

static const char *
chatty_pp_buddy_get_username (ChattyItem *item)
{
  ChattyPpBuddy *self = (ChattyPpBuddy *)item;
  const char *name;

  g_assert (CHATTY_IS_PP_BUDDY (self));

  if (self->contact)
    return chatty_item_get_username (CHATTY_ITEM (self->contact));

  if (self->chat_buddy && self->chat_buddy->name)
    return self->chat_buddy->name;

  if (!self->pp_buddy)
    return "";

  name = purple_buddy_get_name (self->pp_buddy);

  if (!name)
    name = "";

  return name;
}

static gboolean
load_icon (gpointer user_data)
{
  ChattyPpBuddy *self = user_data;
  PurpleContact *contact;
  PurpleBuddy *buddy = NULL;
  PurpleStoredImage *img = NULL;
  PurpleBuddyIcon *icon = NULL;
  gconstpointer data = NULL;
  size_t len;

  if (self->chat_buddy) {
    PurpleConnection         *gc;
    PurplePluginProtocolInfo *prpl_info;
    char                     *who;
    int                       chat_id;

    g_return_val_if_fail (self->conv, G_SOURCE_REMOVE);

    gc = purple_conversation_get_gc (self->conv);

    if (!gc || !(prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO (gc->prpl)))
      return G_SOURCE_REMOVE;

    prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO (gc->prpl);
    if (!prpl_info || !prpl_info->get_cb_real_name)
      return G_SOURCE_REMOVE;

    chat_id = purple_conv_chat_get_id (PURPLE_CONV_CHAT (self->conv));
    who = prpl_info->get_cb_real_name (gc, chat_id, self->chat_buddy->name);

    if (who && *who)
      buddy = purple_find_buddy (self->conv->account, who);
  }

  if (!buddy)
    buddy = self->pp_buddy;

  if (!buddy)
    return G_SOURCE_REMOVE;

  contact = purple_buddy_get_contact (buddy);

  if (contact)
    img = purple_buddy_icons_node_find_custom_icon ((PurpleBlistNode*)contact);

  if (!img) {
    icon = purple_buddy_get_icon (buddy);

    if (!icon)
      icon = purple_buddy_icons_find (buddy->account, buddy->name);

    if (icon)
      data = purple_buddy_icon_get_data (icon, &len);

    if (!data)
      return G_SOURCE_REMOVE;
  }

  if (!data) {
    data = purple_imgstore_get_data (img);
    len = purple_imgstore_get_size (img);
  }

  /* If Icon hasn’t changed, don’t update */
  if (data == self->avatar_data)
    return G_SOURCE_REMOVE;

  g_clear_object (&self->avatar);
  self->avatar_data = (gpointer)data;
  self->avatar = chatty_icon_from_data (data, len);
  g_signal_emit_by_name (self, "avatar-changed");

  return G_SOURCE_REMOVE;
}

static GdkPixbuf *
chatty_pp_buddy_get_avatar (ChattyItem *item)
{
  ChattyPpBuddy *self = (ChattyPpBuddy *)item;

  g_assert (CHATTY_IS_PP_BUDDY (self));

  /*
   * XXX: purple_buddy_icons_node_find_custom_icon() when called
   * the first time, the `update' vfunc of blist PurpleBlistUiOps
   * is run, which in turn updates avatar, which calls
   * chatty_pp_buddy_get_avatar().
   */
  /* Load the icon async. So that we won't end up dead lock. */
  g_idle_add (load_icon, item);

  if (self->avatar)
    return self->avatar;

  return NULL;
}


static void
chatty_pp_buddy_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  ChattyPpBuddy *self = (ChattyPpBuddy *)object;

  switch (prop_id)
    {
    case PROP_PURPLE_ACCOUNT:
      self->pp_account = g_value_get_pointer (value);
      break;

    case PROP_PURPLE_BUDDY:
      self->pp_buddy = g_value_get_pointer (value);
      break;

    case PROP_CHAT_BUDDY:
      self->chat_buddy = g_value_get_pointer (value);
      break;

    case PROP_USERNAME:
      self->username = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
chatty_pp_buddy_constructed (GObject *object)
{
  ChattyPpBuddy *self = (ChattyPpBuddy *)object;
  ChattyAccount *account;
  GListModel *model;
  gboolean has_pp_buddy;

  G_OBJECT_CLASS (chatty_pp_buddy_parent_class)->constructed (object);

  if (self->chat_buddy)
    return;

  has_pp_buddy = self->pp_buddy != NULL;

  if (!has_pp_buddy)
    self->pp_buddy = purple_buddy_new (self->pp_account,
                                       self->username,
                                       self->name);

  chatty_pp_buddy_update_protocol (self);
  PURPLE_BLIST_NODE (self->pp_buddy)->ui_data = self;
  g_object_add_weak_pointer (G_OBJECT (self), (gpointer *)&PURPLE_BLIST_NODE (self->pp_buddy)->ui_data);

  account = self->pp_buddy->account->ui_data;
  model = chatty_account_get_buddies (account);
  g_list_store_append (G_LIST_STORE (model), self);

  if (!has_pp_buddy)
    chatty_add_new_buddy (self);
}

static void
chatty_pp_buddy_dispose (GObject *object)
{
  ChattyPpBuddy *self = (ChattyPpBuddy *)object;

  g_clear_object (&self->avatar);
  g_clear_pointer (&self->username, g_free);
  g_clear_pointer (&self->name, g_free);

  G_OBJECT_CLASS (chatty_pp_buddy_parent_class)->dispose (object);
}

static void
chatty_pp_buddy_class_init (ChattyPpBuddyClass *klass)
{
  GObjectClass *object_class  = G_OBJECT_CLASS (klass);
  ChattyItemClass *item_class = CHATTY_ITEM_CLASS (klass);

  object_class->set_property = chatty_pp_buddy_set_property;
  object_class->constructed  = chatty_pp_buddy_constructed;
  object_class->dispose = chatty_pp_buddy_dispose;

  item_class->get_protocols = chatty_pp_buddy_get_protocols;
  item_class->matches  = chatty_pp_buddy_matches;
  item_class->get_name = chatty_pp_buddy_get_name;
  item_class->set_name = chatty_pp_buddy_set_name;
  item_class->get_username = chatty_pp_buddy_get_username;
  item_class->get_avatar = chatty_pp_buddy_get_avatar;

  properties[PROP_PURPLE_ACCOUNT] =
    g_param_spec_pointer ("purple-account",
                         "PurpleAccount",
                         "The PurpleAccount this buddy belongs to",
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_PURPLE_BUDDY] =
    g_param_spec_pointer ("purple-buddy",
                          "Purple PpBuddy",
                          "The PurpleBuddy to be used to create the object",
                          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_CHAT_BUDDY] =
    g_param_spec_pointer ("chat-buddy",
                          "PurpleConvChatBuddy",
                          "The PurpleConvChatBuddy to be used to create the object",
                          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_USERNAME] =
    g_param_spec_string ("username",
                         "Username",
                         "Username of the Purple buddy",
                         NULL,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * ChattyPpBuddy::changed:
   * @self: a #ChattyPpBuddy
   *
   * changed signal is emitted when any detail
   * of the buddy changes.
   */
  signals [CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
chatty_pp_buddy_init (ChattyPpBuddy *self)
{
}

ChattyPpBuddy *
chatty_pp_buddy_get_object (PurpleBuddy *buddy)
{
  PurpleBlistNode *node = PURPLE_BLIST_NODE (buddy);

  g_return_val_if_fail (PURPLE_BLIST_NODE_IS_BUDDY (node), NULL);

  return node->ui_data;
}

ChattyAccount *
chatty_pp_buddy_get_account (ChattyPpBuddy *self)
{
  PurpleAccount *account;

  g_return_val_if_fail (CHATTY_IS_PP_BUDDY (self), NULL);

  if (self->chat_buddy && self->conv)
    account = self->conv->account;
  else
    account = purple_buddy_get_account (self->pp_buddy);

  return CHATTY_ACCOUNT (chatty_pp_account_get_object (account));
}

void
chatty_pp_buddy_set_chat (ChattyPpBuddy      *self,
                          PurpleConversation *conv)
{
  g_return_if_fail (CHATTY_IS_PP_BUDDY (self));

  self->conv = conv;
}


PurpleBuddy *
chatty_pp_buddy_get_buddy (ChattyPpBuddy *self)
{
  g_return_val_if_fail (CHATTY_IS_PP_BUDDY (self), NULL);

  return self->pp_buddy;
}

ChattyContact *
chatty_pp_buddy_get_contact (ChattyPpBuddy *self)
{
  g_return_val_if_fail (CHATTY_IS_PP_BUDDY (self), NULL);

  return self->contact;
}


void
chatty_pp_buddy_set_contact (ChattyPpBuddy *self,
                             ChattyContact *contact)
{
  g_return_if_fail (CHATTY_IS_PP_BUDDY (self));
  g_return_if_fail (!contact || CHATTY_IS_CONTACT (contact));

  if (g_set_object (&self->contact, contact))
    g_object_notify (G_OBJECT (self), "name");
}


ChattyUserFlag
chatty_pp_buddy_get_flags (ChattyPpBuddy *self)
{
  if (!self->chat_buddy)
    return CHATTY_USER_FLAG_NONE;

  if (self->chat_buddy->flags & PURPLE_CBFLAGS_FOUNDER)
    return CHATTY_USER_FLAG_OWNER;

  if (self->chat_buddy->flags & PURPLE_CBFLAGS_OP)
    return CHATTY_USER_FLAG_MODERATOR;

  if (self->chat_buddy->flags & PURPLE_CBFLAGS_VOICE)
    return CHATTY_USER_FLAG_MEMBER;

  return CHATTY_USER_FLAG_NONE;
}
