/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* utils.c
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

#include "chatty-settings.h"
#include "chatty-phone-utils.h"
#include "chatty-utils.h"

const char *valid[][2] = {
  {"9633123456", "IN"},
  {"9633 123 456", "IN"},
  {"+91 9633 123 456", "IN"},
  {"+91 9633 123 456", "US"},
  {"20 8759 9036", "GB"},
};

const char *invalid[][2] = {
  {"9633123456", "US"},
  {"20 8759 9036", "IN"},
  {"123456", "IN"},
  {"123456", "US"},
  {"123456", NULL},
  {"123456", ""},
  {"", ""},
  {NULL, ""},
  {NULL, "US"},
  {"INVALID", ""},
  {"INVALID", "US"},
};

const char *phone[][3] = {
  {"9633123456", "IN", "+919633123456"},
  {"09633123456", "IN", "+919633123456"},
  {"00919633123456", "IN", "+919633123456"},
  {"+919633123456", "IN", "+919633123456"},
  {"+919633123456", "US", "+919633123456"},
  {"9633 123 456", "IN", "+919633123456"},
  {"9633 123 456", "DE", "+499633123456"},
  {"9633123456", "US", "(963) 312-3456"},
  {"213-321-9876", "US", "+12133219876"},
  {"(213) 321-9876", "US", "+12133219876"},
  {"+1 213 321 9876", "US", "+12133219876"},
  {"+1 213 321 9876", "DE", "+12133219876"},
  {"+1 213 321 9876", "PL", "+12133219876"},
  {"+1 213 321 9876", "GB", "+12133219876"},
  {"+12133219876", "US", "+12133219876"},
  {"00919633123456", "GB", "+919633123456"},
  {"sms://00919633123456", "GB", "+919633123456"},
  {"12345", "IN", "12345"},
  {"12345", "US", "12345"},
  {"12345", "DE", "12345"},
  {"72404", "DE", "72404"},
  {"5800678", "IN", "5800678"},
  {"555555", "IN", "555555"},
  {"5555", "PL", "5555"},
  {"7126", "PL", "7126"},
  {"80510", "PL", "80510"},
  {"112", "DE", "112"},
  {"112", "US", "112"},
  {"112", "IN", "112"},
  {"911", "US", "911"},
  {"sms://911", "US", "911"},
  {"BT-123", "IN", NULL},
  {"123-BT", "IN", NULL},
};

static void
test_phone_utils_valid (void)
{
  for (guint i = 0; i < G_N_ELEMENTS (valid); i++)
    g_assert_true (chatty_phone_utils_is_valid (valid[i][0], valid[i][1]));

  for (guint i = 0; i < G_N_ELEMENTS (invalid); i++)
    g_assert_false (chatty_phone_utils_is_valid (invalid[i][0], invalid[i][1]));
}

static void
test_phone_utils_check_phone (void)
{
  for (guint i = 0; i < G_N_ELEMENTS (phone); i++) {
    g_autofree char *expected = NULL;

    expected = chatty_utils_check_phonenumber (phone[i][0], phone[i][1]);
    g_assert_cmpstr (expected, ==, phone[i][2]);
  }
}


static void
test_utils_username_valid (void)
{
  typedef struct data {
    char *user_name;
    ChattyProtocol protocol;
    ChattyProtocol account_protocol;
  } data;
  data array[] = {
    { "0123456789", CHATTY_PROTOCOL_MMS_SMS},
    { "+1 (1234) 5678", CHATTY_PROTOCOL_MMS_SMS},
    { "+91123456789", CHATTY_PROTOCOL_MMS_SMS},
    { "+91-1234-56789", CHATTY_PROTOCOL_MMS_SMS},
    { "+1 213 321 4567", CHATTY_PROTOCOL_MMS_SMS | CHATTY_PROTOCOL_TELEGRAM},
    { "+12133214567", CHATTY_PROTOCOL_MMS_SMS | CHATTY_PROTOCOL_TELEGRAM},
    { "+919995123456", CHATTY_PROTOCOL_MMS_SMS | CHATTY_PROTOCOL_TELEGRAM},
    { "5555", CHATTY_PROTOCOL_MMS_SMS},
    { "valid@xmpp.example.com", CHATTY_PROTOCOL_XMPP},
    { "@valid:example.com", CHATTY_PROTOCOL_MATRIX},
    { "@നല്ല:matrix.example.com", 0},
    { "invalid", 0},
    { "domain/resource", 0},
    { "/invalid", 0},
    { "invalid/", 0},
    { "@invalid", 0},
    { "invalid:", 0},
    { "@invalid:", 0},
    { "invalid@", 0},
    { "in:valid@", 0},
    { "#invalid:matrix.example.com", 0},
    { "+9876543210A", 0},
    { "", 0},
    { NULL, 0}
  };


  for (int i = 0; i < G_N_ELEMENTS (array); i++) {
    ChattyProtocol protocol;

    protocol = chatty_utils_username_is_valid (array[i].user_name, array[i].protocol);
    g_assert_cmpint (protocol, ==, array[i].protocol);

    protocol = chatty_utils_username_is_valid (array[i].user_name, CHATTY_PROTOCOL_ANY);
    g_assert_cmpint (protocol, ==, array[i].protocol);
  }
}

static void
test_utils_groupname_valid (void)
{
  typedef struct data {
    char *group_name;
    ChattyProtocol protocol;
  } data;
  data array[] = {
    { "valid@xmpp.example.com", CHATTY_PROTOCOL_XMPP },
    { "#valid:matrix.example.com", CHATTY_PROTOCOL_MATRIX },
    { "!valid:matrix.example.com", CHATTY_PROTOCOL_MATRIX },
    { "@invalid:matrix.example.com", 0 },
    { "#:invalid", 0 },
    { "@invalid", 0},
    { "#invalid", 0},
    { "invalid:", 0},
    { "#invalid:", 0},
    { "invalid#", 0},
    { "in:valid#", 0},
    { "#:", 0 },
    { "", 0},
    { NULL, 0}
  };

  for (int i = 0; i < G_N_ELEMENTS (array); i++) {
    ChattyProtocol protocol;

    protocol = chatty_utils_groupname_is_valid (array[i].group_name, array[i].protocol);
    g_assert_cmpint (protocol, ==, array[i].protocol);

    protocol = chatty_utils_groupname_is_valid (array[i].group_name, CHATTY_PROTOCOL_ANY);
    g_assert_cmpint (protocol, ==, array[i].protocol);
  }
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/phone-utils/valid", test_phone_utils_valid);
  g_test_add_func ("/utils/check-phone", test_phone_utils_check_phone);
  g_test_add_func ("/utils/username_valid", test_utils_username_valid);
  g_test_add_func ("/utils/groupname_valid", test_utils_groupname_valid);

  return g_test_run ();
}
