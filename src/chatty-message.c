/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-message.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-message"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "chatty-message.h"
#include "chatty-utils.h"

/**
 * SECTION: chatty-message
 * @title: ChattyMessage
 * @short_description: An abstraction for chat messages
 * @include: "chatty-message.h"
 */

struct _ChattyMessage
{
  GObject          parent_instance;

  ChattyItem      *user;
  char            *user_name;
  char            *user_alias;
  char            *message;
  char            *uid;
  char            *id;
  ChattyMsgStatus  status;
  ChattyMsgDirection direction;
  time_t           time;
};

G_DEFINE_TYPE (ChattyMessage, chatty_message, G_TYPE_OBJECT)

enum {
  UPDATED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
chatty_message_finalize (GObject *object)
{
  ChattyMessage *self = (ChattyMessage *)object;

  g_clear_object (&self->user);
  g_free (self->message);
  g_free (self->uid);
  g_free (self->user_alias);
  g_free (self->user_name);
  g_free (self->id);

  G_OBJECT_CLASS (chatty_message_parent_class)->finalize (object);
}

static void
chatty_message_class_init (ChattyMessageClass *klass)
{
  GObjectClass *object_class  = G_OBJECT_CLASS (klass);

  object_class->finalize = chatty_message_finalize;

  /**
   * ChattyMessage::updated:
   * @self: a #ChattyMessage
   *
   * Emitted when the message or any of its property
   * is updated.
   */
  signals [UPDATED] =
    g_signal_new ("updated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
chatty_message_init (ChattyMessage *self)
{
}


ChattyMessage *
chatty_message_new (ChattyItem         *user,
                    const char         *user_alias,
                    const char         *message,
                    const char         *uid,
                    time_t              timestamp,
                    ChattyMsgDirection  direction,
                    ChattyMsgStatus     status)
{
  ChattyMessage *self;

  if (!timestamp)
    timestamp = time (NULL);

  self = g_object_new (CHATTY_TYPE_MESSAGE, NULL);
  g_set_object (&self->user, user);
  self->user_alias = g_strdup (user_alias);
  self->message = g_strdup (message);
  self->uid = g_strdup (uid);
  self->status = status;
  self->direction = direction;
  self->time = timestamp;

  return self;
}

const char *
chatty_message_get_uid (ChattyMessage *self)
{
  g_return_val_if_fail (CHATTY_IS_MESSAGE (self), NULL);

  return self->uid;
}

const char *
chatty_message_get_id (ChattyMessage *self)
{
  g_return_val_if_fail (CHATTY_IS_MESSAGE (self), "");

  return self->id;
}

void
chatty_message_set_id (ChattyMessage *self,
                       const char    *id)
{
  g_return_if_fail (CHATTY_IS_MESSAGE (self));

  g_free (self->id);
  self->id = g_strdup (id);
}

const char *
chatty_message_get_text (ChattyMessage *self)
{
  g_return_val_if_fail (CHATTY_IS_MESSAGE (self), "");

  if (!self->message)
    return "";

  return self->message;
}


ChattyItem *
chatty_message_get_user (ChattyMessage *self)
{
  g_return_val_if_fail (CHATTY_IS_MESSAGE (self), NULL);

  return self->user;
}

const char *
chatty_message_get_user_name (ChattyMessage *self)
{
  const char *user_name = NULL;

  g_return_val_if_fail (CHATTY_IS_MESSAGE (self), "");

  if (!self->user_name && self->user)
    user_name = chatty_item_get_name (self->user);

  if (user_name)
    self->user_name = chatty_utils_jabber_id_strip (user_name);

  if (self->user_name)
    return self->user_name;

  return "";
}

void
chatty_message_set_user_name (ChattyMessage *self,
                              const char    *user_name)
{
  g_return_if_fail (CHATTY_IS_MESSAGE (self));

  g_free (self->user_name);
  self->user_name = g_strdup (user_name);
}

const char *
chatty_message_get_user_alias (ChattyMessage *self)
{
  g_return_val_if_fail (CHATTY_IS_MESSAGE (self), NULL);

  return self->user_alias;
}

time_t
chatty_message_get_time (ChattyMessage *self)
{
  g_return_val_if_fail (CHATTY_IS_MESSAGE (self), 0);

  return self->time;
}

ChattyMsgStatus
chatty_message_get_status (ChattyMessage *self)
{
  g_return_val_if_fail (CHATTY_IS_MESSAGE (self), CHATTY_STATUS_UNKNOWN);

  return self->status;
}

void
chatty_message_set_status (ChattyMessage   *self,
                           ChattyMsgStatus  status,
                           time_t           mtime)
{
  g_return_if_fail (CHATTY_IS_MESSAGE (self));

  self->status = status;
  if (mtime)
    self->time = mtime;

  g_signal_emit (self, signals[UPDATED], 0);
}

ChattyMsgDirection
chatty_message_get_msg_direction (ChattyMessage *self)
{
  g_return_val_if_fail (CHATTY_IS_MESSAGE (self), CHATTY_DIRECTION_UNKNOWN);

  return self->direction;
}
