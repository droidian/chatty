/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-manager.h
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

#include "users/chatty-pp-account.h"
#include "chatty-contact-provider.h"
#include "chatty-chat.h"

G_BEGIN_DECLS

#define CHATTY_APP_ID       "sm.puri.Chatty"
#define CHATTY_PREFS_ROOT   "/chatty"

typedef enum
{
  CHATTY_SMS_RECEIPT_NONE      = -1,
  CHATTY_SMS_RECEIPT_MM_ACKN   =  0,
  CHATTY_SMS_RECEIPT_SMSC_ACKN,
} e_sms_receipt_states;

#define CHATTY_TYPE_MANAGER (chatty_manager_get_type ())

G_DECLARE_FINAL_TYPE (ChattyManager, chatty_manager, CHATTY, MANAGER, GObject)

ChattyManager  *chatty_manager_get_default        (void);
void            chatty_manager_purple_init        (ChattyManager *self);
void            chatty_manager_purple             (ChattyManager *self);
GListModel     *chatty_manager_get_accounts       (ChattyManager *self);
GListModel     *chatty_manager_get_contact_list      (ChattyManager *self);
GListModel     *chatty_manager_get_chat_list         (ChattyManager *self);
void            chatty_manager_disable_auto_login    (ChattyManager *self,
                                                      gboolean       disable);
gboolean        chatty_manager_get_disable_auto_login (ChattyManager *self);
gboolean        chatty_manager_is_account_supported (ChattyManager   *self,
                                                     ChattyPpAccount *account);

void            chatty_manager_load_plugins           (ChattyManager   *self);
void            chatty_manager_load_buddies           (ChattyManager   *self);
gboolean        chatty_manager_has_carbons_plugin     (ChattyManager   *self);
gboolean        chatty_manager_has_file_upload_plugin (ChattyManager   *self);
gboolean        chatty_manager_lurch_plugin_is_loaded (ChattyManager   *self);
ChattyProtocol  chatty_manager_get_active_protocols   (ChattyManager   *self);
ChattyEds      *chatty_manager_get_eds                (ChattyManager   *self);
void            chatty_manager_update_node            (ChattyManager   *self,
                                                       PurpleBlistNode *node);
void            chatty_manager_delete_conversation    (ChattyManager      *self,
                                                       PurpleConversation *conv);
ChattyChat     *chatty_manager_add_chat               (ChattyManager      *self,
                                                       ChattyChat         *chat);
void            chatty_manager_load_more_chat         (ChattyManager      *self,
                                                       ChattyChat         *chat,
                                                       guint               limit);
void            chatty_conv_im_with_buddy             (PurpleAccount      *account,
                                                       const char         *username);
void            chatty_conv_join_chat                 (PurpleChat         *chat);

G_END_DECLS
