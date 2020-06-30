/*
 * Copyright (C) 2018 Purism SPC
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

#include "chatty-history.h"
#include "chatty-utils.h"
#include <sqlite3.h>
#include <glib.h>
#include "stdio.h"
#include <sys/stat.h>
#include <limits.h>

static sqlite3 *db;


int
get_chat_timestamp_for_uuid(const char *uuid, const char *room)
{

  int rc;
  sqlite3_stmt *stmt;
  unsigned int timestamp  = INT_MAX;

  rc = sqlite3_prepare_v2(db, "SELECT timestamp FROM chatty_chat WHERE uid=(?) AND room=(?)", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    g_debug("Error preparing when getting timestamp for uuid (CHAT). errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, 1, uuid, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
    g_debug("Error binding when getting timestamp for uuid (CHAT) errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, 2, room, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
    g_debug("Error binding when getting timestamp for uuid (CHAT) errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  while ((sqlite3_step(stmt)) == SQLITE_ROW) {
    timestamp = sqlite3_column_int(stmt, 0);
  }

  rc = sqlite3_finalize(stmt);
  if (rc != SQLITE_OK)
    g_debug("Error finalizing when getting timestamp for uuid (CHAT) errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  return timestamp;
}


int
get_im_timestamp_for_uuid(const char *uuid, const char *account)
{

  int rc;
  sqlite3_stmt *stmt;
  unsigned int timestamp  = INT_MAX;

  rc = sqlite3_prepare_v2(db, "SELECT timestamp FROM chatty_im WHERE uid=(?) AND account=(?)", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    g_debug("Error preparing when getting timestamp for uuid (IM). errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, 1, uuid, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
    g_debug("Error binding when getting timestamp for uuid (IM) errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, 2, account, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
    g_debug("Error binding when getting timestamp for uuid (IM) errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  while ((sqlite3_step(stmt)) == SQLITE_ROW) {
    timestamp = sqlite3_column_int(stmt, 0);
  }

  rc = sqlite3_finalize(stmt);
  if (rc != SQLITE_OK)
    g_debug("Error finalizing when getting timestamp for uuid (IM) errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  return timestamp;
}


static void
chatty_history_create_chat_schema (void)
{
  int rc;
  char *sql;
  char *zErrMsg = 0;

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

  rc = sqlite3_exec(db, sql, NULL, NULL, &zErrMsg);

  if( rc != SQLITE_OK ){
    g_debug("Error when creating chatty_chat table. errno: %d, desc: %s. %s", rc, sqlite3_errmsg(db), zErrMsg);
    sqlite3_free(zErrMsg);
  } else {
    g_debug("chatty_chat table created successfully");
  }

  // The archiving entity is room jid, uid may only be unique within entity scope
  sql = "CREATE UNIQUE INDEX IF NOT EXISTS chatty_chat_room_uid ON chatty_chat(room, uid);";

  rc = sqlite3_exec(db, sql, NULL, NULL, &zErrMsg);

  g_debug("Create Index chatty_chat_accuid: %s", (rc == SQLITE_OK)? "OK" : zErrMsg);

}


static void
chatty_history_create_im_schema (void)
{
  int rc;
  char *sql;
  char *zErrMsg = 0;

  sql = "CREATE TABLE IF NOT EXISTS chatty_im("  \
    "id                 INTEGER     NOT NULL    PRIMARY KEY AUTOINCREMENT," \
    "timestamp          INTEGER     NOT_NULL,"  \
    "direction          INTEGER     NOT NULL," \
    "account            TEXT        NOT_NULL," \
    "who                TEXT        NOT_NULL," \
    "uid                TEXT        NOT_NULL," \
    "message            TEXT," \
    "UNIQUE (timestamp, message)"
    ");";

  rc = sqlite3_exec(db, sql, NULL, NULL, &zErrMsg);

  if( rc != SQLITE_OK ){
    g_debug("Error when creating chatty_im table. errno: %d, desc: %s. %s", rc, sqlite3_errmsg(db), zErrMsg);
  } else {
    g_debug("chatty_im table created successfully");
  }

  // The archiving entity is bare jid, uid may only be unique within entity scope
  sql = "CREATE UNIQUE INDEX IF NOT EXISTS chatty_im_acc_uid ON chatty_im(account, uid);";

  rc = sqlite3_exec(db, sql, NULL, NULL, &zErrMsg);

  g_debug("Create Index chatty_im_accuid: %s", (rc == SQLITE_OK)? "OK" : zErrMsg);

}


static void
chatty_history_create_schemas (void)
{
  chatty_history_create_chat_schema();
  chatty_history_create_im_schema();
}


int
chatty_history_open (const char *dir,
                     const char *file_name)
{
  int rc;
  g_autofree char *db_path = NULL;

  g_assert (dir && *dir);
  g_assert (file_name && *file_name);

  if(db == NULL){
    g_mkdir_with_parents (dir, S_IRWXU);
    db_path = g_build_filename (dir, file_name, NULL);
    rc = sqlite3_open(db_path, &db);

    if (rc != SQLITE_OK){
      g_debug("Database could not be opened. errno: %d, desc: %s", rc, sqlite3_errmsg(db));
    } else {
      g_debug("Database opened successfully");
    }

    chatty_history_create_schemas();
  }

  return 1;

}


void
chatty_history_close (void)
{
  int rc;

  if(db != NULL){
    rc = sqlite3_close(db);

    if (rc != SQLITE_OK){
      g_debug("Database could not be closed. errno: %d, desc: %s", rc, sqlite3_errmsg(db));
    } else {
      g_debug("Database closed successfully");
    }
  }

  db = NULL;
}


void
chatty_history_add_chat_message ( const char *message,
                                  int         direction,
                                  const char *account,
                                  const char *who,
                                  const char *uid,
                                  time_t      mtime,
                                  const char *room)
{

  sqlite3_stmt *stmt;
  int rc;

  rc = sqlite3_prepare_v2(db, "INSERT INTO chatty_chat VALUES (?, ?, ?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
      g_debug("Error preparing statement when adding CHAT message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, CHAT_MESSAGE_IDX, message, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding value when adding CHAT message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_int(stmt, CHAT_DIRECTION_IDX, direction);
  if (rc != SQLITE_OK)
      g_debug("Error binding value when adding CHAT message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, CHAT_ACCOUNT_IDX, account, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding value when adding CHAT message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, CHAT_UID_IDX, uid, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding value when adding CHAT message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_int64(stmt, CHAT_TIMESTAMP_IDX, mtime);
  if (rc != SQLITE_OK)
      g_debug("Error binding value when adding CHAT message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, CHAT_ROOM_IDX, room, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding value when adding CHAT message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, CHAT_WHO_IDX, who, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding value when adding CHAT message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE)
      g_debug("Error in step when adding CHAT message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_finalize(stmt);
  if (rc != SQLITE_OK)
    g_debug("Error finalizing when adding CHAT message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

}


void
chatty_history_add_im_message (const char *message,
                              int         direction,
                              const char *account,
                              const char *who,
                              const char *uid,
                              time_t      mtime)
{

  sqlite3_stmt *stmt;
  int rc;

  rc = sqlite3_prepare_v2(db, "INSERT INTO chatty_im VALUES (?, ?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
      g_debug("Error preparing statement when adding IM message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, IM_MESSAGE_IDX, message, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding value when adding IM message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_int(stmt, IM_DIRECTION_IDX, direction);
  if (rc != SQLITE_OK)
      g_debug("Error binding value when adding IM message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, IM_WHO_IDX, who, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding value when adding IM message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, IM_ACCOUNT_IDX, account, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding value when adding IM message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, IM_UID_IDX, uid, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding value when adding IM message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_int64(stmt, IM_TIMESTAMP_IDX, mtime);
  if (rc != SQLITE_OK)
      g_debug("Error binding value when adding IM message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE)
      g_debug("Error in step when adding IM message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_finalize(stmt);
  if (rc != SQLITE_OK)
      g_debug("Error finalizing when adding IM message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

}


int
chatty_history_get_chat_last_message_time (const char* account,
                                           const char* room)
{

  int rc;
  sqlite3_stmt *stmt;
  int time_stamp  = 0;

  rc = sqlite3_prepare_v2(db, "SELECT max(timestamp),max(id)  FROM chatty_chat WHERE room=(?)", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
      g_debug("Error preparing when getting chat last message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, 1, room, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding when getting chat last message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  while ((sqlite3_step(stmt)) == SQLITE_ROW) {
      time_stamp = sqlite3_column_int(stmt, 0);
  }

  rc = sqlite3_finalize(stmt);
  if (rc != SQLITE_OK)
      g_debug("Error finalizing when getting chat last message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  return time_stamp;
}


char
chatty_history_get_im_last_message (const char *account,
                                    const char *who,
                                    ChattyLog  *chatty_log)
{
  int           rc;
  sqlite3_stmt *stmt;

  rc = sqlite3_prepare_v2(db, "SELECT message,direction,max(timestamp),uid,max(id) FROM chatty_im WHERE account=(?) AND who=(?)", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    g_debug("Error preparing when getting chat last message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, 1, account, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
    g_debug("Error binding when getting chat last message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, 2, who, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
    g_debug("Error binding when getting chat last message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  while ((sqlite3_step(stmt)) == SQLITE_ROW) {
    chatty_log->msg = (char *) g_strdup((const gchar *) sqlite3_column_text(stmt, 0));
    chatty_log->dir = sqlite3_column_int(stmt, 1);
    chatty_log->epoch = sqlite3_column_int(stmt, 2);
    chatty_log->uid = g_strdup((const char *)sqlite3_column_text(stmt, 3));
  }

  rc = sqlite3_finalize(stmt);
  if (rc != SQLITE_OK)
    g_debug("Error finalizing when getting chat last message. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  // epoch is 0 if no messages are found in the query
  // the max() query ALWAYS contains a row.
  // TODO: @LELAND: Do something better here
  return chatty_log->epoch;
}


void
chatty_history_get_chat_messages (const char *account,
                                  const char *room,
                                  void (*cb)( const unsigned char *msg,
                                              int direction,
                                              int time_stamp,
                                              const char *room,
                                              const unsigned char *who,
                                              const unsigned char *uuid,
                                              gpointer data),
                                  gpointer    data,
                                  guint       limit,
                                  const char *oldest_message_displayed)
{

  int                  rc;
  sqlite3_stmt        *stmt;
  const unsigned char *msg;
  int                  time_stamp;
  int                  direction;
  const unsigned char *who;
  const unsigned char* uuid;
  int                  from_timestamp;
  char                 skip;

  from_timestamp = get_chat_timestamp_for_uuid(oldest_message_displayed, room);

  rc = sqlite3_prepare_v2(db, "SELECT timestamp,direction,message,who,uid FROM chatty_chat WHERE account=(?) AND room=(?) AND timestamp <= (?) ORDER BY timestamp DESC, id DESC LIMIT (?)", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
      g_debug("Error preparing statement when querying CHAT messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, 1, account, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding values when querying CHAT messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, 2, room, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding values when querying CHAT messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_int(stmt, 3, from_timestamp);
  if (rc != SQLITE_OK)
    g_debug("Error binding values when querying CHAT messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_int(stmt, 4, limit);
  if (rc != SQLITE_OK)
    g_debug("Error binding values when querying CHAT messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  skip = oldest_message_displayed != NULL;
  while ((sqlite3_step(stmt)) == SQLITE_ROW) {
      time_stamp = sqlite3_column_int(stmt, 0);
      direction = sqlite3_column_int(stmt, 1);
      msg = sqlite3_column_text(stmt, 2);
      who = sqlite3_column_text(stmt, 3);
      uuid = sqlite3_column_text(stmt, 4);

      if (skip){
        skip = g_strcmp0(oldest_message_displayed, (const char *)uuid);
      } else {
        cb(msg, direction, time_stamp, room, who, uuid, data);
      }

  }

  rc = sqlite3_finalize(stmt);
  if (rc != SQLITE_OK)
    g_debug("Error finalizing when querying CHAT messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

}


void
chatty_history_get_im_messages (const char* account,
                                const char* who,
                                void (*cb)(const unsigned char* msg,
                                           int                  direction,
                                           time_t               time_stamp,
                                           const unsigned char  *uuid,
                                           gpointer             data,
                                           int                  last_message),
                                gpointer   data,
                                guint      limit,
                                const char *oldest_message_displayed)
{

  int rc;
  sqlite3_stmt        *stmt;
  const unsigned char *msg;
  int                  time_stamp;
  int                  direction;
  int                  first;
  const unsigned char* uuid;
  int                  from_timestamp;
  char                 skip;

  from_timestamp = get_im_timestamp_for_uuid(oldest_message_displayed, account);

   // Then, fetch the result and detect the last row.
  rc = sqlite3_prepare_v2(db, "SELECT timestamp,direction,message,uid FROM chatty_im WHERE account=(?) AND who=(?) AND timestamp <= (?) ORDER BY timestamp DESC, id DESC LIMIT (?)", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
      g_debug("Error preparing statement when querying IM messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, 1, account, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding when querying IM messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, 2, who, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
    g_debug("Error binding when querying IM messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_int(stmt, 3, from_timestamp);
  if (rc != SQLITE_OK)
    g_debug("Error binding when querying IM messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_int(stmt, 4, limit);
  if (rc != SQLITE_OK)
    g_debug("Error binding when querying IM messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  skip = oldest_message_displayed != NULL;
  first = 1;
  while ((sqlite3_step(stmt)) == SQLITE_ROW) {
    time_stamp = sqlite3_column_int(stmt, 0);
    direction = sqlite3_column_int(stmt, 1);
    msg = sqlite3_column_text(stmt, 2);
    uuid = sqlite3_column_text(stmt, 3);

    // TODO: @LELAND: This approach would return a variable number of messages
    // in case a burst of messages were received for the same epoch
    // If this is an issue, we should use a more fine grain timestamp.
    if (skip){
      skip = g_strcmp0(oldest_message_displayed, (const char *) uuid);
    } else {
      cb(msg, direction, time_stamp, uuid, data, first);
      first = 0;
    }
  }

  rc = sqlite3_finalize(stmt);
  if (rc != SQLITE_OK)
      g_debug("Error finalizing when querying IM messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

}


void
chatty_history_delete_chat (const char* account,
                            const char* room){
  int rc;
  sqlite3_stmt *stmt;

  rc = sqlite3_prepare_v2(db, "DELETE FROM chatty_chat WHERE account=(?) AND room=(?)", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
      g_debug("Error preparing statement when deleting CHAT messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, 1, account, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding when deleting CHAT messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, 2, room, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding when deleting CHAT messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  sqlite3_step(stmt);
  if (rc != SQLITE_OK)
      g_debug("Error in step when deleting CHAT messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_finalize(stmt);
  if (rc != SQLITE_OK)
      g_debug("Error finalizing when deleting CHAT messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

}


void
chatty_history_delete_im (const char *account,
                          const char *who){
  int rc;
  sqlite3_stmt *stmt;

  rc = sqlite3_prepare_v2(db, "DELETE FROM chatty_im WHERE account=(?) AND who=(?)", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
      g_debug("Error preparing statement when deleting IM messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, 1, account, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding when deleting IM messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_bind_text(stmt, 2, who, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK)
      g_debug("Error binding when deleting IM messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));
                                                            //
  sqlite3_step(stmt);
  if (rc != SQLITE_OK)
      g_debug("Error in step when deleting IM messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

  rc = sqlite3_finalize(stmt);
  if (rc != SQLITE_OK)
      g_debug("Error finalizing when deleting IM messages. errno: %d, desc: %s", rc, sqlite3_errmsg(db));

}


void
chatty_history_add_message (PurpleAccount *pa, PurpleConvMessage *pcm,
                            char **sid, PurpleConversationType type,
                            gpointer data)
{
  int dir = 0;

  /* direction of message */
  if (pcm->flags & PURPLE_MESSAGE_SYSTEM)
    dir = 0;
  else if (pcm->flags & PURPLE_MESSAGE_RECV)
    dir = 1;
  else if (pcm->flags & PURPLE_MESSAGE_SEND)
    dir = -1;

  // MAM XEP for one should set it to take over the history
  if(pcm->flags & PURPLE_MESSAGE_NO_LOG)
    return;

  g_debug ("Add History: ID:%s, Acc:%s, Who:%s, Room:%s, Flags:%d, Dir:%d, Type:%d, TS:%ld, Body:%s",
              *sid, pa->username, pcm->who, pcm->alias, pcm->flags, dir, type, pcm->when, pcm->what);

  if(sid != NULL && *sid == NULL)
    *sid = g_uuid_string_random ();

  if (type == PURPLE_CONV_TYPE_CHAT) {
    chatty_history_add_chat_message(pcm->what, dir, pa->username, pcm->who,
                                    *sid, pcm->when, pcm->alias);
  } else {
    chatty_history_add_im_message(pcm->what, dir, pa->username, pcm->who,
                                  *sid, pcm->when);
  }
}
