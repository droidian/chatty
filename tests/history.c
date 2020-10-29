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

#include "purple-init.h"
#include "chatty-history.h"

typedef struct Message {
  ChattyChat *chat;
  char *account;
  char *room;
  char *who;
  char *what;
  char *uuid;
  PurpleMessageFlags flags;
  PurpleConversationType type;
  time_t when;
} Message;

static ChattyMsgDirection
chatty_direction_for_flag (PurpleMessageFlags flag)
{
  if (flag & PURPLE_MESSAGE_RECV)
    return CHATTY_DIRECTION_IN;

  if (flag & PURPLE_MESSAGE_SEND)
    return CHATTY_DIRECTION_OUT;

  if (flag & PURPLE_MESSAGE_SYSTEM)
    return CHATTY_DIRECTION_SYSTEM;

  g_return_val_if_reached (CHATTY_DIRECTION_SYSTEM);
}

static PurpleMessageFlags
flag_for_direction (int direction)
{
  if (direction == 1)
    return PURPLE_MESSAGE_RECV;

  if (direction == -1)
    return PURPLE_MESSAGE_SEND;

  if (direction == 0)
    return PURPLE_MESSAGE_SYSTEM;

  g_return_val_if_reached (PURPLE_MESSAGE_SYSTEM);
}

static void
free_message (Message *msg)
{
  g_assert_true (msg);
  g_clear_object (&msg->chat);
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

static Message *
new_message (const char         *account,
             const char         *buddy,
             const char         *msg_text,
             const char         *uuid,
             PurpleMessageFlags  flags,
             time_t              time_stamp,
             const char         *room)
{
  ChattyChat *chat;
  Message *message;

  chat = chatty_chat_new (account, room ? room : buddy, room == NULL);
  message = g_new (Message, 1);
  message->account = g_strdup (account);
  message->who = g_strdup (buddy);
  message->what = g_strdup (msg_text);
  message->uuid = g_strdup (uuid);
  message->room = g_strdup (room);
  message->chat = chat;
  message->when  = time_stamp;
  message->flags = flags;

  return message;
}

static void
compare_message (Message       *message,
                 ChattyMessage *chatty_message)
{
  ChattyMsgDirection direction;

  if (message == NULL)
    g_assert_null (chatty_message);

  if (message == NULL)
    return;

  direction = chatty_direction_for_flag (message->flags);
  g_assert_cmpstr (message->what, ==, chatty_message_get_text (chatty_message));
  g_assert_cmpstr (message->uuid, ==, chatty_message_get_uid (chatty_message));
  g_assert_cmpint (message->when, ==, chatty_message_get_time (chatty_message));
  g_assert_cmpint (direction, ==, chatty_message_get_msg_direction (chatty_message));
}

static void
test_history_new (void)
{
  ChattyHistory *history;
  GTask *task;
  char *dir;
  const char *file_name;
  gboolean status;

  history = chatty_history_get_default ();
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
  g_clear_object (&history);

  g_remove (file_name);
  g_assert_false (g_file_test (file_name, G_FILE_TEST_EXISTS));
  chatty_history_open (g_test_get_dir (G_TEST_BUILT), "test-history.db");
  g_assert_true (g_file_test (file_name, G_FILE_TEST_IS_REGULAR));

  history = chatty_history_get_default ();
  g_assert_true (chatty_history_is_open (history));

  g_object_ref (history);
  chatty_history_close ();
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
             PurpleMessageFlags  flags)
{
  GPtrArray *msg_array;
  Message *msg;
  GTask *task;
  char *uid;
  int time_stamp;
  PurpleConversationType type;
  gboolean success;

  g_assert_true (CHATTY_IS_HISTORY (history));
  g_assert_nonnull (account);

  if (room)
    type = PURPLE_CONV_TYPE_CHAT;
  else
    type = PURPLE_CONV_TYPE_IM;

  uid = g_strdup (uuid);
  msg = new_message (account, who, message, uid, flags, when, room);
  success = chatty_history_add_message (account, room, who, message, &uid, flags, when, type);
  g_assert_true (success);
  g_assert_nonnull (uid);
  g_ptr_array_add (test_msg_array, msg);

  if (msg->uuid == NULL)
    msg->uuid = uid;

  task = g_task_new (NULL, NULL, NULL, NULL);
  chatty_history_get_messages_async (history, msg->chat, NULL, -1, finish_pointer_cb, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  msg_array = g_task_propagate_pointer (task, NULL);
  g_assert_nonnull (msg_array);
  g_assert_cmpint (test_msg_array->len, ==, msg_array->len);

  if (type == PURPLE_CONV_TYPE_CHAT) {
    g_assert_true (chatty_history_chat_exists (account, room));

    time_stamp = chatty_history_get_chat_timestamp (uid, room);
    g_assert_cmpint (when, ==, time_stamp);

    time_stamp = chatty_history_get_last_message_time (account, room);
    g_assert_cmpint (when, ==, time_stamp);
  } else {
    g_assert_true (chatty_history_im_exists (account, who));

    time_stamp = chatty_history_get_im_timestamp (uid, account);
    g_assert_cmpint (when, ==, time_stamp);
  }

  for (guint i = 0; i < msg_array->len; i++)
    compare_message (test_msg_array->pdata[i], msg_array->pdata[i]);
}

static void
delete_existing_chat (const char *account,
                      const char *chat_name,
                      gboolean    is_im)
{
  g_autoptr(ChattyChat) chat = NULL;
  gboolean status;

  if (is_im)
    status = chatty_history_im_exists (account, chat_name);
  else
    status = chatty_history_chat_exists (account, chat_name);
  g_assert_true (status);

  chat = chatty_chat_new (account, chat_name, is_im);
  chatty_history_delete_chat (chat);

  if (is_im)
    status = chatty_history_im_exists (account, chat_name);
  else
    status = chatty_history_chat_exists (account, chat_name);
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
                    ChattyMsgDirection  direction,
                    ChattyMsgStatus     status)
{
  GPtrArray *old_msg_array;
  ChattyMessage *message;
  GTask *task;
  char *uuid;
  gboolean success;


  uuid = g_uuid_string_random ();
  message = chatty_message_new (NULL, NULL, what, uuid, when, direction, status);
  g_assert (CHATTY_IS_MESSAGE (message));
  g_ptr_array_add (msg_array, message);

  if (chatty_chat_is_im (chat))
    chatty_message_set_user_name (message, chatty_chat_get_chat_name (chat));

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
  }
}

static void
test_history_message (void)
{
  ChattyHistory *history;
  ChattyChat *chat;
  GPtrArray *msg_array;
  const char *account, *who;
  int when;

  g_remove (g_test_get_filename (G_TEST_BUILT, "test-history.db", NULL));
  chatty_history_open (g_test_get_dir (G_TEST_BUILT), "test-history.db");
  history = chatty_history_get_default ();
  g_assert_true (chatty_history_is_open (history));

  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)g_object_unref);

  account = "test-account@example.com";
  who = "buddy@example.org";
  chat = chatty_chat_new (account, who, TRUE);
  g_assert (CHATTY_IS_CHAT (chat));

  when = time (NULL);
  add_chatty_message (history, chat, msg_array, "Random message", when, CHATTY_DIRECTION_OUT, 0);
  add_chatty_message (history, chat, msg_array, "Another message", when + 1, CHATTY_DIRECTION_IN, 0);
  add_chatty_message (history, chat, msg_array, "And more message", when + 1, CHATTY_DIRECTION_IN, 0);
  add_chatty_message (history, chat, msg_array, "More message", when + 1, CHATTY_DIRECTION_OUT, 0);
  g_clear_object (&chat);
  g_ptr_array_free (msg_array, TRUE);

  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)g_object_unref);

  chat = chatty_chat_new (account, who, FALSE);
  g_assert (CHATTY_IS_CHAT (chat));
  add_chatty_message (history, chat, msg_array, "Random message", when, CHATTY_DIRECTION_SYSTEM, 0);
  add_chatty_message (history, chat, msg_array, "Another message", when + 1, CHATTY_DIRECTION_IN, 0);
  add_chatty_message (history, chat, msg_array, "And more message", when + 1, CHATTY_DIRECTION_IN, 0);
  add_chatty_message (history, chat, msg_array, "More message", when + 1, CHATTY_DIRECTION_OUT, 0);

  chatty_history_close ();
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
  chatty_history_open (g_test_get_dir (G_TEST_BUILT), "test-history.db");
  history = chatty_history_get_default ();

  /* Test chat message */
  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  account = "account@test";
  who = "buddy@test";
  room = "chatroom@test";
  g_assert_false (chatty_history_im_exists (account, who));
  g_assert_false (chatty_history_chat_exists (account, room));

  uuid = g_uuid_string_random ();
  when = time (NULL);

  add_message (history, msg_array, account, room, who, uuid,
               "Random message", when, PURPLE_MESSAGE_SYSTEM);
  g_clear_pointer (&uuid, g_free);
  g_ptr_array_free (msg_array, TRUE);

  /* Test IM message */
  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  uuid = g_uuid_string_random ();
  add_message (history, msg_array, account, NULL, who, uuid,
               "Some Random message", time(NULL) + 5, PURPLE_MESSAGE_SYSTEM);
  g_clear_pointer (&uuid, g_free);
  g_ptr_array_free (msg_array, TRUE);


  /* Test several IM messages */
  account = "some-account@test";
  who = "buddy@test";
  room = NULL;
  uuid = NULL;

  chatty_history_close ();
  g_remove (g_test_get_filename (G_TEST_BUILT, "test-history.db", NULL));
  chatty_history_open (g_test_get_dir (G_TEST_BUILT), "test-history.db");
  history = chatty_history_get_default ();

  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  add_message (history, msg_array, account, room, who, uuid,
               "Message", when - 4, PURPLE_MESSAGE_SYSTEM);
  add_message (history, msg_array, account, room, who, uuid,
               "Some random message", when - 3, PURPLE_MESSAGE_SYSTEM);
  add_message (history, msg_array, account, room, who, uuid,
               "Yet another random message", when -1, PURPLE_MESSAGE_SEND);
  add_message (history, msg_array, account, room, who, uuid,
               "നല്ല ഒരു അറിവ് message", when, PURPLE_MESSAGE_SEND);
  add_message (history, msg_array, account, room, who, uuid,
               "A very simple message", when, PURPLE_MESSAGE_RECV);
  add_message (history, msg_array, account, room, who, uuid,
               "And one more", when, PURPLE_MESSAGE_RECV);
  add_message (history, msg_array, account, room, who, uuid,
               "And one more", when + 1, PURPLE_MESSAGE_RECV);
  g_ptr_array_free (msg_array, TRUE);

  /* Another buddy */
  who = "somebuddy@test";
  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  add_message (history, msg_array, account, room, who, uuid,
               "Message", when, PURPLE_MESSAGE_SYSTEM);
  add_message (history, msg_array, account, room, who, uuid,
               "Some test message", when + 1, PURPLE_MESSAGE_SYSTEM);
  add_message (history, msg_array, account, room, who, uuid,
               "Yet another test message", when + 1, PURPLE_MESSAGE_SEND);
  add_message (history, msg_array, account, room, who, uuid,
               "നല്ല ഒരു അറിവ്", when + 1, PURPLE_MESSAGE_SEND);
  add_message (history, msg_array, account, room, who, uuid,
               "A Simple message", when + 1, PURPLE_MESSAGE_RECV);
  add_message (history, msg_array, account, room, who, uuid,
               "And one more", when + 2, PURPLE_MESSAGE_RECV);
  add_message (history, msg_array, account, room, who, uuid,
               "And one more", when + 3, PURPLE_MESSAGE_RECV);
  g_ptr_array_free (msg_array, TRUE);

  /* Test several Chat messages */
  room = "room@test";

  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  add_message (history, msg_array, account, room, who, uuid,
               "Message", when - 4, PURPLE_MESSAGE_SYSTEM);
  add_message (history, msg_array, account, room, who, uuid,
               "Some random message", when - 3, PURPLE_MESSAGE_SYSTEM);
  add_message (history, msg_array, account, room, who, uuid,
               "Yet another random message", when -1, PURPLE_MESSAGE_SEND);
  add_message (history, msg_array, account, room, who, uuid,
               "നല്ല ഒരു അറിവ് message", when, PURPLE_MESSAGE_SEND);
  add_message (history, msg_array, account, room, who, uuid,
               "A very simple message", when, PURPLE_MESSAGE_RECV);
  add_message (history, msg_array, account, room, who, uuid,
               "And one more", when, PURPLE_MESSAGE_RECV);
  add_message (history, msg_array, account, room, who, uuid,
               "And one more", when + 1, PURPLE_MESSAGE_RECV);
  g_ptr_array_free (msg_array, TRUE);

  /* Another buddy */
  room = "another@test";
  who = "another-buddy@test";

  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  add_message (history, msg_array, account, room, who, uuid,
               "Message", when, PURPLE_MESSAGE_SYSTEM);
  add_message (history, msg_array, account, room, who, uuid,
               "Some test message", when + 1, PURPLE_MESSAGE_SYSTEM);
  add_message (history, msg_array, account, room, who, uuid,
               "Yet another test message", when + 1, PURPLE_MESSAGE_SEND);
  add_message (history, msg_array, account, room, who, uuid,
               "നല്ല ഒരു അറിവ്", when + 1, PURPLE_MESSAGE_SEND);
  add_message (history, msg_array, account, room, who, uuid,
               "A Simple message", when + 1, PURPLE_MESSAGE_RECV);
  add_message (history, msg_array, account, room, who, uuid,
               "And one more", when + 2, PURPLE_MESSAGE_RECV);
  add_message (history, msg_array, account, room, who, uuid,
               "And one more", when + 3, PURPLE_MESSAGE_RECV);

  /* Test deletion */
  delete_existing_chat (account, "buddy@test", TRUE);
  delete_existing_chat (account, "somebuddy@test", TRUE);
  delete_existing_chat (account, "room@test", FALSE);
  delete_existing_chat (account, "another@test", FALSE);

  chatty_history_close ();
}

static void
test_value (sqlite3    *db,
            const char *statement,
            int         statement_status,
            int         id,
            int         direction,
            const char *account,
            const char *who,
            const char *message,
            const char *room)
{
  g_autofree char *uuid = NULL;
  sqlite3_stmt *stmt;
  int status, time_stamp;
  PurpleConversationType type;
  PurpleMessageFlags flags;

  status = sqlite3_prepare_v2 (db, statement, -1, &stmt, NULL);
  g_assert_cmpint (status, ==, SQLITE_OK);

  uuid = g_uuid_string_random ();
  time_stamp = time (NULL) + g_random_int_range (1, 1000);

  if (room)
    type = PURPLE_CONV_TYPE_CHAT;
  else
    type = PURPLE_CONV_TYPE_IM;

  flags = flag_for_direction (direction);
  if (statement_status == SQLITE_ROW) {
    gboolean success;

    success = chatty_history_add_message (account, room, who, message, &uuid, flags, time_stamp, type);
    g_assert_true (success);
  }

  status = sqlite3_step (stmt);
  g_assert_cmpint (status, ==, statement_status);

  if (statement_status != SQLITE_ROW)
    return;

  g_assert_cmpint (id, ==, sqlite3_column_int (stmt, 0));
  g_assert_cmpint (time_stamp, ==, sqlite3_column_int (stmt, 1));
  g_assert_cmpint (direction, ==, sqlite3_column_int (stmt, 2));
  g_assert_cmpstr (account, ==, (char *)sqlite3_column_text (stmt, 3));
  g_assert_cmpstr (who, ==, (char *)sqlite3_column_text (stmt, 4));
  g_assert_cmpstr (uuid, ==, (char *)sqlite3_column_text (stmt, 5));
  g_assert_cmpstr (message, ==, (char *)sqlite3_column_text (stmt, 6));
  if (room)
    g_assert_cmpstr (room, ==, (char *)sqlite3_column_text (stmt, 7));

  sqlite3_finalize (stmt);
}

static void
test_history_db (void)
{
  const char *file_name, *account, *who, *message, *room;
  sqlite3 *db;
  int status;

  file_name = g_test_get_filename (G_TEST_BUILT, "test-history.db", NULL);
  g_remove (file_name);
  g_assert_false (g_file_test (file_name, G_FILE_TEST_IS_REGULAR));
  chatty_history_open (g_test_get_dir (G_TEST_BUILT), "test-history.db");
  g_assert_true (g_file_test (file_name, G_FILE_TEST_IS_REGULAR));

  status = sqlite3_open (file_name, &db);
  g_assert_cmpint (status, ==, SQLITE_OK);

  account = "account@test";
  who = "buddy@test";
  message = "Random messsage";
  room = "room@test";

  test_value (db, "SELECT id,timestamp,direction,account,who,uid,message FROM chatty_im LIMIT 1",
              SQLITE_DONE, 0, 0, NULL, NULL, NULL, NULL);
  test_value (db, "SELECT id,timestamp,direction,account,who,uid,message FROM chatty_chat LIMIT 1",
              SQLITE_DONE, 0, 0, NULL, NULL, NULL, NULL);
  test_value (db, "SELECT max(id),timestamp,direction,account,who,uid,message FROM chatty_im LIMIT 1",
              SQLITE_ROW, 1, -1, account, who, message, NULL);
  test_value (db, "SELECT id,timestamp,direction,account,who,uid,message FROM chatty_chat LIMIT 1",
              SQLITE_DONE, 0, 0, NULL, NULL, NULL, NULL);
  test_value (db, "SELECT max(id),timestamp,direction,account,who,uid,message FROM chatty_im LIMIT 1",
              SQLITE_ROW, 2, -1, account, who, message, NULL);
  test_value (db, "SELECT max(id),timestamp,direction,account,who,uid,message,room FROM chatty_chat LIMIT 1",
              SQLITE_ROW, 1, -1, account, who, message, room);
  test_value (db, "SELECT max(id),timestamp,direction,account,who,uid,message,room FROM chatty_chat LIMIT 1",
              SQLITE_ROW, 2, -1, account, who, message, room);

  chatty_history_close ();
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/history/new", test_history_new);
  g_test_add_func ("/history/message", test_history_message);
  g_test_add_func ("/history/raw_message", test_history_raw_message);
  g_test_add_func ("/history/db", test_history_db);

  return g_test_run ();
}
