/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-clock.c
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-clock"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>
#include <gdesktop-enums.h>

#include "chatty-settings.h"
#include "chatty-account.h"
#include "chatty-clock.h"
#include "chatty-log.h"

/**
 * SECTION: chatty-clock
 * @title: ChattyClock
 * @short_description:
 * @include: "chatty-clock.h"
 *
 * chatty-clock keeps track of clock time and emits appropriate signals on regular
 * intervals.  The clock can be stopped when not required (eg: when window is not
 * in focus), which can reduce CPU when not required.
 *
 * This can be used to run actions in relation with clock time.
 */

#define EPSILON         2000

struct _ChattyClock
{
  GObject       parent_instance;

  GSettings    *settings;
  GDesktopClockFormat clock_format;
  gint64        last_time;
  guint         sync_timeout_id;
  guint         timeout_id;
};

G_DEFINE_TYPE (ChattyClock, chatty_clock, G_TYPE_OBJECT)

enum {
  CHANGED,
  MINUTE_CHANGED,
  HOUR_CHANGED,
  DAY_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static char *
clock_get_human_time (ChattyClock         *self,
                      GDateTime           *now,
                      GDateTime           *time,
                      GDesktopClockFormat  clock_format,
                      gboolean             detailed)
{
  GTimeSpan time_span;
  gint day, day_now;

  g_assert (CHATTY_IS_CLOCK (self));
  g_assert (now);
  g_assert (time);

  day = g_date_time_get_day_of_year (time);
  day_now = g_date_time_get_day_of_year (now);
  time_span = g_date_time_difference (now, time);

  /* Allow an error offset of 5 seconds */
  if (time_span >= - 5 * G_TIME_SPAN_SECOND &&
      time_span < G_TIME_SPAN_MINUTE) {
    if (detailed)
      return g_strdup (_("Just Now"));

    if (day == day_now) {
      if (clock_format == G_DESKTOP_CLOCK_FORMAT_24H)
        return g_date_time_format (time, "%H∶%M");
      else
        /* TRANSLATORS: Timestamp with 12 hour time, e.g. “06∶42 PM”.
         * See https://docs.gtk.org/glib/method.DateTime.format.html
         */
        return g_date_time_format (time, _("%I∶%M %p"));
    }
  }

  if (time_span < 0)
    goto end;

  if (detailed && time_span < G_TIME_SPAN_HOUR)
    return g_strdup_printf (g_dngettext (GETTEXT_PACKAGE,
                                         "%lu minute ago", "%lu minutes ago",
                                         time_span / G_TIME_SPAN_MINUTE),
                            time_span / G_TIME_SPAN_MINUTE);
  /* The day can be same, but from two different years, so check the time span too */
  if (time_span <= G_TIME_SPAN_DAY &&
      day == day_now) {
    if (clock_format == G_DESKTOP_CLOCK_FORMAT_24H) {
      if (detailed)
        return g_date_time_format (time, _("Today %H∶%M"));

      return g_date_time_format (time, "%H∶%M");
    } else {
      if (detailed)
        /* TRANSLATORS: Timestamp with 12 hour time, e.g. “Today 06∶42 PM”.
         * See https://docs.gtk.org/glib/method.DateTime.format.html
         */
        return g_date_time_format (time, _("Today %I∶%M %p"));

      /* TRANSLATORS: Timestamp with 12 hour time, e.g. “06∶42 PM”.
       * See https://docs.gtk.org/glib/method.DateTime.format.html
       */
      return g_date_time_format (time, _("%I∶%M %p"));
    }
  }

  if (time_span <= G_TIME_SPAN_DAY * 2 &&
      detailed && day_now - day == 1) {
    if (clock_format == G_DESKTOP_CLOCK_FORMAT_24H)
      return g_date_time_format (time, _("Yesterday %H∶%M"));

    /* TRANSLATORS: Timestamp with 12 hour time, e.g. “Yesterday 06∶42 PM”.
     * See https://docs.gtk.org/glib/method.DateTime.format.html
     */
    return g_date_time_format (time, _("Yesterday %I∶%M %p"));
  }

  if (time_span <= G_TIME_SPAN_DAY * 7 &&
      (day_now - day) < 7) {
    if (detailed)
      return g_date_time_format (time, "%A");

    return g_date_time_format (time, "%a");
  }

 end:
  /* TRANSLATORS: Timestamp from more than 7 days ago or future date
   * (eg: when the system time is wrong), e.g. “2022-01-01”.
   * See https://docs.gtk.org/glib/method.DateTime.format.html
   */
  return g_date_time_format (time, _("%Y-%m-%d"));
}

static void
clock_update_time (ChattyClock *self)
{
  gint64 old, now;

  old = self->last_time;
  now = self->last_time = g_get_real_time ();

  g_signal_emit (self, signals[CHANGED], 0);

  if (ABS (now - old) > G_TIME_SPAN_MINUTE - EPSILON)
    g_signal_emit (self, signals[MINUTE_CHANGED], 0);

  if (ABS (now - old) > G_TIME_SPAN_HOUR - EPSILON)
    g_signal_emit (self, signals[HOUR_CHANGED], 0);

  if (ABS (now - old) > G_TIME_SPAN_DAY - EPSILON)
    g_signal_emit (self, signals[DAY_CHANGED], 0);
}

static void
clock_format_changed_cb (ChattyClock *self)
{
  g_assert (CHATTY_IS_CLOCK (self));

  g_signal_emit (self, signals[CHANGED], 0);
  g_signal_emit (self, signals[MINUTE_CHANGED], 0);
  g_signal_emit (self, signals[HOUR_CHANGED], 0);
  g_signal_emit (self, signals[DAY_CHANGED], 0);
}

static gboolean
chatty_clock_changed_cb (gpointer user_data)
{
  ChattyClock *self = user_data;

  clock_update_time (self);

  /* if not in sync (time diff >= 500ms from the nearest second), re-sync. */
  if (self->last_time / G_TIME_SPAN_MILLISECOND % 1000 >= 500) {
    chatty_clock_stop (self);
    chatty_clock_start (self);

    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

static gboolean
chatty_schedule_timer (gpointer user_data)
{
  ChattyClock *self = user_data;

  chatty_clock_stop (self);

  self->timeout_id = g_timeout_add (5 * G_TIME_SPAN_MILLISECOND,
                                    chatty_clock_changed_cb, self);
  chatty_clock_changed_cb (self);

  return G_SOURCE_REMOVE;
}

static void
sync_time_source (ChattyClock *self)
{
  gint64 now_s, timeout;

  g_log (G_LOG_DOMAIN, CHATTY_LOG_LEVEL_TRACE, "sync");

  chatty_clock_stop (self);
  clock_update_time (self);

  now_s = self->last_time / G_TIME_SPAN_SECOND;

  /* milliseconds required to reach next nth second that's properly divisible by 5 */
  timeout = (5 - now_s % 5) * G_TIME_SPAN_MILLISECOND - (self->last_time / G_TIME_SPAN_MILLISECOND) % 1000;

  self->sync_timeout_id = g_timeout_add (MAX (timeout, 1), chatty_schedule_timer, self);
}

static void
chatty_clock_finalize (GObject *object)
{
  ChattyClock *self = (ChattyClock *)object;

  g_clear_handle_id (&self->sync_timeout_id, g_source_remove);
  g_clear_handle_id (&self->timeout_id, g_source_remove);
  g_clear_object (&self->settings);

  G_OBJECT_CLASS (chatty_clock_parent_class)->finalize (object);
}

static void
chatty_clock_class_init (ChattyClockClass *klass)
{
  GObjectClass *object_class  = G_OBJECT_CLASS (klass);

  object_class->finalize = chatty_clock_finalize;

  /**
   * ChattyUser::changed:
   * @self: a #ChattyClock
   *
   * Emitted for every tick
   */
  signals [CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * ChattyUser::minute-changed:
   * @self: a #ChattyClock
   *
   * Emitted when minute is changed
   */
  signals [MINUTE_CHANGED] =
    g_signal_new ("minute-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * ChattyUser::hour-changed:
   * @self: a #ChattyClock
   *
   * Emitted when hour is changed
   */
  signals [HOUR_CHANGED] =
    g_signal_new ("hour-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * ChattyUser::day-changed:
   * @self: a #ChattyClock
   *
   * Emitted when day is changed
   */
  signals [DAY_CHANGED] =
    g_signal_new ("day-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}


static void
chatty_clock_init (ChattyClock *self)
{
  self->settings = g_settings_new ("org.gnome.desktop.interface");
  self->clock_format = g_settings_get_enum (self->settings, "clock-format");

  g_signal_connect_object (self->settings, "changed::clock-format",
                           G_CALLBACK (clock_format_changed_cb), self,
                           G_CONNECT_SWAPPED | G_CONNECT_AFTER);
  chatty_clock_start (self);
}

ChattyClock *
chatty_clock_get_default (void)
{
  static ChattyClock *self;

  if (!self)
    g_set_weak_pointer (&self, g_object_new (CHATTY_TYPE_CLOCK, NULL));

  return self;
}

char *
chatty_clock_get_human_time (ChattyClock *self,
                             time_t       unix_time,
                             gboolean     detailed)
{
  g_autoptr(GDateTime) now = NULL;
  g_autoptr(GDateTime) local = NULL;

  g_return_val_if_fail (CHATTY_IS_CLOCK (self), g_strdup (""));
  g_return_val_if_fail (unix_time >= 0, g_strdup (""));

  now = g_date_time_new_now_local ();
  local = g_date_time_new_from_unix_local (unix_time);

  return clock_get_human_time (self, now, local, self->clock_format, detailed);
}

void
chatty_clock_start (ChattyClock *self)
{
  g_return_if_fail (CHATTY_IS_CLOCK (self));

  if (!self->sync_timeout_id && !self->timeout_id)
    sync_time_source (self);
}

void
chatty_clock_stop (ChattyClock *self)
{
  g_return_if_fail (CHATTY_IS_CLOCK (self));

  g_clear_handle_id (&self->sync_timeout_id, g_source_remove);
  g_clear_handle_id (&self->timeout_id, g_source_remove);
}
