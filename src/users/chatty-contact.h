/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-contact.h
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
#include <libebook-contacts/libebook-contacts.h>

#include "chatty-item.h"
#include "chatty-enums.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_CONTACT (chatty_contact_get_type ())

G_DECLARE_FINAL_TYPE (ChattyContact, chatty_contact, CHATTY, CONTACT, ChattyItem)

ChattyContact     *chatty_contact_new                   (EContact          *contact,
                                                         EVCardAttribute   *attr,
                                                         ChattyProtocol     protocol);
ChattyContact     *chatty_contact_dummy_new             (const char        *name,
                                                         const char        *value);
void               chatty_contact_set_name              (ChattyContact     *self,
                                                         const char        *name);
void               chatty_contact_set_value             (ChattyContact     *self,
                                                         const char        *value);
const char        *chatty_contact_get_value_type        (ChattyContact     *self);
const char        *chatty_contact_get_uid               (ChattyContact     *self);
gboolean           chatty_contact_is_exact_match        (ChattyContact     *self,
                                                         const char        *value,
                                                         ChattyProtocol     protocols);
gboolean           chatty_contact_is_dummy              (ChattyContact     *self);

G_END_DECLS
