/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* matrix-db.h
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
#include "chatty-message.h"

G_BEGIN_DECLS

typedef struct {
  char *sender;
  char *sender_key;
  char *session_pickle;
} MatrixDbData;

/* These values shouldn’t be changed. They are used in DB */
typedef enum {
  SESSION_OLM_V1_IN      = 1,
  SESSION_OLM_V1_OUT     = 2,
  SESSION_MEGOLM_V1_IN   = 3,
  SESSION_MEGOLM_V1_OUT  = 4,
} MatrixSessionType;

#define CHATTY_ALGORITHM_A256CTR 1

#define CHATTY_KEY_TYPE_OCT      1

#define MATRIX_TYPE_DB (matrix_db_get_type ())

G_DECLARE_FINAL_TYPE (MatrixDb, matrix_db, MATRIX, DB, GObject)

MatrixDb      *matrix_db_new                           (void);
void           matrix_db_open_async                    (MatrixDb        *self,
                                                        char            *dir,
                                                        const char      *file_name,
                                                        GAsyncReadyCallback callback,
                                                        gpointer         user_data);
gboolean       matrix_db_open_finish                   (MatrixDb        *self,
                                                        GAsyncResult    *result,
                                                        GError         **error);
gboolean       matrix_db_is_open                       (MatrixDb        *self);
void           matrix_db_close_async                   (MatrixDb        *self,
                                                        GAsyncReadyCallback callback,
                                                        gpointer        user_data);
gboolean       matrix_db_close_finish                  (MatrixDb        *self,
                                                        GAsyncResult    *result,
                                                        GError         **error);
void           matrix_db_save_account_async            (MatrixDb        *db,
                                                        ChattyAccount   *account,
                                                        gboolean         enabled,
                                                        char            *pickle,
                                                        const char      *device_id,
                                                        const char      *next_batch,
                                                        GAsyncReadyCallback callback,
                                                        gpointer         user_data);
gboolean       matrix_db_save_account_finish           (MatrixDb        *self,
                                                        GAsyncResult    *result,
                                                        GError         **error);
void           matrix_db_load_account_async            (MatrixDb        *db,
                                                        ChattyAccount   *account,
                                                        const char      *device_id,
                                                        GAsyncReadyCallback callback,
                                                        gpointer         user_data);
gboolean       matrix_db_load_account_finish           (MatrixDb        *self,
                                                        GAsyncResult    *result,
                                                        GError         **error);
void           matrix_db_save_room_async               (MatrixDb        *self,
                                                        ChattyAccount   *account,
                                                        const char      *account_device,
                                                        const char      *room_id,
                                                        const char      *prev_batch,
                                                        GAsyncReadyCallback callback,
                                                        gpointer         user_data);
gboolean       matrix_db_save_room_finish              (MatrixDb        *self,
                                                        GAsyncResult    *result,
                                                        GError         **error);
void           matrix_db_load_room_async               (MatrixDb        *self,
                                                        ChattyAccount   *account,
                                                        const char      *account_device,
                                                        const char      *room_id,
                                                        GAsyncReadyCallback callback,
                                                        gpointer         user_data);
char          *matrix_db_load_room_finish              (MatrixDb        *self,
                                                        GAsyncResult    *result,
                                                        GError         **error);
void           matrix_db_delete_account_async          (MatrixDb        *self,
                                                        ChattyAccount   *account,
                                                        GAsyncReadyCallback callback,
                                                        gpointer         user_data);
gboolean       matrix_db_delete_account_finish         (MatrixDb        *self,
                                                        GAsyncResult    *result,
                                                        GError         **error);
void           matrix_db_save_file_url_async           (MatrixDb        *self,
                                                        ChattyMessage   *message,
                                                        ChattyFileInfo  *file,
                                                        int              version,
                                                        int              algorithm,
                                                        int              type,
                                                        gboolean         extractable,
                                                        GAsyncReadyCallback callback,
                                                        gpointer         user_data);
gboolean       matrix_db_save_file_url_finish          (MatrixDb        *self,
                                                        GAsyncResult    *result,
                                                        GError         **error);
void           matrix_db_add_session_async             (MatrixDb        *self,
                                                        const char      *account_id,
                                                        const char      *account_device,
                                                        const char      *room_id,
                                                        const char      *session_id,
                                                        const char      *sender_key,
                                                        char            *pickle,
                                                        MatrixSessionType type,
                                                        GAsyncReadyCallback callback,
                                                        gpointer         user_data);
gboolean       matrix_db_add_session_finish            (MatrixDb        *self,
                                                        GAsyncResult    *result,
                                                        GError         **error);
char          *matrix_db_lookup_session                (MatrixDb        *self,
                                                        const char      *account_id,
                                                        const char      *account_device,
                                                        const char      *session_id,
                                                        const char      *sender_key,
                                                        MatrixSessionType type);
void           matrix_db_get_olm_sessions_async        (MatrixDb        *self,
                                                        GAsyncReadyCallback callback,
                                                        gpointer         user_data);
GPtrArray     *matrix_db_get_olm_sessions_finish       (MatrixDb        *self,
                                                        GAsyncResult    *result,
                                                        GError         **error);
void           matrix_db_add_olm_session_async         (MatrixDb        *self,
                                                        const char      *sender,
                                                        const char      *sender_key,
                                                        const char      *pickle,
                                                        GAsyncReadyCallback callback,
                                                        gpointer         user_data);
GPtrArray     *matrix_db_get_messages_finish           (MatrixDb        *self,
                                                        GAsyncResult    *result,
                                                        GError         **error);
void           matrix_db_get_group_in_sessions_async   (MatrixDb        *self,
                                                        guint            limit,
                                                        GAsyncReadyCallback callback,
                                                        gpointer         user_data);
GPtrArray     *matrix_db_group_in_sessions_finish      (MatrixDb        *self,
                                                        GAsyncResult    *result,
                                                        GError         **error);
void           matrix_db_get_group_out_sessions_async  (MatrixDb        *self,
                                                        guint            limit,
                                                        GAsyncReadyCallback callback,
                                                        gpointer         user_data);
GPtrArray     *matrix_db_get_group_out_sessions_finish (MatrixDb        *self,
                                                        GAsyncResult    *result,
                                                        GError         **error);

G_END_DECLS
