/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#ifndef __NOTIFY_H_INCLUDE__
#define __NOTIFY_H_INCLUDE__


typedef enum {
  CHATTY_NOTIFY_MESSAGE_RECEIVED,
  CHATTY_NOTIFY_MESSAGE_ERROR,
  CHATTY_NOTIFY_ACCOUNT_GENERIC,
  CHATTY_NOTIFY_ACCOUNT_CONNECTED,
  CHATTY_NOTIFY_ACCOUNT_ERROR
} ChattyNotifyType;


void chatty_notify_show_notification (const char         *title,
                                      const char         *message,
                                      guint               notification_type,
                                      PurpleConversation *conv,
                                      GdkPixbuf          *pixbuf);

#endif
