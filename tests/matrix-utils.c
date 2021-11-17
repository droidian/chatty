/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* matrix-utils.c
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

#include <glib.h>

#include "matrix-utils.h"

static JsonObject *
get_json_object_for_file (const char *dir,
                          const char *file_name)
{
  g_autoptr(JsonParser) parser = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *path = NULL;

  path = g_build_filename (dir, file_name, NULL);
  parser = json_parser_new ();
  json_parser_load_from_file (parser, path, &error);
  g_assert_no_error (error);

  return json_node_dup_object (json_parser_get_root (parser));
}

static void
test_matrix_utils_canonical (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GDir) dir = NULL;
  g_autofree char *path = NULL;
  const char *name;

  path = g_test_build_filename (G_TEST_DIST, "matrix-utils", NULL);
  dir = g_dir_open (path, 0, &error);
  g_assert_no_error (error);

  while ((name = g_dir_read_name (dir)) != NULL) {
    g_autofree char *expected_file = NULL;
    g_autofree char *expected_name = NULL;
    g_autofree char *expected_json = NULL;
    g_autoptr(JsonObject) object = NULL;
    g_autoptr(GString) json_str = NULL;

    if (!g_str_has_suffix (name, ".json"))
      continue;

    expected_name = g_strconcat (name, ".expected", NULL);
    expected_file = g_build_filename (path, expected_name, NULL);
    g_file_get_contents (expected_file, &expected_json, NULL, &error);
    g_assert_no_error (error);

    object = get_json_object_for_file (path, name);
    json_str = matrix_utils_json_get_canonical (object, NULL);
    g_assert_cmpstr (json_str->str, ==, expected_json);
  }
}

int
main (int   argc,
      char *argv[])
{
  /* DEBUG */
  g_setenv ("G_TEST_SRCDIR", "/media/sadiq/temp/jhbuild/checkout/chatty/tests/", FALSE);

  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/matrix/utils/canonical", test_matrix_utils_canonical);

  return g_test_run ();
}
