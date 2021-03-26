/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-message.h
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

#include "users/chatty-pp-buddy.h"
#include "chatty-enums.h"

G_BEGIN_DECLS

typedef struct _ChattyFileInfo ChattyFileInfo;

struct _ChattyFileInfo {
  char *file_name;
  char *url;
  char *path;
  char *mime_type;
  gpointer user_data;
  gsize width;
  gsize height;
  gsize size;
  /* For audio files */
  gsize duration;
  int status;
};

#define CHATTY_TYPE_MESSAGE (chatty_message_get_type ())

G_DECLARE_FINAL_TYPE (ChattyMessage, chatty_message, CHATTY, MESSAGE, GObject)

ChattyMessage      *chatty_message_new             (ChattyItem         *user,
                                                    const char         *user_alias,
                                                    const char         *message,
                                                    const char         *uid,
                                                    time_t              time,
                                                    ChattyMsgType       type,
                                                    ChattyMsgDirection  direction,
                                                    ChattyMsgStatus     status);
ChattyMessage      *chatty_message_new_purple      (ChattyPpBuddy      *buddy,
                                                    PurpleConvMessage  *message);

gboolean            chatty_message_get_encrypted   (ChattyMessage      *self);
void                chatty_message_set_encrypted   (ChattyMessage      *self,
                                                    gboolean            is_encrypted);

ChattyFileInfo     *chatty_message_get_file        (ChattyMessage      *self);
void                chatty_message_set_file        (ChattyMessage      *self,
                                                    ChattyFileInfo     *file);

GList              *chatty_message_get_files       (ChattyMessage      *self);
void                chatty_message_set_files       (ChattyMessage      *self,
                                                    GList              *files);
void                chatty_message_add_file_from_path (ChattyMessage   *self,
                                                       const char      *file_path);
ChattyFileInfo     *chatty_message_get_preview     (ChattyMessage      *self);
void                chatty_message_set_preview     (ChattyMessage      *self,
                                                    ChattyFileInfo     *info);

const char         *chatty_message_get_uid         (ChattyMessage      *self);
void                chatty_message_set_uid         (ChattyMessage      *self,
                                                    const char         *uid);
const char         *chatty_message_get_id          (ChattyMessage      *self);
void                chatty_message_set_id          (ChattyMessage      *self,
                                                    const char         *id);
guint               chatty_message_get_sms_id      (ChattyMessage      *self);
void                chatty_message_set_sms_id      (ChattyMessage      *self,
                                                    guint               id);
const char         *chatty_message_get_text        (ChattyMessage      *self);
void                chatty_message_set_user        (ChattyMessage      *self,
                                                    ChattyItem         *sender);
ChattyItem         *chatty_message_get_user        (ChattyMessage      *self);
const char         *chatty_message_get_user_name   (ChattyMessage      *self);
void                chatty_message_set_user_name   (ChattyMessage      *self,
                                                    const char         *user_name);
const char         *chatty_message_get_user_alias  (ChattyMessage      *self);
time_t              chatty_message_get_time        (ChattyMessage      *self);
ChattyMsgStatus     chatty_message_get_status      (ChattyMessage      *self);
void                chatty_message_set_status      (ChattyMessage      *self,
                                                    ChattyMsgStatus     status,
                                                    time_t              mtime);
ChattyMsgType       chatty_message_get_msg_type    (ChattyMessage      *self);
ChattyMsgDirection  chatty_message_get_msg_direction (ChattyMessage    *self);
void                chatty_file_info_free          (ChattyFileInfo *file_info);

G_END_DECLS
