/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-item.h
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>
#include <gdk/gdk.h>

#include "chatty-utils.h"
#include "chatty-enums.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_ITEM (chatty_item_get_type ())

G_DECLARE_DERIVABLE_TYPE (ChattyItem, chatty_item, CHATTY, ITEM, GObject)

struct _ChattyItemClass
{
  GObjectClass parent_class;

  ChattyProtocol   (*get_protocols)       (ChattyItem           *self);
  gboolean         (*matches)             (ChattyItem           *self,
                                           const char           *needle,
                                           ChattyProtocol        protocols,
                                           gboolean              match_name);
  const char      *(*get_name)            (ChattyItem           *self);
  void             (*set_name)            (ChattyItem           *self,
                                           const char           *name);
  const char      *(*get_username)        (ChattyItem           *self);
  void             (*set_username)        (ChattyItem           *self,
                                           const char           *username);
  ChattyItemState  (*get_state)           (ChattyItem           *self);
  void             (*set_state)           (ChattyItem           *self,
                                           ChattyItemState       state);
  ChattyFileInfo  *(*get_avatar_file)     (ChattyItem           *self);
  GdkPixbuf       *(*get_avatar)          (ChattyItem           *self);
  void             (*get_avatar_async)    (ChattyItem           *self,
                                           GCancellable         *cancellable,
                                           GAsyncReadyCallback   callback,
                                           gpointer              user_data);
  GdkPixbuf       *(*get_avatar_finish)   (ChattyItem           *self,
                                           GAsyncResult         *result,
                                           GError              **error);
  void             (*set_avatar_async)    (ChattyItem           *self,
                                           const char           *file_name,
                                           GCancellable         *cancellable,
                                           GAsyncReadyCallback   callback,
                                           gpointer              item_data);
  gboolean         (*set_avatar_finish)   (ChattyItem           *self,
                                           GAsyncResult         *result,
                                           GError              **error);
};

ChattyProtocol   chatty_item_get_protocols       (ChattyItem           *self);
gboolean         chatty_item_matches             (ChattyItem           *self,
                                                  const char           *needle,
                                                  ChattyProtocol        protocols,
                                                  gboolean              match_name);
int              chatty_item_compare              (ChattyItem          *a,
                                                   ChattyItem          *b);
const char      *chatty_item_get_name            (ChattyItem           *self);
void             chatty_item_set_name            (ChattyItem           *self,
                                                  const char           *name);
const char      *chatty_item_get_username        (ChattyItem           *self);
void             chatty_item_set_username        (ChattyItem           *self,
                                                  const char           *username);
ChattyItemState  chatty_item_get_state           (ChattyItem           *self);
void             chatty_item_set_state           (ChattyItem           *self,
                                                  ChattyItemState       state);
ChattyFileInfo  *chatty_item_get_avatar_file     (ChattyItem           *self);
GdkPixbuf       *chatty_item_get_avatar          (ChattyItem           *self);
void             chatty_item_get_avatar_async    (ChattyItem           *self,
                                                  GCancellable         *cancellable,
                                                  GAsyncReadyCallback   callback,
                                                  gpointer              user_data);
GdkPixbuf       *chatty_item_get_avatar_finish   (ChattyItem           *self,
                                                  GAsyncResult         *result,
                                                  GError              **error);
void             chatty_item_set_avatar_async    (ChattyItem           *self,
                                                  const char           *file_name,
                                                  GCancellable         *cancellable,
                                                  GAsyncReadyCallback   callback,
                                                  gpointer              item_data);
gboolean         chatty_item_set_avatar_finish   (ChattyItem           *self,
                                                  GAsyncResult         *result,
                                                  GError              **error);

G_END_DECLS
