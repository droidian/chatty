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

#include "purple-init.h"
#include "chatty-history.h"

typedef struct Message {
  char *uuid;
  char *room;
  PurpleConvMessage *msg;
} Message;

static guint array_index = 0;

static int
direction_for_flag (PurpleMessageFlags flag)
{
  if (flag & PURPLE_MESSAGE_RECV)
    return 1;

  if (flag & PURPLE_MESSAGE_SEND)
    return -1;

  if (flag & PURPLE_MESSAGE_SYSTEM)
    return 0;

  g_return_val_if_reached (0);
}

static void
free_message (Message *msg)
{
  g_free (msg->msg->who);
  g_free (msg->msg->what);
  g_free (msg->msg->alias);
  g_free (msg->uuid);
  g_free (msg->room);
  g_free (msg);
}

static Message *
new_message (const char         *buddy_username,
             const char         *msg_text,
             const char         *uuid,
             PurpleMessageFlags  flags,
             time_t              time_stamp,
             const char         *room)
{
  PurpleConvMessage *msg;
  Message *message;

  msg = g_new (PurpleConvMessage, 1);
  msg->who = g_strdup (buddy_username);
  msg->what = g_strdup (msg_text);
  msg->when  = time_stamp;
  msg->flags = flags;
  msg->alias = g_strdup (room);

  if (!msg->alias)
    msg->alias = g_strdup ("");

  message = g_new (Message, 1);
  message->msg = msg;
  message->uuid = g_strdup (uuid);
  message->room = g_strdup (room);

  return message;
}

static void
compare_im (const guchar *msg_text,
            int           direction,
            time_t        time_stamp,
            const guchar *uuid,
            gpointer      data,
            int           last_message)
{
  GPtrArray *msg_array = data;
  PurpleConvMessage *msg;
  Message *message;
  int dir;

  g_assert (data);
  g_assert (array_index < msg_array->len);

  message = msg_array->pdata[msg_array->len - array_index - 1];
  g_assert (message);
  g_assert (message->msg);

  msg = message->msg;
  dir = direction_for_flag (msg->flags);

  g_assert_cmpstr (message->uuid, ==, (const char *)uuid);
  g_assert_cmpstr (msg->what, ==, (const char *)msg_text);
  g_assert_cmpint (msg->when, ==, time_stamp);
  g_assert_cmpint (dir, ==, direction);

  array_index++;
}

static void
compare_chat (const guchar *msg_text,
              int           direction,
              int           time_stamp,
              const char   *room,
              const guchar *who,
              const guchar *uuid,
              gpointer      data)
{
  GPtrArray *msg_array = data;
  PurpleConvMessage *msg;
  Message *message;
  int dir;

  g_assert (data);

  g_assert (array_index < msg_array->len);

  message = msg_array->pdata[msg_array->len - array_index - 1];
  g_assert (message);
  g_assert (message->msg);

  msg = message->msg;
  dir = direction_for_flag (msg->flags);

  g_assert_cmpstr (message->uuid, ==, (const char *)uuid);
  g_assert_cmpstr (message->room, ==, room);
  g_assert_cmpstr (msg->who, ==, (const char *)who);
  g_assert_cmpstr (msg->what, ==, (const char *)msg_text);
  g_assert_cmpint (msg->when, ==, time_stamp);
  g_assert_cmpint (dir, ==, direction);

  array_index++;
}

static void
add_im (GPtrArray          *msg_array,
        const char         *ac,
        const char         *buddy,
        const char         *msg_text,
        time_t              time_stamp,
        PurpleMessageFlags  flags)
{
  PurpleConvMessage *msg;
  Message *message;
  ChattyLog *log_data;
  char *uuid;
  int dir;
  char message_exists;

  uuid = g_uuid_string_random ();
  message = new_message (buddy, msg_text, uuid, flags, time_stamp, NULL);
  g_ptr_array_add (msg_array, message);

  msg = message->msg;
  dir = direction_for_flag (msg->flags);

  array_index = 0;
  chatty_history_add_im_message (msg->what, dir, ac, msg->who, uuid, msg->when);
  chatty_history_get_im_messages (ac, buddy, compare_im, msg_array, msg_array->len, NULL);
  g_assert_cmpint (array_index, ==, msg_array->len);

  log_data = g_new0 (ChattyLog, 1);
  message_exists = chatty_history_get_im_last_message (ac, buddy, log_data);
  g_assert_true (!!message_exists);
  g_assert_cmpint (log_data->epoch, ==, msg->when);
  g_assert_cmpint (log_data->dir, ==, dir);
  g_assert_cmpstr (log_data->msg, ==, msg->what);
  g_assert_cmpstr (log_data->uid, ==, message->uuid);
  g_free (log_data->msg);
  g_free (log_data);
}

static void
add_chat (GPtrArray          *msg_array,
          const char         *ac,
          const char         *buddy,
          const char         *room,
          const char         *msg_text,
          time_t              time_stamp,
          PurpleMessageFlags  flags)
{
  PurpleConvMessage *msg;
  Message *message;
  char *uuid;
  int dir, last_time;

  uuid = g_uuid_string_random ();
  message = new_message (buddy,  msg_text, uuid, flags, time_stamp, room);
  g_ptr_array_add (msg_array, message);

  msg = message->msg;
  dir = direction_for_flag (msg->flags);

  array_index = 0;
  chatty_history_add_chat_message (msg->what, dir, ac, msg->who, uuid, msg->when, room);
  chatty_history_get_chat_messages (ac, room, compare_chat, msg_array, msg_array->len, NULL);
  g_assert_cmpint (array_index, ==, msg_array->len);

  /* Load some of the contents */
  if (msg_array->len >= 2) {
    array_index = 0;
    chatty_history_get_chat_messages (ac, room, compare_chat, msg_array, msg_array->len - 1, NULL);
    g_assert_cmpint (array_index, ==, msg_array->len - 1);
  }

  last_time = chatty_history_get_chat_last_message_time (ac, room);
  g_assert_cmpint (last_time, ==, msg->when);
}

static void
test_history_im (void)
{
  GPtrArray *msg_array;
  ChattyLog *log_data;
  const char *account, *buddy;
  char message_exists;

  g_remove (g_test_get_filename (G_TEST_BUILT, "test-history.db", NULL));
  chatty_history_open (g_test_get_dir (G_TEST_BUILT), "test-history.db");

  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  account = "account@test";
  buddy = "buddy@test";

  log_data = g_new0 (ChattyLog, 1);
  message_exists = chatty_history_get_im_last_message (account, buddy, log_data);
  g_assert_false (!!message_exists);
  g_free (log_data->msg);

  add_im (msg_array, account, buddy,
          "Message", time (NULL) - 4, PURPLE_MESSAGE_SYSTEM);
  add_im (msg_array, account, buddy,
          "Some random message", time (NULL) - 3, PURPLE_MESSAGE_SYSTEM);
  add_im (msg_array, account, buddy,
          "Yet another random message", time (NULL) -1, PURPLE_MESSAGE_SEND);
  add_im (msg_array, account, buddy,
          "നല്ല ഒരു അറിവ് message", time (NULL), PURPLE_MESSAGE_SEND);
  add_im (msg_array, account, buddy,
          "A very simple message", time (NULL), PURPLE_MESSAGE_RECV);
  add_im (msg_array, account, buddy,
          "And one more", time (NULL), PURPLE_MESSAGE_RECV);
  add_im (msg_array, account, buddy,
          "And one more", time (NULL) + 1, PURPLE_MESSAGE_RECV);

  g_ptr_array_free (msg_array, TRUE);

  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  buddy = "somebuddy@test";
  add_im (msg_array, account, buddy,
          "Message", time (NULL), PURPLE_MESSAGE_SYSTEM);
  add_im (msg_array, account, buddy,
          "Some text message", time (NULL) + 1, PURPLE_MESSAGE_SYSTEM);
  add_im (msg_array, account, buddy,
          "Yet another test message", time (NULL) + 1, PURPLE_MESSAGE_SEND);
  add_im (msg_array, account, buddy,
          "നല്ല ഒരു അറിവ്", time (NULL) + 1, PURPLE_MESSAGE_SEND);
  add_im (msg_array, account, buddy,
          "A Simple message", time (NULL) + 1, PURPLE_MESSAGE_RECV);
  add_im (msg_array, account, buddy,
          "And one more", time (NULL) + 2, PURPLE_MESSAGE_RECV);
  add_im (msg_array, account, buddy,
          "And one more", time (NULL) + 3, PURPLE_MESSAGE_RECV);

  buddy = "buddy@test";
  message_exists = chatty_history_get_im_last_message (account, buddy, log_data);
  g_assert_true (!!message_exists);
  chatty_history_delete_im (account, buddy);
  message_exists = chatty_history_get_im_last_message (account, buddy, log_data);
  g_assert_false (!!message_exists);

  buddy = "somebuddy@test";
  message_exists = chatty_history_get_im_last_message (account, buddy, log_data);
  g_assert_true (!!message_exists);
  chatty_history_delete_im (account, buddy);
  message_exists = chatty_history_get_im_last_message (account, buddy, log_data);
  g_assert_false (!!message_exists);

  chatty_history_close ();
}

static void
test_history_chat (void)
{
  GPtrArray *msg_array;
  ChattyLog *log_data;
  const char *account, *buddy, *room;
  int last_time;

  g_remove (g_test_get_filename (G_TEST_BUILT, "test-history.db", NULL));
  chatty_history_open (g_test_get_dir (G_TEST_BUILT), "test-history.db");

  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  account = "account@test";
  buddy = "buddy@test";

  log_data = g_new0 (ChattyLog, 1);
  last_time = chatty_history_get_im_last_message (account, buddy, log_data);
  g_assert_false (!!last_time);
  g_free (log_data->msg);

  room = "room@test";
  add_chat (msg_array, account, buddy, room,
            "Message", time (NULL) - 4, PURPLE_MESSAGE_SYSTEM);
  add_chat (msg_array, account, buddy, room,
            "Some random message", time (NULL) - 3, PURPLE_MESSAGE_SYSTEM);
  add_chat (msg_array, account, buddy, room,
            "Yet another random message", time (NULL) -1, PURPLE_MESSAGE_SEND);
  add_chat (msg_array, account, buddy, room,
            "നല്ല ഒരു അറിവ് message", time (NULL), PURPLE_MESSAGE_SEND);
  add_chat (msg_array, account, buddy, room,
            "A very simple message", time (NULL), PURPLE_MESSAGE_RECV);
  add_chat (msg_array, account, buddy, room,
            "And one more", time (NULL), PURPLE_MESSAGE_RECV);
  add_chat (msg_array, account, buddy, room,
            "And one more", time (NULL) + 1, PURPLE_MESSAGE_RECV);

  g_ptr_array_free (msg_array, TRUE);

  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  room = "another@test";
  buddy = "another-buddy@test";
  add_chat (msg_array, account, buddy, room,
            "Message", time (NULL), PURPLE_MESSAGE_SYSTEM);
  add_chat (msg_array, account, buddy, room,
            "Some text message", time (NULL) + 1, PURPLE_MESSAGE_SYSTEM);
  add_chat (msg_array, account, buddy, room,
            "Yet another test message", time (NULL) + 1, PURPLE_MESSAGE_SEND);
  add_chat (msg_array, account, buddy, room,
            "നല്ല ഒരു അറിവ്", time (NULL) + 1, PURPLE_MESSAGE_SEND);
  add_chat (msg_array, account, buddy, room,
            "A Simple message", time (NULL) + 1, PURPLE_MESSAGE_RECV);
  add_chat (msg_array, account, buddy, room,
            "And one more", time (NULL) + 2, PURPLE_MESSAGE_RECV);
  add_chat (msg_array, account, buddy, room,
          "And one more", time (NULL) + 3, PURPLE_MESSAGE_RECV);

  room = "room@test";
  last_time = chatty_history_get_chat_last_message_time (account, room);
  g_assert_true (!!last_time);
  chatty_history_delete_chat (account, room);
  last_time = chatty_history_get_chat_last_message_time (account, room);
  g_assert_false (!!last_time);

  room = "another@test";
  last_time = chatty_history_get_chat_last_message_time (account, room);
  g_assert_true (!!last_time);
  chatty_history_delete_chat (account, room);
  last_time = chatty_history_get_chat_last_message_time (account, room);
  g_assert_false (!!last_time);

  chatty_history_close ();
}

static void
test_history_message (void)
{
  GPtrArray *msg_array;
  PurpleAccount *pa;
  Message *message;
  const char *buddy;
  char *uuid;

  g_remove (g_test_get_filename (G_TEST_BUILT, "test-history.db", NULL));
  chatty_history_open (g_test_get_dir (G_TEST_BUILT), "test-history.db");

  pa = g_new0 (PurpleAccount, 1);
  pa->username = g_strdup ("account@test");

  /* Test chat message */
  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  uuid = g_uuid_string_random ();
  buddy = "buddy@test";
  message = new_message (buddy, "Random message",
                         uuid, PURPLE_MESSAGE_SYSTEM, time (NULL), "chatroom@test");
  g_ptr_array_add (msg_array, message);
  chatty_history_add_message (pa, message->msg, &uuid, PURPLE_CONV_TYPE_CHAT, NULL);

  array_index = 0;
  chatty_history_get_chat_messages (pa->username, message->room, compare_chat,
                                    msg_array, msg_array->len, NULL);
  g_assert_cmpint (array_index, ==, msg_array->len);
  g_clear_pointer (&uuid, g_free);
  g_ptr_array_free (msg_array, TRUE);

  /* Test IM message */
  msg_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (msg_array, (GDestroyNotify)free_message);

  message = new_message (buddy, "Really Random message",
                         uuid, PURPLE_MESSAGE_SEND, time (NULL), "chatroom@test");
  g_ptr_array_add (msg_array, message);
  chatty_history_add_message (pa, message->msg, &uuid, PURPLE_CONV_TYPE_IM, NULL);
  g_assert_nonnull (uuid);
  message->uuid = uuid;

  array_index = 0;
  chatty_history_get_im_messages (pa->username, buddy, compare_im,
                                    msg_array, msg_array->len, NULL);
  g_assert_cmpint (array_index, ==, msg_array->len);
  g_ptr_array_free (msg_array, TRUE);

  chatty_history_close ();
}

int
main (int   argc,
      char *argv[])
{
  int ret;

  g_test_init (&argc, &argv, NULL);

  test_purple_init ();

  g_test_add_func ("/history/im", test_history_im);
  g_test_add_func ("/history/chat", test_history_chat);
  g_test_add_func ("/history/message", test_history_message);

  ret = g_test_run ();

  /* FIXME: purple_core_quit() results in more leak! */
  purple_core_quit ();

  return ret;
}
