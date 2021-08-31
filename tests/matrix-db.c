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

#undef NDEBUG
#undef G_DISABLE_ASSERT
#undef G_DISABLE_CHECKS
#undef G_DISABLE_CAST_CHECKS
#undef G_LOG_DOMAIN

#include <glib/gstdio.h>
#include <sqlite3.h>

#include "matrix/chatty-ma-account.h"
#include "matrix/matrix-db.h"

static void
finish_bool_cb (GObject      *object,
                GAsyncResult *result,
                gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  GTask *task = user_data;
  GObject *obj;
  gboolean status;

  g_assert_true (G_IS_TASK (task));

  status = g_task_propagate_boolean (G_TASK (result), &error);
  g_assert_no_error (error);

  obj = G_OBJECT (result);
  g_object_set_data (user_data, "enabled", g_object_get_data (obj, "enabled"));
  g_object_set_data_full (user_data, "pickle", g_object_steal_data (obj, "pickle"), g_free);
  g_object_set_data_full (user_data, "device", g_object_steal_data (obj, "device"), g_free);
  g_object_set_data_full (user_data, "username", g_object_steal_data (obj, "username"), g_free);
  g_task_return_boolean (task, status);
}

static gboolean
account_matches_username (gconstpointer account,
                          gconstpointer username)
{
  const char *id = username;

  g_assert_true (CHATTY_IS_MA_ACCOUNT ((gpointer)account));
  g_assert_true (id && *id);

  return g_strcmp0 (id, chatty_item_get_username ((gpointer)account)) == 0;
}

static void
add_matrix_account (MatrixDb   *db,
                    GPtrArray  *account_array,
                    const char *username,
                    const char *pickle,
                    const char *device_id,
                    gboolean    enabled)
{
  ChattyMaAccount *account;
  GObject *object;
  GTask *task;
  GError *error = NULL;
  gboolean success;
  guint i;

  g_assert_true (MATRIX_IS_DB (db));
  g_assert_nonnull (account_array);
  g_assert_nonnull (username);

  if (g_ptr_array_find_with_equal_func (account_array, username,
                                        account_matches_username, &i)) {
    account = account_array->pdata[i];
  } else {
    account = chatty_ma_account_new (username, NULL);
    g_ptr_array_add (account_array, account);
  }

  g_assert_true (CHATTY_IS_MA_ACCOUNT (account));
  object = G_OBJECT (account);

  g_object_set_data (object, "enabled", GINT_TO_POINTER (enabled));
  g_object_set_data_full (object, "pickle", g_strdup (pickle), g_free);
  g_object_set_data_full (object, "device", g_strdup (device_id), g_free);

  task = g_task_new (NULL, NULL, NULL, NULL);
  matrix_db_save_account_async (db, CHATTY_ACCOUNT (account), enabled, g_strdup (pickle), device_id,
                                NULL, finish_bool_cb, task);

  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  success = g_task_propagate_boolean (task, &error);
  g_assert_no_error (error);
  g_assert_true (success);
  g_clear_object (&task);

  g_assert_true (g_ptr_array_find (account_array, account, &i));
  account = account_array->pdata[i];
  task = g_task_new (NULL, NULL, NULL, NULL);
  matrix_db_load_account_async (db, CHATTY_ACCOUNT (account), device_id, finish_bool_cb, task);

  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  success = g_task_propagate_boolean (task, &error);
  g_assert_no_error (error);
  g_assert_true (success);
  g_assert_cmpstr (g_object_get_data (G_OBJECT (task), "username"), ==,
                   chatty_item_get_username (CHATTY_ITEM (account)));
  g_assert_cmpint (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "enabled")),
                   ==, GPOINTER_TO_INT (g_object_get_data (object, "enabled")));
  g_assert_cmpstr (g_object_get_data (G_OBJECT (task), "pickle"), ==,
                   g_object_get_data (object, "pickle"));
  g_assert_cmpstr (g_object_get_data (G_OBJECT (task), "device"), ==,
                   g_object_get_data (object, "device"));
  g_clear_object (&task);
}

static void
test_matrix_db_account (void)
{
  GTask *task;
  MatrixDb *db;
  gboolean status;
  GPtrArray *account_array;

  g_remove (g_test_get_filename (G_TEST_BUILT, "test-matrix.db", NULL));

  db = matrix_db_new ();
  task = g_task_new (NULL, NULL, NULL, NULL);
  matrix_db_open_async (db, g_strdup (g_test_get_dir (G_TEST_BUILT)),
                        "test-matrix.db", finish_bool_cb, task);

  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  status = g_task_propagate_boolean (task, NULL);
  g_assert_true (status);

  account_array = g_ptr_array_new ();
  g_ptr_array_set_free_func (account_array, (GDestroyNotify)g_object_unref);

  add_matrix_account (db, account_array, "@alice:example.org",
                       NULL, NULL, TRUE);
  add_matrix_account (db, account_array, "@alice:example.org",
                      NULL, NULL, FALSE);
  add_matrix_account (db, account_array, "@alice:example.com",
                      NULL, "XXAABBDD", FALSE);
  add_matrix_account (db, account_array, "@alice:example.com",
                      "Some Pickle", "XXAABBDD", TRUE);
  add_matrix_account (db, account_array, "@alice:example.org",
                      NULL, NULL, TRUE);

  add_matrix_account (db, account_array, "@bob:example.org",
                      NULL, NULL, FALSE);
  add_matrix_account (db, account_array, "@alice:example.org",
                      NULL, NULL, FALSE);
  add_matrix_account (db, account_array, "@bob:example.org",
                      NULL, NULL, TRUE);

  add_matrix_account (db, account_array, "@alice:example.net",
                      NULL, NULL, TRUE);
  add_matrix_account (db, account_array, "@alice:example.com",
                      NULL, NULL, FALSE);
}

static void
test_matrix_db_new (void)
{
  const char *file_name;
  MatrixDb *db;
  GTask *task;
  gboolean status;

  file_name = g_test_get_filename (G_TEST_BUILT, "test-matrix.db", NULL);
  g_remove (file_name);
  g_assert_false (g_file_test (file_name, G_FILE_TEST_EXISTS));

  db = matrix_db_new ();
  task = g_task_new (NULL, NULL, NULL, NULL);
  matrix_db_open_async (db, g_strdup (g_test_get_dir (G_TEST_BUILT)),
                        "test-matrix.db", finish_bool_cb, task);

  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  status = g_task_propagate_boolean (task, NULL);
  g_assert_true (g_file_test (file_name, G_FILE_TEST_IS_REGULAR));
  g_assert_true (matrix_db_is_open (db));
  g_assert_true (status);
  g_clear_object (&task);

  task = g_task_new (NULL, NULL, NULL, NULL);
  matrix_db_close_async (db, finish_bool_cb, task);

  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  status = g_task_propagate_boolean (task, NULL);
  g_assert_true (status);
  g_assert_false (matrix_db_is_open (db));
  g_clear_object (&db);
  g_clear_object (&task);

  g_remove (file_name);
  g_assert_false (g_file_test (file_name, G_FILE_TEST_EXISTS));
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/matrix-db/new", test_matrix_db_new);
  g_test_add_func ("/matrix-db/account", test_matrix_db_account);

  return g_test_run ();
}
