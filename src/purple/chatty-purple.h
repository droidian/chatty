/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-purple.h
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

#include "config.h"

#ifdef PURPLE_ENABLED
#include "chatty-pp-account.h"
#include "chatty-pp-buddy.h"
#include "chatty-pp-chat.h"
#include "chatty-history.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_PURPLE (chatty_purple_get_type ())

G_DECLARE_FINAL_TYPE (ChattyPurple, chatty_purple, CHATTY, PURPLE, GObject)

ChattyPurple   *chatty_purple_get_default          (void);
void            chatty_purple_enable_debug         (void);
gboolean        chatty_purple_is_loaded            (ChattyPurple   *self);
void            chatty_purple_start_chat           (ChattyPurple   *self,
                                                    ChattyItem     *item);
GListModel     *chatty_purple_get_accounts         (ChattyPurple   *self);
GListModel     *chatty_purple_get_chat_list        (ChattyPurple   *self);
GListModel     *chatty_purple_get_user_list        (ChattyPurple   *self);
ChattyAccount  *chatty_purple_find_account_with_name(ChattyPurple  *self,
                                                     ChattyProtocol protocol,
                                                     const char    *account_id);
ChattyChat     *chatty_purple_find_chat_with_name  (ChattyPurple   *self,
                                                    ChattyProtocol  protocol,
                                                    const char     *account_id,
                                                    const char     *chat_id);
void            chatty_purple_delete_account_async (ChattyPurple   *self,
                                                    ChattyAccount  *account,
                                                    GCancellable   *cancellable,
                                                    GAsyncReadyCallback callback,
                                                    gpointer       user_data);
gboolean        chatty_purple_delete_account_finish(ChattyPurple   *self,
                                                    GAsyncResult   *result,
                                                    GError        **error);
ChattyProtocol  chatty_purple_get_protocols        (ChattyPurple   *self);
void            chatty_purple_set_history_db       (ChattyPurple   *self,
                                                    ChattyHistory  *db);
void            chatty_purple_load                 (ChattyPurple   *self,
                                                    gboolean        disable_auto_login);
gboolean        chatty_purple_has_encryption       (ChattyPurple   *self);
gboolean        chatty_purple_has_carbon_plugin    (ChattyPurple   *self);
gboolean        chatty_purple_has_telegram_loaded  (ChattyPurple   *self);

G_END_DECLS

#endif  /* PURPLE_ENABLED */
