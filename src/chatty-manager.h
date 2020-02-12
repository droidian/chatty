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

G_BEGIN_DECLS

#define CHATTY_TYPE_MANAGER (chatty_manager_get_type ())

G_DECLARE_FINAL_TYPE (ChattyManager, chatty_manager, CHATTY, MANAGER, GObject)

ChattyManager  *chatty_manager_get_default        (void);
void            chatty_manager_purple_init        (ChattyManager *self);
GListModel     *chatty_manager_get_accounts       (ChattyManager *self);
GListModel     *chatty_manager_get_contact_list      (ChattyManager *self);
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
ChattyFolks    *chatty_manager_get_folks              (ChattyManager   *self);
void            chatty_manager_update_node            (ChattyManager   *self,
                                                       PurpleBlistNode *node);
void            chatty_manager_remove_node            (ChattyManager   *self,
                                                       PurpleBlistNode *node);

G_END_DECLS
