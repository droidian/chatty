/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-pp-account.h
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
#include <purple.h>

#include "chatty-item.h"
#include "chatty-pp-buddy.h"
#include "chatty-account.h"
#include "chatty-enums.h"

G_BEGIN_DECLS

typedef enum _ChattyPpAccountFeatures {
  CHATTY_PP_ACCOUNT_FEATURES_NONE = 0,
  CHATTY_PP_ACCOUNT_FEATURES_CSI  = 1 << 0,
} ChattyPpAccountFeatures;

#define CHATTY_TYPE_PP_ACCOUNT (chatty_pp_account_get_type ())

G_DECLARE_FINAL_TYPE (ChattyPpAccount, chatty_pp_account, CHATTY, PP_ACCOUNT, ChattyAccount)

ChattyPpAccount *chatty_pp_account_get_object         (PurpleAccount   *account);
ChattyPpAccount *chatty_pp_account_new                (ChattyProtocol   protocol,
                                                       const char      *username,
                                                       const char      *server_url,
                                                       gboolean         has_encryption);
ChattyPpAccount *chatty_pp_account_new_purple         (PurpleAccount   *account,
                                                       gboolean         has_encryption);
ChattyPpBuddy   *chatty_pp_account_add_buddy          (ChattyPpAccount *self,
                                                       const char      *username,
                                                       const char      *name);
ChattyPpBuddy   *chatty_pp_account_add_purple_buddy   (ChattyPpAccount *self,
                                                       PurpleBuddy     *pp_buddy);
ChattyChat      *chatty_pp_account_join_chat          (ChattyPpAccount *self,
                                                       const char      *room_id,
                                                       const char      *room_alias,
                                                       const char      *user_alias,
                                                       const char      *password);

PurpleAccount   *chatty_pp_account_get_account        (ChattyPpAccount *self);
PurpleStatus    *chatty_pp_account_get_active_status  (ChattyPpAccount *self);

void             chatty_pp_account_set_features         (ChattyPpAccount *self,
                                                         ChattyPpAccountFeatures features);
void             chatty_pp_account_update_features      (ChattyPpAccount *self,
                                                         ChattyPpAccountFeatures features);
gboolean         chatty_pp_account_has_features         (ChattyPpAccount *self,
                                                         ChattyPpAccountFeatures features);
ChattyPpAccountFeatures chatty_pp_account_get_features  (ChattyPpAccount *self);

G_END_DECLS
