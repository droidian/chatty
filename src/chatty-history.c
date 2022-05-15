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

#ifndef G_LOG_DOMAIN
#define G_LOG_DOMAIN "chatty-history"
#endif

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sqlite3.h>

#include "chatty-ma-account.h"
#include "chatty-ma-chat.h"

#include "chatty-utils.h"
#include "chatty-settings.h"
#include "chatty-mm-account.h"
#include "chatty-mm-buddy.h"
#include "chatty-mm-chat.h"
#include "chatty-history.h"

/* Used as placeholders until we get the real values */
#define MM_NUMBER "invalid-0000000000000000"

#define STRING(arg) STRING_VALUE(arg)
#define STRING_VALUE(arg) #arg

/* increment when DB changes */
#define HISTORY_VERSION 4

/* Shouldn't be modified, new values should be appended */
#define MESSAGE_DIRECTION_OUT    -1
#define MESSAGE_DIRECTION_SYSTEM  0
#define MESSAGE_DIRECTION_IN      1

/* Shouldn't be modified, new values should be appended */
#define CHATTY_ID_UNKNOWN_VALUE 0
#define CHATTY_ID_PHONE_VALUE   1
#define CHATTY_ID_EMAIL_VALUE   2
#define CHATTY_ID_XMPP_VALUE    3
#define CHATTY_ID_MATRIX_VALUE  4
#define CHATTY_ID_SIP_VALUE     5
#define CHATTY_ID_ICCID_VALUE   6

/* Shouldn't be modified, new values should be appended */
#define PROTOCOL_UNKNOWN   0
#define PROTOCOL_MMS_SMS   1
#define PROTOCOL_MMS       2
#define PROTOCOL_XMPP      3
#define PROTOCOL_MATRIX    4
#define PROTOCOL_TELEGRAM  5
#define PROTOCOL_SIP       6

#define MM_PROTOCOL_UNKNOWN  0
#define MM_PROTOCOL_MMS      1
#define MM_PROTOCOL_SMS      1

/* Chat thread type */
/* For SMS/MMS, if it's THREAD_GROUP_CHAT it's always MMS,
 * If it's THREAD_DIRECT_CHAT with 2+ members, it's always
 * SMS.
 */
#define THREAD_DIRECT_CHAT 0
#define THREAD_GROUP_CHAT  1

/* Chat thread visibility */
#define THREAD_VISIBILITY_VISIBLE  0
#define THREAD_VISIBILITY_HIDDEN   1
#define THREAD_VISIBILITY_ARCHIVED 2
#define THREAD_VISIBILITY_BLOCKED  3

/* Shouldn't be modified, new values should be appended */
#define MESSAGE_TYPE_UNKNOWN       0
#define MESSAGE_TYPE_TEXT          1
#define MESSAGE_TYPE_HTML_ESCAPED  2
#define MESSAGE_TYPE_HTML          3
#define MESSAGE_TYPE_MATRIX_HTML   4
#define MESSAGE_TYPE_URL           6
#define MESSAGE_TYPE_LOCATION      7
#define MESSAGE_TYPE_FILE          8
#define MESSAGE_TYPE_IMAGE         9
#define MESSAGE_TYPE_VIDEO         10
#define MESSAGE_TYPE_AUDIO         11
/* An MMS message may have several files of different type */
#define MESSAGE_TYPE_MMS           12
/* Temporary until the link is parsed */
#define MESSAGE_TYPE_LINK          20

#define FILE_STATUS_DOWNLOADED     1
#define FILE_STATUS_MISSING        2
#define FILE_STATUS_DECRYPT_FAILED 3

/* Shouldn't be modified, new values should be appended */
#define MESSAGE_STATUS_UNKNOWN          0
#define MESSAGE_STATUS_DRAFT            1
#define MESSAGE_STATUS_RECEIVED         2
#define MESSAGE_STATUS_SENT             3
#define MESSAGE_STATUS_DELIVERED        4
#define MESSAGE_STATUS_READ             5
#define MESSAGE_STATUS_SENDING_FAILED   6
#define MESSAGE_STATUS_DELIVERY_FAILED  7

struct _ChattyHistory
{
  GObject      parent_instance;
  GAsyncQueue *queue;
  GThread     *worker_thread;
  sqlite3     *db;
  char        *db_path;
};

/*
 * ChattyHistory->db should never be accessed nor modified in main thread
 * except for checking if it’s %NULL.  Any operation should be done only
 * in @worker_thread.  Don't reuse the same #ChattyHistory once closed.
 */

typedef void (*ChattyCallback) (ChattyHistory *self,
                                GTask *task);
static int     add_file_info   (ChattyHistory  *self,
                                ChattyFileInfo *file);

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

static int
history_protocol_to_value (ChattyProtocol protocol)
{
  switch (protocol) {
  case CHATTY_PROTOCOL_MMS_SMS:
    return PROTOCOL_MMS_SMS;

  case CHATTY_PROTOCOL_MMS:
    return PROTOCOL_MMS;

  case CHATTY_PROTOCOL_MATRIX:
    return PROTOCOL_MATRIX;

  case CHATTY_PROTOCOL_XMPP:
    return PROTOCOL_XMPP;

  case CHATTY_PROTOCOL_TELEGRAM:
    return PROTOCOL_TELEGRAM;

  case CHATTY_PROTOCOL_ANY:
  case CHATTY_PROTOCOL_NONE:
  case CHATTY_PROTOCOL_CALL:
  case CHATTY_PROTOCOL_EMAIL:
  case CHATTY_PROTOCOL_DELTA:
  case CHATTY_PROTOCOL_THREEPL:
  default:
    g_return_val_if_reached (0);
  }
}

static int
history_protocol_to_type_value (ChattyProtocol protocol)
{
  switch (protocol) {
  case CHATTY_PROTOCOL_MMS_SMS:
  case CHATTY_PROTOCOL_MMS:
  case CHATTY_PROTOCOL_TELEGRAM:
    return CHATTY_ID_PHONE_VALUE;

  case CHATTY_PROTOCOL_MATRIX:
    return CHATTY_ID_MATRIX_VALUE;

  case CHATTY_PROTOCOL_XMPP:
    return CHATTY_ID_XMPP_VALUE;

  case CHATTY_PROTOCOL_CALL:
  case CHATTY_PROTOCOL_ANY:
  case CHATTY_PROTOCOL_NONE:
  case CHATTY_PROTOCOL_EMAIL:
  case CHATTY_PROTOCOL_DELTA:
  case CHATTY_PROTOCOL_THREEPL:
  default:
    g_return_val_if_reached (0);
  }
}

static int
history_message_type_to_value (ChattyMsgType type)
{
  switch (type) {
  case CHATTY_MESSAGE_UNKNOWN:
    return MESSAGE_TYPE_UNKNOWN;

  case CHATTY_MESSAGE_TEXT:
    return MESSAGE_TYPE_TEXT;

  case CHATTY_MESSAGE_HTML:
  case CHATTY_MESSAGE_HTML_ESCAPED:
    return MESSAGE_TYPE_HTML_ESCAPED;

  case CHATTY_MESSAGE_MATRIX_HTML:
    return MESSAGE_TYPE_MATRIX_HTML;

  case CHATTY_MESSAGE_LOCATION:
    return MESSAGE_TYPE_LOCATION;

  case CHATTY_MESSAGE_FILE:
    return MESSAGE_TYPE_FILE;

  case CHATTY_MESSAGE_IMAGE:
    return MESSAGE_TYPE_IMAGE;

  case CHATTY_MESSAGE_AUDIO:
    return MESSAGE_TYPE_AUDIO;

  case CHATTY_MESSAGE_VIDEO:
    return MESSAGE_TYPE_VIDEO;

  case CHATTY_MESSAGE_MMS:
    return MESSAGE_TYPE_MMS;

  default:
    g_return_val_if_reached (MESSAGE_TYPE_HTML);
  }
}

static ChattyMsgType
history_value_to_message_type (int value)
{
  if (value == MESSAGE_TYPE_UNKNOWN)
    return CHATTY_MESSAGE_UNKNOWN;
  else if (value == MESSAGE_TYPE_TEXT)
    return CHATTY_MESSAGE_TEXT;
  else if (value == MESSAGE_TYPE_HTML_ESCAPED ||
           value == CHATTY_MESSAGE_HTML)
    return CHATTY_MESSAGE_HTML_ESCAPED;
  else if (value == MESSAGE_TYPE_MATRIX_HTML)
    return CHATTY_MESSAGE_MATRIX_HTML;
  else if (value == MESSAGE_TYPE_LOCATION)
    return CHATTY_MESSAGE_LOCATION;
  else if (value == MESSAGE_TYPE_FILE)
    return CHATTY_MESSAGE_FILE;
  else if (value == MESSAGE_TYPE_IMAGE)
    return CHATTY_MESSAGE_IMAGE;
  else if (value == MESSAGE_TYPE_AUDIO)
    return CHATTY_MESSAGE_AUDIO;
  else if (value == MESSAGE_TYPE_VIDEO)
    return CHATTY_MESSAGE_VIDEO;
  else if (value == MESSAGE_TYPE_MMS)
    return CHATTY_MESSAGE_MMS;

  g_return_val_if_reached (CHATTY_MESSAGE_HTML);
}

static int
history_visibility_to_value (ChattyItemState state)
{
  switch (state) {
  case CHATTY_ITEM_VISIBLE:
    return THREAD_VISIBILITY_VISIBLE;

  case CHATTY_ITEM_HIDDEN:
    return THREAD_VISIBILITY_HIDDEN;

  case CHATTY_ITEM_ARCHIVED:
    return THREAD_VISIBILITY_ARCHIVED;

  case CHATTY_ITEM_BLOCKED:
    return THREAD_VISIBILITY_BLOCKED;

  default:
    g_return_val_if_reached (THREAD_VISIBILITY_VISIBLE);
  }

  g_return_val_if_reached (THREAD_VISIBILITY_VISIBLE);
}

static int
history_msg_status_to_value (ChattyMsgStatus status)
{
  switch (status) {
  case CHATTY_STATUS_RECEIVED:
    return MESSAGE_STATUS_RECEIVED;

  case CHATTY_STATUS_DRAFT:
    return MESSAGE_STATUS_DRAFT;

  case CHATTY_STATUS_UNKNOWN:
  case CHATTY_STATUS_SENDING:
    return MESSAGE_STATUS_UNKNOWN;

  case CHATTY_STATUS_SENT:
    return MESSAGE_STATUS_SENT;

  case CHATTY_STATUS_DELIVERED:
    return MESSAGE_STATUS_DELIVERED;

  case CHATTY_STATUS_READ:
    return MESSAGE_STATUS_READ;

  case CHATTY_STATUS_SENDING_FAILED:
    return MESSAGE_STATUS_SENDING_FAILED;

  case CHATTY_STATUS_DELIVERY_FAILED:
    return MESSAGE_STATUS_DELIVERY_FAILED;

  default:
    g_return_val_if_reached (MESSAGE_STATUS_UNKNOWN);
  }

  g_return_val_if_reached (MESSAGE_STATUS_UNKNOWN);
}

static ChattyMsgStatus
history_msg_status_from_value (int value)
{
  if (value == MESSAGE_STATUS_UNKNOWN)
    return CHATTY_STATUS_UNKNOWN;
  if (value == MESSAGE_STATUS_DRAFT)
    return CHATTY_STATUS_DRAFT;
  if (value == MESSAGE_STATUS_RECEIVED)
    return CHATTY_STATUS_RECEIVED;
  if (value == MESSAGE_STATUS_SENT)
    return CHATTY_STATUS_SENT;
  if (value == MESSAGE_STATUS_DELIVERED)
    return CHATTY_STATUS_DELIVERED;
  if (value == MESSAGE_STATUS_READ)
    return CHATTY_STATUS_READ;
  if (value == MESSAGE_STATUS_SENDING_FAILED)
    return CHATTY_STATUS_SENDING_FAILED;
  if (value == MESSAGE_STATUS_DELIVERY_FAILED)
    return CHATTY_STATUS_DELIVERY_FAILED;

  return CHATTY_STATUS_UNKNOWN;
}

static ChattyItemState
history_value_to_visibility (int value)
{
  if (value == THREAD_VISIBILITY_VISIBLE)
    return CHATTY_ITEM_VISIBLE;
  if (value == THREAD_VISIBILITY_HIDDEN)
    return CHATTY_ITEM_HIDDEN;
  if (value == THREAD_VISIBILITY_ARCHIVED)
    return CHATTY_ITEM_ARCHIVED;
  if (value == THREAD_VISIBILITY_BLOCKED)
    return CHATTY_ITEM_BLOCKED;

  g_return_val_if_reached (CHATTY_ITEM_VISIBLE);
}

#if 0

static int
id_type_to_value (ChattyIdType type)
{
  switch (type) {
  case CHATTY_ID_UNKNOWN:
    return CHATTY_ID_UNKNOWN_VALUE;

  case CHATTY_ID_PHONE:
    return CHATTY_ID_PHONE_VALUE;

  case CHATTY_ID_EMAIL:
    return CHATTY_ID_EMAIL_VALUE;

  case CHATTY_ID_MATRIX:
    return CHATTY_ID_MATRIX_VALUE;

  case CHATTY_ID_XMPP:
    return CHATTY_ID_XMPP_VALUE;

  default:
    g_return_val_if_reached (0);
  }
}
#endif

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
chatty_history_get_db_version (ChattyHistory *self,
                               GTask         *task)
{
  sqlite3_stmt *stmt;
  int status, version = -1;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  sqlite3_prepare_v2 (self->db, "PRAGMA user_version;", -1, &stmt, NULL);
  status = sqlite3_step (stmt);

  if (status == SQLITE_ROW)
    version = sqlite3_column_int (stmt, 0);
  else
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Couldn't get database version. errno: %d, desc: %s",
                             status, sqlite3_errmsg (self->db));
  sqlite3_finalize (stmt);

  return version;
}

static gboolean
chatty_history_create_schema (ChattyHistory *self,
                              GTask         *task)
{
  char *error = NULL;
  const char *sql;
  sqlite3 *db;
  int status;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  /* XXX: SELECT * FROM files sounds better, WHERE file.id != x feels better too.
   * So what to name? file or files?
   */
  sql = "BEGIN TRANSACTION;"

    "CREATE TABLE IF NOT EXISTS mime_type ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "name TEXT NOT NULL UNIQUE);"

    "CREATE TABLE IF NOT EXISTS files ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "name TEXT, "
    "url TEXT NOT NULL UNIQUE, "
    "path TEXT, "
    "mime_type_id INTEGER REFERENCES mime_type(id), "
    "status INT, "
    "size INTEGER);"

    /* Introduced in version 4, replaces 'audio', 'image' and 'video' */
    "CREATE TABLE IF NOT EXISTS file_metadata ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "file_id INTEGER NOT NULL UNIQUE REFERENCES files(id) ON DELETE CASCADE, "
    "width INTEGER, "
    "height INTEGER, "
    "duration INTEGER);"

    /* TODO: Someday */
    /* "CREATE TABLE IF NOT EXISTS devices (" */
    /* "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, " */
    /* "device TEXT NOT NULL UNIQUE, " */
    /* "name TEXT);" */

    "CREATE TABLE IF NOT EXISTS users ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "username TEXT NOT NULL, "
    "alias TEXT, "
    /* changed to refer files() in Version 2 */
    "avatar_id INTEGER REFERENCES files(id), "
    "type INTEGER NOT NULL, "
    /* For phone numbers */
    "UNIQUE (username, type));"

    "CREATE TABLE IF NOT EXISTS accounts ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "user_id INTEGER NOT NULL REFERENCES users(id), "
    "password TEXT, "
    "enabled INTEGER DEFAULT 0, "
    "protocol INTEGER NOT NULL, "
    "UNIQUE (user_id, protocol));"

    "INSERT OR IGNORE INTO users(username,type) VALUES "
    /* Some Invalid value */
    "('"MM_NUMBER"'," STRING (CHATTY_ID_PHONE_VALUE) ");"

    "CREATE TABLE IF NOT EXISTS threads ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "name TEXT NOT NULL, "
    "alias TEXT, "
    /* changed to refer files() in Version 2 */
    "avatar_id INTEGER REFERENCES files(id), "
    "account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE, "
    "type INTEGER NOT NULL, "
    "encrypted INTEGER DEFAULT 0, "
    "UNIQUE (name, account_id, type));"

    "CREATE TABLE IF NOT EXISTS thread_members ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "thread_id INTEGER NOT NULL REFERENCES threads(id) ON DELETE CASCADE, "
    "user_id INTEGER NOT NULL REFERENCES users(id), "
    "UNIQUE (thread_id, user_id));"

    "CREATE TABLE IF NOT EXISTS messages ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "uid TEXT NOT NULL, "
    "thread_id INTEGER NOT NULL REFERENCES threads(id) ON DELETE CASCADE, "
    "sender_id INTEGER REFERENCES users(id), "
    "user_alias TEXT, "
    "body TEXT NOT NULL, "
    "body_type INTEGER NOT NULL, "
    "direction INTEGER NOT NULL, "
    "time INTEGER NOT NULL, "
    "status INTEGER, "
    "encrypted INTEGER DEFAULT 0, "
    /* preview file: Introduced in version 2 */
    "preview_id INTEGER REFERENCES files(id), "
    /* Introduced in version 4 */
    "subject TEXT, "
    "UNIQUE (uid, thread_id, body, time));"

    /* Introduced in version 4 */
    "CREATE TABLE IF NOT EXISTS mm_messages ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "message_id INTEGER NOT NULL UNIQUE REFERENCES messages(id) ON DELETE CASCADE, "
    /* messages in same thread can be sent from different SIM */
    "account_id INTEGER NOT NULL REFERENCES accounts(id), "
    /* MM_PROTOCOL_MMS, MM_PROTOCOL_SMS  etc. */
    "protocol INTEGER NOT NULL, "
    "smsc TEXT, "
    /* For outgoing messages: the time message was delivered */
    /* For incoming messages: the time message was sent */
    "time_sent INTEGER, "
    "validity INTEGER, "
    /* TODO: Create a modem table with iccid when multiple SIMs are supported */
    /* "modem_id INTEGER NOT NULL REFERENCES modem(id), " */
    "reference_number INTEGER);"

    /* Introduced in version 4 */
    "CREATE TABLE IF NOT EXISTS message_files ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "message_id INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE, "
    "file_id INTEGER NOT NULL REFERENCES files(id), "
    "preview_id INTEGER REFERENCES files(id), "
    "UNIQUE (message_id, file_id));"

    /* Alter threads after the messages table is created */
    "ALTER TABLE threads ADD COLUMN last_read_id INTEGER REFERENCES messages(id);"

    /* Introduced in Version 3 */
    "ALTER TABLE threads ADD COLUMN visibility INT NOT NULL DEFAULT "
    STRING(THREAD_VISIBILITY_VISIBLE) ";"

    /* Introduced in Version 4 */
    "ALTER TABLE threads ADD COLUMN notification INTEGER NOT NULL DEFAULT 1;"

    "INSERT OR IGNORE INTO accounts(user_id,protocol) "
    "SELECT users.id,"STRING (PROTOCOL_MMS_SMS)" "
    "FROM users "
    "WHERE users.username='"MM_NUMBER"';"

    "COMMIT;";

  status = sqlite3_exec (self->db, sql, NULL, NULL, &error);

  if (status == SQLITE_OK)
    return TRUE;

  g_warning ("error: %s", error);
  db = g_steal_pointer (&self->db);
  g_task_return_new_error (task,
                           G_IO_ERROR,
                           G_IO_ERROR_FAILED,
                           "Couldn't create tables. errno: %d, desc: %s. %s",
                           status, sqlite3_errmsg (db), error);
  sqlite3_close (db);
  sqlite3_free (error);

  return FALSE;
}

static gboolean
chatty_history_update_version (ChattyHistory *self,
                               GTask         *task)
{
  char *error = NULL;
  const char *sql;
  int status;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  sql = "PRAGMA user_version = " STRING (HISTORY_VERSION) ";";

  status = sqlite3_exec (self->db, sql, NULL, NULL, &error);

  if (status == SQLITE_OK)
    return TRUE;

  g_task_return_new_error (task,
                           G_IO_ERROR,
                           G_IO_ERROR_FAILED,
                           "Couldn't set db version. errno: %d, desc: %s. %s",
                           status, sqlite3_errmsg (self->db), error);
  sqlite3_free (error);

  return FALSE;
}

static int
insert_or_ignore_user (ChattyHistory  *self,
                       ChattyProtocol  protocol,
                       const char     *who,
                       const char     *alias,
                       GTask          *task)
{
  g_autofree char *phone = NULL;
  sqlite3_stmt *stmt;
  int status, id = 0;

  if (!who || !*who)
    return 0;

  if (protocol & (CHATTY_PROTOCOL_MMS_SMS | CHATTY_PROTOCOL_MMS | CHATTY_PROTOCOL_TELEGRAM)) {
    char *country;

    country = g_object_get_data (G_OBJECT (task), "country-code");
    phone = chatty_utils_check_phonenumber (who, country);
  }

  sqlite3_prepare_v2 (self->db,
                      "INSERT OR IGNORE INTO users(username,type,alias) "
                      "VALUES(?1,?2,?3) "
                      "ON CONFLICT(username,type) "
                      "DO UPDATE SET alias=coalesce(?3,alias)",
                      -1, &stmt, NULL);
  history_bind_text (stmt, 1, phone ? phone : who, "binding when adding phone number");
  history_bind_int (stmt, 2, history_protocol_to_type_value (protocol), "binding when adding phone number");
  if (alias && who && !g_str_equal (who, alias))
    history_bind_text (stmt, 3, alias, "binding when adding phone number");

  sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  /* We can't use last_row_id as we may ignore the last insert */
  sqlite3_prepare_v2 (self->db,
                      "SELECT users.id FROM users "
                      "WHERE users.username=? AND type=?;",
                      -1, &stmt, NULL);
  history_bind_text (stmt, 1, phone ? phone : who, "binding when getting users");
  history_bind_int (stmt, 2, history_protocol_to_type_value (protocol), "binding when getting users");
  status = sqlite3_step (stmt);

  if (status == SQLITE_ROW)
    id = sqlite3_column_int (stmt, 0);
  sqlite3_finalize (stmt);

  if (status != SQLITE_ROW)
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Couldn't insert into users. errno: %d, desc: %s",
                             status, sqlite3_errmsg (self->db));
  return id;
}

static int
insert_or_ignore_account (ChattyHistory  *self,
                          ChattyProtocol  protocol,
                          int             user_id,
                          GTask          *task)
{
  sqlite3_stmt *stmt;
  int status, id = 0;

  if (!user_id)
    g_return_val_if_reached (0);

  sqlite3_prepare_v2 (self->db,
                      "INSERT OR IGNORE INTO accounts(user_id,protocol) "
                      "VALUES(?,?);",
                      -1, &stmt, NULL);
  history_bind_int (stmt, 1, user_id, "binding when adding account");
  history_bind_int (stmt, 2, history_protocol_to_value (protocol), "binding when adding account");
  sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  /* We can't use last_row_id as we may ignore the last insert */
  sqlite3_prepare_v2 (self->db,
                      "SELECT accounts.id FROM accounts "
                      "WHERE user_id=? AND protocol=?;",
                      -1, &stmt, NULL);
  history_bind_int (stmt, 1, user_id, "binding when getting account");
  history_bind_int (stmt, 2, history_protocol_to_value (protocol), "binding when getting account");
  status = sqlite3_step (stmt);

  if (status == SQLITE_ROW)
    id = sqlite3_column_int (stmt, 0);
  sqlite3_finalize (stmt);

  if (status != SQLITE_ROW)
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Couldn't insert into users. errno: %d, desc: %s",
                             status, sqlite3_errmsg (self->db));
  return id;
}

static int
history_add_phone_user (ChattyHistory *self,
                        GTask         *task,
                        const char    *username,
                        const char    *alias)
{
  sqlite3_stmt *stmt;
  int status, id = 0;

  sqlite3_prepare_v2 (self->db, "INSERT OR IGNORE INTO users(username,alias,type) "
                      "VALUES(?,?,"STRING(CHATTY_ID_PHONE_VALUE)");",
                      -1, &stmt, NULL);
  history_bind_text (stmt, 1, username, "binding when adding user");
  history_bind_text (stmt, 2, alias, "binding when adding user");
  status = sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  if (status != SQLITE_DONE) {
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Couldn't insert into users. errno: %d, desc: %s",
                             status, sqlite3_errmsg (self->db));
    return 0;
  }

  /* We can't use last_row_id as we may ignore the last insert */
  sqlite3_prepare_v2 (self->db,
                      "SELECT users.id FROM users "
                      "WHERE users.username=? AND type="STRING(CHATTY_ID_PHONE_VALUE)";",
                      -1, &stmt, NULL);
  history_bind_text (stmt, 1, username, "binding when getting phone user");
  status = sqlite3_step (stmt);

  if (status == SQLITE_ROW)
    id = sqlite3_column_int (stmt, 0);
  sqlite3_finalize (stmt);

  if (status != SQLITE_ROW)
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Couldn't get user. errno: %d, desc: %s",
                             status, sqlite3_errmsg (self->db));

  return id;
}

static int
get_thread_id (ChattyHistory *self,
               ChattyChat    *chat)
{
  sqlite3_stmt *stmt;
  int status, id = 0;

  sqlite3_prepare_v2 (self->db,
                      "SELECT threads.id FROM threads "
                      "INNER JOIN accounts "
                      "ON accounts.id=account_id "
                      "INNER JOIN users "
                      "ON users.username=? AND accounts.user_id=users.id "
                      "AND threads.name=? AND threads.type=?;",
                      -1, &stmt, NULL);
  history_bind_text (stmt, 1, chatty_item_get_username (CHATTY_ITEM (chat)), "binding when getting thread");
  history_bind_text (stmt, 2, chatty_chat_get_chat_name (chat), "binding when getting thread");
  history_bind_int (stmt, 3, chatty_chat_is_im (chat) ? THREAD_DIRECT_CHAT : THREAD_GROUP_CHAT,
                    "binding when adding phone number");
  status = sqlite3_step (stmt);

  if (status == SQLITE_ROW)
    id = sqlite3_column_int (stmt, 0);
  sqlite3_finalize (stmt);

  return id;
}

static int
insert_or_ignore_thread (ChattyHistory *self,
                         ChattyChat    *chat,
                         GTask         *task)
{
  sqlite3_stmt *stmt;
  int user_id, account_id;
  int status, file_id, id = 0;
  ChattyFileInfo *file;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  user_id = insert_or_ignore_user (self,
                                   chatty_item_get_protocols (CHATTY_ITEM (chat)),
                                   chatty_item_get_username (CHATTY_ITEM (chat)),
                                   NULL,
                                   task);
  if (!user_id) {
    const char *who;

    who = chatty_item_get_username (CHATTY_ITEM (chat));

    if (!who || !*who)
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               "Username empty");

    return 0;
  }

  account_id = insert_or_ignore_account (self,
                                         chatty_item_get_protocols (CHATTY_ITEM (chat)),
                                         user_id, task);

  if (!account_id)
    return 0;

  file = chatty_item_get_avatar_file (CHATTY_ITEM (chat));
  file_id = add_file_info (self, file);

  sqlite3_prepare_v2 (self->db,
                      "INSERT INTO threads(name,alias,account_id,type,visibility,encrypted,avatar_id) "
                      "VALUES(?1,?2,?3,?4,?5,?6,?7) "
                      "ON CONFLICT(name,account_id,type) "
                      "DO UPDATE SET alias=?2, visibility=?5, encrypted=?6, avatar_id=?7",
                      -1, &stmt, NULL);
  history_bind_text (stmt, 1, chatty_chat_get_chat_name (chat), "binding when adding thread");

  if (CHATTY_IS_MM_CHAT (chat) && chatty_mm_chat_has_custom_name (CHATTY_MM_CHAT (chat)))
    history_bind_text (stmt, 2, chatty_item_get_name (CHATTY_ITEM (chat)), "binding when adding thread");
  history_bind_int (stmt, 3, account_id, "binding when adding thread");
  history_bind_int (stmt, 4, chatty_chat_is_im (chat) ? THREAD_DIRECT_CHAT : THREAD_GROUP_CHAT,
                    "binding when adding thread");
  history_bind_int (stmt, 5, history_visibility_to_value (chatty_item_get_state (CHATTY_ITEM (chat))),
                    "binding when adding thread");
  history_bind_int (stmt, 6, chatty_chat_get_encryption (chat) == CHATTY_ENCRYPTION_ENABLED,
                    "binding when adding thread");
  if (file_id)
    history_bind_int (stmt, 7, file_id, "binding when adding thread");

  sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  /* We can't use last_row_id as we may ignore the last insert */
  sqlite3_prepare_v2 (self->db,
                      "SELECT threads.id FROM threads "
                      "WHERE name=? AND account_id=? AND type=?;",
                      -1, &stmt, NULL);
  history_bind_text (stmt, 1, chatty_chat_get_chat_name (chat), "binding when getting thread");
  history_bind_int (stmt, 2, account_id, "binding when getting thread");
  history_bind_int (stmt, 3, chatty_chat_is_im (chat) ? THREAD_DIRECT_CHAT : THREAD_GROUP_CHAT,
                    "binding when getting thread");
  status = sqlite3_step (stmt);

  if (status == SQLITE_ROW)
    id = sqlite3_column_int (stmt, 0);
  sqlite3_finalize (stmt);

  if (status == SQLITE_ROW &&
      CHATTY_IS_MM_CHAT (chat)) {
    GListModel *buddies;
    guint n_items;

    buddies = chatty_chat_get_users (chat);
    n_items = g_list_model_get_n_items (buddies);

    for (guint i = 0; i < n_items; i++) {
      g_autoptr(ChattyMmBuddy) buddy = NULL;
      const char *number, *name;

      buddy = g_list_model_get_item (buddies, i);
      name = chatty_item_get_name (CHATTY_ITEM (buddy));
      number = chatty_mm_buddy_get_number (buddy);
      user_id = history_add_phone_user (self, task, number, name);

      sqlite3_prepare_v2 (self->db,
                          "INSERT OR IGNORE INTO thread_members(thread_id,user_id) "
                          "VALUES(?,?);",
                          -1, &stmt, NULL);
      history_bind_int (stmt, 1, id, "binding when adding phone number");
      history_bind_int (stmt, 2, user_id, "binding when adding phone number");

      sqlite3_step (stmt);
      sqlite3_finalize (stmt);
    }
  }

  if (status != SQLITE_ROW)
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Couldn't insert into users. errno: %d, desc: %s",
                             status, sqlite3_errmsg (self->db));
  return id;
}

static void
chatty_history_backup (ChattyHistory *self)
{
  g_autoptr(GFile) backup_db = NULL;
  g_autoptr(GFile) old_db = NULL;
  g_autofree char *backup_name = NULL;
  g_autoptr(GError) error = NULL;

  backup_name = g_strdup_printf ("%s.%ld", self->db_path, time (NULL));
  g_info ("Copying database for backup");

  old_db = g_file_new_for_path (self->db_path);
  backup_db = g_file_new_for_path (backup_name);
  g_file_copy (old_db, backup_db, G_FILE_COPY_NONE, NULL, NULL, NULL, &error);
  g_info ("Copying database complete");

  if (error &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS))
    g_critical ("Error creating DB backup: %s", error->message);
}

static gboolean
history_add_phone_account (ChattyHistory *self,
                           GTask         *task,
                           const char    *account,
                           int            protocol)
{
  sqlite3_stmt *stmt;
  int status;

  sqlite3_prepare_v2 (self->db, "INSERT OR IGNORE INTO accounts(user_id,protocol)"
                      "VALUES((SELECT users.id FROM users WHERE username=?),?);",
                      -1, &stmt, NULL);
  history_bind_text (stmt, 1, account, "binding when adding account");
  history_bind_int (stmt, 2, protocol, "binding when adding account");
  status = sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  if (status != SQLITE_DONE)
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Couldn't insert into accounts. errno: %d, desc: %s",
                             status, sqlite3_errmsg (self->db));

  return status == SQLITE_DONE;
}

static gboolean
history_add_thread (ChattyHistory *self,
                    GTask         *task,
                    const char    *account,
                    const char    *room,
                    const char    *alias,
                    int            chat_type)
{
  sqlite3_stmt *stmt;
  int status;

  sqlite3_prepare_v2 (self->db, "INSERT OR IGNORE INTO threads(name,alias,account_id,type) "
                      "SELECT ?,?,accounts.id,? "
                      "FROM users "
                      "INNER JOIN accounts ON accounts.user_id=users.id "
                      "AND users.username=?;",
                      -1, &stmt, NULL);

  history_bind_text (stmt, 1, room, "binding when adding thread");
  history_bind_text (stmt, 2, alias, "binding when adding thread");
  history_bind_int (stmt, 3, chat_type, "binding when adding thread");
  history_bind_text (stmt, 4, account, "binding when adding thread");
  status = sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  if (status != SQLITE_DONE)
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Couldn't insert into accounts. errno: %d, desc: %s",
                             status, sqlite3_errmsg (self->db));

  return status == SQLITE_DONE;
}

/* TODO */
/* this function works for migration from v0
 * to v1, v2, and v3 as the tables and columns
 * used in this function doesn't change in
 * v1, v2 or v3.
 */
static gboolean
chatty_history_migrate_db_to_v1_to_v3 (ChattyHistory *self,
                                       GTask         *task)
{
  char *error = NULL;
  int status;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);

  chatty_history_backup (self);

  if (!chatty_history_create_schema (self, task))
    return FALSE;

  status = sqlite3_exec (self->db,

                         "BEGIN TRANSACTION;"
                         "UPDATE chatty_im SET account='"MM_NUMBER"' "
                         "WHERE chatty_im.account='SMS';"
                         "COMMIT;"

                         "BEGIN TRANSACTION;"

                         /*** Users ***/
                         /* XMPP IM accounts */
                         "INSERT OR IGNORE INTO users(username,type) "
                         "SELECT DISTINCT account," STRING(CHATTY_ID_XMPP_VALUE) " FROM chatty_im "
                         /* A Rough match for XMPP accounts */
                         "WHERE chatty_im.account GLOB '[^@+]*' "
                         "AND chatty_im.account!='"MM_NUMBER"' "
                         "AND chatty_im.who GLOB '[^@+]*@*';"

                         /* XMPP MUC accounts */
                         "INSERT OR IGNORE INTO users(username,type) "
                         "SELECT DISTINCT account," STRING(CHATTY_ID_XMPP_VALUE) " FROM chatty_chat "
                         /* A Rough match for XMPP accounts */
                         "WHERE chatty_chat.account GLOB '[^@+]*' "
                         "AND chatty_chat.room GLOB '[^@+]*@*';"

                         /* SMS account */
                         "INSERT OR IGNORE INTO users(username,type) "
                         "SELECT DISTINCT account," STRING(CHATTY_ID_PHONE_VALUE) " FROM chatty_im "
                         "WHERE account='"MM_NUMBER"';"

                         /* Matrix accounts */
                         "INSERT OR IGNORE INTO users(username,type) "
                         "SELECT DISTINCT account," STRING(CHATTY_ID_MATRIX_VALUE) " FROM chatty_chat "
                         /* A Rough match for Matrix chat */
                         "WHERE chatty_chat.room GLOB '!?*:?*' "
                         "AND chatty_chat.account NOT GLOB '?*@?*' "
                         "AND chatty_chat.account NOT GLOB '+?*' "
                         "AND chatty_chat.account!='"MM_NUMBER"';"

                         /* XMPP IM users */
                         "INSERT OR IGNORE INTO users(username,type) "
                         "SELECT DISTINCT substr(who,0,"
                         "CASE "
                         "WHEN instr(who, '/') > 0 THEN instr(who, '/') "
                         "ELSE length(who) + 1 "
                         "END),"
                         STRING(CHATTY_ID_XMPP_VALUE) " FROM chatty_im "
                         /* A Rough match for XMPP users */
                         "WHERE chatty_im.account GLOB '[^@+]*' "
                         "AND chatty_im.account!='"MM_NUMBER"' "
                         "AND chatty_im.who GLOB '[^@]*@*';"

                         /* XMPP MUC users */
                         "INSERT OR IGNORE INTO users(username,type) "
                         "SELECT DISTINCT "
                         /* A Rough match for XMPP users */
                         "CASE "
                         "  WHEN chatty_chat.who LIKE chatty_chat.room || '/%' THEN chatty_chat.who "
                         "  WHEN chatty_chat.who LIKE '%/%' THEN substr(who,0,instr(who, '/')) "
                         "  ELSE chatty_chat.who "
                         "END,"
                         STRING(CHATTY_ID_XMPP_VALUE) " FROM chatty_chat "
                         "WHERE chatty_chat.account GLOB '[^@+]*' "
                         "AND chatty_chat.who GLOB '[^@]*@*' "
                         "AND chatty_chat.room GLOB '[^@]*@*';"

                         /* Matrix chat users */
                         "INSERT OR IGNORE INTO users (username,type) "
                         "SELECT DISTINCT who,"
                         STRING(CHATTY_ID_MATRIX_VALUE) " FROM chatty_chat "
                         /* A Rough match for users that are not regular phone numbers */
                         "WHERE chatty_chat.room GLOB '!?*:?*' "
                         "AND chatty_chat.who GLOB '@?*:?*' "
                         /* Skip possible Telegram accounts */
                         "AND chatty_chat.account NOT GLOB '+[0-9]*';"

                         /*** Accounts ***/
                         /* We have exactly one account for SMS */
                         "INSERT OR IGNORE INTO accounts(user_id,protocol,enabled) "
                         "SELECT DISTINCT users.id," STRING(PROTOCOL_MMS_SMS) ",1 FROM users "
                         "WHERE users.username='"MM_NUMBER"';"

                         /* XMPP IM accounts */
                         "INSERT OR IGNORE INTO accounts(user_id,protocol) "
                         "SELECT DISTINCT users.id," STRING(PROTOCOL_XMPP) " FROM users "
                         "INNER JOIN chatty_im "
                         "ON chatty_im.account=users.username "
                         "AND users.type='" STRING(CHATTY_ID_XMPP_VALUE) "';"

                         /* XMPP MUC accounts */
                         "INSERT OR IGNORE INTO accounts(user_id,protocol) "
                         "SELECT DISTINCT users.id," STRING(PROTOCOL_XMPP) " FROM users "
                         "INNER JOIN chatty_chat "
                         "ON chatty_chat.account=users.username "
                         "AND users.type=" STRING(CHATTY_ID_XMPP_VALUE) ";"

                         /* XMPP MUC accounts */
                         "INSERT OR IGNORE INTO accounts(user_id,protocol) "
                         "SELECT DISTINCT users.id," STRING(PROTOCOL_MATRIX) " FROM users "
                         "INNER JOIN chatty_chat "
                         "ON chatty_chat.account=users.username "
                         "AND users.type=" STRING(CHATTY_ID_MATRIX_VALUE) ";"

                         /*** Threads ***/
                         /* XMPP IM chats */
                         "INSERT OR IGNORE INTO threads(name,account_id,type) "
                         "SELECT DISTINCT substr(who,0,"
                         "CASE "
                         "WHEN instr(who, '/') > 0 THEN instr(who, '/') "
                         "ELSE length(who) + 1 "
                         "END),"
                         "accounts.id," STRING(THREAD_DIRECT_CHAT) " "
                         "FROM chatty_im "
                         "INNER JOIN accounts "
                         "  ON accounts.user_id=users.id "
                         "INNER JOIN users "
                         "  ON users.username=chatty_im.account "
                         "WHERE users.type=" STRING(CHATTY_ID_XMPP_VALUE) ";"

                         /* XMPP MUC chats */
                         "INSERT OR IGNORE INTO threads(name,account_id,type) "
                         "SELECT DISTINCT room,"
                         "accounts.id," STRING(THREAD_GROUP_CHAT) " "
                         "FROM chatty_chat "
                         "INNER JOIN accounts "
                         "  ON accounts.user_id=users.id "
                         "INNER JOIN users "
                         "  ON users.username=chatty_chat.account "
                         "WHERE users.type=" STRING(CHATTY_ID_XMPP_VALUE) ";"

                         /* Matrix chats */
                         "INSERT OR IGNORE INTO threads(name,account_id,type) "
                         "SELECT DISTINCT room,accounts.id," STRING(THREAD_GROUP_CHAT) " "
                         "FROM chatty_chat "
                         "INNER JOIN accounts "
                         "  ON accounts.user_id=users.id "
                         "INNER JOIN users "
                         "  ON users.username=chatty_chat.account "
                         "WHERE users.type=" STRING(CHATTY_ID_MATRIX_VALUE) ";"

                         /* Telegram IM chats */
                         "INSERT OR IGNORE INTO threads(name,account_id,type) "
                         "SELECT DISTINCT who,accounts.id," STRING(THREAD_DIRECT_CHAT) " "
                         "FROM chatty_im "
                         "INNER JOIN accounts "
                         "  ON accounts.user_id=users.id "
                         "INNER JOIN users "
                         "  ON users.username=chatty_im.account "
                         "WHERE chatty_im.account!='"MM_NUMBER"' AND users.type=" STRING(CHATTY_ID_PHONE_VALUE) ";"

                         /* Telegram MUC chats */
                         "INSERT OR IGNORE INTO threads(name,account_id,type) "
                         "SELECT DISTINCT room,accounts.id," STRING(THREAD_GROUP_CHAT) " "
                         "FROM chatty_chat "
                         "INNER JOIN accounts "
                         "  ON accounts.user_id=users.id "
                         "INNER JOIN users "
                         "  ON users.username=chatty_chat.account "
                         "WHERE users.type=" STRING(CHATTY_ID_PHONE_VALUE) ";"

                         /*** Thread Members ***/
                         /* XMPP IM chat members */
                         "INSERT OR IGNORE INTO thread_members(thread_id,user_id) "
                         "SELECT DISTINCT threads.id,u.id FROM chatty_im "
                         "INNER JOIN threads "
                         "ON chatty_im.who LIKE threads.name || '%' "
                         "INNER JOIN users As a "
                         "ON threads.account_id=accounts.id "
                         "INNER JOIN accounts "
                         "ON accounts.user_id=a.id AND a.username=chatty_im.account "
                         "AND a.type=" STRING(CHATTY_ID_XMPP_VALUE) " "
                         "INNER JOIN users AS u "
                         "ON chatty_im.who LIKE u.username || '%';"

                         /* XMPP MUC members */
                         "INSERT OR IGNORE INTO thread_members(thread_id,user_id) "
                         "SELECT DISTINCT threads.id,u.id FROM chatty_chat "
                         "INNER JOIN threads "
                         "ON chatty_chat.room=threads.name "
                         "INNER JOIN accounts "
                         "ON threads.account_id=accounts.id "
                         "INNER JOIN users AS a "
                         "ON accounts.user_id=a.id AND a.username=chatty_chat.account "
                         "AND a.type=" STRING(CHATTY_ID_XMPP_VALUE) " "
                         "INNER JOIN users AS u "
                         "ON chatty_chat.who=u.username "
                         "OR chatty_chat.who LIKE u.username || '/%' "
                         "OR chatty_chat.who LIKE chatty_chat.room || '/' || u.username "
                         "WHERE chatty_chat.direction!=-1;"

                         /* Matrix chat members */
                         "INSERT OR IGNORE INTO thread_members(thread_id,user_id) "
                         "SELECT DISTINCT threads.id,u.id FROM chatty_chat "
                         "INNER JOIN threads "
                         "ON chatty_chat.room=threads.name "
                         "INNER JOIN users As a "
                         "ON threads.account_id=accounts.id "
                         "INNER JOIN accounts "
                         "ON accounts.user_id=a.id AND a.username=chatty_chat.account "
                         "AND a.type=" STRING(CHATTY_ID_MATRIX_VALUE) " "
                         "INNER JOIN users AS u "
                         "ON u.username NOT NULL AND chatty_chat.who=u.username;"

                         /*** Messages ***/
                         /* XMPP IM messages */
                         "INSERT OR IGNORE INTO messages(uid,thread_id,sender_id,body,body_type,time,direction) "
                         /* Always assume HTML */
                         "SELECT DISTINCT uid,threads.id,u.id,message," STRING(MESSAGE_TYPE_HTML_ESCAPED) ",timestamp,direction "
                         "FROM chatty_im "
                         "INNER JOIN threads "
                         "ON chatty_im.who LIKE threads.name || '%' "
                         "INNER JOIN users As a "
                         "ON threads.account_id=accounts.id "
                         "INNER JOIN accounts "
                         "ON accounts.user_id=a.id AND a.username=chatty_im.account "
                         "INNER JOIN users AS u "
                         "ON chatty_im.who LIKE u.username || '%' "
                         "WHERE chatty_im.account GLOB '[^@]*@*' "
                         "ORDER BY timestamp ASC, chatty_im.id ASC;"

                         /* XMPP MUC chat messages */
                         "INSERT OR IGNORE INTO messages(uid,thread_id,sender_id,body,body_type,time,direction) "
                         /* Always assume HTML */
                         "SELECT DISTINCT uid,threads.id,"
                         "CASE "
                         "WHEN chatty_chat.direction=-1 THEN a.id "
                         "WHEN chatty_chat.who=NULL THEN NULL "
                         "ELSE u.id "
                         "END,"
                         "message," STRING(MESSAGE_TYPE_HTML_ESCAPED) ",timestamp,direction "
                         "FROM chatty_chat "
                         "INNER JOIN threads "
                         "ON chatty_chat.room LIKE threads.name || '%' "
                         "INNER JOIN accounts "
                         "ON threads.account_id=accounts.id "
                         "INNER JOIN users As a "
                         "ON accounts.user_id=a.id AND a.username=chatty_chat.account "
                         "LEFT JOIN users AS u "
                         "ON chatty_chat.who=u.username "
                         "OR chatty_chat.who LIKE u.username || '/%' "
                         "OR chatty_chat.who LIKE chatty_chat.room || '/' || u.username "
                         "WHERE chatty_chat.account GLOB '[^@]*@*' "
                         "ORDER BY timestamp ASC, chatty_chat.id ASC;"

                         /* Matrix chat messages */
                         "INSERT OR IGNORE INTO messages(uid,thread_id,sender_id,body,body_type,time,direction) "
                         /* Always assume HTML */
                         "SELECT DISTINCT uid,threads.id,"
                         "CASE "
                         "WHEN chatty_chat.direction=-1 AND chatty_chat.who=NULL THEN a.id "
                         "WHEN chatty_chat.direction=-1 THEN u.id "
                         "WHEN chatty_chat.who=NULL THEN NULL "
                         "ELSE u.id "
                         "END,"
                         "message," STRING(MESSAGE_TYPE_HTML_ESCAPED) ",timestamp,direction "
                         "FROM chatty_chat "
                         "INNER JOIN threads "
                         "ON chatty_chat.room=threads.name "
                         "INNER JOIN accounts "
                         "ON threads.account_id=accounts.id "
                         "INNER JOIN users As a "
                         "ON accounts.user_id=a.id AND a.username=chatty_chat.account "
                         "LEFT JOIN users AS u "
                         "ON chatty_chat.who=u.username "
                         "WHERE chatty_chat.room GLOB '!?*:?*' "
                         "AND chatty_chat.who IS NULL or chatty_chat.who GLOB '@?*:?*' "
                         "ORDER BY timestamp ASC, chatty_chat.id ASC;"

                         "COMMIT;",
                         NULL, NULL, &error);

  if (!e_phone_number_is_supported ())
    g_debug ("Not compiled with libphonenumber");

  if (status == SQLITE_OK) {
    sqlite3_stmt *stmt;

    /* Get all numbers in international format */
    status = sqlite3_prepare_v2 (self->db,
                                 /* Telegram IM */
                                 "SELECT DISTINCT account,who FROM chatty_im "
                                 "WHERE account GLOB '+[0-9]*[^@]*[0-9]';",
                                 -1, &stmt, NULL);
    if (status == SQLITE_OK)
      status = sqlite3_exec (self->db, "BEGIN TRANSACTION;", NULL, NULL, &error);

    while (sqlite3_step (stmt) == SQLITE_ROW) {
      g_autofree char *account_number = NULL;
      g_autofree char *sender_number = NULL;
      sqlite3_stmt *insert_stmt = NULL;
      const char *account, *sender;

      account = (gpointer)sqlite3_column_text (stmt, 0);
      sender = (gpointer)sqlite3_column_text (stmt, 1);

      account_number = chatty_utils_check_phonenumber (account, NULL);
      sender_number  = chatty_utils_check_phonenumber (sender, NULL);

      if (!history_add_phone_user (self, task,
                                   account_number ? account_number : account,
                                   !account_number ? account: NULL))
        return FALSE;

      /* Fill in accounts */
      if (!history_add_phone_account (self, task,
                                      account_number ? account_number : account,
                                      g_strcmp0 (account, MM_NUMBER) == 0 ? PROTOCOL_MMS_SMS : PROTOCOL_TELEGRAM))
        return FALSE;

      if (!history_add_phone_user (self, task,
                                   sender_number ? sender_number : sender,
                                   !sender_number ? sender: NULL))
        return FALSE;

      if (!history_add_thread (self, task,
                               account_number ? account_number : account,
                               sender_number ? sender_number : sender,
                               !sender_number ? sender: NULL,
                               THREAD_DIRECT_CHAT))
        return FALSE;

      /* Fill in messages */
      sqlite3_prepare_v2 (self->db,
                          "INSERT OR IGNORE INTO messages(uid,thread_id,sender_id,body,body_type,time,direction) "
                          "SELECT DISTINCT uid,threads.id,u.id,message," STRING(MESSAGE_TYPE_HTML_ESCAPED) ",timestamp,direction "
                          "FROM chatty_im "
                          "INNER JOIN threads "
                          "ON threads.name=? AND chatty_im.who=? "
                          "INNER JOIN accounts "
                          "ON threads.account_id=accounts.id "
                          "INNER JOIN users AS a "
                          "ON accounts.user_id=a.id AND a.username=? AND chatty_im.account=? "
                          "INNER JOIN users as u "
                          "ON u.username=? "
                          "ORDER BY timestamp ASC, chatty_im.id ASC;",
                          -1, &insert_stmt, NULL);

      history_bind_text (insert_stmt, 1, sender_number ? sender_number : sender, "binding when adding message");
      history_bind_text (insert_stmt, 2, sender, "binding when adding message");
      history_bind_text (insert_stmt, 3, account_number ? account_number : account, "binding when adding message");
      history_bind_text (insert_stmt, 4, account, "binding when adding message");
      history_bind_text (insert_stmt, 5, sender_number ? sender_number : sender, "binding when adding message");
      status = sqlite3_step (insert_stmt);
      sqlite3_finalize (insert_stmt);
    }

    sqlite3_exec (self->db,
                  "INSERT OR IGNORE INTO thread_members(thread_id,user_id) "
                  "SELECT DISTINCT threads.id,u.id FROM threads "
                  "INNER JOIN users AS u "
                  "ON threads.name=u.username "
                  "AND u.type="STRING(CHATTY_ID_PHONE_VALUE) ";",
                  NULL, NULL, &error);

    if (status == SQLITE_DONE || status == SQLITE_OK)
      status = sqlite3_exec (self->db, "COMMIT;", NULL, NULL, &error);

    sqlite3_finalize (stmt);
  }

  if (status == SQLITE_OK) {
    sqlite3_stmt *stmt;
    status = sqlite3_prepare_v2 (self->db,
                                 /* Telegram Chats */
                                 "SELECT DISTINCT account,who,room FROM chatty_chat "
                                 "WHERE account GLOB '+[0-9]*[^@:.]*[0-9]';",
                                 -1, &stmt, NULL);

    if (status == SQLITE_OK)
      status = sqlite3_exec (self->db, "BEGIN TRANSACTION;", NULL, NULL, &error);

    while (sqlite3_step (stmt) == SQLITE_ROW) {
      g_autofree char *account_number = NULL;
      g_autofree char *sender_number = NULL;
      sqlite3_stmt *insert_stmt = NULL;
      const char *account, *sender, *room;

      account = (gpointer)sqlite3_column_text (stmt, 0);
      sender = (gpointer)sqlite3_column_text (stmt, 1);
      room = (gpointer)sqlite3_column_text (stmt, 2);

      account_number = chatty_utils_check_phonenumber (account, NULL);
      sender_number  = chatty_utils_check_phonenumber (sender, NULL);

      if (!history_add_phone_user (self, task,
                                   account_number ? account_number : account,
                                   !account_number ? account: NULL))
        return FALSE;

      /* Fill in accounts */
      if (!history_add_phone_account (self, task,
                                      account_number ? account_number : account,
                                      g_strcmp0 (account, MM_NUMBER) == 0 ? PROTOCOL_MMS_SMS : PROTOCOL_TELEGRAM))
        return FALSE;

      if (sender &&
          !history_add_phone_user (self, task,
                                   sender_number ? sender_number : sender,
                                   !sender_number ? sender: NULL))
        return FALSE;

      if (!history_add_thread (self, task,
                               account_number ? account_number : account,
                               room, room, THREAD_GROUP_CHAT))
        return FALSE;

      /* Fill in messages with no author */
      sqlite3_prepare_v2 (self->db,
                          "INSERT OR IGNORE INTO messages(uid,thread_id,sender_id,body,body_type,time,direction) "
                          "SELECT DISTINCT uid,threads.id,"
                          "CASE "
                          "WHEN chatty_chat.direction=-1 THEN a.id "
                          "WHEN chatty_chat.who=NULL THEN NULL "
                          "END,"
                          "message," STRING(MESSAGE_TYPE_HTML_ESCAPED) ",timestamp,direction "
                          "FROM chatty_chat "
                          "INNER JOIN threads "
                          "ON threads.name=chatty_chat.room AND threads.name=? "
                          "INNER JOIN accounts "
                          "ON threads.account_id=accounts.id "
                          "INNER JOIN users AS a "
                          "ON accounts.user_id=a.id AND a.username=? AND chatty_chat.account=? "
                          "AND chatty_chat.who IS NULL "
                          "ORDER BY timestamp ASC, chatty_chat.id ASC;",
                          -1, &insert_stmt, NULL);

      history_bind_text (insert_stmt, 1, room, "binding when adding message");
      history_bind_text (insert_stmt, 2, account_number ? account_number : account, "binding when adding message");
      history_bind_text (insert_stmt, 3, account, "binding when adding message");
      status = sqlite3_step (insert_stmt);
      sqlite3_finalize (insert_stmt);

      /* Fill in messages with author */
      sqlite3_prepare_v2 (self->db,
                          "INSERT OR IGNORE INTO messages(uid,thread_id,sender_id,body,body_type,time,direction) "
                          "SELECT DISTINCT uid,threads.id,u.id,"
                          "message," STRING(MESSAGE_TYPE_HTML_ESCAPED) ",timestamp,direction "
                          "FROM chatty_chat "
                          "INNER JOIN threads "
                          "ON threads.name=chatty_chat.room AND threads.name=? "
                          "INNER JOIN accounts "
                          "ON threads.account_id=accounts.id "
                          "INNER JOIN users AS a "
                          "ON accounts.user_id=a.id AND a.username=? AND chatty_chat.account=? "
                          "INNER JOIN users as u "
                          "ON u.username=? AND chatty_chat.who=? "
                          "AND chatty_chat.who NOT NULL "
                          "AND chatty_chat.direction!=-1 "
                          "ORDER BY timestamp ASC, chatty_chat.id ASC;",
                          -1, &insert_stmt, NULL);

      history_bind_text (insert_stmt, 1, room, "binding when adding threads");
      history_bind_text (insert_stmt, 2, account_number ? account_number : account, "binding when adding phone number");
      history_bind_text (insert_stmt, 3, account, "binding when adding phone number");
      history_bind_text (insert_stmt, 4, sender_number ? sender_number : sender, "binding when adding threads");
      history_bind_text (insert_stmt, 5, sender, "binding when adding threads");
      status = sqlite3_step (insert_stmt);
      sqlite3_finalize (insert_stmt);

      /* Fill in chat thread members */
      sqlite3_prepare_v2 (self->db, "INSERT OR IGNORE INTO thread_members(thread_id,user_id) "
                          "SELECT DISTINCT threads.id,u.id FROM threads "
                          "INNER JOIN accounts "
                          "ON threads.account_id=accounts.id "
                          "INNER JOIN users AS a "
                          "ON accounts.user_id=a.id AND a.username=? "
                          "INNER JOIN chatty_chat "
                          "ON chatty_chat.account=? "
                          "AND threads.name=chatty_chat.room AND threads.name=? "
                          "INNER JOIN users AS u "
                          "ON u.type=" STRING(CHATTY_ID_PHONE_VALUE) " "
                          "AND u.username=? AND chatty_chat.who=?;",
                          -1, &insert_stmt, NULL);

      history_bind_text (insert_stmt, 1, account_number ? account_number : account, "binding when adding thread member");
      history_bind_text (insert_stmt, 2, account, "binding when adding thread member");
      history_bind_text (insert_stmt, 3, room, "binding when adding thread member");
      history_bind_text (insert_stmt, 4, sender_number ? sender_number : sender, "binding when adding thread member");
      history_bind_text (insert_stmt, 5, sender, "binding when adding thread member");
      status = sqlite3_step (insert_stmt);
      sqlite3_finalize (insert_stmt);
    }

    sqlite3_exec (self->db,
                  "INSERT OR IGNORE INTO thread_members(thread_id,user_id) "
                  "SELECT DISTINCT threads.id,u.id FROM threads "
                  "INNER JOIN users AS u "
                  "ON threads.name=u.username "
                  "AND u.type="STRING(CHATTY_ID_PHONE_VALUE) ";",
                  NULL, NULL, &error);

    if (status == SQLITE_DONE || status == SQLITE_OK)
      status = sqlite3_exec (self->db, "COMMIT;", NULL, NULL, &error);

    sqlite3_finalize (stmt);
  }

  if (status == SQLITE_OK) {
    sqlite3_stmt *stmt;
    status = sqlite3_prepare_v2 (self->db,
                                 /* SMS users with phone numbers sorted */
                                 /* HACK: empty LEFT JOIN is used to keep the first distinct match
                                  * instead of the last in the list.
                                  * We are still relying on implementation details of sqlite,
                                  * which is not good.
                                  */
                                 "SELECT DISTINCT chatty_im.who FROM chatty_im "
                                 "LEFT JOIN chatty_im as im "
                                 "WHERE chatty_im.account='"MM_NUMBER"' ORDER BY chatty_im.id ASC;",
                                 -1, &stmt, NULL);

    if (status == SQLITE_OK)
      status = sqlite3_exec (self->db, "BEGIN TRANSACTION;", NULL, NULL, &error);

    while (sqlite3_step (stmt) == SQLITE_ROW) {
      g_autofree char *sender_number = NULL;
      sqlite3_stmt *insert_stmt = NULL;
      const char *sender, *country;

      sender = (gpointer)sqlite3_column_text (stmt, 0);
      country = g_object_get_data (G_OBJECT (task), "country-code");

      sender_number  = chatty_utils_check_phonenumber (sender, country);

      history_add_phone_user (self, task,
                              sender_number ? sender_number : sender,
                              NULL);

      history_add_thread (self, task, MM_NUMBER,
                          sender_number ? sender_number : sender,
                          sender, THREAD_DIRECT_CHAT);

      /* Fill in messages with no author */
      sqlite3_prepare_v2 (self->db, "INSERT OR IGNORE INTO messages(uid,thread_id,sender_id,body,body_type,time,direction) "
                          "SELECT DISTINCT uid,threads.id,u.id,message," STRING(MESSAGE_TYPE_TEXT) ",timestamp,direction "
                          "FROM chatty_im "
                          "INNER JOIN threads "
                          "ON threads.name=? AND chatty_im.who=? "
                          "INNER JOIN accounts "
                          "ON threads.account_id=accounts.id "
                          "INNER JOIN users AS a "
                          "ON accounts.user_id=a.id AND a.username=chatty_im.account "
                          "AND chatty_im.account='"MM_NUMBER"' "
                          "INNER JOIN users as u "
                          "ON u.username=? "
                          "ORDER BY timestamp ASC, chatty_im.id ASC;",
                          -1, &insert_stmt, NULL);

      history_bind_text (insert_stmt, 1, sender_number ? sender_number : sender, "binding when adding message");
      history_bind_text (insert_stmt, 2, sender, "binding when adding message");
      history_bind_text (insert_stmt, 3, sender_number ? sender_number : sender, "binding when adding message");
      status = sqlite3_step (insert_stmt);
      sqlite3_finalize (insert_stmt);
    }

    sqlite3_exec (self->db,
                  "INSERT OR IGNORE INTO thread_members(thread_id,user_id) "
                  "SELECT DISTINCT threads.id,u.id FROM threads "
                  "INNER JOIN users AS u "
                  "ON threads.name=u.username "
                  "AND u.type="STRING(CHATTY_ID_PHONE_VALUE) ";",
                  NULL, NULL, &error);

    if (status == SQLITE_DONE || status == SQLITE_OK)
      status = sqlite3_exec (self->db, "COMMIT;", NULL, NULL, &error);

    sqlite3_finalize (stmt);
  }

  /* Drop old tables */
  if (status == SQLITE_OK)
    status = sqlite3_exec (self->db,
                           "BEGIN TRANSACTION;"
                           "DROP TABLE chatty_chat;"
                           "DROP TABLE chatty_im;"
                           "COMMIT;", NULL, NULL, &error);

  if (status == SQLITE_OK || status == SQLITE_DONE) {
    /* Update user_version pragma */
    if (!chatty_history_update_version (self, task))
      return FALSE;
    return TRUE;
  }

  g_task_return_new_error (task,
                           G_IO_ERROR,
                           G_IO_ERROR_FAILED,
                           "Couldn't set db version. errno: %d, desc: %s. %s",
                           status, sqlite3_errstr (status), error);
  sqlite3_free (error);

  return FALSE;
}

/* TODO */
static gboolean
chatty_history_migrate_db_to_v2 (ChattyHistory *self,
                                 GTask         *task)
{
  char *error = NULL;
  int status;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);

  chatty_history_backup (self);

  status = sqlite3_exec (self->db,
                         "BEGIN TRANSACTION;"
                         "PRAGMA foreign_keys=OFF;"

                         "DROP TABLE IF EXISTS media;"
                         "DROP TABLE IF EXISTS files;"

                         "CREATE TABLE IF NOT EXISTS mime_type ("
                         "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
                         "name TEXT NOT NULL UNIQUE);"

                         "CREATE TABLE IF NOT EXISTS files ("
                         "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
                         "name TEXT, "
                         "url TEXT NOT NULL UNIQUE, "
                         "path TEXT, "
                         "mime_type_id INTEGER REFERENCES mime_type(id), "
                         "status INT, "
                         "size INTEGER);"

                         "CREATE TABLE IF NOT EXISTS video ("
                         "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
                         "file_id INTEGER NOT NULL UNIQUE, "
                         "width INTEGER, "
                         "height INTEGER, "
                         "duration INTEGER, "
                         "FOREIGN KEY(file_id) REFERENCES files(id));"

                         "CREATE TABLE IF NOT EXISTS image ("
                         "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
                         "file_id INTEGER NOT NULL UNIQUE, "
                         "width INTEGER, "
                         "height INTEGER, "
                         "FOREIGN KEY(file_id) REFERENCES files(id));"

                         "CREATE TABLE IF NOT EXISTS audio ("
                         "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
                         "file_id INTEGER NOT NULL UNIQUE, "
                         "duration INTEGER, "
                         "FOREIGN KEY(file_id) REFERENCES files(id));"

                         "ALTER TABLE messages ADD COLUMN preview_id INTEGER REFERENCES files(id);"


                         "CREATE TABLE IF NOT EXISTS temp_users ("
                         "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
                         "username TEXT NOT NULL, "
                         "alias TEXT, "
                         "avatar_id INTEGER REFERENCES files(id), "
                         "type INTEGER NOT NULL, "
                         "UNIQUE (username, type));"

                         "INSERT INTO temp_users SELECT * FROM users;"
                         "DROP TABLE IF EXISTS users;"
                         "ALTER TABLE temp_users RENAME TO users;"


                         "CREATE TABLE IF NOT EXISTS temp_threads ("
                         "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
                         "name TEXT NOT NULL, "
                         "alias TEXT, "
                         "avatar_id INTEGER REFERENCES files(id), "
                         "account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE, "
                         "type INTEGER NOT NULL, "
                         "encrypted INTEGER DEFAULT 0, "
                         "last_read_id INTEGER REFERENCES messages(id), "
                         "UNIQUE (name, account_id, type));"

                         "INSERT INTO temp_threads SELECT * FROM threads;"
                         "DROP TABLE IF EXISTS threads;"
                         "ALTER TABLE temp_threads RENAME TO threads;"

                         "COMMIT;",
                         NULL, NULL, &error);

  if (status == SQLITE_OK || status == SQLITE_DONE) {
    /* Update user_version pragma */
    if (!chatty_history_update_version (self, task))
      return FALSE;
    return TRUE;
  }

  g_task_return_new_error (task,
                           G_IO_ERROR,
                           G_IO_ERROR_FAILED,
                           "Couldn't set db version. errno: %d, desc: %s. %s",
                           status, sqlite3_errstr (status), error);
  sqlite3_free (error);

  return FALSE;
}

/* For migrating from v2 to v3 */
static gboolean
chatty_history_migrate_db_to_v3 (ChattyHistory *self,
                                 GTask         *task)
{
  char *error = NULL;
  int status;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);

  chatty_history_backup (self);

  status = sqlite3_exec (self->db,
                         "BEGIN TRANSACTION;"
                         "PRAGMA foreign_keys=OFF;"

                         "ALTER TABLE threads ADD COLUMN visibility INT NOT NULL DEFAULT "
                         STRING(THREAD_VISIBILITY_VISIBLE) ";"

                         "COMMIT;",
                         NULL, NULL, &error);

  if (status == SQLITE_OK || status == SQLITE_DONE) {
    /* Update user_version pragma */
    if (!chatty_history_update_version (self, task))
      return FALSE;
    return TRUE;
  }

  g_task_return_new_error (task,
                           G_IO_ERROR,
                           G_IO_ERROR_FAILED,
                           "Couldn't set db version. errno: %d, desc: %s. %s",
                           status, sqlite3_errstr (status), error);
  sqlite3_free (error);

  return FALSE;
}

/* For migrating from v3 to v4 */
static gboolean
chatty_history_migrate_db_to_v4 (ChattyHistory *self,
                                 GTask         *task)
{
  char *error = NULL;
  int status;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);

  chatty_history_backup (self);

  status = sqlite3_exec (self->db,
                         "BEGIN TRANSACTION;"
                         "PRAGMA foreign_keys=OFF;"

                         "ALTER TABLE threads ADD COLUMN notification INTEGER NOT NULL DEFAULT 1;"
                         "ALTER TABLE messages ADD COLUMN subject TEXT;"

                         "CREATE TABLE IF NOT EXISTS mm_messages ("
                         "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
                         "message_id INTEGER NOT NULL UNIQUE REFERENCES messages(id) ON DELETE CASCADE, "
                         /* messages in same thread can be sent from different SIM */
                         "account_id INTEGER NOT NULL REFERENCES accounts(id), "
                         /* MM_PROTOCOL_MMS, MM_PROTOCOL_SMS  etc. */
                         "protocol INTEGER NOT NULL, "
                         "smsc TEXT, "
                         /* For outgoing messages: the time message was delivered */
                         /* For incoming messages: the time message was sent */
                         "time_sent INTEGER, "
                         "validity INTEGER, "
                         "reference_number INTEGER);"

                         "CREATE TABLE IF NOT EXISTS message_files ("
                         "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
                         "message_id INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE, "
                         "file_id INTEGER NOT NULL REFERENCES files(id), "
                         "preview_id INTEGER REFERENCES files(id), "
                         "UNIQUE (message_id, file_id));"

                         /* Introduced in version 4, replaces 'audio', 'image' and 'video' */
                         "CREATE TABLE IF NOT EXISTS file_metadata ("
                         "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
                         "file_id INTEGER NOT NULL UNIQUE REFERENCES files(id) ON DELETE CASCADE, "
                         "width INTEGER, "
                         "height INTEGER, "
                         "duration INTEGER);"

                         "DELETE FROM accounts "
                         "WHERE protocol="STRING (PROTOCOL_MMS)" "
                         "AND accounts.user_id IN ("
                         "SELECT users.id FROM users "
                         "WHERE users.id=accounts.user_id "
                         "AND users.username='MMS' AND "
                         "users.type="STRING (CHATTY_ID_PHONE_VALUE)");"

                         "DELETE FROM users "
                         "WHERE username='MMS' AND type="STRING (CHATTY_ID_PHONE_VALUE)";"

                         "UPDATE users SET username='"MM_NUMBER"' "
                         "WHERE username='SMS' AND type="STRING (CHATTY_ID_PHONE_VALUE)";"

                         "INSERT INTO message_files(message_id,file_id) SELECT messages.id,files.id "
                         "FROM messages "
                         "INNER JOIN files ON messages.body=files.id "
                         "AND messages.body_type >=" STRING (MESSAGE_TYPE_FILE) " "
                         "AND messages.body_type <=" STRING (MESSAGE_TYPE_AUDIO)";"

                         "UPDATE messages SET body='' "
                         "WHERE body_type >=" STRING (MESSAGE_TYPE_FILE) " "
                         "AND body_type <=" STRING (MESSAGE_TYPE_AUDIO)";"

                         "INSERT INTO file_metadata(file_id,width,height,duration) "
                         "SELECT body,width,height,duration "
                         "FROM messages "
                         "INNER JOIN files ON files.id=body "
                         "INNER JOIN video ON video.file_id=files.id "
                         "AND messages.body_type =" STRING (MESSAGE_TYPE_VIDEO) ";"

                         "INSERT INTO file_metadata(file_id,width,height) "
                         "SELECT body,width,height "
                         "FROM messages "
                         "INNER JOIN files ON files.id=body "
                         "INNER JOIN image ON image.file_id=files.id "
                         "AND messages.body_type =" STRING (MESSAGE_TYPE_IMAGE) ";"

                         "INSERT INTO file_metadata(file_id,duration) "
                         "SELECT body,duration "
                         "FROM messages "
                         "INNER JOIN files ON files.id=body "
                         "INNER JOIN audio ON audio.file_id=files.id "
                         "AND messages.body_type =" STRING (MESSAGE_TYPE_AUDIO) ";"

                         /* We now have a single user for both sms and mms */
                         "DELETE FROM accounts "
                         "WHERE protocol="STRING(PROTOCOL_MMS)" "
                         "AND accounts.user_id IN ("
                         "SELECT users.id FROM users "
                         "WHERE users.id=accounts.user_id "
                         "AND users.username='MMS' AND "
                         "users.type="STRING (CHATTY_ID_PHONE_VALUE)");"

                         /* "UPDATE users SET username='"MM_NUMBER"' " */
                         /* "WHERE users.username='SMS' AND type="STRING (CHATTY_ID_PHONE_VALUE)";" */

                         /* We now have single 'meta_data' for */
                         "DROP TABLE IF EXISTS audio;"
                         "DROP TABLE IF EXISTS image;"
                         "DROP TABLE IF EXISTS video;"

                         "COMMIT;",
                         NULL, NULL, &error);

  if (status == SQLITE_OK || status == SQLITE_DONE) {
    /* Update user_version pragma */
    if (!chatty_history_update_version (self, task))
      return FALSE;
    return TRUE;
  }

  g_task_return_new_error (task,
                           G_IO_ERROR,
                           G_IO_ERROR_FAILED,
                           "Couldn't set db version. errno: %d, desc: %s. %s",
                           status, sqlite3_errstr (status), error);
  sqlite3_free (error);

  return FALSE;
}

static gboolean
chatty_history_migrate (ChattyHistory *self,
                        GTask         *task)
{
  int version;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  version = chatty_history_get_db_version (self, task);

  if (version == HISTORY_VERSION)
    return TRUE;

  switch (version) {
  case -1:  /* Error */
    return FALSE;

  case 0:
    if (!chatty_history_migrate_db_to_v1_to_v3 (self, task))
      return FALSE;
    break;

  case 1:
    if (!chatty_history_migrate_db_to_v2 (self, task))
      return FALSE;
    /* fallthrough */

  case 2:
    if (!chatty_history_migrate_db_to_v3 (self, task))
      return FALSE;
    /* fallthrough */

  case 3:
    if (!chatty_history_migrate_db_to_v4 (self, task))
      return FALSE;
    break;

  default:
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Failed to migrate from version %d, unknown version",
                             version);
    return FALSE;
  }

  return TRUE;
}

static void
history_open_db (ChattyHistory *self,
                 GTask         *task)
{
  const char *dir, *file_name;
  sqlite3 *db;
  int status;
  gboolean db_exists;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (!self->db);

  dir = g_object_get_data (G_OBJECT (task), "dir");
  file_name = g_object_get_data (G_OBJECT (task), "file-name");
  g_assert (dir && *dir);
  g_assert (file_name && *file_name);

  g_mkdir_with_parents (dir, S_IRWXU);
  self->db_path = g_build_filename (dir, file_name, NULL);

  db_exists = g_file_test (self->db_path, G_FILE_TEST_EXISTS);
  status = sqlite3_open (self->db_path, &db);

  if (status == SQLITE_OK) {
    self->db = db;

    if (db_exists) {
      if (!chatty_history_migrate (self, task))
        return;
    } else {
      if (!chatty_history_create_schema (self, task))
        return;

      if (!chatty_history_update_version (self, task))
        return;
    }

    sqlite3_exec (self->db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);
    g_task_return_boolean (task, TRUE);
  } else {
    g_task_return_boolean (task, FALSE);
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

static GList *
history_get_files (ChattyHistory *self,
                   int            message_id)
{
  sqlite3_stmt *stmt;
  GList *files = NULL;

  if (!message_id)
    return NULL;

  /*                                     0   1        2      3     4      5      6      7          8 */
  sqlite3_prepare_v2 (self->db, "SELECT url,path,files.name,size,status,width,height,duration,mime_type.name FROM files "
                      "INNER JOIN message_files "
                      "ON message_files.file_id=files.id "
                      "LEFT JOIN mime_type "
                      "ON mime_type.id=files.mime_type_id "
                      "LEFT JOIN file_metadata "
                      "ON file_metadata.file_id=files.id "
                      "WHERE message_files.message_id=?;", -1, &stmt, NULL);
  history_bind_int (stmt, 1, message_id, "binding when getting timestamp");

  while (sqlite3_step (stmt) == SQLITE_ROW) {
    ChattyFileInfo *file;

    file = g_new0 (ChattyFileInfo, 1);
    file->url = g_strdup ((const char *)sqlite3_column_text (stmt, 0));
    file->path = g_strdup ((const char *)sqlite3_column_text (stmt, 1));
    file->file_name = g_strdup ((const char *)sqlite3_column_text (stmt, 2));
    file->size = sqlite3_column_int (stmt, 3);
    file->status = sqlite3_column_int (stmt, 4);
    file->width = sqlite3_column_int (stmt, 5);
    file->height = sqlite3_column_int (stmt, 6);
    file->duration = sqlite3_column_int (stmt, 7);
    file->mime_type = g_strdup ((const char *)sqlite3_column_text (stmt, 8));

    files = g_list_append (files, file);
  }

  sqlite3_finalize (stmt);

  return files;
}

static GPtrArray *
get_messages_before_time (ChattyHistory *self,
                          ChattyChat    *chat,
                          ChattyMessage *start,
                          int            thread_id,
                          guint          since_time,
                          guint          limit)
{
  GPtrArray *messages = NULL;
  sqlite3_stmt *stmt;
  int status;
  gboolean skip = TRUE;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (CHATTY_IS_CHAT (chat));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (limit != 0);

  if (!start)
    skip = FALSE;

  status = sqlite3_prepare_v2 (self->db,
                                               /* 0      1      2    3                 4                         5 */
                               "SELECT DISTINCT time,direction,body,uid,coalesce(users.alias,users.username),body_type,"
                               /*    6            7           8               9            10             11 */
                               "p_files.name,p_files.url,p_files.path,p_mime_type.name,p_files.size,p_files.status,"
                                /* 12      13       14      */
                               "m.width,m.height,m.duration,"
                               /*     15            16        17 */
                               "messages.status,messages.id,subject "
                               "FROM messages "

                               "LEFT JOIN files AS p_files ON messages.preview_id=p_files.id "
                               "LEFT JOIN mime_type AS p_mime_type ON p_files.mime_type_id=p_mime_type.id "
                               "LEFT JOIN file_metadata AS m on p_files.id=m.file_id "
                               "LEFT JOIN users "
                               "ON messages.sender_id=users.id "
                               "WHERE thread_id=? "
                               "AND messages.time <= ? "
                               "AND body NOT NULL "
                               "AND (messages.status !=" STRING(MESSAGE_STATUS_DRAFT) " OR messages.status is null) "
                               "ORDER BY time DESC, messages.id DESC LIMIT ?;",
                               -1, &stmt, NULL);
  history_bind_int (stmt, 1, thread_id, "binding when getting messages");
  history_bind_int (stmt, 2, since_time, "binding when getting messages");
  history_bind_int (stmt, 3, limit, "binding when getting messages");

  while (sqlite3_step (stmt) == SQLITE_ROW) {
    ChattyFileInfo *preview = NULL;
    ChattyMessage *message;
    const char *msg = NULL, *uid;
    const char *who = NULL, *subject;
    ChattyMsgType type;
    guint time_stamp;
    int direction;

    uid = (const char *)sqlite3_column_text (stmt, 3);

    /* Skip until we pass the last message already in chat.
     * This can happen as we load all messages with time <= message,
     * which is required as there can be multiple messages with
     * same timestamp.
     */
    if (skip && start) {
      if (g_strcmp0 (uid, chatty_message_get_uid (start)) == 0)
        skip = FALSE;
      continue;
    }

    if (!messages)
      messages = g_ptr_array_new_full (30, g_object_unref);

    time_stamp = sqlite3_column_int (stmt, 0);
    direction = sqlite3_column_int (stmt, 1);
    type = history_value_to_message_type (sqlite3_column_int (stmt, 5));
    subject = (const char *)sqlite3_column_text (stmt, 17);
    msg = (const char *)sqlite3_column_text (stmt, 2);

    /* Skip if the message is empty and has no attachment */
    if ((!msg || !*msg) && (!subject || !*subject)) {
      sqlite3_stmt *check_stmt;

      status = sqlite3_prepare_v2 (self->db,
                                   "SELECT file_id "
                                   "FROM message_files "
                                   "WHERE message_id=? "
                                   "LIMIT 1;",
                                   -1, &check_stmt, NULL);
      history_bind_int (check_stmt, 1, sqlite3_column_int (stmt, 16), "binding when checking message files");

      status = sqlite3_step (check_stmt);
      sqlite3_finalize (check_stmt);

      if (status != SQLITE_ROW)
        continue;
    }

    /* preview is not limitted to media messages */
    if (sqlite3_column_text (stmt, 7)) {
      preview = g_new0 (ChattyFileInfo, 1);
      preview->file_name = g_strdup ((const char *)sqlite3_column_text (stmt, 6));
      preview->url = g_strdup ((const char *)sqlite3_column_text (stmt, 7));
      preview->path = g_strdup ((const char *)sqlite3_column_text (stmt, 8));
      preview->mime_type = g_strdup ((const char *)sqlite3_column_text (stmt, 9));
      preview->size = sqlite3_column_int (stmt, 10);
      preview->status = sqlite3_column_int (stmt, 11);
      preview->width = sqlite3_column_int (stmt, 12);
      preview->height = sqlite3_column_int (stmt, 13);
      preview->duration = sqlite3_column_int (stmt, 14);
    }

    if (!chatty_chat_is_im (chat) || CHATTY_IS_MA_CHAT (chat))
      who = (const char *)sqlite3_column_text (stmt, 4);

    status = sqlite3_column_int (stmt, 15);

    {
      g_autoptr(ChattyContact) contact = NULL;
      GList *files = NULL;
      int message_id;

      contact = g_object_new (CHATTY_TYPE_CONTACT, NULL);
      chatty_contact_set_name (contact, who);
      chatty_contact_set_value (contact, who);
      message = chatty_message_new (CHATTY_ITEM (contact), msg, uid, time_stamp, type,
                                    history_direction_from_value (direction),
                                    history_msg_status_from_value (status));
      message_id = sqlite3_column_int (stmt, 16);
      files = history_get_files (self, message_id);
      chatty_message_set_files (message, files);
    }

    chatty_message_set_subject (message, subject);
    chatty_message_set_preview (message, preview);
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
  guint limit;
  int thread_id, since = INT_MAX;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);

  if (!self->db) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Database not opened");
    return;
  }

  limit = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "limit"));
  start = g_object_get_data (G_OBJECT (task), "message");
  chat  = g_object_get_data (G_OBJECT (task), "chat");

  g_assert (limit != 0);
  g_assert (!start || CHATTY_IS_MESSAGE (start));
  g_assert (CHATTY_IS_CHAT (chat));

  if (start)
    since = chatty_message_get_time (start);

  thread_id = get_thread_id (self, chat);

  if (!thread_id) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                             "Couldn't find chat %s",
                             chatty_chat_get_chat_name (chat));
    return;
  }

  messages = get_messages_before_time (self, chat, start, thread_id, since, limit);
  g_task_return_pointer (task, messages, (GDestroyNotify)g_ptr_array_unref);
}

static void
history_get_chat_draft_message (ChattyHistory *self,
                                GTask         *task)
{
  sqlite3_stmt *stmt;
  ChattyChat *chat;
  int thread_id;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);

  if (!self->db) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Database not opened");
    return;
  }

  chat = g_object_get_data (G_OBJECT (task), "chat");

  g_assert (CHATTY_IS_CHAT (chat));

  thread_id = get_thread_id (self, chat);

  if (!thread_id) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                             "Couldn't find chat %s",
                             chatty_chat_get_chat_name (chat));
    return;
  }

  sqlite3_prepare_v2 (self->db,
                      "SELECT DISTINCT body "
                      "FROM messages "
                      "WHERE thread_id=? "
                      "AND messages.status=" STRING(MESSAGE_STATUS_DRAFT) " "
                      "ORDER BY time DESC, messages.id DESC LIMIT 1;",
                      -1, &stmt, NULL);

  history_bind_int (stmt, 1, thread_id, "binding when getting draft message");

  if (sqlite3_step (stmt) == SQLITE_ROW) {
    const char *msg = NULL;

    msg = (const char *)sqlite3_column_text (stmt, 0);
    g_task_return_pointer (task, g_strdup (msg), g_free);
  } else {
    g_task_return_pointer (task, NULL, NULL);
  }

  sqlite3_finalize (stmt);
}

static int
add_file_info (ChattyHistory  *self,
               ChattyFileInfo *file)
{
  sqlite3_stmt *stmt;
  int file_id = 0, mime_id = 0;
  int status;

  g_assert (CHATTY_IS_HISTORY (self));

  if (!file)
    return 0;

  if (file->mime_type) {
    sqlite3_prepare_v2 (self->db,
                        "INSERT OR IGNORE INTO mime_type(name) VALUES(?)",
                        -1, &stmt, NULL);
    history_bind_text (stmt, 1, file->mime_type, "binding when getting timestamp");
    sqlite3_step (stmt);
    sqlite3_finalize (stmt);

    sqlite3_prepare_v2 (self->db, "SELECT id FROM mime_type WHERE name=?" ,
                        -1, &stmt, NULL);
    history_bind_text (stmt, 1, file->mime_type, "binding when getting timestamp");
    if (sqlite3_step (stmt) == SQLITE_ROW)
      mime_id = sqlite3_column_int (stmt, 0);
    sqlite3_finalize (stmt);
  }

  if (file->status == CHATTY_FILE_DOWNLOADED)
    status = FILE_STATUS_DOWNLOADED;
  else if (file->status == CHATTY_FILE_DECRYPT_FAILED)
    status = FILE_STATUS_DECRYPT_FAILED;
  else if (file->status == CHATTY_FILE_MISSING)
    status = FILE_STATUS_MISSING;
  else
    status = 0;

  sqlite3_prepare_v2 (self->db, "SELECT id FROM files WHERE url=?", -1, &stmt, NULL);
  history_bind_text (stmt, 1, file->url, "binding when getting file");
  if (sqlite3_step (stmt) == SQLITE_ROW)
    file_id = sqlite3_column_int (stmt, 0);
  sqlite3_finalize (stmt);

  sqlite3_prepare_v2 (self->db,
                      "INSERT INTO files(name,url,path,mime_type_id,size,status) "
                      "VALUES(?1,?2,?3,?4,?5,?6) "
                      "ON CONFLICT(url) DO UPDATE SET path=?3, size=?5, status=?6",
                      -1, &stmt, NULL);
  history_bind_text (stmt, 1, file->file_name, "binding when adding file");
  history_bind_text (stmt, 2, file->url, "binding when adding file");
  history_bind_text (stmt, 3, file->path, "binding when adding file");
  if (mime_id)
    history_bind_int (stmt, 4, mime_id, "binding when adding file");
  if (file->size)
    history_bind_int (stmt, 5, file->size, "binding when adding file");
  if (status)
    history_bind_int (stmt, 6, status, "binding when adding file");
  sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  if (file_id)
    return file_id;

  file_id = sqlite3_last_insert_rowid (self->db);

  if (!file->width && !file->height && !file->duration)
    return file_id;

  sqlite3_prepare_v2 (self->db,
                      "INSERT INTO file_metadata(file_id,width,height,duration) "
                      "VALUES(?1,?2,?3,?4)",
                      -1, &stmt, NULL);

  history_bind_int (stmt, 1, file_id, "binding when adding media");

  if (file->width)
    history_bind_int (stmt, 2, file->width, "binding when adding media");
  if (file->height)
    history_bind_int (stmt, 3, file->height, "binding when adding media");
  if (file->duration)
    history_bind_int (stmt, 4, file->duration, "binding when adding media");

  sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  return file_id;
}

static void
history_add_files (ChattyHistory *self,
                   ChattyMessage *message,
                   int            message_id)
{
  GList *files;

  if (!CHATTY_IS_MESSAGE (message) || !message_id)
    return;

  files = chatty_message_get_files (message);

  for (GList *file = files; file; file = file->next) {
    sqlite3_stmt *stmt;
    int file_id;

    file_id = add_file_info (self, file->data);

    if (!file_id)
      continue;

    sqlite3_prepare_v2 (self->db,
                        "INSERT OR IGNORE INTO message_files(message_id,file_id) "
                        "VALUES(?1,?2)",
                        -1, &stmt, NULL);

    history_bind_int (stmt, 1, message_id, "binding when adding message file");
    history_bind_int (stmt, 2, file_id, "binding when adding message file");
    sqlite3_step (stmt);
    sqlite3_finalize (stmt);
  }
}

static int
get_chat_draft_id (ChattyHistory *self,
                   int            thread_id)
{
  int message_id = 0;
  sqlite3_stmt *stmt;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (thread_id);

  sqlite3_prepare_v2 (self->db,
                      "SELECT messages.id FROM messages "
                      "WHERE thread_id=? AND status=" STRING(MESSAGE_STATUS_DRAFT),
                      -1, &stmt, NULL);
  history_bind_int (stmt, 1, thread_id, "binding when getting draft message");

  if (sqlite3_step (stmt) == SQLITE_ROW)
    message_id = sqlite3_column_int (stmt, 0);

  sqlite3_finalize (stmt);

  return message_id;
}

static void
history_add_message (ChattyHistory *self,
                     GTask         *task)
{
  ChattyMessage *message;
  ChattyChat *chat;
  sqlite3_stmt *stmt;
  const char *who, *uid, *msg, *alias;
  ChattyMsgDirection direction;
  ChattyMsgType type;
  int thread_id = 0, sender_id = 0, preview_id = 0;
  int status, msg_status, dir;
  time_t time_stamp;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);

  if (!self->db) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Database not opened");
    return;
  }

  chat = g_object_get_data (G_OBJECT (task), "chat");
  message = g_object_get_data (G_OBJECT (task), "message");
  g_assert (CHATTY_IS_CHAT (chat));
  g_assert (CHATTY_IS_MESSAGE (message));

  who = chatty_message_get_user_name (message);
  uid = chatty_message_get_uid (message);
  msg = chatty_message_get_text (message);
  time_stamp = chatty_message_get_time (message);
  direction = chatty_message_get_msg_direction (message);
  dir = history_direction_to_value (direction);
  type = chatty_message_get_msg_type (message);
  alias = chatty_message_get_user_alias (message);
  msg_status = history_msg_status_to_value (chatty_message_get_status (message));

  if (direction == CHATTY_DIRECTION_OUT)
    who = chatty_item_get_username (CHATTY_ITEM (chat));

  if ((!who || !*who) && direction == CHATTY_DIRECTION_IN && chatty_chat_is_im (chat))
    who = chatty_chat_get_chat_name (chat);

  thread_id = insert_or_ignore_thread (self, chat, task);
  if (!thread_id)
    return;

  sender_id = insert_or_ignore_user (self, chatty_item_get_protocols (CHATTY_ITEM (chat)), who, alias, task);

  if (direction == CHATTY_DIRECTION_OUT &&
      msg_status == MESSAGE_STATUS_DRAFT) {
    int message_id = 0;

    if (!msg) {
      g_task_return_boolean (task, TRUE);
      return;
    }

    message_id = get_chat_draft_id (self, thread_id);

    if (message_id) {
      sqlite3_prepare_v2 (self->db,
                          "UPDATE messages SET body=?1 "
                          "WHERE messages.id=?2",
                          -1, &stmt, NULL);
      history_bind_text (stmt, 1, msg, "binding when adding draft message");
      history_bind_int (stmt, 2, message_id, "binding when adding draft message");
    } else {
      sqlite3_prepare_v2 (self->db,
                          /*                     ?1    ?2      ?3     ?4        ?5      ?6    ?7 */
                          "INSERT INTO messages(uid,thread_id,body,body_type,direction,time,status) "
                          "VALUES(?1,?2,?3,?4,?5,?6," STRING(MESSAGE_STATUS_DRAFT) ") ",
                          -1, &stmt, NULL);
      history_bind_text (stmt, 1, uid, "binding when adding draft message");
      history_bind_int (stmt, 2, thread_id, "binding when adding draft message");
      history_bind_text (stmt, 3, msg, "binding when adding draft message");
      history_bind_int (stmt, 4, history_message_type_to_value (type), "binding when adding draft message");
      history_bind_int (stmt, 5, dir, "binding when adding draft message");
      history_bind_int (stmt, 6, time_stamp, "binding when adding draft message");
    }

    status = sqlite3_step (stmt);
    sqlite3_finalize (stmt);

    if (status == SQLITE_DONE)
      g_task_return_boolean (task, TRUE);
    else
      g_task_return_new_error (task,
                               G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Failed to save message. errno: %d, desc: %s",
                               status, sqlite3_errmsg (self->db));
    return;
  }

  if (sender_id && direction == CHATTY_DIRECTION_IN) {
    sqlite3_prepare_v2 (self->db,
                        "INSERT OR IGNORE INTO thread_members(thread_id,user_id) "
                        "VALUES(?1,?2)",
                        -1, &stmt, NULL);
    history_bind_int (stmt, 1, thread_id, "binding when adding thread member");
    history_bind_int (stmt, 2, sender_id, "binding when adding thread member");
    sqlite3_step (stmt);
    sqlite3_finalize (stmt);
  }

  preview_id = add_file_info (self, chatty_message_get_preview (message));
  sqlite3_prepare_v2 (self->db,
                      "INSERT INTO messages(uid,thread_id,sender_id,body,body_type,direction,time,preview_id,encrypted,status,subject) "
                      "VALUES(?1,?2,?3,?4,"
                      "?5,?6,?7,?9,?10,?11,?12) "
                      "ON CONFLICT (uid,thread_id,body,time) DO UPDATE "
                      "SET status=?11",
                      -1, &stmt, NULL);

  history_bind_text (stmt, 1, uid, "binding when adding message");
  history_bind_int (stmt, 2, thread_id, "binding when adding message");
  if (sender_id)
    history_bind_int (stmt, 3, sender_id, "binding when adding message");
  history_bind_text (stmt, 4, msg, "binding when adding message");
  history_bind_int (stmt, 5, history_message_type_to_value (type),
                    "binding when adding message");
  history_bind_int (stmt, 6, dir, "binding when adding message");
  history_bind_int (stmt, 7, time_stamp, "binding when adding message");
  if (preview_id)
    history_bind_int (stmt, 9, preview_id, "binding when adding message");
  history_bind_int (stmt, 10, chatty_message_get_encrypted (message),
                    "binding when adding message");
  if (msg_status != MESSAGE_STATUS_UNKNOWN)
    history_bind_int (stmt, 11, msg_status, "binding when adding message");
  history_bind_text (stmt, 12, chatty_message_get_subject (message), "binding when adding message");

  status = sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  /* We can't use last_row_id as we may ignore the last insert */
  sqlite3_prepare_v2 (self->db,
                      "SELECT messages.id FROM messages "
                      "WHERE messages.uid=?;",
                      -1, &stmt, NULL);
  history_bind_text (stmt, 1, uid, "binding when getting message id");
  status = sqlite3_step (stmt);

  if (status == SQLITE_ROW)
    history_add_files (self, message, sqlite3_column_int (stmt, 0));
  sqlite3_finalize (stmt);

  if (status == SQLITE_DONE || status == SQLITE_ROW)
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Failed to save message. errno: %d, desc: %s",
                             status, sqlite3_errmsg (self->db));
}

static GPtrArray *
get_sms_thread_members (ChattyHistory *self,
                        int            thread_id)
{
  GPtrArray *members = NULL;
  sqlite3_stmt *stmt;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (g_thread_self () == self->worker_thread);

  sqlite3_prepare_v2 (self->db, "SELECT username,alias FROM users "
                      "INNER JOIN thread_members "
                      "ON thread_id=? AND user_id=users.id "
                      "WHERE users.username != 'SMS' AND users.username != 'MMS'",
                      -1, &stmt, NULL);
  history_bind_int (stmt, 1, thread_id, "binding when getting thread members");

  while (sqlite3_step (stmt) == SQLITE_ROW) {
    ChattyMmBuddy *buddy;
    const char *name, *alias;

    if (!members)
      members = g_ptr_array_new_full (30, g_object_unref);

    name = (const char *)sqlite3_column_text (stmt, 0);
    alias = (const char *)sqlite3_column_text (stmt, 1);

    buddy = chatty_mm_buddy_new (name, alias);
    g_ptr_array_insert (members, 0, buddy);
  }

  sqlite3_finalize (stmt);

  return members;
}

static guint
get_unread_message_count (ChattyHistory *self,
                          int            thread_id)
{
  sqlite3_stmt *stmt;
  guint unread_count = 0;

  g_assert (CHATTY_IS_HISTORY (self));

  if (!thread_id)
    return 0;

  sqlite3_prepare_v2 (self->db,
                      "SELECT COUNT(*) FROM messages "
                      "INNER JOIN threads "
                      /* We consider the message with last_read_id to be unread */
                      "ON messages.thread_id=threads.id AND messages.id >= threads.last_read_id  "
                      "WHERE messages.thread_id=? ",
                      -1, &stmt, NULL);
  history_bind_int (stmt, 1, thread_id, "binding when getting unread message count");

  if (sqlite3_step (stmt) == SQLITE_ROW)
    unread_count = sqlite3_column_int (stmt, 0);

  sqlite3_finalize (stmt);

  return unread_count;
}

static void
history_get_chats (ChattyHistory *self,
                   GTask         *task)
{
  GPtrArray *threads = NULL;
  ChattyAccount *account;
  sqlite3_stmt *stmt;
  const char *user_id;
  int protocol, encrypted;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);

  if (!self->db) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Database not opened");
    return;
  }

  account = g_object_get_data (G_OBJECT (task), "account");
  /* We currently handle only matrix accounts */
  g_assert (CHATTY_IS_MA_ACCOUNT (account) || CHATTY_IS_MM_ACCOUNT (account));

  user_id = chatty_item_get_username (CHATTY_ITEM (account));

  if (CHATTY_IS_MA_ACCOUNT (account))
    protocol = PROTOCOL_MATRIX;
  else
    protocol = PROTOCOL_MMS_SMS;

  sqlite3_prepare_v2 (self->db,
                      /*           0           1             2              3                4  */
                      "SELECT threads.id,threads.name,threads.alias,threads.encrypted,threads.type,"
                      "files.url,files.path,visibility "
                      "FROM threads "
                      "INNER JOIN accounts ON accounts.id=threads.account_id "
                      "INNER JOIN users ON users.id=accounts.user_id "
                      "AND users.username=? AND accounts.protocol=? "
                      "LEFT JOIN files ON threads.avatar_id=files.id "
                      "WHERE visibility!=" STRING(THREAD_VISIBILITY_HIDDEN),
                      -1, &stmt, NULL);
  history_bind_text (stmt, 1, user_id, "binding when getting threads");
  history_bind_int (stmt, 2, protocol, "binding when getting threads");

  while (sqlite3_step (stmt) == SQLITE_ROW) {
    g_autoptr(GPtrArray) messages = NULL;
    ChattyFileInfo *file = NULL;
    const char *name, *alias;
    ChattyChat *chat;
    int thread_id, visibility;

    if (!threads)
      threads = g_ptr_array_new_full (30, g_object_unref);

    thread_id = sqlite3_column_int (stmt, 0);
    name = (const char *)sqlite3_column_text (stmt, 1);
    alias = (const char *)sqlite3_column_text (stmt, 2);
    encrypted = sqlite3_column_int (stmt, 3);
    visibility = sqlite3_column_int (stmt, 7);

    if (sqlite3_column_text (stmt, 5)) {
      file = g_new0 (ChattyFileInfo, 1);
      file->url = g_strdup ((const char *)sqlite3_column_text (stmt, 5));
      file->path = g_strdup ((const char *)sqlite3_column_text (stmt, 6));
    }

    if (CHATTY_IS_MA_ACCOUNT (account)) {
      chat = (gpointer)chatty_ma_chat_new (name, alias, file, encrypted);
    } else if (sqlite3_column_int (stmt, 4) == THREAD_GROUP_CHAT) {
      chat = (gpointer)chatty_mm_chat_new (name, alias, CHATTY_PROTOCOL_MMS, FALSE,
                                           history_value_to_visibility (visibility));
    } else {
      chat = (gpointer)chatty_mm_chat_new (name, alias, CHATTY_PROTOCOL_MMS_SMS, TRUE,
                                           history_value_to_visibility (visibility));
    }

    messages = get_messages_before_time (self, chat, NULL, thread_id, INT_MAX, 1);

    /* For now, we handle unread count only for MM accounts */
    if (CHATTY_IS_MM_ACCOUNT (account)) {
      int unread_count;

      unread_count = get_unread_message_count (self, thread_id);
      chatty_chat_set_unread_count (chat, unread_count);
    }

    if (CHATTY_IS_MA_ACCOUNT (account))
      chatty_ma_chat_add_messages (CHATTY_MA_CHAT (chat), messages);
    else
      chatty_mm_chat_prepend_messages (CHATTY_MM_CHAT (chat), messages);

    g_ptr_array_insert (threads, -1, chat);

    if (protocol == PROTOCOL_MMS_SMS) {
      g_autoptr(GPtrArray) members = NULL;

      members = get_sms_thread_members (self, thread_id);
      chatty_mm_chat_add_users (CHATTY_MM_CHAT (chat), members);
    }
  }

  sqlite3_finalize (stmt);
  g_task_return_pointer (task, threads, (GDestroyNotify)g_ptr_array_unref);
}

static void
history_update_chat (ChattyHistory *self,
                     GTask         *task)
{
  ChattyChat *chat;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);

  if (!self->db) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Database not opened");
    return;
  }

  chat = g_object_get_data (G_OBJECT (task), "chat");
  g_assert (CHATTY_IS_CHAT (chat));

  if (insert_or_ignore_thread (self, chat, task))
    g_task_return_boolean (task, TRUE);
}

static void
history_update_user (ChattyHistory *self,
                     GTask         *task)
{
  ChattyAccount *account;
  const char *user_name, *name;
  ChattyFileInfo *file_info;
  sqlite3_stmt *stmt;
  ChattyProtocol protocol;
  int id, file_id = 0;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);

  if (!self->db) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Database not opened");
    return;
  }

  account = g_object_get_data (G_OBJECT (task), "account");
  g_assert (CHATTY_IS_ACCOUNT (account));

  user_name = chatty_item_get_username (CHATTY_ITEM (account));
  protocol = chatty_item_get_protocols (CHATTY_ITEM (account));
  name = chatty_item_get_name (CHATTY_ITEM (account));

  if (!user_name || !*user_name) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Username is empty");
    return;
  }

  id = insert_or_ignore_user (self, protocol, user_name, name, task);

  if (!id)
    return;

  file_info = chatty_item_get_avatar_file (CHATTY_ITEM (account));
  file_id = add_file_info (self, file_info);

  sqlite3_prepare_v2 (self->db,
                      "UPDATE users SET avatar_id=iif(?1 = 0, null, ?1) "
                      "WHERE users.id=?2",
                      -1, &stmt, NULL);
  history_bind_int (stmt, 1, file_id, "binding when setting avatar");
  history_bind_int (stmt, 2, id, "binding when settings avatar");

  sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  g_task_return_boolean (task, TRUE);
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
                             "Database not opened");
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

  account = chatty_item_get_username (CHATTY_ITEM (chat));

  status = sqlite3_prepare_v2 (self->db,
                               "DELETE FROM threads "
                               "WHERE threads.type=? AND threads.name=? "
                               "AND threads.account_id IN ("
                               "SELECT accounts.id FROM accounts "
                               "INNER JOIN users "
                               "ON accounts.id=threads.account_id "
                               "AND users.id=accounts.user_id AND users.username=?);",
                               -1, &stmt, NULL);

  history_bind_int (stmt, 1, chatty_chat_is_im (chat) ? THREAD_DIRECT_CHAT : THREAD_GROUP_CHAT,
                    "binding when deleting thread");
  history_bind_text (stmt, 2, chat_name, "binding when deleting thread");
  history_bind_text (stmt, 3, account, "binding when deleting thread");

  status = sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  if (status == SQLITE_DONE)
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Failed to delete chat. errno: %d, desc: %s",
                             status, sqlite3_errmsg (self->db));
}

static void
history_load_account (ChattyHistory *self,
                      GTask         *task)
{
  ChattyAccount *account;
  sqlite3_stmt *stmt;
  const char *user_name;
  int status;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);

  if (!self->db) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Database not opened");
    return;
  }

  account = g_object_get_data (G_OBJECT (task), "account");
  g_assert (CHATTY_IS_ACCOUNT (account));

  user_name = chatty_item_get_username (CHATTY_ITEM (account));

  if (!user_name || !*user_name) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Error: username name is empty");
    return;
  }

  sqlite3_prepare_v2 (self->db, "SELECT users.alias,files.url,files.path FROM users "
                      "LEFT JOIN files ON files.id=users.avatar_id "
                      "WHERE users.username=? LIMIT 1;", -1, &stmt, NULL);
  history_bind_text (stmt, 1, user_name, "binding when getting user details");

  if (sqlite3_step (stmt) == SQLITE_ROW) {
    const char *name, *avatar_url, *avatar_path;
    GObject *object;

    name = (char *)sqlite3_column_text (stmt, 0);
    avatar_url = (char *)sqlite3_column_text (stmt, 1);
    avatar_path = (char *)sqlite3_column_text (stmt, 2);

    object = G_OBJECT (task);
    g_object_set_data_full (object, "name", g_strdup (name), g_free);
    g_object_set_data_full (object, "avatar-url", g_strdup (avatar_url), g_free);
    g_object_set_data_full (object, "avatar-path", g_strdup (avatar_path), g_free);
  }

  status = sqlite3_finalize (stmt);
  warn_if_sql_error (status, "finalizing when getting user details");

  g_task_return_boolean (task, TRUE);
}

static void
history_set_last_read_msg (ChattyHistory *self,
                           GTask         *task)
{
  ChattyMessage *message;
  sqlite3_stmt *stmt;
  ChattyChat *chat;
  const char *uid = NULL;
  int thread_id, message_id = 0;

  g_assert (CHATTY_IS_HISTORY (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);

  if (!self->db) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Database not opened");
    return;
  }

  chat = g_object_get_data (G_OBJECT (task), "chat");
  message = g_object_get_data (G_OBJECT (task), "message");
  g_assert (CHATTY_IS_CHAT (chat));
  g_assert (!message || CHATTY_IS_MESSAGE (message));

  if (message)
    uid = chatty_message_get_uid (message);
  thread_id = insert_or_ignore_thread (self, chat, task);

  if (!thread_id)
    return;

  sqlite3_prepare_v2 (self->db,
                      "SELECT messages.id FROM messages "
                      "INNER JOIN threads ON threads.id=messages.thread_id AND threads.id=?"
                      "WHERE messages.uid=?;", -1, &stmt, NULL);
  history_bind_int (stmt, 1, thread_id, "binding when setting last read message");
  history_bind_text (stmt, 2, uid, "binding when setting last read message");

  if (sqlite3_step (stmt) == SQLITE_ROW)
    message_id = sqlite3_column_int (stmt, 0);
  sqlite3_finalize (stmt);

  sqlite3_prepare_v2 (self->db,
                      "UPDATE threads SET last_read_id=iif(?1 = 0, null, ?1) "
                      "WHERE threads.id=?2;", -1, &stmt, NULL);
  history_bind_int (stmt, 1, message_id, "binding when setting last read message");
  history_bind_int (stmt, 2, thread_id, "binding when setting last read message");
  sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  g_task_return_boolean (task, TRUE);
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

  uuid = g_object_get_data (G_OBJECT (task), "uuid");
  room = g_object_get_data (G_OBJECT (task), "room");

  g_assert (uuid);
  g_assert (room);

  sqlite3_prepare_v2 (self->db, "SELECT time FROM messages "
                      "INNER JOIN threads "
                      "ON threads.name=? "
                      "WHERE uid=? LIMIT 1;", -1, &stmt, NULL);
  history_bind_text (stmt, 1, room, "binding when getting timestamp");
  history_bind_text (stmt, 2, uuid, "binding when getting timestamp");

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

  uuid = g_object_get_data (G_OBJECT (task), "uuid");
  account = g_object_get_data (G_OBJECT (task), "account");

  sqlite3_prepare_v2 (self->db, "SELECT time FROM messages "
                      "INNER JOIN threads "
                      "ON threads.account_id=accounts.id "
                      "INNER JOIN accounts "
                      "ON accounts.user_id=users.id "
                      "INNER JOIN users "
                      "ON users.id=accounts.user_id AND users.username=? "
                      "WHERE messages.uid=? LIMIT 1",
                      -1, &stmt, NULL);
  history_bind_text (stmt, 1, account, "binding when getting timestamp");
  history_bind_text (stmt, 2, uuid, "binding when getting timestamp");

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

  account = g_object_get_data (G_OBJECT (task), "account");
  room = g_object_get_data (G_OBJECT (task), "room");

  status = sqlite3_prepare_v2 (self->db,
                               "SELECT max(time),messages.id FROM messages "
                               "INNER JOIN threads "
                               "ON threads.name=? AND messages.thread_id=threads.id "
                               "INNER JOIN accounts "
                               "ON accounts.id=threads.account_id "
                               "INNER JOIN users "
                               "ON users.id=accounts.user_id AND users.username=? "
                               "ORDER BY messages.id DESC LIMIT 1;",
                               -1, &stmt, NULL);
  history_bind_text (stmt, 1, room, "binding when getting timestamp");
  history_bind_text (stmt, 2, account, "binding when getting timestamp");

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

  sqlite3_prepare_v2 (self->db,
                      "SELECT time FROM messages "
                      "INNER JOIN threads "
                      "ON threads.name=? "
                      "INNER JOIN accounts "
                      "ON threads.account_id=accounts.id "
                      "INNER JOIN users "
                      "ON users.id=accounts.user_id AND users.username=? "
                      "WHERE messages.thread_id=threads.id LIMIT 1;",
                      -1, &stmt, NULL);

  if (room)
    history_bind_text (stmt, 1, room, "binding when getting timestamp");
  else
    history_bind_text (stmt, 1, who, "binding when getting timestamp");

  history_bind_text (stmt, 2, account, "binding when getting timestamp");

  if (sqlite3_step (stmt) == SQLITE_ROW)
    found = TRUE;

  status = sqlite3_finalize (stmt);
  warn_if_sql_error (status, "finalizing when getting timestamp");

  g_task_return_boolean (task, found);
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
  g_free (self->db_path);

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
 * chatty_history_new:
 *
 * Create a new #ChattyHistory
 *
 * Returns: (transfer full): A #ChattyHistory
 */
ChattyHistory *
chatty_history_new (void)
{
  return g_object_new (CHATTY_TYPE_HISTORY, NULL);
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
  g_autoptr(GTask) task = NULL;
  const char *country;

  g_return_if_fail (CHATTY_IS_HISTORY (self));
  g_return_if_fail (dir && *dir);
  g_return_if_fail (file_name && *file_name);

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, chatty_history_open_async);
  g_task_set_task_data (task, history_open_db, NULL);

  if (self->db) {
    g_warning ("A DataBase is already open");
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_ALREADY_MOUNTED,
                             "Database is already open");
    return;
  }

  if (!self->worker_thread)
    self->worker_thread = g_thread_new ("chatty-history-worker",
                                        chatty_history_worker,
                                        self);

  country = chatty_settings_get_country_iso_code (chatty_settings_get_default ());
  g_object_set_data_full (G_OBJECT (task), "dir", dir, g_free);
  g_object_set_data_full (G_OBJECT (task), "file-name", g_strdup (file_name), g_free);
  g_object_set_data_full (G_OBJECT (task), "country-code", g_strdup (country), g_free);

  g_async_queue_push (self->queue, g_steal_pointer (&task));
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

void
chatty_history_get_draft_async (ChattyHistory       *self,
                                ChattyChat          *chat,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (CHATTY_IS_HISTORY (self));
  g_return_if_fail (CHATTY_IS_CHAT (chat));

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, chatty_history_get_draft_async);
  g_task_set_task_data (task, history_get_chat_draft_message, NULL);
  g_object_set_data_full (G_OBJECT (task), "chat", g_object_ref (chat), g_object_unref);

  g_async_queue_push_front (self->queue, task);
}

char *
chatty_history_get_draft_finish (ChattyHistory  *self,
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

void
chatty_history_get_chats_async (ChattyHistory       *self,
                                ChattyAccount       *account,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  GTask *task;
  const char *protocol;

  g_return_if_fail (CHATTY_IS_HISTORY (self));
  g_return_if_fail (CHATTY_IS_ACCOUNT (account));

  /* Currently we handle only matrix and SMS accounts */
  protocol = chatty_account_get_protocol_name (account);
  if (!g_str_equal (protocol, "Matrix") &&
      !g_str_equal (protocol, "SMS"))
    g_return_if_reached ();

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, chatty_history_get_chats_async);
  g_task_set_task_data (task, history_get_chats, NULL);
  g_object_set_data_full (G_OBJECT (task), "account", g_object_ref (account), g_object_unref);

  g_async_queue_push (self->queue, task);
}

GPtrArray *
chatty_history_get_chats_finish (ChattyHistory  *self,
                                 GAsyncResult   *result,
                                 GError        **error)
{
  g_return_val_if_fail (CHATTY_IS_HISTORY (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);
  g_return_val_if_fail (!error || !*error, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

gboolean
chatty_history_update_chat (ChattyHistory *self,
                            ChattyChat    *chat)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(GError) error = NULL;
  gboolean status;

  g_return_val_if_fail (CHATTY_IS_HISTORY (self), FALSE);
  g_return_val_if_fail (CHATTY_IS_CHAT (chat), FALSE);

  task = g_task_new (NULL, NULL, NULL, NULL);
  g_object_ref (task);
  g_task_set_source_tag (task, chatty_history_update_chat);
  g_task_set_task_data (task, history_update_chat, NULL);
  g_object_set_data_full (G_OBJECT (task), "chat", g_object_ref (chat), g_object_unref);

  g_async_queue_push (self->queue, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  status = g_task_propagate_boolean (task, &error);

  if (error)
    g_warning ("Error updating chat: %s", error->message);

  return status;
}

gboolean
chatty_history_update_user (ChattyHistory *self,
                            ChattyAccount *account)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(GError) error = NULL;
  gboolean status;

  g_return_val_if_fail (CHATTY_IS_HISTORY (self), FALSE);
  g_return_val_if_fail (CHATTY_IS_ACCOUNT (account), FALSE);

  task = g_task_new (NULL, NULL, NULL, NULL);
  g_object_ref (task);
  g_task_set_source_tag (task, chatty_history_update_user);
  g_task_set_task_data (task, history_update_user, NULL);
  g_object_ref (account);
  g_object_set_data_full (G_OBJECT (task), "account", account, g_object_unref);

  g_async_queue_push (self->queue, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  status = g_task_propagate_boolean (task, &error);

  if (error)
    g_warning ("Error updating user: %s", error->message);

  return status;
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

void
chatty_history_load_account_async (ChattyHistory       *self,
                                   ChattyAccount       *account,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (CHATTY_IS_HISTORY (self));
  g_return_if_fail (CHATTY_IS_ACCOUNT (account));
  g_return_if_fail (callback);

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, chatty_history_load_account_async);
  g_task_set_task_data (task, history_load_account, NULL);
  g_object_ref (account);
  g_object_set_data_full (G_OBJECT (task), "account", account, g_object_unref);

  g_async_queue_push (self->queue, task);
}

gboolean
chatty_history_load_account_finish (ChattyHistory  *self,
                                    GAsyncResult   *result,
                                    GError        **error)
{
  g_return_val_if_fail (CHATTY_IS_HISTORY (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

void
chatty_history_set_last_read_msg (ChattyHistory *self,
                                  ChattyChat    *chat,
                                  ChattyMessage *message)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (self->db);
  g_return_if_fail (CHATTY_IS_HISTORY (self));
  g_return_if_fail (CHATTY_IS_MM_CHAT (chat));
  g_return_if_fail (!message || CHATTY_IS_MESSAGE (message));

  if (message)
    g_object_ref (message);

  task = g_task_new (NULL, NULL, NULL, NULL);
  g_object_ref (task);
  g_task_set_task_data (task, history_set_last_read_msg, NULL);
  g_object_set_data_full (G_OBJECT (task), "chat", g_object_ref (chat), g_object_unref);
  g_object_set_data_full (G_OBJECT (task), "message", message, g_object_unref);

  g_async_queue_push (self->queue, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);
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
 * @self: A #ChattyHistory
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
chatty_history_open (ChattyHistory *self,
                     const char    *dir,
                     const char    *file_name)
{
  g_autoptr(GTask) task = NULL;

  task = g_task_new (NULL, NULL, NULL, NULL);
  chatty_history_open_async (self, g_strdup (dir), file_name, finish_cb, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);
}

/**
 * chatty_history_close:
 * @self: A #ChattyHistory
 *
 * Close database opened with default #ChattyHistory,
 * if any.
 *
 * This method runs synchronously.
 *
 */
void
chatty_history_close (ChattyHistory *self)
{
  g_autoptr(GTask) task = NULL;

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
 * @self: A #ChattyHistory
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
chatty_history_get_chat_timestamp (ChattyHistory *self,
                                   const char    *uuid,
                                   const char    *room)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = NULL;
  int time_stamp;

  g_return_val_if_fail (uuid, 0);
  g_return_val_if_fail (room, 0);

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
 * @self: A #ChattyHistory
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
chatty_history_get_im_timestamp (ChattyHistory *self,
                                 const char    *uuid,
                                 const char    *account)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = NULL;
  int time_stamp;

  g_return_val_if_fail (CHATTY_IS_HISTORY (self), 0);
  g_return_val_if_fail (uuid, 0);
  g_return_val_if_fail (account, 0);

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
 * @self: A #ChattyHistory
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
chatty_history_get_last_message_time (ChattyHistory *self,
                                      const char    *account,
                                      const char    *room)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = NULL;
  int time_stamp;

  g_return_val_if_fail (CHATTY_IS_HISTORY (self), 0);
  g_return_val_if_fail (account, 0);
  g_return_val_if_fail (room, 0);
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
 * @self: A #ChattyHistory
 * @chat: a #ChattyChat
 *
 * Delete all messages matching @chat
 * from default #ChattyHistory.
 *
 * This method runs synchronously.
 *
 */
void
chatty_history_delete_chat (ChattyHistory *self,
                            ChattyChat    *chat)
{
  g_autoptr(GTask) task = NULL;

  task = g_task_new (NULL, NULL, NULL, NULL);
  chatty_history_delete_chat_async (self, chat, finish_cb, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);
}

static gboolean
chatty_history_exists (ChattyHistory *self,
                       const char    *account,
                       const char    *room,
                       const char    *who)
{

  g_autoptr(GTask) task = NULL;

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
 * @self: A #ChattyHistory
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
chatty_history_im_exists (ChattyHistory *self,
                          const char    *account,
                          const char    *who)
{
  g_return_val_if_fail (account, 0);
  g_return_val_if_fail (who, 0);

  return chatty_history_exists (self, account, NULL, who);
}

/**
 * chatty_history_exists:
 * @self: A #ChattyHistory
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
chatty_history_chat_exists (ChattyHistory *self,
                            const char    *account,
                            const char    *room)
{
  g_return_val_if_fail (account, 0);
  g_return_val_if_fail (room, 0);

  return chatty_history_exists (self, account, room, NULL);
}

/**
 * chatty_history_add_message:
 * @self: A #ChattyHistory
 * @chat: the #ChattyChat @message belongs to
 * @message: A #ChattyMessage
 *
 * This method runs synchronously.
 *
 * Return: %TRUE if the message was stored to database.
 * %FALSE otherwise.
 */
gboolean
chatty_history_add_message (ChattyHistory *self,
                            ChattyChat    *chat,
                            ChattyMessage *message)
{
  g_autoptr(GTask) task = NULL;

  task = g_task_new (NULL, NULL, NULL, NULL);
  chatty_history_add_message_async (self, chat, message, finish_cb, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  return g_task_propagate_boolean (task, NULL);
}

