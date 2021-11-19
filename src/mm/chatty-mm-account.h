/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-mm-account.h
 *
 * Copyright 2020,2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>
#include <libmm-glib/libmm-glib.h>

#include "chatty-chat.h"
#include "chatty-mm-buddy.h"
#include "chatty-account.h"
#include "chatty-contact-provider.h"
#include "chatty-enums.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_MM_ACCOUNT (chatty_mm_account_get_type ())

G_DECLARE_FINAL_TYPE (ChattyMmAccount, chatty_mm_account, CHATTY, MM_ACCOUNT, ChattyAccount)

ChattyMmAccount *chatty_mm_account_new                 (void);
void             chatty_mm_account_set_eds             (ChattyMmAccount     *self,
                                                        ChattyEds           *eds);
void             chatty_mm_account_set_history_db      (ChattyMmAccount     *self,
                                                        gpointer             history_db);
GListModel      *chatty_mm_account_get_chat_list       (ChattyMmAccount     *self);
void             chatty_mm_account_load_async          (ChattyMmAccount     *self,
                                                        GAsyncReadyCallback  callback,
                                                        gpointer             user_data);
gboolean         chatty_mm_account_load_finish         (ChattyMmAccount     *self,
                                                        GAsyncResult        *result,
                                                        GError             **error);
ChattyChat      *chatty_mm_account_find_chat           (ChattyMmAccount     *self,
                                                        const char          *phone);
ChattyChat      *chatty_mm_account_start_chat          (ChattyMmAccount     *self,
                                                        const char          *phone);
void             chatty_mm_account_delete_chat         (ChattyMmAccount     *self,
                                                        ChattyChat          *chat);
gboolean         chatty_mm_account_has_mms_feature     (ChattyMmAccount     *self);
void             chatty_mm_account_send_message_async  (ChattyMmAccount     *self,
                                                        ChattyChat          *chat,
                                                        ChattyMmBuddy       *buddy,
                                                        ChattyMessage       *message,
                                                        gboolean             is_mms,
                                                        GCancellable        *cancellable,
                                                        GAsyncReadyCallback  callback,
                                                        gpointer             user_data);
gboolean         chatty_mm_account_send_message_finish (ChattyMmAccount    *self,
                                                        GAsyncResult       *result,
                                                        GError            **error);
void             chatty_mm_account_recieve_mms_cb      (ChattyMmAccount *self,
                                                        ChattyMessage   *message,
                                                        const char      *sender,
                                                        const char      *recipientlist);


G_END_DECLS
