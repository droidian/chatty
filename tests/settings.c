/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* settings.c
 *
 * Copyright 2019 Purism SPC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

#include "chatty-settings.h"

static void
set_settings_bool (ChattySettings *settings,
                   gboolean        value)
{
  g_assert_true (CHATTY_IS_SETTINGS (settings));
  g_object_set (settings,
                "send-receipts", value,
                "message-carbons", value,
                "send-typing", value,
                "greyout-offline-buddies", value,
                "blur-idle-buddies", value,
                "indicate-unknown-contacts", value,
                "return-sends-message", value,
                NULL);
}

static void
check_settings_true (ChattySettings *settings)
{
  g_assert_true (CHATTY_IS_SETTINGS (settings));

  set_settings_bool (settings, TRUE);

  g_assert_true (chatty_settings_get_send_receipts (settings));
  g_assert_true (chatty_settings_get_message_carbons (settings));
  g_assert_true (chatty_settings_get_send_typing (settings));
  g_assert_true (chatty_settings_get_greyout_offline_buddies (settings));
  g_assert_true (chatty_settings_get_blur_idle_buddies (settings));
  g_assert_true (chatty_settings_get_indicate_unknown_contacts (settings));
  g_assert_true (chatty_settings_get_return_sends_message (settings));
}

static void
check_settings_false (ChattySettings *settings)
{
  g_assert_true (CHATTY_IS_SETTINGS (settings));

  set_settings_bool (settings, FALSE);

  g_assert_false (chatty_settings_get_send_receipts (settings));
  g_assert_false (chatty_settings_get_message_carbons (settings));
  g_assert_false (chatty_settings_get_send_typing (settings));
  g_assert_false (chatty_settings_get_greyout_offline_buddies (settings));
  g_assert_false (chatty_settings_get_blur_idle_buddies (settings));
  g_assert_false (chatty_settings_get_indicate_unknown_contacts (settings));
  g_assert_false (chatty_settings_get_return_sends_message (settings));
}

static void
test_settings_all_bool (void)
{
  ChattySettings *settings;

  settings = chatty_settings_get_default ();
  check_settings_true (settings);
  g_object_unref (settings);

  /*
   * Test again with a new settings to see
   * if settings are saved to gsettings
   */
  settings = chatty_settings_get_default ();
  check_settings_true (settings);
  g_object_unref (settings);

  settings = chatty_settings_get_default ();
  check_settings_false (settings);
  g_object_unref (settings);

  settings = chatty_settings_get_default ();
  check_settings_false (settings);
  g_object_unref (settings);
}

static void
test_settings_first_start (void)
{
  ChattySettings *settings;
  g_autoptr(GSettings) gsettings = NULL;
  gboolean first_start;

  /* Reset the “first-start” settings */
  gsettings = g_settings_new ("sm.puri.Chatty");
  g_settings_reset (gsettings, "first-start");

  settings = chatty_settings_get_default ();
  g_assert_true (CHATTY_IS_SETTINGS (settings));
  g_object_get (settings, "first-start", &first_start, NULL);
  g_assert_true (first_start);
  g_assert_true (chatty_settings_get_first_start (settings));
  g_object_unref (settings);

  /*
   * create a new object, and check again.  Everytime the settings
   * is finalized (ie, the program ends) “first-start” should be
   * set.
   */
  settings = chatty_settings_get_default ();
  g_assert_true (CHATTY_IS_SETTINGS (settings));
  g_object_get (settings, "first-start", &first_start, NULL);
  g_assert_false (first_start);
  g_assert_false (chatty_settings_get_first_start (settings));
  g_object_unref (settings);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);

  g_test_add_func ("/settings/first_start", test_settings_first_start);
  g_test_add_func ("/settings/all_bool", test_settings_all_bool);

  return g_test_run ();
}
