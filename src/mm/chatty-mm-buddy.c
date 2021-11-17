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

#define G_LOG_DOMAIN "chatty-mm-buddy"

#define _GNU_SOURCE
#include <string.h>
#include <glib/gi18n.h>

#include "chatty-settings.h"
#include "chatty-account.h"
#include "chatty-mm-account.h"
#include "chatty-window.h"
#include "chatty-mm-buddy.h"

/**
 * SECTION: chatty-mm-buddy
 * @title: ChattyMmBuddy
 * @short_description: An abstraction over ModemManager
 * @include: "chatty-mm-buddy.h"
 */

struct _ChattyMmBuddy
{
  ChattyItem       parent_instance;

  char            *phone_number;
  char            *name;
  ChattyContact   *contact;
};

G_DEFINE_TYPE (ChattyMmBuddy, chatty_mm_buddy, CHATTY_TYPE_ITEM)

enum {
  CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static ChattyProtocol
chatty_mm_buddy_get_protocols (ChattyItem *item)
{
  ChattyMmBuddy *self = (ChattyMmBuddy *)item;

  g_assert (CHATTY_IS_MM_BUDDY (self));

  return CHATTY_PROTOCOL_MMS_SMS;
}

static gboolean
chatty_mm_buddy_matches (ChattyItem     *item,
                         const char     *needle,
                         ChattyProtocol  protocols,
                         gboolean        match_name)
{
  ChattyMmBuddy *self = (ChattyMmBuddy *)item;

  if (self->phone_number)
    return strcasestr (self->phone_number, needle) != NULL;

  return FALSE;
}

static const char *
chatty_mm_buddy_get_name (ChattyItem *item)
{
  ChattyMmBuddy *self = (ChattyMmBuddy *)item;

  g_assert (CHATTY_IS_MM_BUDDY (self));

  if (self->contact) {
    const char *name;

    name = chatty_item_get_name (CHATTY_ITEM (self->contact));

    if (name && *name)
      return name;
  }

  if (self->name)
    return self->name;

  return "";
}

static GdkPixbuf *
chatty_mm_buddy_get_avatar (ChattyItem *item)
{
  ChattyMmBuddy *self = (ChattyMmBuddy *)item;

  g_assert (CHATTY_IS_MM_BUDDY (self));

  if (self->contact)
    return chatty_item_get_avatar (CHATTY_ITEM (self->contact));

  return NULL;
}

static void
chatty_mm_buddy_finalize (GObject *object)
{
  ChattyMmBuddy *self = (ChattyMmBuddy *)object;

  g_clear_object (&self->contact);
  g_clear_pointer (&self->phone_number, g_free);
  g_clear_pointer (&self->name, g_free);

  G_OBJECT_CLASS (chatty_mm_buddy_parent_class)->finalize (object);
}

static void
chatty_mm_buddy_class_init (ChattyMmBuddyClass *klass)
{
  GObjectClass *object_class  = G_OBJECT_CLASS (klass);
  ChattyItemClass *item_class = CHATTY_ITEM_CLASS (klass);

  object_class->finalize = chatty_mm_buddy_finalize;

  item_class->get_protocols = chatty_mm_buddy_get_protocols;
  item_class->matches  = chatty_mm_buddy_matches;
  item_class->get_name = chatty_mm_buddy_get_name;
  item_class->get_avatar = chatty_mm_buddy_get_avatar;

  /**
   * ChattyMmBuddy::changed:
   * @self: a #ChattyMmBuddy
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
chatty_mm_buddy_init (ChattyMmBuddy *self)
{
}

ChattyMmBuddy *
chatty_mm_buddy_new (const char *phone_number,
                     const char *name)
{
  ChattyMmBuddy *self;

  self = g_object_new (CHATTY_TYPE_MM_BUDDY, NULL);
  self->phone_number = g_strdup (phone_number);
  self->name = g_strdup (name);

  return self;
}

/**
 * chatty_mm_buddy_get_number:
 * @self: a #ChattyMmBuddy
 *
 * Get the phone number of @self.
 *
 * Returns: (transfer none): the phone number of Buddy.
 * or an empty string if not found or on error.
 */
const char *
chatty_mm_buddy_get_number (ChattyMmBuddy *self)
{
  g_return_val_if_fail (CHATTY_IS_MM_BUDDY (self), "");

  /* Prefer local copy of the phone number, as it will
   * be well formatted, or in international format,
   * while there is no guarantee for that to be the case
   * for the value saved in contacts
   */
  if (self->phone_number)
    return self->phone_number;

  if (self->contact)
    return chatty_item_get_username (CHATTY_ITEM (self->contact));

  return "";
}

ChattyContact *
chatty_mm_buddy_get_contact (ChattyMmBuddy *self)
{
  g_return_val_if_fail (CHATTY_IS_MM_BUDDY (self), NULL);

  return self->contact;
}

void
chatty_mm_buddy_set_contact (ChattyMmBuddy *self,
                             ChattyContact *contact)
{
  g_return_if_fail (CHATTY_IS_MM_BUDDY (self));
  g_return_if_fail (!contact || CHATTY_IS_CONTACT (contact));

  if (g_set_object (&self->contact, contact)) {
    g_object_notify (G_OBJECT (self), "name");
    g_signal_emit_by_name (self, "avatar-changed");
  }
}
