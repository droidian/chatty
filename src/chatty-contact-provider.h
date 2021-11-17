/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-contact-provider.h
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>
#include <gdk/gdk.h>

#include "chatty-contact.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_EDS (chatty_eds_get_type ())

G_DECLARE_FINAL_TYPE (ChattyEds, chatty_eds, CHATTY, EDS, GObject)

ChattyEds     *chatty_eds_new            (ChattyProtocol protocols);
gboolean       chatty_eds_is_ready       (ChattyEds  *self);
GListModel    *chatty_eds_get_model      (ChattyEds  *self);
ChattyContact *chatty_eds_find_by_number (ChattyEds  *self,
                                          const char *phone_number);
void           chatty_eds_open_contacts_app        (ChattyEds            *self,
                                                    GCancellable         *cancellable,
                                                    GAsyncReadyCallback   callback,
                                                    gpointer              user_data);
gboolean       chatty_eds_open_contacts_app_finish (ChattyEds            *self,
                                                    GAsyncResult         *result,
                                                    GError              **error);
void           chatty_eds_write_contact_async      (const char           *name,
                                                    const char           *phone_number,
                                                    GAsyncReadyCallback   callback,
                                                    gpointer              user_data);
gboolean       chatty_eds_write_contact_finish     (GAsyncResult         *result,
                                                    GError              **error);

G_END_DECLS
