/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* account.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#undef NDEBUG
#undef G_DISABLE_ASSERT
#undef G_DISABLE_CHECKS
#undef G_DISABLE_CAST_CHECKS
#undef G_LOG_DOMAIN

#define MESSAGE_LIMIT 20

#include <glib/gstdio.h>
#include <sqlite3.h>

#include "chatty-history.c"
#include "chatty-ma-account.h"
#include "chatty-ma-chat.h"

#include "chatty-contact.h"
#include "chatty-settings.h"
#include "chatty-utils.h"

typedef struct Message {
  ChattyChat *chat;
  ChattyMessage *message;
  char *account;
  char *room;
  char *who;
  char *what;
  char *uuid;
  ChattyMsgDirection direction;
  time_t when;
} Message;

static void
free_message (Message *msg)
{
  g_assert_true (msg);
  g_clear_object (&msg->chat);
  g_clear_object (&msg->message);
  g_free (msg->account);
  g_free (msg->who);
  g_free (msg->what);
  g_free (msg->room);
  g_free (msg->uuid);
  g_free (msg);
}

static void
finish_pointer_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  GTask *task = user_data;
  gpointer data;

  g_assert_true (G_IS_TASK (task));

  data = g_task_propagate_pointer (G_TASK (result), &error);
  g_assert_no_error (error);

  g_task_return_pointer (task, data, NULL);
}

static void
finish_bool_cb (GObject      *object,
                GAsyncResult *result,
                gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  GTask *task = user_data;
  gboolean status;

  g_assert_true (G_IS_TASK (task));

  status = g_task_propagate_boolean (G_TASK (result), &error);
  g_assert_no_error (error);

  g_task_return_boolean (task, status);
}

static int
history_db_get_int (sqlite3    *db,
                    const char *statement)
{
  sqlite3_stmt *stmt;
  int value, status;

  g_assert (db);

  status = sqlite3_prepare_v2 (db, statement, -1, &stmt, NULL);
  g_assert_cmpint (status, ==, SQLITE_OK);

  status = sqlite3_step (stmt);
  g_assert_cmpint (status, ==, SQLITE_ROW);

  value = sqlite3_column_int (stmt, 0);

  sqlite3_finalize (stmt);

  return value;
}

static void
compare_table (sqlite3    *db,
               const char *sql,
               int         expected_count,
               int         count)
{
  sqlite3_stmt *stmt;
  int status;

  status = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (status != SQLITE_OK)
    g_message ("%s", sqlite3_errmsg (db));

  if (status != SQLITE_OK)
    g_warning ("sql: %s", sql);

  g_assert_cmpint (status, ==, SQLITE_OK);

  if (expected_count != count)
    g_warning ("%d %d sql: %s", expected_count, count, sql);
  status = sqlite3_step (stmt);
  g_assert_cmpint (status, ==, SQLITE_ROW);
  g_assert_cmpint (expected_count, ==, count);

  count = sqlite3_column_int (stmt, 0);
  if (expected_count != count)
    g_warning ("%d %d sql: %s", expected_count, count, sql);

  g_assert_cmpint (expected_count, ==, count);

  sqlite3_finalize (stmt);
}

static void
compare_db_new_columns (sqlite3 *db)
{
  g_assert (HISTORY_VERSION == 4);

  /* TO REMOVE */
  compare_table (db,
                 "SELECT COUNT (*) FROM ("
                 "SELECT threads.name,a.username,u.username,threads.encrypted,visibility FROM main.thread_members "
                 "INNER JOIN main.users AS u ON u.id=thread_members.user_id "
                 "INNER JOIN main.threads ON threads.id=thread_id "
                 "INNER JOIN main.accounts ON accounts.id=threads.account_id "
                 "INNER JOIN main.users As a ON a.id=accounts.user_id "
                 "UNION "
                 "SELECT threads.name,a.username,u.username,threads.encrypted,visibility FROM test.thread_members "
                 "INNER JOIN test.users AS u ON u.id=thread_members.user_id "
                 "INNER JOIN test.threads ON threads.id=thread_id "
                 "INNER JOIN test.accounts ON accounts.id=threads.account_id "
                 "INNER JOIN test.users As a ON a.id=accounts.user_id "
                 ");",
                 history_db_get_int (db, "SELECT COUNT(*) FROM main.thread_members;"),
                 history_db_get_int (db, "SELECT COUNT(*) FROM test.thread_members;"));

  compare_table (db,
                 "SELECT COUNT (*) FROM ("
                 "SELECT name FROM main.mime_type "
                 "UNION "
                 "SELECT name FROM test.mime_type "
                 ");",
                 history_db_get_int (db, "SELECT COUNT(*) FROM main.mime_type;"),
                 history_db_get_int (db, "SELECT COUNT(*) FROM test.mime_type;"));

  compare_table (db,
                 "SELECT COUNT (*) FROM ("
                 "SELECT f.name,f.url,f.path,f.status,f.size FROM message_files "
                 "INNER JOIN main.messages AS m ON m.id=message_files.message_id "
                 "INNER JOIN main.files AS f ON f.id=message_files.file_id "
                 "UNION "
                 "SELECT f.name,f.url,f.path,f.status,f.size FROM message_files "
                 "INNER JOIN main.messages AS m ON m.id=message_files.message_id "
                 "INNER JOIN main.files AS f ON f.id=message_files.file_id "
                 ");",
                 history_db_get_int (db, "SELECT COUNT(*) FROM main.message_files;"),
                 history_db_get_int (db, "SELECT COUNT(*) FROM test.message_files;"));
}

static void
compare_history_db (sqlite3 *db)
{
  sqlite3_stmt *stmt;
  int status;

  /* Pragma version should match */
  g_assert_cmpint (history_db_get_int (db, "PRAGMA main.user_version;"), ==,
                   history_db_get_int (db, "PRAGMA test.user_version;"));

  /* Each db should have the same count of table rows */
  g_assert_cmpint (history_db_get_int (db, "SELECT COUNT(*) FROM main.sqlite_master;"),
                   ==,
                   history_db_get_int (db, "SELECT COUNT(*) FROM test.sqlite_master;"));

  status = sqlite3_prepare_v2 (db, "SELECT DISTINCT tbl_name,type FROM main.sqlite_master "
                               /* A column with 'sqlite' in name is not ours */
                               "WHERE NOT instr(name,'sqlite');", -1, &stmt, NULL);
  g_assert_cmpint (status, ==, SQLITE_OK);

  while ((status = sqlite3_step (stmt)) == SQLITE_ROW) {
    g_autofree char *main_sql = NULL;
    g_autofree char *test_sql = NULL;
    g_autofree char *sql = NULL;

    sql = g_strdup_printf ("SELECT COUNT (*) FROM ("
                           "SELECT name,type,\"notnull\",dflt_value,pk FROM pragma_table_info('%s', 'main') "
                           "UNION "
                           "SELECT name,type,\"notnull\",dflt_value,pk FROM pragma_table_info('%s', 'test') "
                           ");", sqlite3_column_text (stmt, 0), sqlite3_column_text (stmt, 0));
    main_sql = g_strdup_printf ("SELECT COUNT(*) FROM pragma_table_info('%s', 'main');",
                                sqlite3_column_text (stmt, 0));
    test_sql = g_strdup_printf ("SELECT COUNT(*) FROM pragma_table_info('%s', 'test');",
                                sqlite3_column_text (stmt, 0));
    /* DEBUG */
    /* g_debug ("%d items in '%s' table", history_db_get_int (db, main_sql), sqlite3_column_text (stmt, 0)); */
    compare_table (db, sql, history_db_get_int (db, main_sql), history_db_get_int (db, test_sql));
  }

  sqlite3_finalize (stmt);
  g_assert_cmpint (status, ==, SQLITE_DONE);

  /* As duplicate rows are removed, SELECT count should match the size of one table. */
  compare_table (db,
                 "SELECT COUNT (*) FROM ("
                 "SELECT username,alias,type FROM main.users "
                 "UNION "
                 "SELECT username,alias,type FROM test.users "
                 ");",
                 history_db_get_int (db, "SELECT COUNT(*) FROM main.users;"),
                 history_db_get_int (db, "SELECT COUNT(*) FROM test.users;"));

  /* sqlite doesn't guarantee `id` to be always incremented by one.  It
   * may depend on the order they are updated in chatty-history.  And so
   * compare by values.
   */
  compare_table (db,
                 "SELECT COUNT (*) FROM ("
                 "SELECT username,protocol FROM main.accounts "
                 "INNER JOIN main.users ON users.id=accounts.user_id "
                 "UNION "
                 "SELECT username,protocol  FROM test.accounts "
                 "INNER JOIN test.users ON users.id=accounts.user_id "
                 ");",
                 history_db_get_int (db, "SELECT COUNT(*) FROM main.accounts;"),
                 history_db_get_int (db, "SELECT COUNT(*) FROM test.accounts;"));

  compare_table (db,
                 "SELECT COUNT (*) FROM ("
                 "SELECT name,threads.alias,users.username,protocol,threads.type FROM main.threads "
                 "INNER JOIN main.accounts ON accounts.id=threads.account_id "
                 "INNER JOIN main.users ON users.id=accounts.user_id "
                 "UNION "
                 "SELECT name,threads.alias,users.username,protocol,threads.type FROM test.threads "
                 "INNER JOIN test.accounts ON accounts.id=threads.account_id "
                 "INNER JOIN test.users ON users.id=accounts.user_id "
                 ");",
                 history_db_get_int (db, "SELECT COUNT(*) FROM main.threads;"),
                 history_db_get_int (db, "SELECT COUNT(*) FROM test.threads;"));

  compare_table (db,
                 "SELECT COUNT (*) FROM ("
                 "SELECT threads.name,a.username,u.username,threads.encrypted,visibility FROM main.thread_members "
                 "INNER JOIN main.users AS u ON u.id=thread_members.user_id "
                 "INNER JOIN main.threads ON threads.id=thread_id "
                 "INNER JOIN main.accounts ON accounts.id=threads.account_id "
                 "INNER JOIN main.users As a ON a.id=accounts.user_id "
                 "UNION "
                 "SELECT threads.name,a.username,u.username,threads.encrypted,visibility FROM test.thread_members "
                 "INNER JOIN test.users AS u ON u.id=thread_members.user_id "
                 "INNER JOIN test.threads ON threads.id=thread_id "
                 "INNER JOIN test.accounts ON accounts.id=threads.account_id "
                 "INNER JOIN test.users As a ON a.id=accounts.user_id "
                 ");",
                 history_db_get_int (db, "SELECT COUNT(*) FROM main.thread_members;"),
                 history_db_get_int (db, "SELECT COUNT(*) FROM test.thread_members;"));

  compare_table (db,
                 "SELECT COUNT (*) FROM ("
                 "SELECT uid,threads.name,users.username,body,body_type,direction,time FROM main.messages "
                 "LEFT JOIN main.users ON users.id=sender_id "
                 "INNER JOIN main.threads ON threads.id=thread_id "
                 "UNION "
                 "SELECT uid,threads.name,users.username,body,body_type,direction,time FROM test.messages "
                 "LEFT JOIN test.users ON users.id=sender_id "
                 "INNER JOIN test.threads ON threads.id=thread_id "
                 ");",
                 history_db_get_int (db, "SELECT COUNT(*) FROM main.messages;"),
                 history_db_get_int (db, "SELECT COUNT(*) FROM test.messages;"));
  compare_table (db,
                 "SELECT COUNT (*) FROM ("
                 "SELECT f.name,f.url,f.path,mime.name,f.status,f.size FROM main.files as f "
                 "LEFT JOIN main.mime_type as mime ON f.mime_type_id=mime.id "
                 "UNION "
                 "SELECT f.name,f.url,f.path,mime.name,f.status,f.size FROM test.files as f "
                 "LEFT JOIN test.mime_type as mime ON f.mime_type_id=mime.id"
                 ");",
                 history_db_get_int (db, "SELECT COUNT(*) FROM main.files;"),
                 history_db_get_int (db, "SELECT COUNT(*) FROM test.files;"));
}

static Message *
new_message (const char         *account,
             const char         *buddy,
             const char         *msg_text,
             const char         *uuid,
             ChattyMsgDirection  direction,
             time_t              time_stamp,
             ChattyMsgType       type,
             const char         *room)
{
  g_autoptr(ChattyContact) contact = NULL;
  ChattyChat *chat;
  Message *message;

  chat = chatty_chat_new (account, room ? room : buddy, room == NULL);
  g_object_set (G_OBJECT (chat), "protocols", CHATTY_PROTOCOL_XMPP, NULL);
  message = g_new (Message, 1);
  contact = g_object_new (CHATTY_TYPE_CONTACT,
                          "protocols", CHATTY_PROTOCOL_XMPP,
                          NULL);
  chatty_contact_set_value (contact, buddy);
  message->message = chatty_message_new (CHATTY_ITEM (contact), msg_text,
                                         uuid, time_stamp, type, direction, 0);
  if (type == CHATTY_MESSAGE_FILE ||
      type == CHATTY_MESSAGE_IMAGE ||
      type == CHATTY_MESSAGE_VIDEO ||
      type == CHATTY_MESSAGE_AUDIO) {
    ChattyFileInfo *file;
    int duration, width, height, size;

    duration = g_random_int_range (1, 1000000);
    width = g_random_int_range (1, 100000);
    height = g_random_int_range (1, 100000);
    size = g_random_int_range (0, 10000000);

    if (size == 0)
      width = height = duration = 0;

    /* add fake preview */
    file = g_new0 (ChattyFileInfo, 1);
    file->file_name = g_strdup (chatty_message_get_text (message->message));
    file->url = g_strdup_printf ("%s.preview", file->file_name);
    file->size = size / 2;

    if (type == CHATTY_MESSAGE_AUDIO)
      file->mime_type = g_strdup ("audio/ogg");
    else if (type == CHATTY_MESSAGE_FILE)
      file->mime_type = g_strdup ("image/png");
    else
      file->mime_type = g_strdup ("image/jpg");

    if (type == CHATTY_MESSAGE_AUDIO)
      file->duration = duration / 2;

    if (type == CHATTY_MESSAGE_IMAGE ||
        type == CHATTY_MESSAGE_VIDEO ||
        type == CHATTY_MESSAGE_FILE) {
      file->width = width / 2;
      file->height = height / 2;
    }

    chatty_message_set_preview (message->message, file);


    /* add fake file */
    file = g_new0 (ChattyFileInfo, 1);
    file->file_name = g_strdup (chatty_message_get_text (message->message));
    file->url = g_strdup (file->file_name);
    file->size = size;

    if (type == CHATTY_MESSAGE_AUDIO)
      file->mime_type = g_strdup ("audio/ogg");
    else if (type == CHATTY_MESSAGE_FILE)
      file->mime_type = g_strdup ("application/pdf");
    else if (type == CHATTY_MESSAGE_VIDEO)
      file->mime_type = g_strdup ("video/ogg");
    else if (type == CHATTY_MESSAGE_IMAGE)
      file->mime_type = g_strdup ("image/png");

    if (type == CHATTY_MESSAGE_VIDEO ||
        type == CHATTY_MESSAGE_AUDIO)
      file->duration = duration;

    if (type == CHATTY_MESSAGE_IMAGE ||
        type == CHATTY_MESSAGE_VIDEO) {
      file->width = width;
      file->height = height;
    }

    chatty_message_set_files (message->message, g_list_append (NULL, file));
  }

  /* test with some random files for MMS */
  if (type == CHATTY_MESSAGE_MMS) {
    GList *files = NULL;
    guint i;

    i = g_random_int_range (2, 12);
    g_debug ("files to add: %u", i);

    for (; i > 0; i--) {
      g_autofree char *uid = NULL;
      ChattyFileInfo *file;

      uid = g_uuid_string_random ();
      file = g_new0 (ChattyFileInfo, 1);
      file->url = g_strdup_printf ("http://example.com/some-file/%s", uid);
      file->size = g_random_int_range (1000, 5000);

      files = g_list_append (files, file);
    }
    chatty_message_set_files (message->message, files);
  }

  message->account = g_strdup (account);
  message->who = g_strdup (buddy);
  message->what = g_strdup (msg_text);
  message->uuid = g_strdup (uuid);
  message->room = g_strdup (room);
  message->chat = chat;
  message->when  = time_stamp;
  message->direction = direction;

  return message;
}

static void
compare_file (ChattyFileInfo *file_a,
              ChattyFileInfo *file_b)
{
  g_assert_true (!!file_a == !!file_b);

  if (!file_a)
    return;

  g_assert_cmpstr (file_a->file_name, ==, file_b->file_name);
  g_assert_cmpstr (file_a->url, ==, file_b->url);
  g_assert_cmpstr (file_a->path, ==, file_b->path);
  g_assert_cmpstr (file_a->mime_type, ==, file_b->mime_type);
  g_assert_cmpint (file_a->width, ==, file_b->width);
  g_assert_cmpint (file_a->height, ==, file_b->height);
  g_assert_cmpint (file_a->size, ==, file_b->size);
  g_assert_cmpint (file_a->duration, ==, file_b->duration);
}

static void
compare_message (Message       *message,
                 ChattyMessage *chatty_message)
{
  ChattyMsgType type;
  GList *files;

  if (message == NULL)
    g_assert_null (chatty_message);

  if (message == NULL)
    return;

  type = chatty_message_get_msg_type (message->message);
  if (type == CHATTY_MESSAGE_HTML)
    type = CHATTY_MESSAGE_HTML_ESCAPED;

  g_assert_cmpstr (message->what, ==, chatty_message_get_text (chatty_message));
  g_assert_cmpstr (message->uuid, ==, chatty_message_get_uid (chatty_message));
  g_assert_cmpint (message->when, ==, chatty_message_get_time (chatty_message));
  g_assert_cmpint (message->direction, ==, chatty_message_get_msg_direction (chatty_message));
  g_assert_cmpint (type, ==, chatty_message_get_msg_type (chatty_message));
  compare_file (chatty_message_get_preview (chatty_message),
                chatty_message_get_preview (message->message));

  files = chatty_message_get_files (chatty_message);
  for (int i = g_list_length (files) - 1; i >= 0; i--) {
    GList *expected = NULL;

    expected = chatty_message_get_files (message->message);
    g_assert (expected);
    compare_file (g_list_nth_data (files, i), g_list_nth_data (expected, i));
  }
}

static void
test_history_new (void)
{
  ChattyHistory *history;
  GTask *task;
  char *dir;
  const char *file_name;
  gboolean status;

  history = chatty_history_new ();
  g_assert (CHATTY_IS_HISTORY (history));
  g_assert_false (chatty_history_is_open (history));

  task = g_task_new (NULL, NULL, NULL, NULL);
  dir = g_strdup (g_test_get_dir (G_TEST_BUILT));
  file_name = g_test_get_filename (G_TEST_BUILT, "test-history.db", NULL);
  g_remove (file_name);
  g_assert_false (g_file_test (file_name, G_FILE_TEST_EXISTS));
  chatty_history_open_async (history, dir, "test-history.db", finish_bool_cb, task);

  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  status = g_task_propagate_boolean (task, NULL);
  g_assert_true (g_file_test (file_name, G_FILE_TEST_IS_REGULAR));
  g_assert_true (chatty_history_is_open (history));
  g_assert_true (status);
  g_clear_object (&task);

  task = g_task_new (NULL, NULL, NULL, NULL);
  g_object_ref (history);
  chatty_history_close_async (history, finish_bool_cb, task);

  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  status = g_task_propagate_boolean (task, NULL);
  g_assert_false (chatty_history_is_open (history));
  g_assert_true (status);
  g_clear_object (&task);
  g_object_unref (history);
  /* FIXME */
  /* g_assert_finalize_object (history); */

  g_remove (file_name);
  g_assert_false (g_file_test (file_name, G_FILE_TEST_EXISTS));

  history = chatty_history_new ();
  chatty_history_open (history, g_test_get_dir (G_TEST_BUILT), "test-history.db");
  g_assert_true (g_file_test (file_name, G_FILE_TEST_IS_REGULAR));
  g_assert_true (chatty_history_is_open (history));

  chatty_history_close (history);
  g_assert_false (chatty_history_is_open (history));
  g_object_unref (history);
}

static void
add_message (ChattyHistory      *history,
             GPtrArray          *test_msg_array,
             const char         *account,
             const char         *room,
             const char         *who,
             const char         *uuid,
             const char         *message,
             time_t              when,
             ChattyMsgType       type,
             ChattyMsgDirection  direction)
{
  g_autofree char *uid = NULL;
  GPtrArray *msg_array;
  Message *msg;
  GTask *task;
  int time_stamp;
  gboolean success;

  g_assert_true (CHATTY_IS_HISTORY (history));
  g_assert_nonnull (account);

  if (!uuid)
    uid = g_uuid_string_random ();
  else
    uid = g_strdup (uuid);

  msg = new_message (account, who, message, uid, direction, when, type, room);
  success = chatty_history_add_message (history, msg->chat, msg->message);
  g_assert_true (success);
  g_assert_nonnull (uid);
  g_ptr_array_add (test_msg_array, msg);

  task = g_task_new (NULL, NULL, NULL, NULL);
  chatty_history_get_messages_async (history, msg->chat, NULL, -1, finish_pointer_cb, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  msg_array = g_task_propagate_pointer (task, NULL);
  g_assert_finalize_object (task);
  g_assert_nonnull (msg_array);
  g_assert_cmpint (test_msg_array->len, ==, msg_array->len);

  if (!chatty_chat_is_im (msg->chat)) {
    g_assert_true (chatty_history_chat_exists (history, account, room));

    time_stamp = chatty_history_get_chat_timestamp (history, uid, room);
    g_assert_cmpint (when, ==, time_stamp);

    time_stamp = chatty_history_get_last_message_time (history, account, room);
    g_assert_cmpint (when, ==, time_stamp);
  } else {
    g_assert_true (chatty_history_im_exists (history, account, who));

    time_stamp = chatty_history_get_im_timestamp (history, uid, account);
    g_assert_cmpint (when, ==, time_stamp);
  }

  for (guint i = 0; i < msg_array->len; i++)
    compare_message (test_msg_array->pdata[i], msg_array->pdata[i]);
  g_ptr_array_unref (msg_array);
}

static void
delete_existing_chat (ChattyHistory *history,
                      const char    *account,
                      const char    *chat_name,
                      gboolean       is_im)
{
  g_autoptr(ChattyChat) chat = NULL;
  gboolean status;

  if (is_im)
    status = chatty_history_im_exists (history, account, chat_name);
  else
    status = chatty_history_chat_exists (history, account, chat_name);
  g_assert_true (status);

  chat = chatty_chat_new (account, chat_name, is_im);
  chatty_history_delete_chat (history, chat);

  if (is_im)
    status = chatty_history_im_exists (history, account, chat_name);
  else
    status = chatty_history_chat_exists (history, account, chat_name);
  g_assert_false (status);
}

static void
compare_chat_message (ChattyMessage *a,
                      ChattyMessage *b)
{
  if (a == b)
    return;

  g_assert_true (CHATTY_IS_MESSAGE (a));
  g_assert_true (CHATTY_IS_MESSAGE (b));

  g_assert_cmpstr (chatty_message_get_uid (a), ==, chatty_message_get_uid (b));
  g_assert_cmpstr (chatty_message_get_text (a), ==, chatty_message_get_text (b));
  g_assert_cmpint (chatty_message_get_time (a), ==, chatty_message_get_time (b));
  g_assert_cmpint (chatty_message_get_msg_direction (a), ==, chatty_message_get_msg_direction (b));
}

static void
add_chatty_message (ChattyHistory      *history,
                    ChattyChat         *chat,
                    GPtrArray          *msg_array,
                    const char         *what,
                    int                 when,
                    ChattyMsgType       type,
                    ChattyMsgDirection  direction,
                    ChattyMsgStatus     status)
{
  g_autoptr(ChattyContact) contact = NULL;
  GPtrArray *old_msg_array;
  ChattyMessage *message;
  GTask *task;
  char *uuid;
  gboolean success;

  uuid = g_uuid_string_random ();
  contact = g_object_new (CHATTY_TYPE_CONTACT, NULL);
  chatty_contact_set_name (contact, chatty_chat_get_chat_name (chat));
  chatty_contact_set_value (contact, chatty_chat_get_chat_name (chat));
  message = chatty_message_new (CHATTY_ITEM (contact), what, uuid, when, type, direction, status);
  g_clear_pointer (&uuid, g_free);
  g_assert (CHATTY_IS_MESSAGE (message));
  g_ptr_array_add (msg_array, message);

  task = g_task_new (NULL, NULL, NULL, NULL);
  chatty_history_add_message_async (history, chat, message, finish_bool_cb, task);

  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  success = g_task_propagate_boolean (task, NULL);
  g_assert_true (success);
  g_clear_object (&task);

  message = msg_array->pdata[0];
  task = g_task_new (NULL, NULL, NULL, NULL);
  chatty_history_get_messages_async (history, chat, message, -1, finish_pointer_cb, task);

  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  old_msg_array = g_task_propagate_pointer (task, NULL);
  g_assert_null (old_msg_array);
  g_clear_object (&task);

  if (msg_array->len > 2) {
    message = msg_array->pdata[msg_array->len - 2];
    g_assert (CHATTY_IS_MESSAGE (message));

    task = g_task_new (NULL, NULL, NULL, NULL);
    chatty_history_get_messages_async (history, chat, message, -1, finish_pointer_cb, task);

    while (!g_task_get_completed (task))
      g_main_context_iteration (NULL, TRUE);

    old_msg_array = g_task_propagate_pointer (task, NULL);
    g_assert_nonnull (old_msg_array);
    g_assert_cmpint (old_msg_array->len, ==, msg_array->len - 2);

    for (guint i = 0; i < msg_array->len - 2; i++)
      compare_chat_message (msg_array->pdata[i], old_msg_array->pdata[i]);

    g_assert_finalize_object (task);
    g_ptr_array_unref (old_msg_array);
  }
}

static void
add_chat_and_test (ChattyHistory   *history,
                   ChattyMaAccount *account,
                   const char      *room_id,
                   const char      *room_name,
                   gboolean         hidden,
                   guint            total_count)
{
  g_autoptr(GPtrArray) chat_list = NULL;
  g_autoptr(GTask) task = NULL;

  if (room_id) {
    g_autoptr(ChattyMaChat) chat = NULL;

    chat = chatty_ma_chat_new (room_id, room_name, NULL, FALSE);
    g_assert (CHATTY_IS_MA_CHAT (chat));
    if (hidden)
      chatty_item_set_state (CHATTY_ITEM (chat), CHATTY_ITEM_HIDDEN);
    chatty_ma_account_add_chat (account, CHATTY_CHAT (chat));
    g_assert_true (chatty_history_update_chat (history, CHATTY_CHAT (chat)));
  }

  task = g_task_new (NULL, NULL, NULL, NULL);
  chatty_history_get_chats_async (history, CHATTY_ACCOUNT (account),
                                  finish_pointer_cb, task);

  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);
  chat_list = g_task_propagate_pointer (task, NULL);
  if (total_count) {
    g_assert_nonnull (chat_list);
    g_assert_cmpint (chat_list->len, ==, total_count);
  } else {
    g_assert_null (chat_list);
  }
}

static void
test_history_chat (void)
{
  g_autoptr(ChattyHistory) history = NULL;
  g_autoptr(ChattyMaAccount) ma_account = NULL;
  const char *account;

  g_remove (g_test_get_filename (G_TEST_BUILT, "test-history.db", NULL));

  history = chatty_history_new ();
  chatty_history_open (history, g_test_get_dir (G_TEST_BUILT), "test-history.db");
  g_assert_true (chatty_history_is_open (history));

  account = "@alice:example.com";
  ma_account = chatty_ma_account_new (account, NULL);
  g_assert (CHATTY_IS_MA_ACCOUNT (ma_account));

  add_chat_and_test (history, ma_account, NULL, NULL, FALSE, 0);
  add_chat_and_test (history, ma_account, "!xdesSDcdsSXXs", "Test room", FALSE, 1);
  add_chat_and_test (history, ma_account, "!aabbdesSDcdsS", "Another test room", FALSE, 2);
  add_chat_and_test (history, ma_account, "!aabbdesSDcdsS", "Name changed", FALSE, 2);
  add_chat_and_test (history, ma_account, "!aabbdesSDcdsS", "Name changed", TRUE, 1);

  chatty_history_close (history);
}

static void
test_history_message (void)
{
  g_autoptr(ChattyHistory) history = NULL;
  ChattyChat *chat;
  GPtrArray *msg_array;
  const char *account, *who;
  int when;

  g_remove (g_test_get_filename (G_TEST_BUILT, "test-history.db", NULL));

  history = chatty_history_new ();
  chatty_history_open (history, g_test_get_dir (G_TEST_BUILT), "test-history.db");
  g_assert_true (chatty_history_is_open (history));

  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)g_object_unref);

  account = "test-account@example.com";
  who = "buddy@example.org";
  chat = chatty_chat_new (account, who, TRUE);
  g_assert (CHATTY_IS_CHAT (chat));
  g_object_set (G_OBJECT (chat), "protocols", CHATTY_PROTOCOL_XMPP, NULL);

  when = time (NULL);
  add_chatty_message (history, chat, msg_array, "http://example.org/video/video.webm", when,
                      CHATTY_MESSAGE_TEXT, CHATTY_DIRECTION_OUT, 0);
  add_chatty_message (history, chat, msg_array, "Another message", when + 1,
                      CHATTY_MESSAGE_HTML_ESCAPED, CHATTY_DIRECTION_IN, 0);
  add_chatty_message (history, chat, msg_array, "And more message", when + 1,
                      CHATTY_MESSAGE_HTML_ESCAPED, CHATTY_DIRECTION_IN, 0);
  add_chatty_message (history, chat, msg_array, "More message", when + 1,
                      CHATTY_MESSAGE_TEXT, CHATTY_DIRECTION_OUT, 0);
  g_clear_object (&chat);
  g_ptr_array_free (msg_array, TRUE);

  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)g_object_unref);

  chat = chatty_chat_new (account, who, FALSE);
  g_assert (CHATTY_IS_CHAT (chat));
  g_object_set (G_OBJECT (chat), "protocols", CHATTY_PROTOCOL_XMPP, NULL);
  add_chatty_message (history, chat, msg_array, "Random message", when,
                      CHATTY_MESSAGE_TEXT, CHATTY_DIRECTION_SYSTEM, 0);
  add_chatty_message (history, chat, msg_array, "Another message", when + 1,
                      CHATTY_MESSAGE_HTML_ESCAPED, CHATTY_DIRECTION_IN, 0);
  add_chatty_message (history, chat, msg_array, "And more message", when + 1,
                      CHATTY_MESSAGE_HTML_ESCAPED, CHATTY_DIRECTION_IN, 0);
  add_chatty_message (history, chat, msg_array, "More message", when + 1,
                      CHATTY_MESSAGE_HTML_ESCAPED, CHATTY_DIRECTION_OUT, 0);
  /* FIXME */
  /* g_assert_finalize_object (chat); */

  g_ptr_array_unref (msg_array);
  chatty_history_close (history);
}

static void
test_history_raw_message (void)
{
  ChattyHistory *history;
  GPtrArray *msg_array;
  const char *account, *who, *room;
  char *uuid;
  int when;

  g_remove (g_test_get_filename (G_TEST_BUILT, "test-history.db", NULL));

  history = chatty_history_new ();
  chatty_history_open (history, g_test_get_dir (G_TEST_BUILT), "test-history.db");

  /* Test chat message */
  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  account = "account@test";
  who = "buddy@test";
  room = "chatroom@test";
  g_assert_false (chatty_history_im_exists (history, account, who));
  g_assert_false (chatty_history_chat_exists (history, account, room));

  uuid = g_uuid_string_random ();
  when = time (NULL);

  add_message (history, msg_array, account, room, who, uuid,
               "Random message", when, CHATTY_MESSAGE_TEXT,
               CHATTY_DIRECTION_SYSTEM);
  g_clear_pointer (&uuid, g_free);
  g_ptr_array_free (msg_array, TRUE);

  /* Test IM message */
  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  uuid = g_uuid_string_random ();
  add_message (history, msg_array, account, NULL, who, uuid,
               "Some Random message", time(NULL) + 5,
               CHATTY_MESSAGE_HTML_ESCAPED, CHATTY_DIRECTION_SYSTEM);
  g_clear_pointer (&uuid, g_free);
  g_ptr_array_free (msg_array, TRUE);


  /* Test several IM messages */
  account = "some-account@test";
  who = "buddy@test";
  room = NULL;
  uuid = NULL;

  chatty_history_close (history);
  g_remove (g_test_get_filename (G_TEST_BUILT, "test-history.db", NULL));
  g_object_unref (history);

  history = chatty_history_new ();
  chatty_history_open (history, g_test_get_dir (G_TEST_BUILT), "test-history.db");
  /* history = chatty_history_get_default (); */

  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  add_message (history, msg_array, account, room, who, uuid,
               "Message", when - 4, CHATTY_MESSAGE_TEXT, CHATTY_DIRECTION_SYSTEM);
  add_message (history, msg_array, account, room, who, uuid,
               "Some random message", when - 3, CHATTY_MESSAGE_HTML, CHATTY_DIRECTION_SYSTEM);
  add_message (history, msg_array, account, room, who, uuid,
               "https://www.example.com/media/audio.ogg", when -1, CHATTY_MESSAGE_AUDIO, CHATTY_DIRECTION_OUT);
  add_message (history, msg_array, account, room, who, uuid,
               "https://www.example.com/video/അറിവ്.mp4", when, CHATTY_MESSAGE_VIDEO, CHATTY_DIRECTION_OUT);
  add_message (history, msg_array, account, room, who, uuid,
               "http://www.example.com/invalid-file.xml", when, CHATTY_MESSAGE_FILE, CHATTY_DIRECTION_IN);
  add_message (history, msg_array, account, room, who, uuid,
               "And one more", when, CHATTY_MESSAGE_MATRIX_HTML, CHATTY_DIRECTION_IN);
  add_message (history, msg_array, account, room, who, uuid,
               "http://www.example.com/invalid-file.mp4", when + 1, CHATTY_MESSAGE_VIDEO, CHATTY_DIRECTION_IN);
  g_ptr_array_free (msg_array, TRUE);

  /* Another buddy */
  who = "somebuddy@test";
  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  add_message (history, msg_array, account, room, who, uuid,
               "Message", when, CHATTY_MESSAGE_TEXT, CHATTY_DIRECTION_SYSTEM);
  add_message (history, msg_array, account, room, who, uuid,
               "Some test message", when + 1, CHATTY_MESSAGE_HTML_ESCAPED, CHATTY_DIRECTION_SYSTEM);
  add_message (history, msg_array, account, room, who, uuid,
               "Yet another test message", when + 1, CHATTY_MESSAGE_MATRIX_HTML, CHATTY_DIRECTION_OUT);
  add_message (history, msg_array, account, room, who, uuid,
               "നല്ല ഒരു അറിവ്", when + 1, CHATTY_MESSAGE_HTML, CHATTY_DIRECTION_OUT);
  add_message (history, msg_array, account, room, who, uuid,
               "A Simple message", when + 1, CHATTY_MESSAGE_TEXT, CHATTY_DIRECTION_IN);
  add_message (history, msg_array, account, room, who, uuid,
               "And one more", when + 2, CHATTY_MESSAGE_HTML_ESCAPED, CHATTY_DIRECTION_IN);
  add_message (history, msg_array, account, room, who, uuid,
               "And one more", when + 3, CHATTY_MESSAGE_TEXT, CHATTY_DIRECTION_IN);
  g_ptr_array_free (msg_array, TRUE);

  /* Test several Chat messages */
  room = "room@test";

  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  add_message (history, msg_array, account, room, who, uuid,
               "Message", when - 4, CHATTY_MESSAGE_TEXT, CHATTY_DIRECTION_SYSTEM);
  add_message (history, msg_array, account, room, who, uuid,
               "Some random message", when - 3, CHATTY_MESSAGE_HTML_ESCAPED, CHATTY_DIRECTION_SYSTEM);
  add_message (history, msg_array, account, room, who, uuid,
               "Yet another random message", when -1, CHATTY_MESSAGE_TEXT, CHATTY_DIRECTION_OUT);
  add_message (history, msg_array, account, room, who, uuid,
               "നല്ല ഒരു അറിവ് message", when, CHATTY_MESSAGE_HTML_ESCAPED, CHATTY_DIRECTION_OUT);
  add_message (history, msg_array, account, room, who, uuid,
               "A very simple message", when, CHATTY_MESSAGE_TEXT, CHATTY_DIRECTION_IN);
  add_message (history, msg_array, account, room, who, uuid,
               "https://www.example.net/file/random.pdf", when, CHATTY_MESSAGE_FILE, CHATTY_DIRECTION_IN);
  add_message (history, msg_array, account, room, who, uuid,
               "And one more", when + 1, CHATTY_MESSAGE_HTML_ESCAPED, CHATTY_DIRECTION_IN);
  g_ptr_array_free (msg_array, TRUE);

  /* Another buddy */
  room = "another@test";
  who = "another-buddy@test";

  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  add_message (history, msg_array, account, room, who, uuid,
               "https://www.example.com/images/random-image.png", when, CHATTY_MESSAGE_IMAGE, CHATTY_DIRECTION_SYSTEM);
  add_message (history, msg_array, account, room, who, uuid,
               "Some test message", when + 1, CHATTY_MESSAGE_TEXT, CHATTY_DIRECTION_SYSTEM);
  add_message (history, msg_array, account, room, who, uuid,
               "https://www.example.com/videos/video.flv", when + 1, CHATTY_MESSAGE_VIDEO, CHATTY_DIRECTION_OUT);
  add_message (history, msg_array, account, room, who, uuid,
               "https://www.example.com/audio/അറിവ്.ogg", when + 1, CHATTY_MESSAGE_AUDIO, CHATTY_DIRECTION_OUT);
  add_message (history, msg_array, account, room, who, uuid,
               "geo:51.5008,0.1247", when + 1, CHATTY_MESSAGE_LOCATION, CHATTY_DIRECTION_IN);
  add_message (history, msg_array, account, room, who, uuid,
               "geo:51.5008,0.1247", when + 2, CHATTY_MESSAGE_LOCATION, CHATTY_DIRECTION_IN);
  add_message (history, msg_array, account, room, who, uuid,
               "And one more", when + 3, CHATTY_MESSAGE_MMS, CHATTY_DIRECTION_IN);
  g_ptr_array_unref (msg_array);

  /* Test deletion */
  delete_existing_chat (history, account, "buddy@test", TRUE);
  delete_existing_chat (history, account, "somebuddy@test", TRUE);
  delete_existing_chat (history, account, "room@test", FALSE);
  delete_existing_chat (history, account, "another@test", FALSE);

  chatty_history_close (history);
  g_object_unref (history);
}

static void
test_value (ChattyHistory *history,
            sqlite3       *db,
            const char    *statement,
            int            statement_status,
            int            id,
            int            direction,
            const char    *account,
            const char    *who,
            const char    *message,
            const char    *room)
{
  g_autofree char *uuid = NULL;
  sqlite3_stmt *stmt;
  int status, time_stamp;

  status = sqlite3_prepare_v2 (db, statement, -1, &stmt, NULL);
  g_assert_cmpint (status, ==, SQLITE_OK);

  uuid = g_uuid_string_random ();
  time_stamp = time (NULL) + g_random_int_range (1, 1000);

  if (statement_status == SQLITE_ROW) {
    g_autoptr(ChattyChat) chat = NULL;
    g_autoptr(ChattyContact) contact = NULL;
    g_autoptr(ChattyMessage) chat_message = NULL;
    ChattyMsgDirection chat_direction;
    gboolean success;

    chat = chatty_chat_new (account, room ? room : who, room == NULL);
    g_object_set (G_OBJECT (chat), "protocols", CHATTY_PROTOCOL_XMPP, NULL);

    if (!uuid)
      uuid = g_uuid_string_random ();

    chat_direction = history_direction_from_value (direction);
    contact = g_object_new (CHATTY_TYPE_CONTACT, NULL);
    chatty_contact_set_name (contact, who);
    chatty_contact_set_value (contact, who);
    chat_message = chatty_message_new (CHATTY_ITEM (contact), message, uuid, time_stamp,
                                       CHATTY_MESSAGE_TEXT, chat_direction, 0);
    success = chatty_history_add_message (history, chat, chat_message);
    g_assert_true (success);
  }

  status = sqlite3_step (stmt);
  g_assert_cmpint (status, ==, statement_status);

  if (statement_status != SQLITE_ROW)
    goto end;

  g_assert_cmpint (id, ==, sqlite3_column_int (stmt, 0));
  g_assert_cmpstr (uuid, ==, (char *)sqlite3_column_text (stmt, 1));
  g_assert_cmpstr (message, ==, (char *)sqlite3_column_text (stmt, 2));
  g_assert_cmpint (direction, ==, sqlite3_column_int (stmt, 3));
  g_assert_cmpstr (account, ==, (char *)sqlite3_column_text (stmt, 4));

  if (direction == -1)
    g_assert_cmpstr (account, ==, (char *)sqlite3_column_text (stmt, 5));
  else
    g_assert_cmpstr (who, ==, (char *)sqlite3_column_text (stmt, 5));

  if (room)
    g_assert_cmpstr (room, ==, (char *)sqlite3_column_text (stmt, 6));
  else
    g_assert_cmpstr (who, ==, (char *)sqlite3_column_text (stmt, 6));

 end:
  sqlite3_finalize (stmt);
}

static void
test_history_db (void)
{
  g_autoptr(ChattyHistory) history = NULL;
  const char *file_name, *account, *who, *message, *room;
  const char *statement;
  sqlite3 *db;
  int status;

  file_name = g_test_get_filename (G_TEST_BUILT, "test-history.db", NULL);
  g_remove (file_name);
  g_assert_false (g_file_test (file_name, G_FILE_TEST_IS_REGULAR));

  history = chatty_history_new ();
  chatty_history_open (history, g_test_get_dir (G_TEST_BUILT), "test-history.db");
  g_assert_true (g_file_test (file_name, G_FILE_TEST_IS_REGULAR));

  status = sqlite3_open (file_name, &db);
  g_assert_cmpint (status, ==, SQLITE_OK);

  account = "account@test";
  who = "buddy@test";
  message = "Random messsage";
  room = "room@test";

  statement = "SELECT max(messages.id),uid,body,direction,a.username,s.username,threads.name "
    "FROM messages "
    "INNER JOIN threads "
    "ON threads.id=thread_id "
    "INNER JOIN accounts "
    "ON threads.account_id=accounts.id "
    "INNER JOIN users as a "
    "ON accounts.user_id=a.id "
    "INNER JOIN users as s "
    "ON sender_id=s.id;";

  test_value (history, db, statement, SQLITE_ROW, 1, -1, account, who, message, NULL);
  test_value (history, db, statement, SQLITE_ROW, 2, 1, account, who, message, NULL);
  test_value (history, db, statement, SQLITE_ROW, 3, -1, account, who, message, room);
  test_value (history, db, statement, SQLITE_ROW, 4, 1, account, who, message, room);

  sqlite3_close (db);
  chatty_history_close (history);
}

static void
export_sql_file (const char  *sql_path,
                 const char  *file_name,
                 sqlite3    **db)
{
  g_autofree char *export_file = NULL;
  g_autofree char *input_file = NULL;
  g_autofree char *content = NULL;
  g_autoptr(GError) err = NULL;
  char *error = NULL;
  int status;

  g_assert (sql_path && *sql_path);
  g_assert (file_name && *file_name);
  g_assert (db);

  input_file = g_build_filename (sql_path, file_name, NULL);
  export_file = g_test_build_filename (G_TEST_BUILT, file_name, NULL);
  strcpy (export_file + strlen (export_file) - strlen ("sql"), "db");
  g_remove (export_file);
  status = sqlite3_open (export_file, db);
  g_assert_cmpint (status, ==, SQLITE_OK);
  g_file_get_contents (input_file, &content, NULL, &err);
  g_assert_no_error (err);

  status = sqlite3_exec (*db, content, NULL, NULL, &error);
  if (error)
    g_warning ("%s error: %s", G_STRLOC, error);
  g_assert_cmpint (status, ==, SQLITE_OK);
}

static void
test_history_migration_db (void)
{
  g_autoptr(GDir) dir = NULL;
  g_autofree char *path = NULL;
  g_autoptr(GError) error = NULL;
  const char *name;

  path = g_test_build_filename (G_TEST_DIST, "history-db", NULL);
  dir = g_dir_open (path, 0, &error);
  g_assert_no_error (error);

  while ((name = g_dir_read_name (dir)) != NULL) {
    g_autoptr(ChattyHistory) history = NULL;
    g_autofree char *expected_file = NULL;
    g_autofree char *input_file = NULL;
    g_autofree char *input = NULL;
    sqlite3 *db = NULL;
    int status;

    if (g_str_has_suffix (name, "v4.db"))
      continue;

    g_assert_true (g_str_has_suffix (name, "sql"));
    g_debug ("Migrating %s", name);

    if (strstr (name, "DE"))
      chatty_settings_set_country_iso_code (chatty_settings_get_default (), "DE");
    else if (strstr (name, "IN"))
      chatty_settings_set_country_iso_code (chatty_settings_get_default (), "IN");
    else
      chatty_settings_set_country_iso_code (chatty_settings_get_default (), "US");

    /* Export old version sql file */
    export_sql_file (path, name, &db);
    sqlite3_close (db);

    /* Export migrated version sql file */
    expected_file = g_strdelimit (g_strdup (name), "0123", '4');
    export_sql_file (path, expected_file, &db);

    /* Open history with old db, which will result in db migration */
    input_file = g_strdup (name);
    strcpy (input_file + strlen (input_file) - strlen ("sql"), "db");
    history = chatty_history_new ();
    chatty_history_open (history, g_test_get_dir (G_TEST_BUILT), input_file);
    chatty_history_close (history);
    g_free (input_file);

    /* Attach old (now migrated) db with expected migrated db */
    input_file = g_test_build_filename (G_TEST_BUILT, name, NULL);
    strcpy (input_file + strlen (input_file) - strlen ("sql"), "db");
    input = g_strconcat ("ATTACH '", input_file, "' as test;", NULL);
    status = sqlite3_exec (db, input, NULL, NULL, NULL);
    g_assert_cmpint (status, ==, SQLITE_OK);

    compare_history_db (db);
    compare_db_new_columns (db);
    sqlite3_close (db);
  }
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);

  g_test_add_func ("/history/new", test_history_new);
  g_test_add_func ("/history/chat", test_history_chat);
  g_test_add_func ("/history/message", test_history_message);
  g_test_add_func ("/history/raw_message", test_history_raw_message);
  g_test_add_func ("/history/db", test_history_db);
  g_test_add_func ("/history/db_migration", test_history_migration_db);

  return g_test_run ();
}
