/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-sms-uri.h
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define CHATTY_TYPE_SMS_URI (chatty_sms_uri_get_type ())

G_DECLARE_FINAL_TYPE (ChattySmsUri, chatty_sms_uri, CHATTY, SMS_URI, GObject)

ChattySmsUri *chatty_sms_uri_new             (const char   *uri);
void          chatty_sms_uri_set_uri         (ChattySmsUri *self,
                                              const char   *uri);
gboolean      chatty_sms_uri_is_valid        (ChattySmsUri *self);
gboolean      chatty_sms_uri_can_send        (ChattySmsUri *self);
GPtrArray    *chatty_sms_uri_get_numbers     (ChattySmsUri *self);
const char   *chatty_sms_uri_get_numbers_str (ChattySmsUri *self);
const char   *chatty_sms_uri_get_body        (ChattySmsUri *self);

G_END_DECLS
