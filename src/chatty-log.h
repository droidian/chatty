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

#pragma once

#ifndef CHATTY_LOG_LEVEL_TRACE
# define CHATTY_LOG_LEVEL_TRACE ((GLogLevelFlags)(1 << G_LOG_LEVEL_USER_SHIFT))
#endif

/* XXX: Should we use the semi-private g_log_structured_standard() API? */
#define CHATTY_TRACE_MSG(fmt, ...)                              \
  g_log_structured (G_LOG_DOMAIN, CHATTY_LOG_LEVEL_TRACE,       \
                    "MESSAGE", "%s():%d: " fmt,                 \
                    G_STRFUNC, __LINE__, ##__VA_ARGS__)
#define CHATTY_DEBUG_MSG(fmt, ...)                              \
  g_log_structured (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,            \
                    "MESSAGE", "%s():%d: " fmt,                 \
                    G_STRFUNC, __LINE__, ##__VA_ARGS__)
#define CHATTY_PROBE                                            \
  g_log_strucutured (G_LOG_DOMAIN, CHATTY_LOG_LEVEL_TRACE,      \
                     "MESSAGE", "PROBE: %s():%d",               \
                     G_STRFUNC, __LINE__)
#define CHATTY_TODO(_msg)                                       \
  g_log_structured (G_LOG_DOMAIN, CHATTY_LOG_LEVEL_TRACE,       \
                    "MESSAGE", " TODO: %s():%d: %s",            \
                    G_STRFUNC, __LINE__, _msg)
#define CHATTY_ENTRY                                            \
  g_log_structured (G_LOG_DOMAIN, CHATTY_LOG_LEVEL_TRACE,       \
                    "MESSAGE", "ENTRY: %s():%d",                \
                    G_STRFUNC, __LINE__)
#define CHATTY_EXIT                                             \
  G_STMT_START {                                                \
    g_log_structured (G_LOG_DOMAIN, CHATTY_LOG_LEVEL_TRACE,     \
                      "MESSAGE", " EXIT: %s():%d",              \
                      G_STRFUNC, __LINE__);                     \
    return;                                                     \
  } G_STMT_END
#define CHATTY_GOTO(_l)                                         \
  G_STMT_START {                                                \
    g_log_structured (G_LOG_DOMAIN, CHATTY_LOG_LEVEL_TRACE,     \
                      "MESSAGE", " GOTO: %s():%d ("#_l ")",     \
                      G_STRFUNC, __LINE__);                     \
    goto _l;                                                    \
  } G_STMT_END
#define CHATTY_RETURN(_r)                                       \
  G_STMT_START {                                                \
    g_log_structured (G_LOG_DOMAIN, CHATTY_LOG_LEVEL_TRACE,     \
                      "MESSAGE", " EXIT: %s():%d ",             \
                      G_STRFUNC, __LINE__);                     \
    return _r;                                                  \
  } G_STMT_END

void chatty_log_init               (void);
void chatty_log_increase_verbosity (void);
int  chatty_log_get_verbosity      (void);
