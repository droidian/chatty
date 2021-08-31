/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-account.h
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <handy.h>

#include "chatty-item.h"
#include "chatty-enums.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_ACCOUNT (chatty_account_get_type ())

G_DECLARE_DERIVABLE_TYPE (ChattyAccount, chatty_account, CHATTY, ACCOUNT, ChattyItem)

typedef struct _ChattyChat ChattyChat;

struct _ChattyAccountClass
{
  ChattyItemClass parent_class;

  const char  *(*get_protocol_name)     (ChattyAccount *self);
  ChattyStatus (*get_status)            (ChattyAccount *self);
  GListModel  *(*get_buddies)           (ChattyAccount *self);
  gboolean     (*buddy_exists)          (ChattyAccount *self,
                                         const char    *buddy_username);
  gboolean     (*get_enabled)           (ChattyAccount *self);
  void         (*set_enabled)           (ChattyAccount *self,
                                         gboolean       enable);
  void         (*connect)               (ChattyAccount *self,
                                         gboolean       delay);
  void         (*disconnect)            (ChattyAccount *self);
  const char  *(*get_password)          (ChattyAccount *self);
  void         (*set_password)          (ChattyAccount *self,
                                         const char    *password);
  gboolean     (*get_remember_password) (ChattyAccount *self);
  void         (*set_remember_password) (ChattyAccount *self,
                                         gboolean       remember);
  void         (*save)                  (ChattyAccount *self);
  void         (*delete)                (ChattyAccount *self);
  HdyValueObject *(*get_device_fp)      (ChattyAccount *self);
  GListModel  *(*get_fp_list)           (ChattyAccount *self);
  void         (*load_fp_async)         (ChattyAccount *self,
                                         GAsyncReadyCallback callback,
                                         gpointer       user_data);
  gboolean     (*load_fp_finish)        (ChattyAccount *self,
                                         GAsyncResult  *result,
                                         GError       **error);
  void         (*join_chat_async)       (ChattyAccount *self,
                                         ChattyChat    *chat,
                                         GAsyncReadyCallback callback,
                                         gpointer       user_data);
  gboolean     (*join_chat_finish)      (ChattyAccount *self,
                                         GAsyncResult  *result,
                                         GError       **error);
  void         (*leave_chat_async)      (ChattyAccount *self,
                                         ChattyChat    *chat,
                                         GAsyncReadyCallback callback,
                                         gpointer       user_data);
  gboolean     (*leave_chat_finish)     (ChattyAccount *self,
                                         GAsyncResult  *result,
                                         GError       **error);
  void         (*start_direct_chat_async)  (ChattyAccount *self,
                                            GPtrArray     *buddies,
                                            GAsyncReadyCallback callback,
                                            gpointer       user_data);
  gboolean     (*start_direct_chat_finish) (ChattyAccount *self,
                                            GAsyncResult  *result,
                                            GError       **error);
};


const char   *chatty_account_get_protocol_name     (ChattyAccount *self);
ChattyStatus  chatty_account_get_status            (ChattyAccount *self);
GListModel   *chatty_account_get_buddies           (ChattyAccount *self);
gboolean      chatty_account_buddy_exists          (ChattyAccount *self,
                                                    const char    *buddy_username);
gboolean      chatty_account_get_enabled           (ChattyAccount *self);
void          chatty_account_set_enabled           (ChattyAccount *self,
                                                    gboolean       enable);
void          chatty_account_connect               (ChattyAccount *self,
                                                    gboolean       delay);
void          chatty_account_disconnect            (ChattyAccount *self);
const char   *chatty_account_get_password          (ChattyAccount *self);
void          chatty_account_set_password          (ChattyAccount *self,
                                                    const char    *password);
gboolean      chatty_account_get_remember_password (ChattyAccount *self);
void          chatty_account_set_remember_password (ChattyAccount *self,
                                                    gboolean       remember);
void          chatty_account_save                  (ChattyAccount *self);
void          chatty_account_delete                (ChattyAccount *self);
HdyValueObject *chatty_account_get_device_fp       (ChattyAccount *self);
GListModel   *chatty_account_get_fp_list           (ChattyAccount *self);
void          chatty_account_load_fp_async         (ChattyAccount *self,
                                                    GAsyncReadyCallback callback,
                                                    gpointer       user_data);
gboolean      chatty_account_load_fp_finish        (ChattyAccount *self,
                                                    GAsyncResult  *result,
                                                    GError       **error);

void          chatty_account_join_chat_async       (ChattyAccount *self,
                                                    ChattyChat    *chat,
                                                    GAsyncReadyCallback callback,
                                                    gpointer       user_data);
gboolean      chatty_account_join_chat_finish      (ChattyAccount  *self,
                                                    GAsyncResult   *result,
                                                    GError        **error);
void          chatty_account_leave_chat_async      (ChattyAccount *self,
                                                    ChattyChat    *chat,
                                                    GAsyncReadyCallback callback,
                                                    gpointer       user_data);
gboolean      chatty_account_leave_chat_finish     (ChattyAccount *self,
                                                    GAsyncResult  *result,
                                                    GError       **error);

void          chatty_account_start_direct_chat_async  (ChattyAccount *self,
                                                       GPtrArray     *buddies,
                                                       GAsyncReadyCallback callback,
                                                       gpointer       user_data);
gboolean      chatty_account_start_direct_chat_finish (ChattyAccount *self,
                                                       GAsyncResult  *result,
                                                       GError       **error);

G_END_DECLS
