/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* matrix-enc.h
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <json-glib/json-glib.h>
#include <glib-object.h>

#include "chatty-chat.h"

G_BEGIN_DECLS

typedef struct _MatrixFileEncInfo MatrixFileEncInfo;

struct _MatrixFileEncInfo {
  guchar *aes_iv;
  guchar *aes_key;
  guchar *sha256;

  char *aes_iv_base64;
  char *aes_key_base64;
  char *sha256_base64;

  gsize aes_iv_len;
  gsize aes_key_len;
  gsize sha256_len;
};

#define ALGORITHM_MEGOLM  "m.megolm.v1.aes-sha2"
#define ALGORITHM_OLM     "m.olm.v1.curve25519-aes-sha2"
#define CURVE25519_SIZE   43    /* when base64 encoded */
#define ED25519_SIZE      43    /* when base64 encoded */

#define MATRIX_TYPE_ENC (matrix_enc_get_type ())

G_DECLARE_FINAL_TYPE (MatrixEnc, matrix_enc, MATRIX, ENC, GObject)

MatrixEnc     *matrix_enc_new                        (gpointer      matrix_db,
                                                      const char   *pickle,
                                                      const char   *key);
void           matrix_enc_set_details                (MatrixEnc    *self,
                                                      const char   *user_id,
                                                      const char   *device_id);
char          *matrix_enc_get_account_pickle         (MatrixEnc    *self);
char          *matrix_enc_get_pickle_key             (MatrixEnc    *self);
char          *matrix_enc_sign_string                (MatrixEnc    *self,
                                                      const char   *str,
                                                      size_t        len);
gboolean       matrix_enc_verify                     (MatrixEnc    *self,
                                                      JsonObject   *object,
                                                      const char   *matrix_id,
                                                      const char   *device_id,
                                                      const char   *ed_key);
size_t         matrix_enc_max_one_time_keys          (MatrixEnc    *self);
size_t         matrix_enc_create_one_time_keys       (MatrixEnc    *self,
                                                      size_t        count);
void           matrix_enc_publish_one_time_keys      (MatrixEnc    *self);
JsonObject    *matrix_enc_get_one_time_keys          (MatrixEnc    *self);
char          *matrix_enc_get_one_time_keys_json     (MatrixEnc    *self);
char          *matrix_enc_get_device_keys_json       (MatrixEnc    *self);
void           matrix_enc_handle_room_encrypted      (MatrixEnc    *self,
                                                      JsonObject   *object);
char          *matrix_enc_handle_join_room_encrypted (MatrixEnc    *self,
                                                      const char   *room_id,
                                                      JsonObject   *object);
JsonObject    *matrix_enc_encrypt_for_chat           (MatrixEnc    *self,
                                                      const char   *room_id,
                                                      const char   *message);
JsonObject    *matrix_enc_create_out_group_keys      (MatrixEnc    *self,
                                                      const char   *room_id,
                                                      GListModel   *members_list);
void           matrix_file_enc_info_free             (MatrixFileEncInfo *enc_info);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MatrixFileEncInfo, matrix_file_enc_info_free)

/* For tests */
const char    *matrix_enc_get_curve25519_key     (MatrixEnc    *self);
const char    *matrix_enc_get_ed25519_key        (MatrixEnc    *self);

G_END_DECLS
