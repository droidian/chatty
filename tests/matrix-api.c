/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* matrix-api.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "matrix-api.h"

static void
test_matrix_api_new (void)
{
  MatrixApi *api;
  const char *name;

  name = "@alice:example.com";
  api = matrix_api_new (NULL);
  g_assert (MATRIX_IS_API (api));
  g_assert_cmpstr (matrix_api_get_username (api), ==, NULL);
  matrix_api_set_login_username (api, name);
  g_assert_cmpstr (matrix_api_get_login_username (api), ==, name);
  g_object_unref (api);

  name = "@alice:example.org";
  api = matrix_api_new (name);
  g_assert (MATRIX_IS_API (api));
  g_assert_cmpstr (matrix_api_get_login_username (api), ==, name);
  g_assert_cmpstr (matrix_api_get_password (api), ==, NULL);
  g_assert_cmpstr (matrix_api_get_homeserver (api), ==, NULL);

  matrix_api_set_password (api, "hunter2");
  g_assert_cmpstr (matrix_api_get_password (api), ==, "hunter2");
  matrix_api_set_password (api, NULL);
  g_assert_cmpstr (matrix_api_get_password (api), ==, "hunter2");

  matrix_api_set_homeserver (api, "example.net");
  g_assert_cmpstr (matrix_api_get_homeserver (api), ==, NULL);
  matrix_api_set_homeserver (api, "https://example.com");
  g_assert_cmpstr (matrix_api_get_homeserver (api), ==, "https://example.com");
  matrix_api_set_homeserver (api, NULL);
  g_assert_cmpstr (matrix_api_get_homeserver (api), ==, "https://example.com");
  matrix_api_set_homeserver (api, "https://example.org/");
  g_assert_cmpstr (matrix_api_get_homeserver (api), ==, "https://example.org");
  matrix_api_set_homeserver (api, "https://chat.example.net/page");
  g_assert_cmpstr (matrix_api_get_homeserver (api), ==, "https://chat.example.net");
  matrix_api_set_homeserver (api, "http://talk.example.com");
  g_assert_cmpstr (matrix_api_get_homeserver (api), ==, "http://talk.example.com");
  matrix_api_set_homeserver (api, "http://example.com:80");
  g_assert_cmpstr (matrix_api_get_homeserver (api), ==, "http://example.com");
  matrix_api_set_homeserver (api, "http://example.com:80");
  g_assert_cmpstr (matrix_api_get_homeserver (api), ==, "http://example.com");
  matrix_api_set_homeserver (api, "https://talk.example.net:80");
  g_assert_cmpstr (matrix_api_get_homeserver (api), ==, "https://talk.example.net:80");

  g_object_unref (api);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/matrix/api/new", test_matrix_api_new);

  return g_test_run ();
}
