/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* matrix-api.h
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

#include "chatty-chat.h"
#include "users/chatty-account.h"
#include "chatty-message.h"
#include "matrix-enc.h"
#include "matrix-enums.h"

G_BEGIN_DECLS

#define MATRIX_TYPE_API (matrix_api_get_type ())

G_DECLARE_FINAL_TYPE (MatrixApi, matrix_api, MATRIX, API, GObject)

typedef void   (*MatrixCallback)                 (gpointer        object,
                                                  MatrixApi      *self,
                                                  MatrixAction    action,
                                                  JsonObject     *jobject,
                                                  GError         *err);

MatrixApi     *matrix_api_new                    (const char     *username);
void           matrix_api_set_enc                (MatrixApi      *self,
                                                  MatrixEnc      *enc);
const char    *matrix_api_get_username           (MatrixApi      *self);
void           matrix_api_set_username           (MatrixApi      *self,
                                                  const char     *username);

const char    *matrix_api_get_password           (MatrixApi      *self);
void           matrix_api_set_password           (MatrixApi      *self,
                                                  const char     *password);

const char    *matrix_api_get_homeserver         (MatrixApi      *self);
void           matrix_api_set_homeserver         (MatrixApi      *self,
                                                  const char     *homeserver);
const char    *matrix_api_get_device_id          (MatrixApi      *self);
const char    *matrix_api_get_access_token       (MatrixApi      *self);
void           matrix_api_set_access_token       (MatrixApi      *self,
                                                  const char     *access_token,
                                                  const char     *device_id);
const char    *matrix_api_get_next_batch         (MatrixApi      *self);
void           matrix_api_set_next_batch         (MatrixApi      *self,
                                                  const char     *next_batch);
void          matrix_api_set_sync_callback       (MatrixApi      *self,
                                                  MatrixCallback  callback,
                                                  gpointer        object);
void          matrix_api_start_sync              (MatrixApi      *self);
void          matrix_api_stop_sync               (MatrixApi      *self);
void          matrix_api_set_upload_key          (MatrixApi      *self,
                                                  char           *key);
void          matrix_api_set_typing              (MatrixApi      *self,
                                                  const char     *room_id,
                                                  gboolean        is_typing);
void          matrix_api_get_room_state_async    (MatrixApi      *self,
                                                  const char     *room_id,
                                                  GAsyncReadyCallback callback,
                                                  gpointer        user_data);
JsonArray    *matrix_api_get_room_state_finish   (MatrixApi      *self,
                                                  GAsyncResult   *result,
                                                  GError        **error);
void          matrix_api_get_room_name_async     (MatrixApi      *self,
                                                  const char     *room_id,
                                                  GAsyncReadyCallback callback,
                                                  gpointer        user_data);
JsonObject   *matrix_api_get_room_name_finish    (MatrixApi      *self,
                                                  GAsyncResult   *result,
                                                  GError        **error);
void          matrix_api_get_members_async       (MatrixApi      *self,
                                                  const char *room_id,
                                                  GAsyncReadyCallback callback,
                                                  gpointer        user_data);
JsonObject   *matrix_api_get_members_finish      (MatrixApi      *self,
                                                  GAsyncResult   *result,
                                                  GError        **error);
void         matrix_api_load_prev_batch_async    (MatrixApi      *self,
                                                  const char     *room_id,
                                                  char           *prev_batch,
                                                  char           *last_batch,
                                                  GAsyncReadyCallback callback,
                                                  gpointer        user_data);
JsonObject   *matrix_api_load_prev_batch_finish  (MatrixApi      *self,
                                                  GAsyncResult   *result,
                                                  GError        **error);
void          matrix_api_query_keys_async        (MatrixApi      *self,
                                                  GListModel     *members_list,
                                                  const char     *token,
                                                  GAsyncReadyCallback callback,
                                                  gpointer        user_data);
JsonObject   *matrix_api_query_keys_finish       (MatrixApi      *self,
                                                  GAsyncResult   *result,
                                                  GError        **error);
void          matrix_api_claim_keys_async        (MatrixApi      *self,
                                                  GListModel     *member_list,
                                                  GAsyncReadyCallback callback,
                                                  gpointer        user_data);
JsonObject   *matrix_api_claim_keys_finish       (MatrixApi      *self,
                                                  GAsyncResult   *result,
                                                  GError        **error);
void           matrix_api_get_file_async         (MatrixApi      *self,
                                                  ChattyMessage  *message,
                                                  ChattyFileInfo *file,
                                                  GCancellable   *cancellable,
                                                  GFileProgressCallback progress_callback,
                                                  GAsyncReadyCallback callback,
                                                  gpointer        user_data);
gboolean     matrix_api_get_file_finish          (MatrixApi      *self,
                                                  GAsyncResult   *result,
                                                  GError        **error);
void          matrix_api_send_file_async         (MatrixApi      *self,
                                                  ChattyChat     *chat,
                                                  const char     *file_name,
                                                  GCancellable   *cancellable,
                                                  GAsyncReadyCallback callback,
                                                  gpointer        user_data);
gboolean     matrix_api_send_file_finish         (MatrixApi      *self,
                                                  GAsyncResult   *result,
                                                  GError        **error);
void         matrix_api_send_message_async       (MatrixApi      *self,
                                                  ChattyChat     *chat,
                                                  const char     *room_id,
                                                  ChattyMessage  *message,
                                                  GAsyncReadyCallback callback,
                                                  gpointer        user_data);
gboolean     matrix_api_send_message_finish      (MatrixApi      *self,
                                                  GAsyncResult   *result,
                                                  GError        **error);
void         matrix_api_set_read_marker_async    (MatrixApi      *self,
                                                  const char     *room_id,
                                                  ChattyMessage  *message,
                                                  GAsyncReadyCallback callback,
                                                  gpointer        user_data);
gboolean    matrix_api_set_read_marker_finish    (MatrixApi      *self,
                                                  GAsyncResult   *result,
                                                  GError        **error);
void         matrix_api_upload_group_keys_async  (MatrixApi      *self,
                                                  const char     *room_id,
                                                  GListModel     *member_list,
                                                  GAsyncReadyCallback callback,
                                                  gpointer        user_data);
gboolean     matrix_api_upload_group_keys_finish (MatrixApi      *self,
                                                  GAsyncResult   *result,
                                                  GError        **error);
void         matrix_api_leave_chat_async         (MatrixApi      *self,
                                                  const char     *room_id,
                                                  GAsyncReadyCallback callback,
                                                  gpointer        user_data);
gboolean     matrix_api_leave_chat_finish        (MatrixApi      *self,
                                                  GAsyncResult   *result,
                                                  GError        **error);
void         matrix_api_get_user_info_async      (MatrixApi      *self,
                                                  const char     *user_id,
                                                  GCancellable   *cancellable,
                                                  GAsyncReadyCallback callback,
                                                  gpointer        user_data);
gboolean     matrix_api_get_user_info_finish     (MatrixApi      *self,
                                                  char         **name,
                                                  char         **avatar_url,
                                                  GAsyncResult   *result,
                                                  GError        **error);
void         matrix_api_set_name_async           (MatrixApi      *self,
                                                  const char     *name,
                                                  GCancellable   *cancellable,
                                                  GAsyncReadyCallback callback,
                                                  gpointer        user_data);
gboolean     matrix_api_set_name_finish          (MatrixApi      *self,
                                                  GAsyncResult   *result,
                                                  GError        **error);
void         matrix_api_get_3pid_async           (MatrixApi      *self,
                                                  GCancellable   *cancellable,
                                                  GAsyncReadyCallback callback,
                                                  gpointer        user_data);
gboolean     matrix_api_get_3pid_finish          (MatrixApi      *self,
                                                  GPtrArray     **emails,
                                                  GPtrArray     **phones,
                                                  GAsyncResult   *result,
                                                  GError        **error);
void         matrix_api_delete_3pid_async        (MatrixApi      *self,
                                                  const char     *value,
                                                  ChattyIdType    type,
                                                  GCancellable   *cancellable,
                                                  GAsyncReadyCallback callback,
                                                  gpointer        user_data);
gboolean     matrix_api_delete_3pid_finish       (MatrixApi      *self,
                                                  GAsyncResult   *result,
                                                  GError        **error);

G_END_DECLS
