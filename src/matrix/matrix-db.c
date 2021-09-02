/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* matrix-db.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* This is heavily based on chatty-history */

#define G_LOG_DOMAIN "matrix-db"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib.h>
#include <fcntl.h>
#include <sqlite3.h>

#include "chatty-utils.h"
#include "matrix-enc.h"
#include "matrix-db.h"

struct _MatrixDb
{
  GObject      parent_instance;

  GAsyncQueue *queue;
  GThread     *worker_thread;
  sqlite3     *db;
};

/*
 * MatrixDb->db should never be accessed nor modified in main thread
 * except for checking if it’s %NULL.  Any operation should be done only
 * in @worker_thread.  Don't reuse the same #MatrixDb once closed.
 */

typedef void (*MatrixDbCallback) (MatrixDb *self,
                                  GTask *task);

G_DEFINE_TYPE (MatrixDb, matrix_db, G_TYPE_OBJECT)

static void
warn_if_sql_error (int         status,
                   const char *message)
{
  if (status == SQLITE_OK || status == SQLITE_ROW || status == SQLITE_DONE)
    return;

  g_warning ("Error %s. errno: %d, message: %s", message, status, sqlite3_errstr (status));
}

static void
matrix_bind_text (sqlite3_stmt *statement,
                   guint         position,
                   const char   *bind_value,
                   const char   *message)
{
  guint status;

  status = sqlite3_bind_text (statement, position, bind_value, -1, SQLITE_TRANSIENT);
  warn_if_sql_error (status, message);
}

static void
matrix_bind_int (sqlite3_stmt *statement,
                  guint         position,
                  int           bind_value,
                  const char   *message)
{
  guint status;

  status = sqlite3_bind_int (statement, position, bind_value);
  warn_if_sql_error (status, message);
}

static int
matrix_db_create_schema (MatrixDb *self,
                         GTask    *task)
{
  const char *sql;
  char *error = NULL;
  int status;

  g_assert (MATRIX_IS_DB (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  sql = "BEGIN TRANSACTION;"

    "CREATE TABLE IF NOT EXISTS devices ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "device TEXT NOT NULL UNIQUE);"

    "CREATE TABLE IF NOT EXISTS users ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "username TEXT NOT NULL, "
    "device_id INTEGER REFERENCES devices(id), "
    "UNIQUE (username, device_id));"

    "CREATE TABLE IF NOT EXISTS accounts ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "user_id INTEGER NOT NULL REFERENCES users(id), "
    "next_batch TEXT, "
    "pickle TEXT, "
    "enabled INTEGER DEFAULT 0, "
    "UNIQUE (user_id));"

    "CREATE TABLE IF NOT EXISTS rooms ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "account_id INTEGER NOT NULL REFERENCES accounts(id), "
    "room_name TEXT NOT NULL, "
    "prev_batch TEXT, "
    "UNIQUE (account_id, room_name));"

    "CREATE TABLE IF NOT EXISTS encryption_keys ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "file_url TEXT NOT NULL, "
    "file_sha256 TEXT, "
    /* Initialization vector: iv in JSON */
    "iv TEXT NOT NULL, "
    /* v in JSON */
    "version INT DEFAULT 2 NOT NULL, "
    /* alg in JSON */
    "algorithm INT NOT NULL, "
    /* k in JSON */
    "key TEXT NOT NULL, "
    /* kty in JSON */
    "type INT NOT NULL, "
    /* ext in JSON */
    "extractable INT DEFAULT 1 NOT NULL, "
    "UNIQUE (file_url));"

    /* TODO: Someday */
    "CREATE TABLE IF NOT EXISTS session ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "
    "account_id INTEGER NOT NULL REFERENCES accounts(id), "
    "sender_key TEXT NOT NULL, "
    "session_id TEXT NOT NULL, "
    "type INTEGER NOT NULL, "
    "pickle TEXT NOT NULL, "
    "time INT, "
    "UNIQUE (account_id, sender_key, session_id));"

    "COMMIT;";

  status = sqlite3_exec (self->db, sql, NULL, NULL, &error);

  if (status != SQLITE_OK) {
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Error creating chatty_im table. errno: %d, desc: %s. %s",
                             status, sqlite3_errmsg (self->db), error);
    return status;
  }

  return status;
}


static void
matrix_open_db (MatrixDb *self,
                GTask    *task)
{
  const char *dir, *file_name;
  g_autofree char *db_path = NULL;
  sqlite3 *db;
  int status;

  g_assert (MATRIX_IS_DB (self));
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
    status = matrix_db_create_schema (self, task);

    if (status == SQLITE_OK)
      g_task_return_boolean (task, TRUE);
  } else {
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Database could not be opened. errno: %d, desc: %s",
                             status, sqlite3_errmsg (db));
    sqlite3_close (db);
  }
}

static void
matrix_close_db (MatrixDb *self,
                 GTask    *task)
{
  sqlite3 *db;
  int status;

  g_assert (MATRIX_IS_DB (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  db = self->db;
  self->db = NULL;
  status = sqlite3_close (db);

  if (status == SQLITE_OK) {
    /*
     * We can’t know when will @self associated with the task will
     * be unref.  So matrix_db_get_default() called immediately
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

static void
matrix_db_save_account (MatrixDb *self,
                        GTask    *task)
{
  sqlite3_stmt *stmt;
  const char *device, *pickle, *username, *batch;
  int status, device_id = 0, user_id = 0;
  gboolean enabled;

  g_assert (MATRIX_IS_DB (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  batch = g_object_get_data (G_OBJECT (task), "batch");
  pickle = g_object_get_data (G_OBJECT (task), "pickle");
  device = g_object_get_data (G_OBJECT (task), "device");
  enabled = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "enabled"));
  username = g_object_get_data (G_OBJECT (task), "username");

  if (device) {
    sqlite3_prepare_v2 (self->db,
                        "INSERT OR IGNORE INTO devices(device) VALUES(?1)",
                        -1, &stmt, NULL);

    matrix_bind_text (stmt, 1, device, "binding when updating device");
    sqlite3_step (stmt);
    sqlite3_finalize (stmt);

    sqlite3_prepare_v2 (self->db,
                        "SELECT id FROM devices WHERE device=?",
                        -1, &stmt, NULL);
    matrix_bind_text (stmt, 1, device, "binding when getting device id");

    if (sqlite3_step (stmt) == SQLITE_ROW)
      device_id = sqlite3_column_int (stmt, 0);
    sqlite3_finalize (stmt);
  }


  /* Find user id with no device id set and have an associated account */
  sqlite3_prepare_v2 (self->db,
                      "SELECT users.id FROM users "
                      "INNER JOIN accounts ON user_id=users.id "
                      "WHERE username=?1 AND device_id IS NULL LIMIT 1",
                      -1, &stmt, NULL);
  matrix_bind_text (stmt, 1, username, "binding when getting user");

  if (sqlite3_step (stmt) == SQLITE_ROW)
    user_id = sqlite3_column_int (stmt, 0);

  if (user_id) {
    sqlite3_prepare_v2 (self->db,
                        "UPDATE users SET device_id=? WHERE users.id=?",
                        -1, &stmt, NULL);
    if (device_id)
      matrix_bind_int (stmt, 1, device_id, "binding when updating existing user");
    matrix_bind_int (stmt, 2, user_id, "binding when updating existing user");
    user_id = 0;
  } else {
    sqlite3_prepare_v2 (self->db,
                        "INSERT OR IGNORE INTO users(username,device_id) VALUES(?1,?2)",
                        -1, &stmt, NULL);
    matrix_bind_text (stmt, 1, username, "binding when updating user");
    if (device_id)
      matrix_bind_int (stmt, 2, device_id, "binding when updating user");
  }

  sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  sqlite3_prepare_v2 (self->db,
                      "SELECT id FROM users WHERE username=?1 AND (device_id=?2 "
                      "OR (device_id IS NULL AND ?2 IS NULL)) LIMIT 1",
                      -1, &stmt, NULL);
  matrix_bind_text (stmt, 1, username, "binding when getting user id");
  if (device_id)
    matrix_bind_int (stmt, 2, device_id, "binding when getting user id");

  if (sqlite3_step (stmt) == SQLITE_ROW)
    user_id = sqlite3_column_int (stmt, 0);
  sqlite3_finalize (stmt);

  sqlite3_prepare_v2 (self->db,
                      "INSERT INTO accounts(user_id,pickle,next_batch,enabled) "
                      "VALUES(?1,?2,?3,?4) "
                      "ON CONFLICT(user_id) "
                      "DO UPDATE SET pickle=?2, next_batch=?3, enabled=?4",
                      -1, &stmt, NULL);

  matrix_bind_int (stmt, 1, user_id, "binding when updating account");
  if (pickle && *pickle)
    matrix_bind_text (stmt, 2, pickle, "binding when updating account");
  matrix_bind_text (stmt, 3, batch, "binding when updating account");
  matrix_bind_int (stmt, 4, enabled, "binding when updating account");

  status = sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  if (status == SQLITE_DONE)
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "Error saving account. errno: %d, desc: %s",
                             status, sqlite3_errmsg (self->db));
}

/* Find the account id of matching @username and @device_id,
 * The item should already be in the db, otherwise it's an
 * error.
 */
static int
matrix_db_get_account_id (MatrixDb   *self,
                          GTask      *task,
                          const char *username,
                          const char *device_name)
{
  sqlite3_stmt *stmt;
  const char *error;
  int status;

  g_assert (username && *username);
  g_assert (device_name && *device_name);

  sqlite3_prepare_v2 (self->db,
                      "SELECT accounts.id FROM accounts "
                      "INNER JOIN users ON username=? "
                      "INNER JOIN devices ON device_id=users.device_id AND devices.device=? "
                      "WHERE accounts.user_id=users.id",
                      -1, &stmt, NULL);
  matrix_bind_text (stmt, 1, username, "binding when getting account id");
  matrix_bind_text (stmt, 2, device_name, "binding when getting account id");

  status = sqlite3_step (stmt);
  if (status == SQLITE_ROW)
    return sqlite3_column_int (stmt, 0);

  if (status == SQLITE_DONE)
    error = "Account not found in db";
  else
    error = sqlite3_errmsg (self->db);

  g_task_return_new_error (task,
                           G_IO_ERROR,
                           G_IO_ERROR_FAILED,
                           "Couldn't find user %s. error: %s",
                           username, error);
  return 0;
}

static void
matrix_db_load_account (MatrixDb *self,
                        GTask    *task)
{
  sqlite3_stmt *stmt;
  char *username, *device_id;
  int status;

  g_assert (MATRIX_IS_DB (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  username = g_object_get_data (G_OBJECT (task), "username");
  device_id = g_object_get_data (G_OBJECT (task), "device");

  status = sqlite3_prepare_v2 (self->db,
                               "SELECT enabled,pickle,next_batch,device "
                               "FROM accounts "
                               "INNER JOIN users "
                               "ON users.username=?1 AND users.id=user_id "
                               "LEFT JOIN devices "
                               "ON device_id=devices.id AND devices.device=?2 "
                               "WHERE (?2 is NULL AND device is NULL) OR (?2 is NOT NULL AND device=?2)",
                               -1, &stmt, NULL);
  matrix_bind_text (stmt, 1, username, "binding when loading account");
  matrix_bind_text (stmt, 2, device_id, "binding when loading account");

  status = sqlite3_step (stmt);

  if (status == SQLITE_ROW) {
    GObject *object = G_OBJECT (task);

    g_object_set_data (object, "enabled", GINT_TO_POINTER (sqlite3_column_int (stmt, 0)));
    g_object_set_data_full (object, "pickle", g_strdup ((char *)sqlite3_column_text (stmt, 1)), g_free);
    g_object_set_data_full (object, "batch", g_strdup ((char *)sqlite3_column_text (stmt, 2)), g_free);
    g_object_set_data_full (object, "device", g_strdup ((char *)sqlite3_column_text (stmt, 3)), g_free);
  }

  sqlite3_finalize (stmt);
  g_task_return_boolean (task, status == SQLITE_ROW);
}

static void
matrix_db_save_room (MatrixDb *self,
                     GTask    *task)
{
  sqlite3_stmt *stmt;
  const char *username, *room_name, *batch, *account_device;
  int status, account_id;

  g_assert (MATRIX_IS_DB (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  batch = g_object_get_data (G_OBJECT (task), "prev-batch");
  username = g_object_get_data (G_OBJECT (task), "account-id");
  room_name = g_object_get_data (G_OBJECT (task), "room");
  account_device = g_object_get_data (G_OBJECT (task), "account-device");

  account_id = matrix_db_get_account_id (self, task, username, account_device);

  if (!account_id)
    return;

  sqlite3_prepare_v2 (self->db,
                      "INSERT INTO rooms(account_id,room_name,prev_batch) "
                      "VALUES(?1,?2,?3) "
                      "ON CONFLICT(account_id, room_name) "
                      "DO UPDATE SET prev_batch=?3",
                      -1, &stmt, NULL);
  matrix_bind_int (stmt, 1, account_id, "binding when saving room");
  matrix_bind_text (stmt, 2, room_name, "binding when saving room");
  matrix_bind_text (stmt, 3, batch, "binding when saving room");

  status = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  g_task_return_boolean (task, status == SQLITE_DONE);
}

static void
matrix_db_load_room (MatrixDb *self,
                     GTask    *task)
{
  const char *username, *room_name, *account_device;
  sqlite3_stmt *stmt;
  int account_id;

  g_assert (MATRIX_IS_DB (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  username = g_object_get_data (G_OBJECT (task), "account-id");
  room_name = g_object_get_data (G_OBJECT (task), "room");
  account_device = g_object_get_data (G_OBJECT (task), "account-device");

  account_id = matrix_db_get_account_id (self, task, username, account_device);

  if (!account_id)
    return;

  sqlite3_prepare_v2 (self->db,
                      "SELECT prev_batch FROM rooms "
                      "WHERE account_id=? AND room_name=? ",
                      -1, &stmt, NULL);

  matrix_bind_int (stmt, 1, account_id, "binding when loading room");
  matrix_bind_text (stmt, 2, room_name, "binding when loading room");

  if (sqlite3_step (stmt) == SQLITE_ROW)
    g_task_return_pointer (task, g_strdup ((char *)sqlite3_column_text (stmt, 0)), g_free);
  else
    g_task_return_pointer (task, NULL, NULL);

  sqlite3_finalize (stmt);
}

static void
matrix_db_delete_account (MatrixDb *self,
                          GTask    *task)
{
  sqlite3_stmt *stmt;
  char *username;
  int status;

  g_assert (MATRIX_IS_DB (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  username = g_object_get_data (G_OBJECT (task), "username");

  status = sqlite3_prepare_v2 (self->db,
                               "DELETE FROM accounts "
                               "WHERE accounts.id IN ("
                               "SELECT users.id FROM users "
                               "WHERE users.username=?)",
                               -1, &stmt, NULL);
  matrix_bind_text (stmt, 1, username, "binding when loading account");

  status = sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  g_task_return_boolean (task, status == SQLITE_ROW);
}

static void
matrix_db_add_session (MatrixDb *self,
                       GTask    *task)
{
  sqlite3_stmt *stmt;
  const char *username, *account_device, *session_id, *sender_key, *pickle;
  MatrixSessionType type;
  int status, account_id;

  g_assert (MATRIX_IS_DB (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  type = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "type"));

  pickle = g_object_get_data (G_OBJECT (task), "pickle");
  username = g_object_get_data (G_OBJECT (task), "account-id");
  session_id = g_object_get_data (G_OBJECT (task), "session-id");
  sender_key = g_object_get_data (G_OBJECT (task), "sender-key");
  account_device = g_object_get_data (G_OBJECT (task), "account-device");

  account_id = matrix_db_get_account_id (self, task, username, account_device);

  if (!account_id)
    return;

  status = sqlite3_prepare_v2 (self->db,
                               "INSERT INTO session(account_id,sender_key,session_id,type,pickle) "
                               "VALUES(?1,?2,?3,?4,?5)",
                               -1, &stmt, NULL);

  matrix_bind_int (stmt, 1, account_id, "binding when adding session");
  matrix_bind_text (stmt, 2, sender_key, "binding when adding session");
  matrix_bind_text (stmt, 3, session_id, "binding when adding session");
  matrix_bind_int (stmt, 4, type, "binding when adding session");
  matrix_bind_text (stmt, 5, pickle, "binding when adding session");

  status = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  g_task_return_boolean (task, status == SQLITE_DONE);
}

static void
matrix_db_save_file_url (MatrixDb *self,
                         GTask    *task)
{
  ChattyFileInfo *file;
  MatrixFileEncInfo *enc;
  sqlite3_stmt *stmt;
  int status, version, type, extractable, algorithm;

  g_assert (MATRIX_IS_DB (self));
  g_assert (G_IS_TASK (task));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (self->db);

  file = g_object_get_data (G_OBJECT (task), "file");
  g_return_if_fail (file && file->user_data);
  enc = file->user_data;

  type = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "type"));
  version = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "version"));
  algorithm = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "algorithm"));
  extractable = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "extractable"));

  status = sqlite3_prepare_v2 (self->db,
                               "INSERT INTO encryption_keys(file_url,file_sha256,"
                               "iv,version,algorithm,key,type,extractable) "
                               "VALUES(?1,?2,?3,?4,?5,?6,?7,?8)",
                               -1, &stmt, NULL);

  matrix_bind_text (stmt, 1, file->url, "binding when adding file url");
  matrix_bind_text (stmt, 2, enc->sha256_base64, "binding when adding file url");
  matrix_bind_text (stmt, 3, enc->aes_iv_base64, "binding when adding file url");
  matrix_bind_int (stmt, 4, version, "binding when adding file url");
  matrix_bind_int (stmt, 5, algorithm, "binding when adding file url");
  matrix_bind_text (stmt, 6, enc->aes_key_base64, "binding when adding file url");
  matrix_bind_int (stmt, 7, type, "binding when adding file url");
  matrix_bind_int (stmt, 8, extractable, "binding when adding file url");

  status = sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  g_task_return_boolean (task, status == SQLITE_DONE);
}

static void
db_lookup_session (MatrixDb *self,
                   GTask    *task)
{
  sqlite3_stmt *stmt;
  const char *username, *account_device, *session_id, *sender_key;
  char *pickle = NULL;
  MatrixSessionType type;
  int status, account_id;

  g_assert (MATRIX_IS_DB (self));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (G_IS_TASK (task));

  type = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "type"));

  pickle = g_object_get_data (G_OBJECT (task), "pickle");
  username = g_object_get_data (G_OBJECT (task), "account-id");
  session_id = g_object_get_data (G_OBJECT (task), "session-id");
  sender_key = g_object_get_data (G_OBJECT (task), "sender-key");
  account_device = g_object_get_data (G_OBJECT (task), "account-device");

  account_id = matrix_db_get_account_id (self, task, username, account_device);

  if (!account_id)
    return;

  sqlite3_prepare_v2 (self->db,
                      "SELECT pickle FROM session "
                      "WHERE account_id=? AND sender_key=? AND session_id=? AND type=?",
                      -1, &stmt, NULL);

  matrix_bind_int (stmt, 1, account_id, "binding when adding session");
  matrix_bind_text (stmt, 2, sender_key, "binding when looking up session");
  matrix_bind_text (stmt, 3, session_id, "binding when looking up session");
  matrix_bind_int (stmt, 4, type, "binding when looking up session");

  status = sqlite3_step (stmt);

  if (status == SQLITE_ROW)
    pickle = g_strdup ((char *)sqlite3_column_text (stmt, 0));

  sqlite3_finalize (stmt);
  g_task_return_pointer (task, pickle, g_free);
}

static void
history_get_olm_sessions (MatrixDb *self,
                          GTask    *task)
{
  GPtrArray *sessions = NULL;
  sqlite3_stmt *stmt;
  int status;

  g_assert (MATRIX_IS_DB (self));
  g_assert (g_thread_self () == self->worker_thread);
  g_assert (G_IS_TASK (task));

  status = sqlite3_prepare_v2 (self->db,
                               "SELECT sender,sender_key,session_pickle FROM olm_session "
                               "ORDER BY id DESC LIMIT 100", -1, &stmt, NULL);

  warn_if_sql_error (status, "getting olm sessions");

  while (sqlite3_step (stmt) == SQLITE_ROW) {
    MatrixDbData *data;

    if (!sessions)
      sessions = g_ptr_array_new_full (100, NULL);

    data = g_new (MatrixDbData, 1);
    data->sender = g_strdup ((char *)sqlite3_column_text (stmt, 0));
    data->sender_key = g_strdup ((char *)sqlite3_column_text (stmt, 1));
    data->session_pickle = g_strdup ((char *)sqlite3_column_text (stmt, 2));

    g_ptr_array_insert (sessions, 0, data);
  }

  status = sqlite3_finalize (stmt);
  warn_if_sql_error (status, "finalizing when getting olm sessions");

  g_task_return_pointer (task, sessions, (GDestroyNotify)g_ptr_array_unref);
}

static gpointer
matrix_db_worker (gpointer user_data)
{
  MatrixDb *self = user_data;
  GTask *task;

  g_assert (MATRIX_IS_DB (self));

  while ((task = g_async_queue_pop (self->queue))) {
    MatrixDbCallback callback;

    g_assert (task);
    callback = g_task_get_task_data (task);
    callback (self, task);
    g_object_unref (task);

    if (callback == matrix_close_db)
      break;
  }

  return NULL;
}

static void
matrix_db_dispose (GObject *object)
{
  MatrixDb *self = (MatrixDb *)object;

  g_clear_pointer (&self->worker_thread, g_thread_unref);

  G_OBJECT_CLASS (matrix_db_parent_class)->dispose (object);
}

static void
matrix_db_finalize (GObject *object)
{
  MatrixDb *self = (MatrixDb *)object;

  if (self->db)
    g_warning ("Database not closed");

  g_clear_pointer (&self->queue, g_async_queue_unref);

  G_OBJECT_CLASS (matrix_db_parent_class)->finalize (object);
}

static void
matrix_db_class_init (MatrixDbClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose  = matrix_db_dispose;
  object_class->finalize = matrix_db_finalize;
}

static void
matrix_db_init (MatrixDb *self)
{
  self->queue = g_async_queue_new ();
}

MatrixDb *
matrix_db_new (void)
{
  return g_object_new (MATRIX_TYPE_DB, NULL);
}

/**
 * matrix_db_open_async:
 * @self: a #MatrixDb
 * @dir: (transfer full): The database directory
 * @file_name: The file name of database
 * @callback: a #GAsyncReadyCallback, or %NULL
 * @user_data: closure data for @callback
 *
 * Open the database file @file_name from path @dir.
 * Complete with matrix_db_open_finish() to get
 * the result.
 */
void
matrix_db_open_async (MatrixDb            *self,
                      char                *dir,
                      const char          *file_name,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (MATRIX_IS_DB (self));
  g_return_if_fail (dir || !*dir);
  g_return_if_fail (file_name || !*file_name);

  if (self->db) {
    g_warning ("A DataBase is already open");
    return;
  }

  if (!self->worker_thread)
    self->worker_thread = g_thread_new ("matrix-db-worker",
                                        matrix_db_worker,
                                        self);

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, matrix_db_open_async);
  g_task_set_task_data (task, matrix_open_db, NULL);
  g_object_set_data_full (G_OBJECT (task), "dir", dir, g_free);
  g_object_set_data_full (G_OBJECT (task), "file-name", g_strdup (file_name), g_free);

  g_async_queue_push (self->queue, task);
}

/**
 * matrix_db_open_finish:
 * @self: a #MatrixDb
 * @result: a #GAsyncResult provided to callback
 * @error: a location for a #GError or %NULL
 *
 * Completes opening a database started with
 * matrix_db_open_async().
 *
 * Returns: %TRUE if database was opened successfully.
 * %FALSE otherwise with @error set.
 */
gboolean
matrix_db_open_finish (MatrixDb      *self,
                       GAsyncResult  *result,
                       GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_DB (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * matrix_db_is_open:
 * @self: a #MatrixDb
 *
 * Get if the database is open or not
 *
 * Returns: %TRUE if a database is open.
 * %FALSE otherwise.
 */
gboolean
matrix_db_is_open (MatrixDb *self)
{
  g_return_val_if_fail (MATRIX_IS_DB (self), FALSE);

  return !!self->db;
}

/**
 * matrix_db_close_async:
 * @self: a #MatrixDb
 * @callback: a #GAsyncReadyCallback, or %NULL
 * @user_data: closure data for @callback
 *
 * Close the database opened.
 * Complete with matrix_db_close_finish() to get
 * the result.
 */
void
matrix_db_close_async (MatrixDb            *self,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (MATRIX_IS_DB (self));

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, matrix_db_close_async);
  g_task_set_task_data (task, matrix_close_db, NULL);

  g_async_queue_push (self->queue, task);
}

/**
 * matrix_db_open_finish:
 * @self: a #MatrixDb
 * @result: a #GAsyncResult provided to callback
 * @error: a location for a #GError or %NULL
 *
 * Completes closing a database started with
 * matrix_db_close_async().  @self is
 * g_object_unref() if closing succeeded.
 * So @self will be freed if you haven’t kept
 * your own reference on @self.
 *
 * Returns: %TRUE if database was closed successfully.
 * %FALSE otherwise with @error set.
 */
gboolean
matrix_db_close_finish (MatrixDb      *self,
                        GAsyncResult  *result,
                        GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_DB (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

void
matrix_db_save_account_async (MatrixDb            *self,
                              ChattyAccount       *account,
                              gboolean             enabled,
                              char                *pickle,
                              const char          *device_id,
                              const char          *next_batch,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  GObject *object;
  GTask *task;
  const char *username;

  g_return_if_fail (MATRIX_IS_DB (self));
  g_return_if_fail (CHATTY_IS_ACCOUNT (account));

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, matrix_db_save_account_async);
  g_task_set_task_data (task, matrix_db_save_account, NULL);

  object = G_OBJECT (task);
  username = chatty_item_get_username (CHATTY_ITEM (account));

  if (g_application_get_default ())
    g_application_hold (g_application_get_default ());

  g_object_set_data (object, "enabled", GINT_TO_POINTER (enabled));
  g_object_set_data_full (object, "pickle", pickle, g_free);
  g_object_set_data_full (object, "device", g_strdup (device_id), g_free);
  g_object_set_data_full (object, "batch", g_strdup (next_batch), g_free);
  g_object_set_data_full (object, "username", g_strdup (username), g_free);
  g_object_set_data_full (object, "account", g_object_ref (account), g_object_unref);

  g_async_queue_push (self->queue, task);
}

gboolean
matrix_db_save_account_finish (MatrixDb      *self,
                               GAsyncResult  *result,
                               GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_DB (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  if (g_application_get_default ())
    g_application_release (g_application_get_default ());

  return g_task_propagate_boolean (G_TASK (result), error);
}

void
matrix_db_load_account_async (MatrixDb            *self,
                              ChattyAccount       *account,
                              const char          *device_id,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  GTask *task;
  const char *username;

  g_return_if_fail (!device_id || *device_id);
  g_return_if_fail (MATRIX_IS_DB (self));
  g_return_if_fail (CHATTY_IS_ACCOUNT (account));

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, matrix_db_load_account_async);
  g_task_set_task_data (task, matrix_db_load_account, NULL);

  username = chatty_item_get_username (CHATTY_ITEM (account));
  g_object_set_data_full (G_OBJECT (task), "device", g_strdup (device_id), g_free);
  g_object_set_data_full (G_OBJECT (task), "username", g_strdup (username), g_free);
  g_object_set_data_full (G_OBJECT (task), "account", g_object_ref (account), g_object_unref);

  g_async_queue_push (self->queue, task);
}

gboolean
matrix_db_load_account_finish (MatrixDb      *self,
                               GAsyncResult  *result,
                               GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_DB (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

void
matrix_db_save_room_async (MatrixDb            *self,
                           ChattyAccount       *account,
                           const char          *account_device,
                           const char          *room_id,
                           const char          *prev_batch,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  GTask *task;
  const char *username;

  g_return_if_fail (MATRIX_IS_DB (self));
  g_return_if_fail (CHATTY_IS_ACCOUNT (account));
  g_return_if_fail (room_id && *room_id == '!');

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, matrix_db_save_room_async);
  g_task_set_task_data (task, matrix_db_save_room, NULL);

  username = chatty_item_get_username (CHATTY_ITEM (account));
  g_object_set_data_full (G_OBJECT (task), "room", g_strdup (room_id), g_free);
  g_object_set_data_full (G_OBJECT (task), "username", g_strdup (username), g_free);
  g_object_set_data_full (G_OBJECT (task), "account-id", g_strdup (username), g_free);
  g_object_set_data_full (G_OBJECT (task), "prev-batch", g_strdup (prev_batch), g_free);
  g_object_set_data_full (G_OBJECT (task), "account", g_object_ref (account), g_object_unref);
  g_object_set_data_full (G_OBJECT (task), "account-device", g_strdup (account_device), g_free);

  g_async_queue_push (self->queue, task);
}

gboolean
matrix_db_save_room_finish (MatrixDb      *self,
                            GAsyncResult  *result,
                            GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_DB (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

void
matrix_db_load_room_async (MatrixDb            *self,
                           ChattyAccount       *account,
                           const char          *account_device,
                           const char          *room_id,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  GTask *task;
  const char *username;

  g_return_if_fail (MATRIX_IS_DB (self));
  g_return_if_fail (CHATTY_IS_ACCOUNT (account));
  g_return_if_fail (room_id && *room_id == '!');

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, matrix_db_load_room_async);
  g_task_set_task_data (task, matrix_db_load_room, NULL);

  username = chatty_item_get_username (CHATTY_ITEM (account));
  g_object_set_data_full (G_OBJECT (task), "room", g_strdup (room_id), g_free);
  g_object_set_data_full (G_OBJECT (task), "username", g_strdup (username), g_free);
  g_object_set_data_full (G_OBJECT (task), "account-id", g_strdup (username), g_free);
  g_object_set_data_full (G_OBJECT (task), "account", g_object_ref (account), g_object_unref);
  g_object_set_data_full (G_OBJECT (task), "account-device", g_strdup (account_device), g_free);

  g_async_queue_push (self->queue, task);
}

char *
matrix_db_load_room_finish (MatrixDb      *self,
                            GAsyncResult  *result,
                            GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_DB (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_pointer (G_TASK (result), error);
}

void
matrix_db_delete_account_async (MatrixDb            *self,
                                ChattyAccount       *account,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  GTask *task;
  const char *username;

  g_return_if_fail (MATRIX_IS_DB (self));
  g_return_if_fail (CHATTY_IS_ACCOUNT (account));

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, matrix_db_delete_account_async);
  g_task_set_task_data (task, matrix_db_delete_account, NULL);

  username = chatty_item_get_username (CHATTY_ITEM (account));
  g_object_set_data_full (G_OBJECT (task), "username", g_strdup (username), g_free);
  g_object_set_data_full (G_OBJECT (task), "account", g_object_ref (account), g_object_unref);

  g_async_queue_push (self->queue, task);
}

gboolean
matrix_db_delete_account_finish (MatrixDb      *self,
                                 GAsyncResult  *result,
                                 GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_DB (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

void
matrix_db_add_session_async (MatrixDb            *self,
                             const char          *account_id,
                             const char          *account_device,
                             const char          *room_id,
                             const char          *session_id,
                             const char          *sender_key,
                             char                *pickle,
                             MatrixSessionType    type,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  GObject *object;
  GTask *task;

  g_return_if_fail (MATRIX_IS_DB (self));
  g_return_if_fail (account_id && *account_id);
  g_return_if_fail (account_device && *account_device);
  g_return_if_fail (room_id && *room_id);
  g_return_if_fail (session_id && *session_id);
  g_return_if_fail (sender_key && *sender_key);
  g_return_if_fail (pickle && *pickle);

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, matrix_db_add_session_async);
  g_task_set_task_data (task, matrix_db_add_session, NULL);
  object = G_OBJECT (task);

  g_object_set_data_full (object, "account-id", g_strdup (account_id), g_free);
  g_object_set_data_full (object, "account-device", g_strdup (account_device), g_free);
  g_object_set_data_full (object, "room-id", g_strdup (room_id), g_free);
  g_object_set_data_full (object, "session-id", g_strdup (session_id), g_free);
  g_object_set_data_full (object, "sender-key", g_strdup (sender_key), g_free);
  g_object_set_data_full (object, "pickle", pickle, g_free);
  g_object_set_data (object, "type", GINT_TO_POINTER (type));

  g_async_queue_push (self->queue, task);
}

void
matrix_db_save_file_url_async (MatrixDb            *self,
                               ChattyMessage       *message,
                               ChattyFileInfo      *file,
                               int                  version,
                               int                  algorithm,
                               int                  type,
                               gboolean             extractable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
  GObject *object;
  GTask *task;

  g_return_if_fail (MATRIX_IS_DB (self));
  g_return_if_fail (version == 2);
  g_return_if_fail (extractable);

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, matrix_db_save_file_url_async);
  g_task_set_task_data (task, matrix_db_save_file_url, NULL);
  object = G_OBJECT (task);

  g_object_set_data (object, "file", file);
  g_object_set_data_full (object, "message", g_object_ref (message), g_object_unref);

  g_object_set_data (object, "type", GINT_TO_POINTER (type));
  g_object_set_data (object, "version", GINT_TO_POINTER (version));
  g_object_set_data (object, "algorithm", GINT_TO_POINTER (algorithm));
  g_object_set_data (object, "extractable", GINT_TO_POINTER (extractable));

  g_async_queue_push (self->queue, task);
}

char *
matrix_db_lookup_session (MatrixDb          *self,
                          const char        *account_id,
                          const char        *account_device,
                          const char        *session_id,
                          const char        *sender_key,
                          MatrixSessionType  type)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(GError) error = NULL;
  GObject *object;
  char *pickle;

  g_return_val_if_fail (MATRIX_IS_DB (self), NULL);
  g_return_val_if_fail (account_id && *account_id, NULL);
  g_return_val_if_fail (account_device && *account_device, NULL);
  g_return_val_if_fail (session_id && *session_id, NULL);
  g_return_val_if_fail (sender_key && *sender_key, NULL);

  task = g_task_new (self, NULL, NULL, NULL);
  g_object_ref (task);

  g_task_set_source_tag (task, matrix_db_lookup_session);
  g_task_set_task_data (task, db_lookup_session, NULL);
  object = G_OBJECT (task);

  g_object_set_data_full (object, "account-id", g_strdup (account_id), g_free);
  g_object_set_data_full (object, "account-device", g_strdup (account_device), g_free);
  g_object_set_data_full (object, "session-id", g_strdup (session_id), g_free);
  g_object_set_data_full (object, "sender-key", g_strdup (sender_key), g_free);
  g_object_set_data (object, "type", GINT_TO_POINTER (type));

  g_async_queue_push (self->queue, task);
  g_assert (task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  pickle = g_task_propagate_pointer (task, &error);

  if (error)
    g_debug ("Error getting session: %s", error->message);

  return pickle;
}

void
matrix_db_get_olm_sessions_async (MatrixDb            *self,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (MATRIX_IS_DB (self));

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, matrix_db_get_olm_sessions_async);
  g_task_set_task_data (task, history_get_olm_sessions, NULL);

  g_async_queue_push (self->queue, task);
}

GPtrArray *
matrix_db_get_olm_sessions_finish (MatrixDb      *self,
                                   GAsyncResult  *result,
                                   GError       **error)
{
  g_return_val_if_fail (MATRIX_IS_DB (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);
  g_return_val_if_fail (!error || !*error, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}
