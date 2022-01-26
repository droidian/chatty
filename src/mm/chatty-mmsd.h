/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-mmsd.c
 *
 * Copyright 2020, 2021 Purism SPC
 *           2021, Chris Talbot
 *
 * Author(s):
 *   Chris Talbot
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib-object.h>
#include <libmm-glib/libmm-glib.h>

#include "chatty-mm-account.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_MMSD (chatty_mmsd_get_type ())

G_DECLARE_FINAL_TYPE (ChattyMmsd, chatty_mmsd, CHATTY, MMSD, GObject)

ChattyMmsd *chatty_mmsd_new                   (ChattyMmAccount *account);
gboolean    chatty_mmsd_is_ready              (ChattyMmsd      *self);
gboolean    chatty_mmsd_send_mms_async        (ChattyMmsd      *self,
                                               ChattyChat      *chat,
                                               ChattyMessage   *message,
                                               gpointer         user_data);
gboolean    chatty_mmsd_get_settings          (ChattyMmsd      *self,
                                               const char     **apn,
                                               const char     **mmsc,
                                               const char     **proxy,
                                               gboolean        *use_smil);
void        chatty_mmsd_set_settings_async    (ChattyMmsd      *self,
                                               const char      *apn,
                                               const char      *mmsc,
                                               const char      *proxy,
                                               gboolean         use_smil,
                                               GCancellable    *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer         user_data);
gboolean    chatty_mmsd_set_settings_finish   (ChattyMmsd      *self,
                                               GAsyncResult    *result,
                                               GError         **error);
void        chatty_mmsd_delete_mms            (ChattyMmsd *self,
                                               const char *uid);

G_END_DECLS
