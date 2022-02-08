/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* clock.c
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

#include "chatty-clock.c"

typedef struct data {
  char *time_in;
  char *time_now;
  char *human_time;
  char *human_time_detailed;
  char *human_time24;
  char *human_time24_detailed;
} data;
data array[] = {
  {
    "2021-12-07 19:17:58+03:30", "2021-12-07 19:17:58+03:30",
    "07∶17 PM", "Just Now",
    "19∶17", "Just Now",
  },
  { /* We allow the time to be a in future up to 5 seconds */
    "2021-12-07 19:18:02+03:30", "2021-12-07 19:18:01+03:30",
    "07∶18 PM", "Just Now",
    "19∶18", "Just Now",
  },
  { /* We allow the time to be a in future up to 5 seconds */
    "2021-12-07 19:18:02+03:30", "2021-12-07 19:17:56+03:30",
    "2021-12-07", "2021-12-07",
    "2021-12-07", "2021-12-07",
  },
  {
    "2021-12-07 19:17:58+03:30", "2021-12-07 19:18:57+03:30",
    "07∶17 PM", "Just Now",
    "19∶17", "Just Now",
  },
  {
    "2021-12-07 23:59:04+03:30", "2021-12-08 00:00:02+03:30",
    "Tue", "Just Now",
    "Tue", "Just Now",
  },
  {
    "2021-12-31 23:59:04+03:30", "2022-01-01 00:00:02+03:30",
    "Fri", "Just Now",
    "Fri", "Just Now",
  },
  {
    "2021-12-07 19:17:58+03:30", "2021-12-07 19:18:59+03:30",
    "07∶17 PM", "1 minute ago",
    "19∶17", "1 minute ago",
  },
  {
    "2021-12-07 23:59:04+03:30", "2021-12-08 00:01:02+03:30",
    "Tue", "1 minute ago",
    "Tue",  "1 minute ago",
  },
  {
    "2021-12-07 19:17:58+03:30", "2021-12-07 19:20:59+03:30",
    "07∶17 PM", "3 minutes ago",
    "19∶17", "3 minutes ago",
  },
  {
    "2021-12-07 19:17:58+03:30", "2021-12-07 20:17:57+03:30",
    "07∶17 PM", "59 minutes ago",
    "19∶17", "59 minutes ago",
  },
  {
   "2021-12-07 23:59:04+03:30", "2021-12-08 00:59:02+03:30",
   "Tue", "59 minutes ago",
   "Tue",  "59 minutes ago",
  },
  {
   "2021-12-07 19:17:58+03:30", "2021-12-07 20:17:58+03:30",
   "07∶17 PM", "Today 07∶17 PM",
   "19∶17", "Today 19∶17",
  },
  {
    "2021-12-07 19:17:58+03:30", "2021-12-07 23:59:59+03:30",
    "07∶17 PM", "Today 07∶17 PM",
    "19∶17", "Today 19∶17",
  },
  {
    "2021-12-07 19:17:58+03:30", "2021-12-08 00:00:00+03:30",
    "Tue", "Yesterday 07∶17 PM",
    "Tue", "Yesterday 19∶17",
  },
  {
    "2021-12-07 19:17:58+03:30", "2021-12-08 23:59:59+03:30",
    "Tue", "Yesterday 07∶17 PM",
    "Tue", "Yesterday 19∶17",
  },
  {
    "2021-12-07 19:17:58+03:30", "2021-12-09 00:00:00+03:30",
    "Tue", "Tuesday",
    "Tue", "Tuesday",
  },
  {
    "2021-12-07 19:17:58+03:30", "2021-12-13 23:59:59+03:30",
    "Tue", "Tuesday",
    "Tue", "Tuesday",
  },
  {
    "2021-12-07 19:17:58+03:30", "2021-12-14 00:00:00+03:30",
    "2021-12-07", "2021-12-07",
    "2021-12-07", "2021-12-07",
  },
  {
    "2021-12-07 10:17:58Z", "2021-12-07 19:17:58+05:30",
    "03∶47 PM", "Today 03∶47 PM",
    "15∶47", "Today 15∶47",
  },
  {
    "2018-09-10 19:17:58+01:30", "2021-12-07 19:18:57+01:30",
    "2018-09-10", "2018-09-10",
    "2018-09-10", "2018-09-10",
  },
  {
    "2021-09-10 23:59:20+01:30", "2021-09-11 00:00:18+01:30",
    "Fri", "Just Now",
    "Fri", "Just Now",
  },
  { /* System date wrong */
   "2021-09-10 23:59:20+01:30", "2021-09-10 23:59:00+01:30",
   "2021-09-10", "2021-09-10",
   "2021-09-10", "2021-09-10",
  },
  { /* System date wrong */
   "2021-09-10 23:59:20+01:30", "1970-01-01 00:00:00Z",
   "2021-09-10", "2021-09-10",
   "2021-09-10", "2021-09-10",
  },
};

static void
test_clock_human_time (void)
{
  ChattyClock *clock;

  clock = chatty_clock_get_default ();
  g_assert (CHATTY_IS_CLOCK (clock));

  for (guint i = 0; i < G_N_ELEMENTS (array); i++) {
    g_autoptr(GDateTime) time = NULL;
    g_autoptr(GDateTime) now = NULL;
    g_autoptr(GDateTime) time_local = NULL;
    char *str;

    now = g_date_time_new_from_iso8601 (array[i].time_now, NULL);
    time = g_date_time_new_from_iso8601 (array[i].time_in, NULL);

    time_local = g_date_time_to_timezone (time, g_date_time_get_timezone (now));

    str = clock_get_human_time (clock, now, time_local, G_DESKTOP_CLOCK_FORMAT_12H, FALSE);
    g_assert_cmpstr (str, ==, array[i].human_time);
    g_free (str);

    str = clock_get_human_time (clock, now, time_local, G_DESKTOP_CLOCK_FORMAT_12H, TRUE);
    g_assert_cmpstr (str, ==, array[i].human_time_detailed);
    g_free (str);

    str = clock_get_human_time (clock, now, time_local, G_DESKTOP_CLOCK_FORMAT_24H, FALSE);
    g_assert_cmpstr (str, ==, array[i].human_time24);
    g_free (str);

    str = clock_get_human_time (clock, now, time_local, G_DESKTOP_CLOCK_FORMAT_24H, TRUE);
    g_assert_cmpstr (str, ==, array[i].human_time24_detailed);
    g_free (str);
  }

  g_assert_finalize_object (clock);
}

static void
test_clock_new (void)
{
  ChattyClock *clock;

  clock = chatty_clock_get_default ();
  g_assert (CHATTY_IS_CLOCK (clock));
  g_assert_finalize_object (clock);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  chatty_log_init ();

  g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);

  g_test_add_func ("/clock/new", test_clock_new);
  g_test_add_func ("/clock/human-time", test_clock_human_time);

  return g_test_run ();
}
