/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* sms-uri.c
 *
 * Copyright 2021 Purism SPC
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

#include "chatty-sms-uri.c"

typedef struct data {
  char *uri;
  char *body;
  char *numbers;
  guint n_numbers;
  gboolean valid;
  gboolean can_send;
  char *country;
} data;
data array[] = {
  { "sms:123", "", "123", 1, TRUE, TRUE},
  /* If there are 2+ numbers, with atleast one invalid, the URI is invalid
   * because this can't happen in an incoming nor outgoing message
   */
  { "sms:( BCD),123", "", "( BCD),123", 2, FALSE, FALSE},
  /* A URI with exactly one non numeric sender is considered valid
   * As it can happen in incoming messages, but in that case, body
   * should be empty as a message can't be send to the given number
   */
  { "sms:( BCD)", "", "( BCD)", 1, TRUE, FALSE},
  { "sms:DL-ABC?body=some body", "", "DL-ABC", 1, FALSE, FALSE},
  { "sms://+919995112233", "", "+919995112233", 1, TRUE, TRUE},
  { "sms://+919995112233?body=I'm busy", "I'm busy", "+919995112233", 1, TRUE, TRUE},
  { "sms://123,453?body=a ചെറിയ test", "a ചെറിയ test", "123,453", 2, TRUE, TRUE},
  { "sms://453,123,145,123,453?body=HELP", "HELP", "123,145,453", 3, TRUE, TRUE},
  { "sms://453,123,145,123,453?body=HELP%20me", "HELP me", "123,145,453", 3, TRUE, TRUE},
  { "sms:9995 123 123?body=Call me later", "Call me later", "+919995123123", 1, TRUE, TRUE, "IN"},
  { "sms:+919995 123 123?body= before and after ", " before and after ", "+919995123123", 1, TRUE, TRUE, "US"},
  { "sms:00919995 123 123?body=%26%20is ampersand", "& is ampersand", "+919995123123", 1, TRUE, TRUE, "GB"},
  { "sms:(213) 321-9876,+12133219876?body=Test", "Test", "+12133219876", 1, TRUE, TRUE, "US"},
  /* '&' is used to separate arguments, so everything from '&' is not a part of body here,
   * '&' can be incuded in body with '%26' */
  { "sms:123?body=is & ampersand?", "is ", "123", 1, TRUE, TRUE},
};

static void
test_mm_sms_uri (void)
{
  ChattySettings *settings;
  ChattySmsUri *uri;

  settings = chatty_settings_get_default ();

  uri = chatty_sms_uri_new (NULL);

  for (guint i = 0; i < G_N_ELEMENTS (array); i++) {
    g_autofree char *numbers_str = NULL;
    GPtrArray *numbers;

    if (array[i].country)
      chatty_settings_set_country_iso_code (settings, array[i].country);

    chatty_sms_uri_set_uri (uri, array[i].uri);
    g_assert_cmpint (chatty_sms_uri_is_valid (uri), ==, array[i].valid);
    g_assert_cmpint (chatty_sms_uri_can_send (uri), ==, array[i].can_send);
    g_assert_cmpstr (chatty_sms_uri_get_body (uri), ==, array[i].body);

    numbers = chatty_sms_uri_get_numbers (uri);
    g_assert_cmpint (numbers->len, ==, array[i].n_numbers);
    g_ptr_array_set_size (numbers, numbers->len + 1);
    numbers_str = g_strjoinv (",", (char **)numbers->pdata);
    g_ptr_array_set_size (numbers, numbers->len - 1);

    g_assert_cmpstr (chatty_sms_uri_get_numbers_str (uri), ==, array[i].numbers);
    g_assert_cmpstr (numbers_str, ==, array[i].numbers);
  }

  g_assert_finalize_object (uri);
}

static void
test_mm_sms_uri_new (void)
{
  ChattySmsUri *uri;

  uri = chatty_sms_uri_new (array[0].uri);
  g_assert_finalize_object (uri);

  uri = chatty_sms_uri_new (array[0].uri);
  g_assert_cmpint (chatty_sms_uri_is_valid (uri), ==, array[0].valid);
  g_assert_cmpstr (chatty_sms_uri_get_body (uri), ==, array[0].body);
  g_assert_cmpstr (chatty_sms_uri_get_numbers_str (uri), ==, array[0].numbers);
  g_assert_cmpint (chatty_sms_uri_can_send (uri), ==, array[0].can_send);

  chatty_sms_uri_set_uri (uri, array[1].uri);
  g_assert_cmpint (chatty_sms_uri_is_valid (uri), ==, array[1].valid);
  g_assert_cmpstr (chatty_sms_uri_get_body (uri), ==, array[1].body);
  g_assert_cmpint (chatty_sms_uri_can_send (uri), ==, array[1].can_send);
  g_assert_finalize_object (uri);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);

  g_test_add_func ("/mm/sms-uri/new", test_mm_sms_uri_new);
  g_test_add_func ("/mm/sms-uri", test_mm_sms_uri);

  return g_test_run ();
}
