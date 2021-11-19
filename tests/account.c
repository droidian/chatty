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

#include <glib.h>

#include "purple-init.h"
#include "chatty-pp-account.h"

static void
test_account (ChattyAccount *ac,
              const char    *protocol_id,
              const char    *username,
              gboolean       is_sms)
{
  PurpleAccount *account;
  const gchar *str;
  gboolean value;

  g_assert_true (CHATTY_IS_PP_ACCOUNT (ac));

  account = chatty_pp_account_get_account (CHATTY_PP_ACCOUNT (ac));
  g_assert_nonnull (account);

  str = chatty_item_get_username (CHATTY_ITEM (ac));
  g_assert_cmpstr (str, ==, username);

  chatty_account_set_enabled (ac, TRUE);
  value = chatty_account_get_enabled (ac);
  g_assert_true (value);

  chatty_account_set_enabled (ac, FALSE);
  value = chatty_account_get_enabled (ac);
  g_assert_false (value);

  chatty_account_set_remember_password (ac, TRUE);
  value = chatty_account_get_remember_password (ac);
  g_assert_true (value);

  chatty_account_set_remember_password (ac, FALSE);
  value = chatty_account_get_remember_password (ac);
  g_assert_false (value);

  chatty_account_set_password (ac, "P@ssw0rd");
  str = chatty_account_get_password (ac);
  g_assert_cmpstr (str, ==, "P@ssw0rd");

  chatty_account_set_password (ac, "രഹസ്യം");
  str = chatty_account_get_password (ac);
  g_assert_cmpstr (str, ==, "രഹസ്യം");
}

static void
test_account_new_xmpp (void)
{
  ChattyPpAccount *account;

  account = chatty_pp_account_new (CHATTY_PROTOCOL_XMPP, "test@example.com", NULL, FALSE);
  test_account (CHATTY_ACCOUNT (account), "prpl-jabber", "test@example.com", FALSE);
  g_object_unref (account);

  account = chatty_pp_account_new (CHATTY_PROTOCOL_XMPP, "test@example.org", "not-used.com", FALSE);
  test_account (CHATTY_ACCOUNT (account), "prpl-jabber", "test@example.org", FALSE);
  g_object_unref (account);
}

static void
test_new_account (void)
{
  ChattyPpAccount *account = NULL;
  PurpleAccount *pp_account;
  const gchar *str;

  account = chatty_pp_account_new (CHATTY_PROTOCOL_XMPP, "xmpp@example.com", NULL, FALSE);
  g_assert (CHATTY_IS_PP_ACCOUNT (account));

  str = chatty_account_get_protocol_name (CHATTY_ACCOUNT (account));
  g_assert_cmpstr (str, ==, "XMPP");

  pp_account = chatty_pp_account_get_account (account);
  g_assert_nonnull (pp_account);

  g_object_unref (account);
  purple_account_destroy (pp_account);

  pp_account = purple_account_new ("xmpp@example.com", "prpl-jabber");
  account = chatty_pp_account_new_purple (pp_account, FALSE);
  g_assert (CHATTY_IS_PP_ACCOUNT (account));

  g_object_unref (account);
  purple_account_destroy (pp_account);
}

int
main (int   argc,
      char *argv[])
{
  int ret;

  g_test_init (&argc, &argv, NULL);

  test_purple_init ();

  g_test_add_func ("/account/new", test_new_account);
  g_test_add_func ("/account/new/xmpp", test_account_new_xmpp);

  ret = g_test_run ();

  /* FIXME: purple_core_quit() results in more leak! */
  purple_core_quit ();

  return ret;
}
