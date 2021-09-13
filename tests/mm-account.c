/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* mm-account.c
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "users/chatty-mm-account.c"

static void
test_mm_account_find_chat (void)
{
  ChattyMmAccount *account;
  ChattyHistory *history;
  GListModel *chat_list;

  account = chatty_mm_account_new ();
  g_assert_true (CHATTY_IS_MM_ACCOUNT (account));
  chat_list = chatty_mm_account_get_chat_list (account);
  g_assert_true (G_IS_LIST_STORE (chat_list));
  g_assert_cmpint (g_list_model_get_n_items (chat_list), ==, 0);

  history = chatty_history_new ();
  g_assert_true (CHATTY_IS_HISTORY (history));
  chatty_mm_account_set_history_db (account, history);

  chatty_mm_account_start_chat (account, "123");
  g_assert_cmpint (g_list_model_get_n_items (chat_list), ==, 1);

  chatty_mm_account_start_chat (account, "9633-111-222");
  g_assert_cmpint (g_list_model_get_n_items (chat_list), ==, 2);

  /* The same number above with different formating */
  chatty_mm_account_start_chat (account, "9633111222");
  g_assert_cmpint (g_list_model_get_n_items (chat_list), ==, 2);

  chatty_mm_account_start_chat (account, "GNU");
  g_assert_cmpint (g_list_model_get_n_items (chat_list), ==, 3);

  chatty_mm_account_start_chat (account, "Purism");
  g_assert_cmpint (g_list_model_get_n_items (chat_list), ==, 4);

  chatty_mm_account_start_chat (account, "GNU");
  g_assert_cmpint (g_list_model_get_n_items (chat_list), ==, 4);

  chatty_mm_account_start_chat (account, "gnu");
  g_assert_cmpint (g_list_model_get_n_items (chat_list), ==, 5);

  chatty_mm_account_start_chat (account, "Purism");
  g_assert_cmpint (g_list_model_get_n_items (chat_list), ==, 5);

  g_list_store_remove_all (G_LIST_STORE (chat_list));
  g_assert_finalize_object (account);
}

static void
test_mm_account_new (void)
{
  ChattyMmAccount *account;
  GListModel *chat_list;

  account = chatty_mm_account_new ();
  g_assert_true (CHATTY_IS_MM_ACCOUNT (account));

  g_assert_cmpstr (chatty_item_get_username (CHATTY_ITEM (account)), ==, "SMS");
  g_assert_cmpint (chatty_item_get_protocols (CHATTY_ITEM (account)), ==, CHATTY_PROTOCOL_MMS_SMS);

  chat_list = chatty_mm_account_get_chat_list (account);
  g_assert_true (G_IS_LIST_MODEL (chat_list));
  g_assert_cmpint (g_list_model_get_n_items (chat_list), ==, 0);

  g_assert_finalize_object (account);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);

  g_test_add_func ("/mm/account/new", test_mm_account_new);
  g_test_add_func ("/mm/account/find-chat", test_mm_account_find_chat);

  return g_test_run ();
}
