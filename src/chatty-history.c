/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-history.c
 *
 * Copyright 2018,2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-history"

#define CHAT_ID_IDX         1
#define CHAT_TIMESTAMP_IDX  2
#define CHAT_DIRECTION_IDX  3
#define CHAT_ACCOUNT_IDX    4
#define CHAT_ROOM_IDX       5
#define CHAT_WHO_IDX        6
#define CHAT_UID_IDX        7
#define CHAT_MESSAGE_IDX    8

#define IM_ID_IDX         1
#define IM_TIMESTAMP_IDX  2
#define IM_DIRECTION_IDX  3
#define IM_ACCOUNT_IDX    4
#define IM_WHO_IDX        5
#define IM_UID_IDX        6
#define IM_MESSAGE_IDX    7

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sqlite3.h>

#include "chatty-utils.h"
#include "chatty-history.h"

struct _ChattyHistory
{
  GObject      parent_instance;

  GAsyncQueue *queue;
  GThread     *worker_thread;
  sqlite3     *db;
};

/*
 * ChattyHistory->db should never be accessed nor modified in main thread
 * except for checking if it’s %NULL.  Any operation should be done only
 * in @worker_thread.  Don't reuse the same #ChattyHistory once closed.
 */

typedef void (*ChattyCallback) (ChattyHistory *self,
                                GTask *task);

G_DEFINE_TYPE (ChattyHistory, chatty_history, G_TYPE_OBJECT)

static ChattyMsgDirection
history_direction_from_value (int direction)
{
  if (direction == 1)
    return CHATTY_DIRECTION_IN;

  if (direction == -1)
    return CHATTY_DIRECTION_OUT;

  if (direction == 0)
    return CHATTY_DIRECTION_SYSTEM;

  g_return_val_if_reached (CHATTY_DIRECTION_UNKNOWN);
}

static int
history_direction_to_value (ChattyMsgDirection direction)
{
  switch (direction) {
  case CHATTY_DIRECTION_IN:
    return 1;

  case CHATTY_DIRECTION_OUT:
    return -1;

  case CHATTY_DIRECTION_SYSTEM:
    return 0;

  case CHATTY_DIRECTION_UNKNOWN:
  default:
    g_return_val_if_reached (0);
  }
}

static void
warn_if_sql_error (int         status,
                   const char *message)
{
  if (status == SQLITE_OK || status == SQLITE_ROW || status == SQLITE_DONE)
    return;

  g_warning ("Error %s. errno: %d, message: %s", message, status, sqlite3_errstr (status));
}

static void
history_bind_text (sqlite3_stmt *statement,
                   guint         position,
                   const char   *bind_value,
                   const char   *message)
{
  guint status;

  status = sqlite3_bind_text (statement, position, bind_value, -1, SQLITE_TRANSIENT);
  warn_if_sql_error (status, message);
}

static void
history_bind_int (sqlite3_stmt *statement,
                  guint         position,
                  int           bind_value,
                  const char   *message)
{
  guint status;

  status = sqlite3_bind_int (statement, position, bind_value);
  warn_if_sql_error (status, message);
}

static int
chatty_history_create_chat_schema (ChattyHistory *self,
                                   GTask         *task)
{
  const char *sql;
  char *error = NULL;
  int status;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  sql = "CREATE TABLE IF NOT EXISTS chatty_chat("  \
    "id                 INTEGER     NOT NULL    PRIMARY KEY AUTOINCREMENT," \
    "timestamp          INTEGER     NOT_NULL," \
    "direction          INTEGER     NOT NULL," \
    "account            TEXT        NOT NULL," \
    "room               TEXT        NOT_NULL," \
    "who                TEXT,"  \
    "uid                TEXT        NOT_NULL," \
    "message            TEXT," \
    "UNIQUE (timestamp, message)"
    ");";

  status = sqlite3_exec (self->db, sql, NULL, NULL, &error);

  if (status != SQLITE_OK) {
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Error creating chatty_im table. errno: %d, desc: %s. %s",
                             status, sqlite3_errstr (status), error);
    return status;
  }

  // The archiving entity is room jid, uid may only be unique within entity scope
  sql = "CREATE UNIQUE INDEX IF NOT EXISTS chatty_chat_room_uid ON chatty_chat(room, uid);";
  status = sqlite3_exec (self->db, sql, NULL, NULL, &error);

  if (status != SQLITE_OK) {
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Error creating chatty_im table. errno: %d, desc: %s. %s",
                             status, sqlite3_errstr (status), error);
  }

  return status;
}


static int
chatty_history_create_im_schema (ChattyHistory *self,
                                 GTask         *task)
{
  const char *sql;
  char *error = NULL;
  int status;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  sql = "CREATE TABLE IF NOT EXISTS chatty_im("  \
    "id                 INTEGER     NOT NULL    PRIMARY KEY AUTOINCREMENT," \
    "timestamp          INTEGER     NOT_NULL,"  \
    "direction          INTEGER     NOT NULL," \
    "account            TEXT        NOT_NULL," \
    "who                TEXT        NOT_NULL," \
    "uid                TEXT        NOT_NULL," \
    "message            TEXT,"                 \
    "UNIQUE (timestamp, message)"
    ");";

  status = sqlite3_exec (self->db, sql, NULL, NULL, &error);

  if (status != SQLITE_OK) {
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Error creating chatty_im table. errno: %d, desc: %s. %s",
                             status, sqlite3_errstr (status), error);
    return status;
  }

  // The archiving entity is bare jid, uid may only be unique within entity scope
  sql = "CREATE UNIQUE INDEX IF NOT EXISTS chatty_im_acc_uid ON chatty_im(account, uid);";
  status = sqlite3_exec (self->db, sql, NULL, NULL, &error);

  if (status != SQLITE_OK) {
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Error creating chatty_im table. errno: %d, desc: %s. %s",
                             status, sqlite3_errstr (status), error);
  }

  return status;
}

static void
history_open_db (ChattyHistory *self,
                 GTask         *task)
{
  const char *dir, *file_name;
  g_autofree char *db_path = NULL;
  sqlite3 *db;
  int status;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (!self->db);

  dir = g_object_get_data (G_OBJECT (task), "dir");
  file_name = g_object_get_data (G_OBJECT (task), "file-name");
  g_assert (dir && *dir);
  g_assert (file_name && *file_name);

  g_mkdir_with_parents (dir, S_IRWXU);
  db_path = g_build_filename (dir, file_name, NULL);

  status = sqlite3_open (db_path, &db);

  if (status == SQLITE_OK) {
    self->db = db;
    status = chatty_history_create_chat_schema (self, task);

    if (status == SQLITE_OK)
      status = chatty_history_create_im_schema (self, task);

    if (status == SQLITE_OK)
      g_task_return_boolean (task, TRUE);
  } else {
    sqlite3_close (db);
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Database could not be opened. errno: %d, desc: %s",
                             status, sqlite3_errstr (status));
  }
}

static void
history_close_db (ChattyHistory *self,
                  GTask         *task)
{
  sqlite3 *db;
  int status;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  db = self->db;
  self->db = NULL;
  status = sqlite3_close (db);

  if (status == SQLITE_OK) {
    /*
     * We can’t know when will @self associated with the task will
     * be unref.  So chatty_history_get_default() called immediately
     * after this may return the @self that is yet to be free.  But
     * as the worker_thread is exited after closing the database, any
     * actions with the same @self will not execute, and so the tasks
     * will take ∞ time to complete.
     *
     * So Instead of relying on GObject to free the object, Let’s
     * explicitly run dispose
     */
    g_object_run_dispose (G_OBJECT (self));
    g_object_unref (self);
    g_debug ("Database closed successfully");
    g_task_return_boolean (task, TRUE);
  } else {
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Database could not be closed. errno: %d, desc: %s",
                             status, sqlite3_errmsg (db));
  }
}

static guint
get_id_for_message (ChattyHistory *self,
                    ChattyChat    *chat,
                    ChattyMessage *message)
{
  const char *uuid, *who;
  sqlite3_stmt *stmt;
  int status;
  guint id = INT_MAX;

  if (!CHATTY_IS_MESSAGE (message) || !CHATTY_IS_CHAT (chat))
    return id;

  g_assert (g_thread_self () == self->worker_thread);

  uuid = chatty_message_get_uid (message);

  if (!uuid || !*uuid)
    return id;

  if (chatty_chat_is_im (chat))
    who = chatty_chat_get_username (chat);
  else
    who = chatty_chat_get_chat_name (chat);

  if (chatty_chat_is_im (chat))
    status = sqlite3_prepare_v2 (self->db, "SELECT id FROM chatty_im WHERE uid=(?) AND account=(?)", -1, &stmt, NULL);
  else
    status = sqlite3_prepare_v2 (self->db, "SELECT id FROM chatty_chat WHERE uid=(?) AND room=(?)", -1, &stmt, NULL);

  warn_if_sql_error (status, "preparing to get id");

  history_bind_text (stmt, 1, uuid, "binding when getting id");
  history_bind_text (stmt, 2, who, "binding when getting id");

  status = sqlite3_step (stmt);
  warn_if_sql_error (status, "finding message for id");
  if (status == SQLITE_ROW)
    id = sqlite3_column_int (stmt, 0);

  status = sqlite3_finalize (stmt);
  warn_if_sql_error (status, "finalizing when getting id");

  return id;
}

static GPtrArray *
get_messages_before_id (ChattyHistory *self,
                        ChattyChat    *chat,
                        guint          since_id,
                        guint          limit)
{
  GPtrArray *messages = NULL;
  const char *chat_name, *account;
  sqlite3_stmt *stmt;
  int status;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (CHATTY_IS_CHAT (chat));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (limit != 0);

  account = chatty_chat_get_username (chat);
  chat_name = chatty_chat_get_chat_name (chat);

  if (chatty_chat_is_im (chat))
    status = sqlite3_prepare_v2 (self->db, "SELECT timestamp,direction,message,uid FROM chatty_im "
                                 "WHERE account=(?) AND who=(?) AND id < (?) AND message != \"\" ORDER BY timestamp DESC, id DESC LIMIT (?)", -1, &stmt, NULL);
  else
    status = sqlite3_prepare_v2 (self->db, "SELECT timestamp,direction,message,uid,who FROM chatty_chat "
                                 "WHERE account=(?) AND room=(?) AND id < (?) AND message != \"\" ORDER BY timestamp DESC, id DESC LIMIT (?)", -1, &stmt, NULL);

  warn_if_sql_error (status, "preparing to get messages");

  history_bind_text (stmt, 1, account, "binding when getting messages");
  history_bind_text (stmt, 2, chat_name, "binding when getting messages");
  history_bind_int (stmt, 3, since_id, "binding when getting messages");
  history_bind_int (stmt, 4, limit, "binding when getting messages");

  while (sqlite3_step (stmt) == SQLITE_ROW) {
    ChattyMessage *message;
    const char *msg, *uid;
    const char *who = NULL;
    guint time_stamp;
    int direction;

    if (!messages)
      messages = g_ptr_array_new_full (30, g_object_unref);

    time_stamp = sqlite3_column_int (stmt, 0);
    direction = sqlite3_column_int (stmt, 1);
    msg = (const char *)sqlite3_column_text (stmt, 2);
    uid = (const char *)sqlite3_column_text (stmt, 3);

    if (!chatty_chat_is_im (chat))
      who = (const char *)sqlite3_column_text (stmt, 4);

    message = chatty_message_new (NULL, who, msg, uid, time_stamp,
                                  history_direction_from_value (direction), 0);
    g_ptr_array_insert (messages, 0, message);
  }

  status = sqlite3_finalize (stmt);
  warn_if_sql_error (status, "finalizing when getting messages");

  return messages;
}

static void
history_get_messages (ChattyHistory *self,
                      GTask         *task)
{
  GPtrArray *messages;
  ChattyMessage *start;
  ChattyChat *chat;
  guint since_id;
  guint limit;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);

  if (!self->db) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Error: Database not opened");
    return;
  }

  limit = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "limit"));
  start = g_object_get_data (G_OBJECT (task), "message");
  chat  = g_object_get_data (G_OBJECT (task), "chat");

  g_assert (limit != 0);
  g_assert (!start || CHATTY_IS_MESSAGE (start));
  g_assert (CHATTY_IS_CHAT (chat));

  since_id = get_id_for_message (self, chat, start);
  messages = get_messages_before_id (self, chat, since_id, limit);
  g_task_return_pointer (task, messages, (GDestroyNotify)g_ptr_array_unref);
}

static void
history_add_message (ChattyHistory *self,
                     GTask         *task)
{
  ChattyMessage *message;
  ChattyChat *chat;
  sqlite3_stmt *stmt;
  const char *account, *who, *uid, *msg;
  ChattyMsgDirection direction;
  int status, dir;
  time_t time_stamp;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);

  if (!self->db) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Error: Database not opened");
    return;
  }

  chat = g_object_get_data (G_OBJECT (task), "chat");
  message = g_object_get_data (G_OBJECT (task), "message");
  g_assert (CHATTY_IS_CHAT (chat));
  g_assert (CHATTY_IS_MESSAGE (message));

  account = chatty_chat_get_username (chat);
  who = chatty_message_get_user_name (message);
  uid = chatty_message_get_uid (message);
  msg = chatty_message_get_text (message);
  time_stamp = chatty_message_get_time (message);
  direction = chatty_message_get_msg_direction (message);
  dir = history_direction_to_value (direction);

  if (chatty_chat_is_im (chat))
    status = sqlite3_prepare_v2 (self->db, "INSERT INTO chatty_im VALUES (?, ?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);
  else
    status = sqlite3_prepare_v2 (self->db, "INSERT INTO chatty_chat VALUES (?, ?, ?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);

  warn_if_sql_error (status, "preparing statement when adding message");

  if (chatty_chat_is_im (chat)) {
    history_bind_text (stmt, IM_ACCOUNT_IDX, account, "binding when adding IM message");
    history_bind_text (stmt, IM_WHO_IDX, who, "binding when adding IM message");
    history_bind_text (stmt, IM_UID_IDX, uid, "binding when adding IM message");
    history_bind_text (stmt, IM_MESSAGE_IDX, msg, "binding when adding IM message");
    history_bind_int (stmt, IM_TIMESTAMP_IDX, time_stamp, "binding when adding IM message");
    history_bind_int (stmt, IM_DIRECTION_IDX, dir, "binding when adding IM message");
  } else {
    const char *room;

    room = chatty_chat_get_chat_name (chat);
    history_bind_text (stmt, CHAT_ACCOUNT_IDX, account, "binding when adding CHAT message");
    history_bind_text (stmt, CHAT_ROOM_IDX, room, "binding when adding CHAT message");
    history_bind_text (stmt, CHAT_WHO_IDX, who, "binding when adding CHAT message");
    history_bind_text (stmt, CHAT_UID_IDX, uid, "binding when adding CHAT message");
    history_bind_text (stmt, CHAT_MESSAGE_IDX, msg, "binding when adding CHAT message");
    history_bind_int (stmt, CHAT_TIMESTAMP_IDX, time_stamp, "binding when adding CHAT message");
    history_bind_int (stmt, CHAT_DIRECTION_IDX, dir, "binding when adding CHAT message");
  }

  status = sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  if (status == SQLITE_DONE)
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Error: Failed to save message. errno: %d, desc: %s",
                             status, sqlite3_errstr (status));
}

static void
history_delete_chat (ChattyHistory *self,
                     GTask         *task)
{
  ChattyChat *chat;
  sqlite3_stmt *stmt;
  const char *account, *chat_name;
  int status;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);

  if (!self->db) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Error: Database not opened");
    return;
  }

  chat = g_object_get_data (G_OBJECT (task), "chat");
  g_assert (CHATTY_IS_CHAT (chat));

  chat_name = chatty_chat_get_chat_name (chat);

  if (!chat_name || !*chat_name) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Error: chat name is empty");
    return;
  }

  account = chatty_chat_get_username (chat);

  if (chatty_chat_is_im (chat))
    status = sqlite3_prepare_v2 (self->db, "DELETE FROM chatty_im WHERE account=(?) AND who=(?)", -1, &stmt, NULL);
  else
    status = sqlite3_prepare_v2 (self->db, "DELETE FROM chatty_chat WHERE account=(?) AND room=(?)", -1, &stmt, NULL);

  warn_if_sql_error (status, "preparing statement when deleting chat");

  history_bind_text (stmt, 1, account, "binding when deleting message");
  history_bind_text (stmt, 2, chat_name, "binding when deleting message");

  status = sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  if (status == SQLITE_DONE)
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Error: Failed to delete chat. errno: %d, desc: %s",
                             status, sqlite3_errstr (status));
}

static void
history_get_chat_timestamp (ChattyHistory *self,
                            GTask         *task)
{
  sqlite3_stmt *stmt;
  const char *uuid, *room;
  int status, timestamp = INT_MAX;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));

  status = sqlite3_prepare_v2 (self->db, "SELECT timestamp FROM chatty_chat WHERE uid=(?) AND room=(?) LIMIT 1", -1, &stmt, NULL);
  warn_if_sql_error (status, "preparing statement when getting timestamp");

  uuid = g_object_get_data (G_OBJECT (task), "uuid");
  room = g_object_get_data (G_OBJECT (task), "room");

  g_assert (uuid);
  g_assert (room);

  history_bind_text (stmt, 1, uuid, "binding when getting timestamp");
  history_bind_text (stmt, 2, room, "binding when getting timestamp");

  if (sqlite3_step (stmt) == SQLITE_ROW)
    timestamp = sqlite3_column_int (stmt, 0);

  status = sqlite3_finalize (stmt);
  warn_if_sql_error (status, "finalizing when getting timestamp");

  g_task_return_int (task, timestamp);
}

static void
history_get_im_timestamp (ChattyHistory *self,
                          GTask         *task)
{
  sqlite3_stmt *stmt;
  const char *uuid, *account;
  int status, timestamp = INT_MAX;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));

  status = sqlite3_prepare_v2 (self->db, "SELECT timestamp FROM chatty_im WHERE uid=(?) AND account=(?) LIMIT 1", -1, &stmt, NULL);
  warn_if_sql_error (status, "preparing statement when getting timestamp");

  uuid = g_object_get_data (G_OBJECT (task), "uuid");
  account = g_object_get_data (G_OBJECT (task), "account");

  history_bind_text (stmt, 1, uuid, "binding when getting timestamp");
  history_bind_text (stmt, 2, account, "binding when getting timestamp");

  if (sqlite3_step (stmt) == SQLITE_ROW)
    timestamp = sqlite3_column_int (stmt, 0);

  status = sqlite3_finalize (stmt);
  warn_if_sql_error (status, "finalizing when getting timestamp");

  g_task_return_int (task, timestamp);
}

static void
history_get_last_message_time (ChattyHistory *self,
                               GTask         *task)
{
  sqlite3_stmt *stmt;
  const char *account, *room;
  int status, timestamp = 0;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));

  status = sqlite3_prepare_v2 (self->db, "SELECT max(timestamp),max(id) FROM chatty_chat WHERE account=(?) AND room=(?)", -1, &stmt, NULL);
  warn_if_sql_error (status, "preparing statement when getting timestamp");

  account = g_object_get_data (G_OBJECT (task), "account");
  room = g_object_get_data (G_OBJECT (task), "room");

  history_bind_text (stmt, 1, account, "binding when getting timestamp");
  history_bind_text (stmt, 2, room, "binding when getting timestamp");

  if (sqlite3_step (stmt) == SQLITE_ROW)
    timestamp = sqlite3_column_int (stmt, 0);

  status = sqlite3_finalize (stmt);
  warn_if_sql_error (status, "finalizing when getting timestamp");

  g_task_return_int (task, timestamp);
}

static void
history_exists (ChattyHistory *self,
                GTask         *task)
{
  sqlite3_stmt *stmt;
  const char *account, *who, *room;
  int status;
  gboolean found = FALSE;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));

  account = g_object_get_data (G_OBJECT (task), "account");
  room = g_object_get_data (G_OBJECT (task), "room");
  who = g_object_get_data (G_OBJECT (task), "who");

  g_assert (account);
  g_assert (room || who);

  if (who)
    status = sqlite3_prepare_v2 (self->db, "SELECT timestamp FROM chatty_im WHERE account=(?) AND who=(?) LIMIT 1", -1, &stmt, NULL);
  else
    status = sqlite3_prepare_v2 (self->db, "SELECT timestamp FROM chatty_chat WHERE account=(?) AND room=(?) LIMIT 1", -1, &stmt, NULL);

  warn_if_sql_error (status, "preparing statement when getting timestamp");

  history_bind_text (stmt, 1, account, "binding when getting timestamp");

  if (who)
    history_bind_text (stmt, 2, who, "binding when getting timestamp");
  else
    history_bind_text (stmt, 2, room, "binding when getting timestamp");

  if (sqlite3_step (stmt) == SQLITE_ROW)
    found = TRUE;

  status = sqlite3_finalize (stmt);
  warn_if_sql_error (status, "finalizing when getting timestamp");

  g_task_return_boolean (task, found);
}

static void
history_add_raw_message (ChattyHistory *self,
                         GTask         *task)
{
  const char *account, *room, *who, *msg, *uid;
  sqlite3_stmt *stmt;
  int status, time_stamp, direction;
  PurpleConversationType type;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));

  if (!self->db) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Error: Database not opened");
    return;
  }

  account = g_object_get_data (G_OBJECT (task), "account");
  room = g_object_get_data (G_OBJECT (task), "room");
  type = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "type"));
  who = g_object_get_data (G_OBJECT (task), "who");
  msg = g_object_get_data (G_OBJECT (task), "message");
  uid = g_object_get_data (G_OBJECT (task), "uid");
  time_stamp = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "time"));
  direction = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "dir"));

  g_assert (account);
  g_assert (uid);

  if (type == PURPLE_CONV_TYPE_CHAT)
    status = sqlite3_prepare_v2 (self->db, "INSERT INTO chatty_chat VALUES (?, ?, ?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);
  else
    status = sqlite3_prepare_v2 (self->db, "INSERT INTO chatty_im VALUES (?, ?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);

  warn_if_sql_error (status, "preparing statement when adding raw message");

  if (type == PURPLE_CONV_TYPE_CHAT) {
    history_bind_text (stmt, CHAT_ACCOUNT_IDX, account, "binding when adding CHAT message");
    history_bind_text (stmt, CHAT_ROOM_IDX, room, "binding when adding CHAT message");
    history_bind_text (stmt, CHAT_WHO_IDX, who, "binding when adding CHAT message");
    history_bind_text (stmt, CHAT_UID_IDX, uid, "binding when adding CHAT message");
    history_bind_text (stmt, CHAT_MESSAGE_IDX, msg, "binding when adding CHAT message");
    history_bind_int (stmt, CHAT_TIMESTAMP_IDX, time_stamp, "binding when adding CHAT message");
    history_bind_int (stmt, CHAT_DIRECTION_IDX, direction, "binding when adding CHAT message");
  } else {
    history_bind_text (stmt, IM_ACCOUNT_IDX, account, "binding when adding IM message");
    history_bind_text (stmt, IM_WHO_IDX, who, "binding when adding IM message");
    history_bind_text (stmt, IM_UID_IDX, uid, "binding when adding IM message");
    history_bind_text (stmt, IM_MESSAGE_IDX, msg, "binding when adding IM message");
    history_bind_int (stmt, IM_TIMESTAMP_IDX, time_stamp, "binding when adding IM message");
    history_bind_int (stmt, IM_DIRECTION_IDX, direction, "binding when adding IM message");
  }

  status = sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  g_task_return_boolean (task, status == SQLITE_DONE);
}

static gpointer
chatty_history_worker (gpointer user_data)
{
  ChattyHistory *self = user_data;
  GTask *task;

  g_assert (CHATTY_IS_HISTORY (self));

  while ((task = g_async_queue_pop (self->queue))) {
    ChattyCallback callback;

    g_assert (task);
    callback = g_task_get_task_data (task);
    callback (self, task);
    g_object_unref (task);

    if (callback == history_close_db)
      break;
  }

  return NULL;
}

static void
chatty_history_dispose (GObject *object)
{
  ChattyHistory *self = (ChattyHistory *)object;

  g_clear_pointer (&self->worker_thread, g_thread_unref);

  G_OBJECT_CLASS (chatty_history_parent_class)->dispose (object);
}

static void
chatty_history_finalize (GObject *object)
{
  ChattyHistory *self = (ChattyHistory *)object;

  if (self->db)
    g_warning ("Database not closed");

  g_clear_pointer (&self->queue, g_async_queue_unref);

  G_OBJECT_CLASS (chatty_history_parent_class)->finalize (object);
}

static void
chatty_history_class_init (ChattyHistoryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose  = chatty_history_dispose;
  object_class->finalize = chatty_history_finalize;
}

static void
chatty_history_init (ChattyHistory *self)
{
  self->queue = g_async_queue_new ();
}

/**
 * chatty_history_get_default:
 *
 * Get the default #ChattyHistory
 *
 * Returns: (transfer none): A #ChattyHistory
 */
ChattyHistory *
chatty_history_get_default (void)
{
  static ChattyHistory *self;

  if (!self) {
    self = g_object_new (CHATTY_TYPE_HISTORY, NULL);
    g_object_add_weak_pointer (G_OBJECT (self), (gpointer *)&self);
  }

  return self;
}

/**
 * chatty_history_open_async:
 * @self: a #ChattyHistory
 * @dir: (transfer full): The database directory
 * @file_name: The file name of database
 * @callback: a #GAsyncReadyCallback, or %NULL
 * @user_data: closure data for @callback
 *
 * Open the database file @file_name from path @dir.
 * Complete with chatty_history_open_finish() to get
 * the result.
 */
void
chatty_history_open_async (ChattyHistory       *self,
                           char                *dir,
                           const char          *file_name,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (CHATTY_IS_HISTORY (self));
  g_return_if_fail (dir || !*dir);
  g_return_if_fail (file_name || !*file_name);

  if (self->db) {
    g_warning ("A DataBase is already open");
    return;
  }

  if (!self->worker_thread)
    self->worker_thread = g_thread_new ("chatty-history-worker",
                                        chatty_history_worker,
                                        self);

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, chatty_history_open_async);
  g_task_set_task_data (task, history_open_db, NULL);
  g_object_set_data_full (G_OBJECT (task), "dir", dir, g_free);
  g_object_set_data_full (G_OBJECT (task), "file-name", g_strdup (file_name), g_free);

  g_async_queue_push (self->queue, task);
}

/**
 * chatty_history_open_finish:
 * @self: a #ChattyHistory
 * @result: a #GAsyncResult provided to callback
 * @error: a location for a #GError or %NULL
 *
 * Completes opening a database started with
 * chatty_history_open_async().
 *
 * Returns: %TRUE if database was opened successfully.
 * %FALSE otherwise with @error set.
 */
gboolean
chatty_history_open_finish (ChattyHistory  *self,
                            GAsyncResult   *result,
                            GError        **error)
{
  g_return_val_if_fail (CHATTY_IS_HISTORY (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * chatty_history_is_open:
 * @self: a #ChattyHistory
 *
 * Get if the database is open or not
 *
 * Returns: %TRUE if a database is open.
 * %FALSE otherwise.
 */
gboolean
chatty_history_is_open (ChattyHistory *self)
{
  g_return_val_if_fail (CHATTY_IS_HISTORY (self), FALSE);

  return !!self->db;
}

/**
 * chatty_history_close_async:
 * @self: a #ChattyHistory
 * @callback: a #GAsyncReadyCallback, or %NULL
 * @user_data: closure data for @callback
 *
 * Close the database opened.
 * Complete with chatty_history_close_finish() to get
 * the result.
 */
void
chatty_history_close_async (ChattyHistory       *self,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (CHATTY_IS_HISTORY (self));

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, chatty_history_close_async);
  g_task_set_task_data (task, history_close_db, NULL);

  g_async_queue_push (self->queue, task);
}

/**
 * chatty_history_open_finish:
 * @self: a #ChattyHistory
 * @result: a #GAsyncResult provided to callback
 * @error: a location for a #GError or %NULL
 *
 * Completes closing a database started with
 * chatty_history_close_async().  @self is
 * g_object_unref() if closing succeeded.
 * So @self will be freed if you haven’t kept
 * your own reference on @self.
 *
 * Returns: %TRUE if database was closed successfully.
 * %FALSE otherwise with @error set.
 */
gboolean
chatty_history_close_finish (ChattyHistory  *self,
                             GAsyncResult   *result,
                             GError        **error)
{
  g_return_val_if_fail (CHATTY_IS_HISTORY (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * chatty_history_get_messages_async:
 * @self: a #ChattyHistory
 * @chat: a #ChattyChat to get messages
 * @start: (nullable): A #ChattyMessage
 * @limit: a non-zero number
 * @callback: a #GAsyncReadyCallback, or %NULL
 * @user_data: closure data for @callback
 *
 * Load @limit number of messages before @start
 * for the given @chat.
 * Finish with chatty_history_get_messages_finish()
 * to get the result.
 */
void
chatty_history_get_messages_async  (ChattyHistory       *self,
                                    ChattyChat          *chat,
                                    ChattyMessage       *start,
                                    guint                limit,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (CHATTY_IS_HISTORY (self));
  g_return_if_fail (CHATTY_IS_CHAT (chat));
  g_return_if_fail (limit != 0);

  if (start)
    g_object_ref (start);

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, chatty_history_get_messages_async);
  g_task_set_task_data (task, history_get_messages, NULL);
  g_object_set_data_full (G_OBJECT (task), "chat", g_object_ref (chat), g_object_unref);
  g_object_set_data_full (G_OBJECT (task), "message", start, g_object_unref);
  g_object_set_data (G_OBJECT (task), "limit", GINT_TO_POINTER (limit));

  g_async_queue_push (self->queue, task);
}

/**
 * chatty_history_get_messages_finish:
 * @self: a #ChattyHistory
 * @result: a #GAsyncResult provided to callback
 * @error: a location for a #GError or %NULL
 *
 * Completes chatty_history_get_messages_async() call.
 *
 * Returns: (element-type #ChattyMessage) (transfer full):
 * An array of #ChattyMessage or %NULL if no messages
 * available or on error.  Free with g_ptr_array_unref()
 * or similar.
 */
GPtrArray *
chatty_history_get_messages_finish (ChattyHistory  *self,
                                    GAsyncResult   *result,
                                    GError        **error)
{
  g_return_val_if_fail (CHATTY_IS_HISTORY (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_pointer (G_TASK (result), error);
}

/**
 * chatty_history_add_message_async:
 * @self: a #ChattyHistory
 * @chat: the #ChattyChat @message belongs to
 * @message: A #ChattyMessage
 * @callback: a #GAsyncReadyCallback, or %NULL
 * @user_data: closure data for @callback
 *
 * Store @message content to database.
 */
void
chatty_history_add_message_async (ChattyHistory       *self,
                                  ChattyChat          *chat,
                                  ChattyMessage       *message,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (CHATTY_IS_HISTORY (self));
  g_return_if_fail (CHATTY_IS_CHAT (chat));
  g_return_if_fail (CHATTY_IS_MESSAGE (message));

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, chatty_history_add_message_async);
  g_task_set_task_data (task, history_add_message, NULL);
  g_object_set_data_full (G_OBJECT (task), "chat", g_object_ref (chat), g_object_unref);
  g_object_set_data_full (G_OBJECT (task), "message", g_object_ref (message), g_object_unref);

  g_async_queue_push (self->queue, task);
}

/**
 * chatty_history_add_message_finish:
 * @self: a #ChattyHistory
 * @result: a #GAsyncResult provided to callback
 * @error: a location for a #GError or %NULL
 *
 * Completes chatty_history_add_message_async() call.
 *
 * Returns: %TRUE if saving message succeeded.  %FALSE
 * otherwise with @error set.
 */
gboolean
chatty_history_add_message_finish  (ChattyHistory  *self,
                                    GAsyncResult   *result,
                                    GError        **error)
{
  g_return_val_if_fail (CHATTY_IS_HISTORY (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * chatty_history_delete_chat_async:
 * @self: a #ChattyHistory
 * @chat: the #ChattyChat to delete
 * @callback: a #GAsyncReadyCallback, or %NULL
 * @user_data: closure data for @callback
 *
 * Delete all messages belonging to @chat from
 * database.  To get the result, finish with
 * chatty_history_delete_chat_finish()
 */
void
chatty_history_delete_chat_async (ChattyHistory       *self,
                                  ChattyChat          *chat,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (CHATTY_IS_HISTORY (self));
  g_return_if_fail (CHATTY_IS_CHAT (chat));

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, chatty_history_delete_chat_async);
  g_task_set_task_data (task, history_delete_chat, NULL);
  g_object_set_data_full (G_OBJECT (task), "chat", g_object_ref (chat), g_object_unref);

  g_async_queue_push (self->queue, task);
}

/**
 * chatty_history_delete_chat_finish:
 * @self: a #ChattyHistory
 * @result: a #GAsyncResult provided to callback
 * @error: a location for a #GError or %NULL
 *
 * Completes chatty_history_delete_chat_async() call.
 *
 * Returns: %TRUE if saving message succeeded.  %FALSE
 * otherwise with @error set.
 */
gboolean
chatty_history_delete_chat_finish (ChattyHistory  *self,
                                   GAsyncResult   *result,
                                   GError        **error)
{
  g_return_val_if_fail (CHATTY_IS_HISTORY (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
finish_cb (GObject      *object,
           GAsyncResult *result,
           gpointer      user_data)
{
  g_autoptr(GError) error = NULL;

  g_task_propagate_boolean (G_TASK (result), &error);

  if (error)
    g_warning ("Error: %s", error->message);

  g_task_return_boolean (G_TASK (user_data), !error);
}

/**
 * chatty_history_open:
 * @dir: The database directory
 * @file_name: The file name of database
 *
 * Open the database file @file_name from path @dir
 * with the default #ChattyHistory.
 *
 * This method runs synchronously.
 *
 *
 */
void
chatty_history_open (const char *dir,
                     const char *file_name)
{
  ChattyHistory *self;
  g_autoptr(GTask) task = NULL;

  self = chatty_history_get_default ();
  task = g_task_new (NULL, NULL, NULL, NULL);
  chatty_history_open_async (self, g_strdup (dir), file_name, finish_cb, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);
}

/**
 * chatty_history_close:
 *
 * Close database opened with default #ChattyHistory,
 * if any.
 *
 * This method runs synchronously.
 *
 */
void
chatty_history_close (void)
{
  ChattyHistory *self;
  g_autoptr(GTask) task = NULL;

  self = chatty_history_get_default ();

  if (!self->db)
    return;

  task = g_task_new (NULL, NULL, NULL, NULL);
  chatty_history_close_async (self, finish_cb, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);
}

/**
 * chatty_history_get_chat_timestamp:
 * @uuid: A valid uid string
 * @room: A valid chat room name.
 *
 * Get the timestamp for the message matching
 * @uuid and @room, if any.
 *
 * This method runs synchronously.
 *
 * Returns: the timestamp for the matching message.
 * or %INT_MAX if no match found.
 */
int
chatty_history_get_chat_timestamp (const char *uuid,
                                   const char *room)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = NULL;
  ChattyHistory *self;
  int time_stamp;

  g_return_val_if_fail (uuid, 0);
  g_return_val_if_fail (room, 0);

  self = chatty_history_get_default ();
  g_return_val_if_fail (self->db, FALSE);

  task = g_task_new (NULL, NULL, NULL, NULL);
  g_object_ref (task);
  g_task_set_task_data (task, history_get_chat_timestamp, NULL);
  g_object_set_data_full (G_OBJECT (task), "uuid", g_strdup (uuid), g_free);
  g_object_set_data_full (G_OBJECT (task), "room", g_strdup (room), g_free);

  g_async_queue_push (self->queue, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  time_stamp = g_task_propagate_int (task, &error);

  if (error) {
    g_warning ("Error: %s", error->message);
    return INT_MAX;
  }

  return time_stamp;
}

/**
 * chatty_history_get_im_timestamp:
 * @uuid: A valid uid string
 * @account: A valid user id name.
 *
 * Get the timestamp for the IM message matching
 * @uuid and @account, if any.
 *
 * This method runs synchronously.
 *
 * Returns: the timestamp for the matching message.
 * or %INT_MAX if no match found.
 */
int
chatty_history_get_im_timestamp (const char *uuid,
                                 const char *account)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = NULL;
  ChattyHistory *self;
  int time_stamp;

  g_return_val_if_fail (uuid, 0);
  g_return_val_if_fail (account, 0);

  self = chatty_history_get_default ();
  g_return_val_if_fail (self->db, FALSE);

  task = g_task_new (NULL, NULL, NULL, NULL);
  g_object_ref (task);
  g_task_set_task_data (task, history_get_im_timestamp, NULL);
  g_object_set_data_full (G_OBJECT (task), "uuid", g_strdup (uuid), g_free);
  g_object_set_data_full (G_OBJECT (task), "account", g_strdup (account), g_free);

  g_async_queue_push (self->queue, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  time_stamp = g_task_propagate_int (task, &error);

  if (error) {
    g_warning ("Error: %s", error->message);
    return INT_MAX;
  }

  return time_stamp;
}

/**
 * chatty_history_get_last_message_time:
 * @account: A valid account name
 * @roome: A valid room name.
 *
 * Get the timestamp of the last message in @room
 * with the account @account.
 *
 * This method runs synchronously.
 *
 * Returns: The timestamp of the last matching message
 * or 0 if no match found.
 */
int
chatty_history_get_last_message_time (const char *account,
                                      const char *room)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = NULL;
  ChattyHistory *self;
  int time_stamp;

  g_return_val_if_fail (account, 0);
  g_return_val_if_fail (room, 0);

  self = chatty_history_get_default ();
  g_return_val_if_fail (self->db, 0);

  task = g_task_new (NULL, NULL, NULL, NULL);
  g_object_ref (task);
  g_task_set_task_data (task, history_get_last_message_time, NULL);

  g_object_set_data_full (G_OBJECT (task), "account", g_strdup (account), g_free);
  g_object_set_data_full (G_OBJECT (task), "room", g_strdup (room), g_free);

  g_async_queue_push (self->queue, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  time_stamp = g_task_propagate_int (task, &error);

  if (error) {
    g_warning ("Error: %s", error->message);
    return 0;
  }

  return time_stamp;
}

/**
 * chatty_history_delete_chat:
 * @chat: a #ChattyChat
 *
 * Delete all messages matching @chat
 * from default #ChattyHistory.
 *
 * This method runs synchronously.
 *
 */
void
chatty_history_delete_chat (ChattyChat *chat)
{
  ChattyHistory *self;
  g_autoptr(GTask) task = NULL;

  self = chatty_history_get_default ();
  task = g_task_new (NULL, NULL, NULL, NULL);
  chatty_history_delete_chat_async (self, chat, finish_cb, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);
}

static gboolean
chatty_history_exists (const char *account,
                       const char *room,
                       const char *who)
{

  ChattyHistory *self;
  g_autoptr(GTask) task = NULL;

  self = chatty_history_get_default ();
  g_return_val_if_fail (self->db, FALSE);

  task = g_task_new (NULL, NULL, NULL, NULL);
  g_object_ref (task);
  g_task_set_task_data (task, history_exists, NULL);
  g_object_set_data_full (G_OBJECT (task), "account", g_strdup (account), g_free);
  g_object_set_data_full (G_OBJECT (task), "room", g_strdup (room), g_free);
  g_object_set_data_full (G_OBJECT (task), "who", g_strdup (who), g_free);

  g_async_queue_push (self->queue, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  return g_task_propagate_boolean (task, NULL);
}

/**
 * chatty_history_im_exists:
 * @account: a valid account name
 * @who: A valid user name
 *
 * Get if atleast one message exists for
 * the given IM.
 *
 * This method runs synchronously.
 *
 * Return: %TRUE if atleast one message exists
 * for the given detail.  %FALSE otherwise.
 */
gboolean
chatty_history_im_exists (const char *account,
                          const char *who)
{
  g_return_val_if_fail (account, 0);
  g_return_val_if_fail (who, 0);

  return chatty_history_exists (account, NULL, who);
}

/**
 * chatty_history_exists:
 * @account: a valid account name
 * @room: A Valid room name
 *
 * Get if atleast one message exists for
 * the given chat.
 *
 * This method runs synchronously.
 *
 * Return: %TRUE if atleast one message exists
 * for the given detail.  %FALSE otherwise.
 */
gboolean
chatty_history_chat_exists (const char *account,
                            const char *room)
{
  g_return_val_if_fail (account, 0);
  g_return_val_if_fail (room, 0);

  return chatty_history_exists (account, room, NULL);
}

/**
 * chatty_history_add_message:
 * @account: a valid account name
 * @room: A room name or %NULL for IM chats
 * @who: A user name or %NULL
 * @message The chat message to store
 * @uid: (inout): A pointer to uid string for the @message
 * @flags: A #PurpleMessageFlags
 * @time_stamp: A unix time
 * @type: A #PurpleConversationType
 *
 * Store a message to database.  If @flags has
 * %PURPLE_MESSAGE_NO_LOG set, this method returns
 * without saving.  If @uid points to %NULL, an
 * RFC 4122 version 4 random UUID will generated for
 * you.
 *
 * This method runs synchronously.
 *
 * Return: %TRUE if the message was stored to database.
 * %FALSE otherwise.
 */
gboolean
chatty_history_add_message (const char              *account,
                            const char              *room,
                            const char              *who,
                            const char              *message,
                            char                   **uid,
                            PurpleMessageFlags       flags,
                            time_t                   time_stamp,
                            PurpleConversationType   type)
{
  ChattyHistory *self;
  g_autoptr(GTask) task = NULL;
  int dir = 0;

  g_return_val_if_fail (account, FALSE);
  g_return_val_if_fail (uid, FALSE);

  if (type == PURPLE_CONV_TYPE_CHAT)
    g_return_val_if_fail (room, FALSE);

  if (type == PURPLE_CONV_TYPE_IM)
    g_return_val_if_fail (who, FALSE);

  self = chatty_history_get_default ();
  g_return_val_if_fail (self->db, FALSE);

  /* Don’t save if marked so */
  if (flags & PURPLE_MESSAGE_NO_LOG)
    return FALSE;

  /* direction of message */
  if (flags & PURPLE_MESSAGE_SYSTEM)
    dir = 0;
  else if (flags & PURPLE_MESSAGE_RECV)
    dir = 1;
  else if (flags & PURPLE_MESSAGE_SEND)
    dir = -1;

  if (!*uid)
    *uid = g_uuid_string_random ();

  task = g_task_new (NULL, NULL, NULL, NULL);
  g_object_ref (task);
  g_task_set_task_data (task, history_add_raw_message, NULL);
  g_object_set_data_full (G_OBJECT (task), "account", g_strdup (account), g_free);
  g_object_set_data_full (G_OBJECT (task), "room", g_strdup (room), g_free);
  g_object_set_data_full (G_OBJECT (task), "who", g_strdup (who), g_free);
  g_object_set_data_full (G_OBJECT (task), "message", g_strdup (message), g_free);
  g_object_set_data_full (G_OBJECT (task), "uid", g_strdup (*uid), g_free);
  g_object_set_data (G_OBJECT (task), "dir", GINT_TO_POINTER (dir));
  g_object_set_data (G_OBJECT (task), "time", GINT_TO_POINTER (time_stamp));
  g_object_set_data (G_OBJECT (task), "type", GINT_TO_POINTER (type));

  g_async_queue_push (self->queue, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  return g_task_propagate_boolean (task, NULL);
}
