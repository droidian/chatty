/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-mm-account-private.h
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
#include <libmm-glib/libmm-glib.h>

#include "chatty-mm-account.h"

#define CHATTY_TYPE_MM_DEVICE (chatty_mm_device_get_type ())
G_DECLARE_FINAL_TYPE (ChattyMmDevice, chatty_mm_device, CHATTY, MM_DEVICE, GObject)

GListModel *chatty_mm_account_get_devices (ChattyMmAccount *self);
MMObject   *chatty_mm_device_get_object   (ChattyMmDevice  *device);
char       *chatty_mm_device_get_number   (ChattyMmDevice  *device);
