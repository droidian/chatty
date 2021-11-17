/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-secret-store.h
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib.h>

#include "chatty-account.h"

#define CHATTY_USERNAME_ATTRIBUTE  "username"
#define CHATTY_SERVER_ATTRIBUTE    "server"
#define CHATTY_PROTOCOL_ATTRIBUTE  "protocol"

G_BEGIN_DECLS

void       chatty_secret_store_save_async  (ChattyAccount       *account,
                                            char                *access_token,
                                            const char          *device_id,
                                            char                *pickle_key,
                                            GCancellable        *cancellable,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data);
gboolean   chatty_secret_store_save_finish (GAsyncResult        *result,
                                            GError             **error);

void       chatty_secret_load_async        (GCancellable        *cancellable,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data);
GPtrArray *chatty_secret_load_finish       (GAsyncResult        *result,
                                            GError             **error);

void       chatty_secret_delete_async      (ChattyAccount       *account,
                                            GCancellable        *cancellable,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data);
gboolean   chatty_secret_delete_finish     (GAsyncResult        *result,
                                            GError             **error);

G_END_DECLS
