/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* matrix-net.h
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

#include "chatty-message.h"

G_BEGIN_DECLS

#define MATRIX_TYPE_NET (matrix_net_get_type ())

G_DECLARE_FINAL_TYPE (MatrixNet, matrix_net, MATRIX, NET, GObject)

MatrixNet *matrix_net_new              (void);
void       matrix_net_set_homeserver   (MatrixNet         *self,
                                        const char        *homeserver);
void       matrix_net_set_access_token (MatrixNet         *self,
                                        const char        *access_token);
void       matrix_net_send_data_async  (MatrixNet         *self,
                                        int                priority,
                                        char              *data,
                                        gsize              size,
                                        const char        *uri_path,
                                        const char        *method, /* interned */
                                        GHashTable        *query,
                                        GCancellable      *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer           user_data);
void       matrix_net_send_json_async  (MatrixNet         *self,
                                        int                priority,
                                        JsonObject        *object,
                                        const char        *uri_path,
                                        const char        *method, /* interned */
                                        GHashTable        *query,
                                        GCancellable      *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer           user_data);
void       matrix_net_get_file_async   (MatrixNet         *self,
                                        ChattyMessage     *message,
                                        ChattyFileInfo    *file,
                                        GCancellable      *cancellable,
                                        GFileProgressCallback progress_callback,
                                        GAsyncReadyCallback callback,
                                        gpointer           user_data);
gboolean   matrix_net_get_file_finish  (MatrixNet         *self,
                                        GAsyncResult      *result,
                                        GError           **error);

G_END_DECLS
