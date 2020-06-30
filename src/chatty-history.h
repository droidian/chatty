/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#ifndef __HISTORY_H_INCLUDE__
#define __HISTORY_H_INCLUDE__

#include <glib.h>
#include <purple.h>
#include <time.h>

/* #include "chatty-conversation.h" */

typedef struct chatty_log ChattyLog;

struct chatty_log {
  time_t   epoch;  // TODO: @LELAND: Once log-parsing functions are cleaned, review this
  char    *uid;
  char    *msg;
  int      dir;
};

//TODO:LELAND: Document methods!

int chatty_history_open (const char *dir,
                         const char *file_name);

void chatty_history_close (void);

void chatty_history_add_chat_message (const char *stanza,
                                      int         direction,
                                      const char *account,
                                      const char *who,
                                      const char *uid,
                                      time_t      m_time,
                                      const char *room);

void chatty_history_add_im_message (const char *stanza,
                                    int         direction,
                                    const char *account,
                                    const char *who,
                                    const char *uid,
                                    time_t      m_time);

/**
 * Returns the timestamp (time_t) for the message stored under specified ID
 * for the given account.
 *
 * @param uuid    zero terminated string containing message ID
 * @param account zero terminated string containing account name
 *
 * @return  the int representing timestamp (time_t) of the stored message or
 *          INT_MAX if message not found.
 */
int get_im_timestamp_for_uuid(const char *uuid, const char *account);
int get_chat_timestamp_for_uuid(const char *uuid, const char *room);

void chatty_history_get_im_messages (const char* account,
                                     const char* who,
                                     void (*cb)(const unsigned char *msg,
                                                int                  direction,
                                                time_t               time_stamp,
                                                const unsigned char *uuid,
                                                gpointer            data,
                                                int                 last_message),
                                     gpointer   data,
                                     guint      limit,
                                     const char *oldest_message_displayed);

void
chatty_history_get_chat_messages (const char *account,
                                  const char *room,
                                  void (*cb)(const unsigned char *msg,
                                            int                  direction,
                                            int                  time_stamp,
                                            const char           *room,
                                            const unsigned char  *who,
                                            const unsigned char  *uuid,
                                            gpointer             data),
                                  gpointer  data,
                                  guint      limit,
                                  const char *oldest_message_displayed);

int
chatty_history_get_chat_last_message_time (const char* account,
                                           const char* room);


void
chatty_history_delete_chat (const char* account,
                            const char* room);

void
chatty_history_delete_im (const char *account,
                          const char *who);

char
chatty_history_get_im_last_message (const char*       account,
                                    const char*       who,
                                    ChattyLog*  chatty_log);

/**
 * Adds history message to persistent storage, acts as a default handler
 * for "conversation_write" signal.
 *
 * @param pa    PurpleAccount under which to store message/event
 * @param pcm   pointer to PurpleConvMessage object with message payload.
 *              Its fields could be updated by previous handlers and thus
 *              must be dynamic object freed after use (or re-allocated)
 *              The flags field also serves as stop-flag should
 *              PURPLE_MESSAGE_NO_LOG flag be set by previous handlers.
 *              The alias field serves as a room name for group-chats.
 * @param sid   pointer to the (char) string which should carry unique
 *              message ID. If points to NULL - will be dynamically allocated
 *              with random UUID.
 * @param type  Conversation type, if PURPLE_CONV_TYPE_CHAT the message will
 *              be stored in the MUC archive, otherwise it goes to IM store.
 * @param data  not used
 */
void
chatty_history_add_message (PurpleAccount *pa, PurpleConvMessage *pcm,
                            char **sid, PurpleConversationType type,
                            gpointer data);

#endif
