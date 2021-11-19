/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-log.h
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib.h>

#ifndef CHATTY_LOG_LEVEL_TRACE
# define CHATTY_LOG_LEVEL_TRACE ((GLogLevelFlags)(1 << G_LOG_LEVEL_USER_SHIFT))
# define CHATTY_LOG_DETAILED ((GLogLevelFlags)(1 << (G_LOG_LEVEL_USER_SHIFT + 1)))
#endif

#define CHATTY_TRACE_MSG(fmt, ...)                              \
  chatty_log (G_LOG_DOMAIN,                                     \
              CHATTY_LOG_LEVEL_TRACE | CHATTY_LOG_DETAILED,     \
              NULL, __FILE__, G_STRINGIFY (__LINE__),           \
              G_STRFUNC, fmt, ##__VA_ARGS__)
#define CHATTY_TRACE(value, fmt, ...)                           \
  chatty_log (G_LOG_DOMAIN,                                     \
              CHATTY_LOG_LEVEL_TRACE | CHATTY_LOG_DETAILED,     \
              value, __FILE__, G_STRINGIFY (__LINE__),          \
              G_STRFUNC, fmt, ##__VA_ARGS__)
#define CHATTY_DEBUG_MSG(fmt, ...)                              \
  chatty_log (G_LOG_DOMAIN,                                     \
              G_LOG_LEVEL_DEBUG | CHATTY_LOG_DETAILED,          \
              NULL, __FILE__, G_STRINGIFY (__LINE__),           \
              G_STRFUNC, fmt, ##__VA_ARGS__)
#define CHATTY_DEBUG(value, fmt, ...)                           \
  chatty_log (G_LOG_DOMAIN,                                     \
              G_LOG_LEVEL_DEBUG,                                \
              value, __FILE__, G_STRINGIFY (__LINE__),          \
              G_STRFUNC, fmt, ##__VA_ARGS__)
#define CHATTY_DEBUG_DETAILED(value, fmt, ...)                  \
  chatty_log (G_LOG_DOMAIN,                                     \
              G_LOG_LEVEL_DEBUG | CHATTY_LOG_DETAILED,          \
              value, __FILE__, G_STRINGIFY (__LINE__),          \
              G_STRFUNC, fmt, ##__VA_ARGS__)
#define CHATTY_INFO(value, fmt, ...)                            \
  chatty_log (G_LOG_DOMAIN,                                     \
              G_LOG_LEVEL_INFO,                                 \
              value, __FILE__, G_STRINGIFY (__LINE__),          \
              G_STRFUNC, fmt, ##__VA_ARGS__)
#define CHATTY_WARNING(value, fmt, ...)                         \
  chatty_log (G_LOG_DOMAIN,                                     \
              G_LOG_LEVEL_WARNING | CHATTY_LOG_DETAILED,        \
              value, __FILE__, G_STRINGIFY (__LINE__),          \
              G_STRFUNC, fmt, ##__VA_ARGS__)

#define CHATTY_LOG_BOOL(value)                                  \
  chatty_log_bool_str (value, FALSE)
#define CHATTY_LOG_SUCESS(value)                                \
  chatty_log_bool_str (value, TRUE)

void chatty_log_init               (void);
void chatty_log_increase_verbosity (void);
int  chatty_log_get_verbosity      (void);
const char *chatty_log_bool_str    (gboolean value,
                                    gboolean use_success);
void chatty_log                    (const char     *domain,
                                    GLogLevelFlags  log_level,
                                    const char     *value,
                                    const char     *file,
                                    const char     *line,
                                    const char     *func,
                                    const char     *message_format,
                                    ...) G_GNUC_PRINTF (7, 8);
void chatty_log_anonymize_value    (GString        *str,
                                    const char     *value);
