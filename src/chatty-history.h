/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-history.h
 *
 * Copyright 2018,2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib.h>

#include "chatty-chat.h"
#include "chatty-message.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_HISTORY (chatty_history_get_type ())

G_DECLARE_FINAL_TYPE (ChattyHistory, chatty_history, CHATTY, HISTORY, GObject)

ChattyHistory *chatty_history_get_default         (void);
void           chatty_history_open_async          (ChattyHistory        *self,
                                                   char                 *dir,
                                                   const char           *file_name,
                                                   GAsyncReadyCallback   callback,
                                                   gpointer              user_data);
gboolean       chatty_history_open_finish         (ChattyHistory        *self,
                                                   GAsyncResult         *result,
                                                   GError              **error);
gboolean       chatty_history_is_open             (ChattyHistory        *self);
void           chatty_history_close_async         (ChattyHistory        *self,
                                                   GAsyncReadyCallback   callback,
                                                   gpointer              user_data);
gboolean       chatty_history_close_finish        (ChattyHistory        *self,
                                                   GAsyncResult         *result,
                                                   GError              **error);
void           chatty_history_get_messages_async  (ChattyHistory        *self,
                                                   ChattyChat           *chat,
                                                   ChattyMessage        *start,
                                                   guint                 limit,
                                                   GAsyncReadyCallback   callback,
                                                   gpointer              user_data);
GPtrArray     *chatty_history_get_messages_finish (ChattyHistory        *self,
                                                   GAsyncResult         *result,
                                                   GError              **error);
void           chatty_history_add_message_async   (ChattyHistory        *self,
                                                   ChattyChat           *chat,
                                                   ChattyMessage        *message,
                                                   GAsyncReadyCallback   callback,
                                                   gpointer              user_data);
gboolean       chatty_history_add_message_finish  (ChattyHistory        *self,
                                                   GAsyncResult         *result,
                                                   GError              **error);
void           chatty_history_delete_chat_async   (ChattyHistory        *self,
                                                   ChattyChat           *chat,
                                                   GAsyncReadyCallback   callback,
                                                   gpointer              user_data);
gboolean       chatty_history_delete_chat_finish  (ChattyHistory        *self,
                                                   GAsyncResult         *result,
                                                   GError              **error);

/* old APIs */
void           chatty_history_open                (const char            *dir,
                                                   const char            *file_name);
void           chatty_history_close               (void);
int            chatty_history_get_chat_timestamp  (const char            *uuid,
                                                   const char            *room);
int            chatty_history_get_im_timestamp    (const char            *uuid,
                                                   const char            *account);
int            chatty_history_get_last_message_time (const char            *account,
                                                     const char            *room);
void           chatty_history_delete_chat         (ChattyChat            *chat);
gboolean       chatty_history_im_exists           (const char            *account,
                                                   const char            *who);
gboolean       chatty_history_chat_exists         (const char            *account,
                                                   const char            *room);
gboolean       chatty_history_add_message         (ChattyChat            *chat,
                                                   ChattyMessage         *message);

G_END_DECLS
