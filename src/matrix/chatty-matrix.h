/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-matrix.h
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

#include "chatty-history.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_MATRIX (chatty_matrix_get_type ())

G_DECLARE_FINAL_TYPE (ChattyMatrix, chatty_matrix, CHATTY, MATRIX, GObject)

ChattyMatrix  *chatty_matrix_new                     (ChattyHistory  *history,
                                                      gboolean        disable_auto_login);
void           chatty_matrix_load                    (ChattyMatrix   *self);
GListModel     *chatty_matrix_get_account_list       (ChattyMatrix   *self);
GListModel     *chatty_matrix_get_chat_list          (ChattyMatrix   *self);
void            chatty_matrix_delete_account_async   (ChattyMatrix   *self,
                                                      ChattyAccount  *account,
                                                      GCancellable   *cancellable,
                                                      GAsyncReadyCallback callback,
                                                      gpointer       user_data);
gboolean        chatty_matrix_delete_account_finish  (ChattyMatrix   *self,
                                                      GAsyncResult   *result,
                                                      GError        **error);
void            chatty_matrix_save_account_async     (ChattyMatrix   *self,
                                                      ChattyAccount  *account,
                                                      GCancellable   *cancellable,
                                                      GAsyncReadyCallback callback,
                                                      gpointer        user_data);
gboolean        chatty_matrix_save_account_finish    (ChattyMatrix   *self,
                                                      GAsyncResult   *result,
                                                      GError        **error);
ChattyAccount  *chatty_matrix_find_account_with_name (ChattyMatrix   *self,
                                                      const char     *account_id);
ChattyChat     *chatty_matrix_find_chat_with_name    (ChattyMatrix   *self,
                                                      ChattyProtocol  protocol,
                                                      const char     *account_id,
                                                      const char     *chat_id);

G_END_DECLS
