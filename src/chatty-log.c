/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-log.c
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>

#include "chatty-log.h"

#define DEFAULT_DOMAIN_PREFIX "chatty"

char *domains;
static int verbosity;
gboolean any_domain;
gboolean no_anonymize;
gboolean stderr_is_journal;

static gboolean
should_log (const char     *log_domain,
            GLogLevelFlags  log_level)
{
  /* Ignore custom flags set */
  log_level = log_level & ~CHATTY_LOG_DETAILED;

  /* Don't skip serious logs */
  if (log_level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_WARNING))
    return TRUE;

  if (any_domain && domains) {
    /* If domain is “all” show logs upto debug regardless of the verbosity */
    if (log_level & ~CHATTY_LOG_LEVEL_TRACE)
      return TRUE;

    /* If the log is trace level, log if verbosity >= 4 */
    return verbosity >= 4;
  }

  if (!any_domain && domains && log_domain && strstr (domains, log_domain))
    return TRUE;

  switch ((int)log_level)
    {
    case G_LOG_LEVEL_MESSAGE:
      if (verbosity < 1)
        return FALSE;
      break;

    case G_LOG_LEVEL_INFO:
      if (verbosity < 2)
        return FALSE;
      break;

    case G_LOG_LEVEL_DEBUG:
      if (verbosity < 3)
        return FALSE;
      break;

    case CHATTY_LOG_LEVEL_TRACE:
      if (verbosity < 4)
        return FALSE;
      break;

    default:
      break;
    }

  if (!log_domain)
    log_domain = "**";

  /* Skip logs from other domains if verbosity level is low */
  if (any_domain && !domains &&
      verbosity < 5 &&
      log_level > G_LOG_LEVEL_MESSAGE &&
      !strstr (log_domain, DEFAULT_DOMAIN_PREFIX))
    return FALSE;

  /* GdkPixbuf logs are too much verbose, skip unless asked not to. */
  if (log_level >= G_LOG_LEVEL_MESSAGE &&
      verbosity < 7 &&
      g_strcmp0 (log_domain, "GdkPixbuf") == 0 &&
      (!domains || !strstr (domains, log_domain)))
    return FALSE;

  return TRUE;
}

static void
log_str_append_log_domain (GString    *log_str,
                           const char *log_domain,
                           gboolean    color)
{
  static const char *colors[] = {
    "\033[1;32m",
    "\033[1;33m",
    "\033[1;35m",
    "\033[1;36m",
    "\033[1;91m",
    "\033[1;92m",
    "\033[1;93m",
    "\033[1;94m",
    "\033[1;95m",
    "\033[1;96m",
  };
  guint i;

  g_assert (log_domain && *log_domain);

  i = g_str_hash (log_domain) % G_N_ELEMENTS (colors);

  if (color)
    g_string_append (log_str, colors[i]);
  g_string_append_printf (log_str, "%20s", log_domain);

  if (color)
    g_string_append (log_str, "\033[0m");
}

static const char *
get_log_level_prefix (GLogLevelFlags log_level,
                      gboolean       use_color)
{
  /* Ignore custom flags set */
  log_level = log_level & ~CHATTY_LOG_DETAILED;

  if (use_color)
    {
      switch ((int)log_level)        /* Same colors as used in GLib */
        {
        case G_LOG_LEVEL_ERROR:       return "   \033[1;31mERROR\033[0m";
        case G_LOG_LEVEL_CRITICAL:    return "\033[1;35mCRITICAL\033[0m";
        case G_LOG_LEVEL_WARNING:     return " \033[1;33mWARNING\033[0m";
        case G_LOG_LEVEL_MESSAGE:     return " \033[1;32mMESSAGE\033[0m";
        case G_LOG_LEVEL_INFO:        return "    \033[1;32mINFO\033[0m";
        case G_LOG_LEVEL_DEBUG:       return "   \033[1;32mDEBUG\033[0m";
        case CHATTY_LOG_LEVEL_TRACE:  return "   \033[1;36mTRACE\033[0m";
        default:                      return " UNKNOWN";
        }
    }
  else
    {
      switch ((int)log_level)
        {
        case G_LOG_LEVEL_ERROR:      return "   ERROR";
        case G_LOG_LEVEL_CRITICAL:   return "CRITICAL";
        case G_LOG_LEVEL_WARNING:    return " WARNING";
        case G_LOG_LEVEL_MESSAGE:    return " MESSAGE";
        case G_LOG_LEVEL_INFO:       return "    INFO";
        case G_LOG_LEVEL_DEBUG:      return "   DEBUG";
        case CHATTY_LOG_LEVEL_TRACE: return "   TRACE";
        default:                     return " UNKNOWN";
        }
    }
}

static GLogWriterOutput
chatty_log_write (GLogLevelFlags   log_level,
                  const char      *log_domain,
                  const char      *log_message,
                  const GLogField *fields,
                  gsize            n_fields,
                  gpointer         user_data)
{
  g_autoptr(GString) log_str = NULL;
  FILE *stream;
  gboolean can_color;

  if (stderr_is_journal)
    if (g_log_writer_journald (log_level, fields, n_fields, user_data) == G_LOG_WRITER_HANDLED)
      return G_LOG_WRITER_HANDLED;

  if (log_level & (G_LOG_LEVEL_ERROR |
                   G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING))
    stream = stderr;
  else
    stream = stdout;

  log_str = g_string_new (NULL);

  /* Add local time */
  {
    char buffer[32];
    struct tm tm_now;
    time_t sec_now;
    gint64 now;

    now = g_get_real_time ();
    sec_now = now / G_USEC_PER_SEC;
    tm_now = *localtime (&sec_now);
    strftime (buffer, sizeof (buffer), "%H:%M:%S", &tm_now);

    g_string_append_printf (log_str, "%s.%04d ", buffer,
                            (int)((now % G_USEC_PER_SEC) / 100));
  }

  can_color = g_log_writer_supports_color (fileno (stream));
  
  log_str_append_log_domain (log_str, log_domain, can_color);
  g_string_append_printf (log_str, "[%5d]:", getpid ());

  g_string_append_printf (log_str, "%s: ", get_log_level_prefix (log_level, can_color));

  if (log_level & CHATTY_LOG_DETAILED)
    {
      const char *code_func = NULL, *code_line = NULL;
      for (guint i = 0; i < n_fields; i++)
        {
          const GLogField *field = &fields[i];

          if (!code_func && g_strcmp0 (field->key, "CODE_FUNC") == 0)
            code_func = field->value;
          else if (!code_line && g_strcmp0 (field->key, "CODE_LINE") == 0)
            code_line = field->value;

          if (code_func && code_line)
            break;
        }

      if (code_func)
        {
          g_string_append_printf (log_str, "%s():", code_func);

          if (code_line)
            g_string_append_printf (log_str, "%s:", code_line);
          g_string_append_c (log_str, ' ');
        }
    }

  g_string_append (log_str, log_message);

  fprintf (stream, "%s\n", log_str->str);
  fflush (stream);

  return G_LOG_WRITER_HANDLED;
}

static GLogWriterOutput
chatty_log_handler (GLogLevelFlags   log_level,
                    const GLogField *fields,
                    gsize            n_fields,
                    gpointer         user_data)
{
  const char *log_domain = NULL;
  const char *log_message = NULL;

  for (guint i = 0; (!log_domain || !log_message) && i < n_fields; i++)
    {
      const GLogField *field = &fields[i];

      if (g_strcmp0 (field->key, "GLIB_DOMAIN") == 0)
        log_domain = field->value;
      else if (g_strcmp0 (field->key, "MESSAGE") == 0)
        log_message = field->value;
    }

  if (!should_log (log_domain, log_level))
    return G_LOG_WRITER_HANDLED;

  if (!log_domain)
    log_domain = "**";

  if (!log_message)
    log_message = "(NULL) message";

  if (any_domain)
    return chatty_log_write (log_level, log_domain, log_message,
                             fields, n_fields, user_data);

  if (!log_domain || strstr (domains, log_domain))
    return chatty_log_write (log_level, log_domain, log_message,
                             fields, n_fields, user_data);

  return G_LOG_WRITER_HANDLED;
}

static void
chatty_log_finalize (void)
{
  g_clear_pointer (&domains, g_free);
}

void
chatty_log_init (void)
{
  static gsize initialized = 0;

  if (g_once_init_enter (&initialized))
    {
      domains = g_strdup (g_getenv ("G_MESSAGES_DEBUG"));

      if (domains && !*domains)
        g_clear_pointer (&domains, g_free);

      if (!domains || g_str_equal (domains, "all"))
        any_domain = TRUE;

      if (domains && g_str_equal (domains, "no-anonymize"))
        {
          any_domain = TRUE;
          no_anonymize = TRUE;
          g_clear_pointer (&domains, g_free);
        }

      stderr_is_journal = g_log_writer_is_journald (fileno (stderr));
      g_log_set_writer_func (chatty_log_handler, NULL, NULL);
      g_once_init_leave (&initialized, 1);
      atexit (chatty_log_finalize);
    }
}

void
chatty_log_increase_verbosity (void)
{
  verbosity++;
}

int
chatty_log_get_verbosity (void)
{
  return verbosity;
}

const char *
chatty_log_bool_str (gboolean value,
                     gboolean use_success)
{
  if (!g_log_writer_supports_color (fileno (stdout)) ||
      stderr_is_journal)
    {
      if (value)
        return use_success ? "succeeded" : "true";
      else
        return use_success ? "failed" : "false";
    }

  if (value)
    {
      if (use_success)
        return "\033[1;32m" "succeeded" "\033[0m";
      else
        return "\033[1;32m" "true" "\033[0m";
    }
  else
    {
      if (use_success)
        return "\033[1;31m" "failed" "\033[0m";
      else
        return "\033[1;31m" "false" "\033[0m";
    }
}

void
chatty_log (const char     *domain,
            GLogLevelFlags  log_level,
            const char     *value,
            const char     *file,
            const char     *line,
            const char     *func,
            const char     *message_format,
            ...)
{
  g_autoptr(GString) str = NULL;
  va_list args;

  if (!message_format || !*message_format)
    return;

  if (!should_log (domain, log_level))
    return;

  str = g_string_new (NULL);
  va_start (args, message_format);
  g_string_append_vprintf (str, message_format, args);
  va_end (args);

  chatty_log_anonymize_value (str, value);
  g_log_structured (domain, log_level,
                    "CODE_FILE", file,
                    "CODE_LINE", line,
                    "CODE_FUNC", func,
                    "MESSAGE", "%s", str->str);
}

void
chatty_log_anonymize_value (GString    *str,
                            const char *value)
{
  if (!value || !*value)
    return;

  g_assert (str);

  if (str->len && str->str[str->len - 1] != ' ')
    g_string_append_c (str, ' ');

  if (no_anonymize)
    {
      g_string_append (str, value);
      return;
    }

  if (!isalnum (*value))
    g_string_append_c (str, *value++);

  if (*value)
    g_string_append_c (str, *value++);

  if (*value)
    g_string_append_c (str, *value++);

  if (!*value)
    return;

  /* Replace all but the last two alnum chars with 'x' */
  while (*value)
    {
      while (isalnum (*value) && value[1] && value[2])
        {
          if (value[1] == ':' || value[1] == '@' || value[1] == ' ' ||
              value[-1] == ':' || value[-1] == '@' || value[-1] == ' ')
            g_string_append_c (str, *value);
          else
            g_string_append_c (str, '#');
          value++;
        }
      g_string_append_c (str, *value++);
    }
}
