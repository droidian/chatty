/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-item.c
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-item"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define _GNU_SOURCE
#include <string.h>

#include "chatty-item.h"

/**
 * SECTION: chatty-item
 * @title: ChattyItem
 * @short_description: The base class for Accounts, Buddies, Contacts and Chats
 */

typedef struct
{
  ChattyProtocol protocols;
} ChattyItemPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ChattyItem, chatty_item, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_PROTOCOLS,
  PROP_NAME,
  N_PROPS
};

enum {
  AVATAR_CHANGED,
  DELETED,
  N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

static ChattyProtocol
chatty_item_real_get_protocols (ChattyItem *self)
{
  ChattyItemPrivate *priv = chatty_item_get_instance_private (self);

  g_assert (CHATTY_IS_ITEM (self));

  return priv->protocols;
}

static gboolean
chatty_item_real_matches (ChattyItem     *self,
                          const char     *needle,
                          ChattyProtocol  protocols,
                          gboolean        match_name)
{
  g_autofree char *str = NULL;
  const char *name;

  g_assert (CHATTY_IS_ITEM (self));

  name = chatty_item_get_name (self);
  str = g_utf8_casefold (name, -1);

  return strstr (str, needle) != NULL;
}

static const char *
chatty_item_real_get_name (ChattyItem *self)
{
  g_assert (CHATTY_IS_ITEM (self));

  return "";
}

static void
chatty_item_real_set_name (ChattyItem *self,
                           const char *name)
{
  g_assert (CHATTY_IS_ITEM (self));

  /* Do Nothing */
}

static const char *
chatty_item_real_get_username (ChattyItem *self)
{
  g_assert (CHATTY_IS_ITEM (self));

  return "";
}

static void
chatty_item_real_set_username (ChattyItem *self,
                               const char *username)
{
  /* Do Nothing */
}

static ChattyItemState
chatty_item_real_get_state (ChattyItem *self)
{
  g_assert (CHATTY_IS_ITEM (self));

  return CHATTY_ITEM_VISIBLE;
}

static void
chatty_item_real_set_state (ChattyItem      *self,
                            ChattyItemState  state)
{
  g_assert (CHATTY_IS_ITEM (self));

  /* Do Nothing */
}

static ChattyFileInfo *
chatty_item_real_get_avatar_file (ChattyItem *self)
{
  g_assert (CHATTY_IS_ITEM (self));

  return NULL;
}

static GdkPixbuf *
chatty_item_real_get_avatar (ChattyItem *self)
{
  g_assert (CHATTY_IS_ITEM (self));

  return NULL;
}


static void
chatty_item_real_get_avatar_async (ChattyItem          *self,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  g_assert (CHATTY_IS_ITEM (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  g_task_report_new_error (self, callback, user_data,
                           chatty_item_real_get_avatar_async,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "Loading Custom avatar not supported");
}


static GdkPixbuf *
chatty_item_real_get_avatar_finish (ChattyItem    *self,
                                    GAsyncResult  *result,
                                    GError       **error)
{
  g_assert (CHATTY_IS_ITEM (self));
  g_assert (G_IS_TASK (result));

  return g_task_propagate_pointer (G_TASK (result), error);
}


static void
chatty_item_real_set_avatar_async (ChattyItem          *self,
                                   const char          *filename,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             item_data)
{
  g_assert (CHATTY_IS_ITEM (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  g_task_report_new_error (self, callback, item_data,
                           chatty_item_real_set_avatar_async,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "Setting Custom avatar not supported");
}

static gboolean
chatty_item_real_set_avatar_finish (ChattyItem    *self,
                                    GAsyncResult  *result,
                                    GError       **error)
{
  g_assert (CHATTY_IS_ITEM (self));
  g_assert (G_IS_TASK (result));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
chatty_item_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  ChattyItem *self = (ChattyItem *)object;

  switch (prop_id)
    {
    case PROP_PROTOCOLS:
      g_value_set_int (value, chatty_item_get_protocols (self));
      break;

    case PROP_NAME:
      g_value_set_string (value, chatty_item_get_name (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
chatty_item_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  ChattyItem *self = (ChattyItem *)object;
  ChattyItemPrivate *priv = chatty_item_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_PROTOCOLS:
      priv->protocols = g_value_get_int (value);
      break;

    case PROP_NAME:
      chatty_item_set_name (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
chatty_item_class_init (ChattyItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = chatty_item_get_property;
  object_class->set_property = chatty_item_set_property;

  klass->get_protocols = chatty_item_real_get_protocols;
  klass->matches  = chatty_item_real_matches;
  klass->get_name = chatty_item_real_get_name;
  klass->set_name = chatty_item_real_set_name;
  klass->get_username = chatty_item_real_get_username;
  klass->set_username = chatty_item_real_set_username;
  klass->get_state = chatty_item_real_get_state;
  klass->set_state = chatty_item_real_set_state;
  klass->get_avatar_file = chatty_item_real_get_avatar_file;
  klass->get_avatar = chatty_item_real_get_avatar;
  klass->get_avatar_async  = chatty_item_real_get_avatar_async;
  klass->get_avatar_finish = chatty_item_real_get_avatar_finish;
  klass->set_avatar_async  = chatty_item_real_set_avatar_async;
  klass->set_avatar_finish = chatty_item_real_set_avatar_finish;

  /**
   * ChattyItem:name:
   *
   * The name of the Item.
   */
  properties[PROP_NAME] =
    g_param_spec_string ("name",
                         "name",
                         "The name of the Item",
                         "",
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * ChattyItem:protocols:
   *
   * Protocols supported by the Item.  An item may
   * support more than one protocol.  Currently only
   * #ChattyContact supports more than one protocol.
   */
  properties[PROP_PROTOCOLS] =
    g_param_spec_int ("protocols",
                      "Protocols",
                      "Protocols supported by item",
                      CHATTY_PROTOCOL_NONE,
                      CHATTY_PROTOCOL_LAST,
                      CHATTY_PROTOCOL_NONE,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * ChattyItem::avatar-changed:
   * @self: a #ChattyItem
   *
   * avatar-changed signal is emitted when the item’s
   * avatar change.
   */
  signals [AVATAR_CHANGED] =
    g_signal_new ("avatar-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * ChattyItem::deleted:
   * @self: a #ChattyItem
   *
   * deleted signal is emitted when the account
   * is deleted
   */
  signals [DELETED] =
    g_signal_new ("deleted",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
chatty_item_init (ChattyItem *self)
{
}

/**
 * chatty_item_get_protocols:
 * @self: a #ChattyItem
 *
 * Get the protocols supported/implemented by @self.
 * There can be more than one protocol supported by
 * @self.  Currently only #ChattyContact supports
 * more than one protocol.
 *
 * Returns: %ChattyProtocol flag
 */
ChattyProtocol
chatty_item_get_protocols (ChattyItem *self)
{
  g_return_val_if_fail (CHATTY_IS_ITEM (self), CHATTY_PROTOCOL_NONE);

  return CHATTY_ITEM_GET_CLASS (self)->get_protocols (self);
}

/**
 * chatty_item_matches:
 * @self: a #ChattyItem
 * @needle: The string to search for
 * @protocols: Allowed protocols
 * @match_name: %TRUE to match name of contact
 *
 * See if @needle matches @self (partially or fully).
 * If @match_name is %TRUE, first matches the name of @self.
 *
 * Returns: %TRUE if @needle and @self has some match.
 * %FALSE otherwise.
 */
gboolean
chatty_item_matches (ChattyItem     *self,
                     const char     *needle,
                     ChattyProtocol  protocols,
                     gboolean        match_name)
{
  g_return_val_if_fail (CHATTY_IS_ITEM (self), FALSE);

  if (!(protocols & chatty_item_get_protocols (self)))
    return FALSE;

  if (!needle || !*needle)
    return TRUE;

  if (match_name)
    {
      gboolean match;

      match = chatty_item_real_matches (self, needle, protocols, match_name);

      if (match)
        return match;
    }

  /* We have done this already, and if we reached here, we have no match */
  if (CHATTY_ITEM_GET_CLASS (self)->matches == chatty_item_real_matches)
    return FALSE;

  return CHATTY_ITEM_GET_CLASS (self)->matches (self, needle, protocols, match_name);
}

/**
 * chatty_item_compare:
 * @a: a #ChattyItem
 * @b: a #ChattyItem
 *
 * Compare to items and find the order they
 * should be sorted.
 *
 * Returns: < 0 if @a before @b, 0 if they
 * compare equal, > 0 if @a compares after @b.
 */
int
chatty_item_compare (ChattyItem *a,
                     ChattyItem *b)
{
  g_return_val_if_fail (CHATTY_IS_ITEM (a), 0);
  g_return_val_if_fail (CHATTY_IS_ITEM (b), 0);

  return g_utf8_collate (chatty_item_get_name (a),
                         chatty_item_get_name (b));
}

/**
 * chatty_item_get_name:
 * @self: a #ChattyItem
 *
 * Get the name of Item.  In purple/pidgin it’s
 * termed as ‘alias.’ If real name is empty,
 * it fallbacks to the user id.  The user id
 * may have the resource stripped (eg: For the
 * user id xmpp@example.com/someclient.6 you shall
 * get xmpp@example.com)
 *
 * Returns: (transfer none): the name of Item.
 */
const char *
chatty_item_get_name (ChattyItem *self)
{
  g_return_val_if_fail (CHATTY_IS_ITEM (self), "");

  return CHATTY_ITEM_GET_CLASS (self)->get_name (self);
}

/**
 * chatty_item_set_name:
 * @self: a #ChattyItem
 * @name: (nullable): a text to set as name
 *
 * Set the name of Item. Can be %NULL.
 */
void
chatty_item_set_name (ChattyItem *self,
                      const char *name)
{
  g_return_if_fail (CHATTY_IS_ITEM (self));

  CHATTY_ITEM_GET_CLASS (self)->set_name (self, name);
}

/**
 * chatty_item_get_username:
 * @self: a #ChattyItem
 *
 * Returns the username/value of the item.  It can be a phone
 * number, an XMPP/matrix ID, etc, depending on what @self
 * represents.  For a #ChattyChat, the username of the account
 * for the chat was created, is returned.
 *
 * Returns: the value of the item
 */
const char *
chatty_item_get_username (ChattyItem *self)
{
  g_return_val_if_fail (CHATTY_IS_ITEM (self), NULL);

  return CHATTY_ITEM_GET_CLASS (self)->get_username (self);
}

void
chatty_item_set_username (ChattyItem *self,
                          const char *username)
{
  g_return_if_fail (CHATTY_IS_ITEM (self));
  g_return_if_fail (username && *username);

  return CHATTY_ITEM_GET_CLASS (self)->set_username (self, username);
}

/**
 * chatty_item_get_state:
 * @self: a #ChattyItem
 *
 * Get the visibility state of the item
 *
 * Returns: A #ChattyItemState
 */
ChattyItemState
chatty_item_get_state (ChattyItem *self)
{
  g_return_val_if_fail (CHATTY_IS_ITEM (self), CHATTY_ITEM_HIDDEN);

  return CHATTY_ITEM_GET_CLASS (self)->get_state (self);
}

/**
 * chatty_item_set_state:
 * @self: a #ChattyItem
 * @state: A #ChattyItemState
 *
 * Set the visibility state of the item
 */
void
chatty_item_set_state (ChattyItem      *self,
                       ChattyItemState  state)
{
  g_return_if_fail (CHATTY_IS_ITEM (self));

  return CHATTY_ITEM_GET_CLASS (self)->set_state (self, state);
}

ChattyFileInfo *
chatty_item_get_avatar_file (ChattyItem *self)
{
  g_return_val_if_fail (CHATTY_IS_ITEM (self), NULL);

  return CHATTY_ITEM_GET_CLASS (self)->get_avatar_file (self);
}

/**
 * chatty_item_get_avatar:
 * @self: a #ChattyItem
 *
 * Get the avatar of @item.  If avatar hasn’t loaded
 * A %NULL may be returned immediately and load the
 * avatar async.  So hook to ::avatar-changed signal
 * if you need to update avatar on change or use
 * chatty_item_get_avatar_async().
 *
 * Returns: (transfer none) (nullable): A #GdkPixbuf.
 */
GdkPixbuf *
chatty_item_get_avatar (ChattyItem *self)
{
  g_return_val_if_fail (CHATTY_IS_ITEM (self), NULL);

  return CHATTY_ITEM_GET_CLASS (self)->get_avatar (self);
}


void
chatty_item_get_avatar_async (ChattyItem          *self,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  g_return_if_fail (CHATTY_IS_ITEM (self));

  CHATTY_ITEM_GET_CLASS (self)->get_avatar_async (self, cancellable,
                                                  callback, user_data);
}


GdkPixbuf *
chatty_item_get_avatar_finish (ChattyItem    *self,
                               GAsyncResult  *result,
                               GError       **error)
{
  g_return_val_if_fail (CHATTY_IS_ITEM (self), NULL);

  return CHATTY_ITEM_GET_CLASS (self)->get_avatar_finish (self, result, error);
}


void
chatty_item_set_avatar_async (ChattyItem          *self,
                              const char          *file_name,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             item_data)
{
  g_return_if_fail (CHATTY_IS_ITEM (self));

  CHATTY_ITEM_GET_CLASS (self)->set_avatar_async (self, file_name, cancellable,
                                                  callback, item_data);
}

gboolean
chatty_item_set_avatar_finish (ChattyItem    *self,
                               GAsyncResult  *result,
                               GError       **error)
{
  g_return_val_if_fail (CHATTY_IS_ITEM (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return CHATTY_ITEM_GET_CLASS (self)->set_avatar_finish (self, result, error);
}
