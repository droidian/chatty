/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-mm-buddy.h
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

#include "chatty-contact.h"
#include "chatty-item.h"
#include "chatty-enums.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_MM_BUDDY (chatty_mm_buddy_get_type ())

G_DECLARE_FINAL_TYPE (ChattyMmBuddy, chatty_mm_buddy, CHATTY, MM_BUDDY, ChattyItem)

ChattyMmBuddy   *chatty_mm_buddy_new           (const char    *phone_number,
                                                const char    *name);
const char      *chatty_mm_buddy_get_number    (ChattyMmBuddy *self);
ChattyContact   *chatty_mm_buddy_get_contact   (ChattyMmBuddy *self);
void             chatty_mm_buddy_set_contact   (ChattyMmBuddy *self,
                                                ChattyContact *contact);

G_END_DECLS
