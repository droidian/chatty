/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-phone-utils.h
 *
 * Copyright (C) 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

gboolean     chatty_phone_utils_is_valid      (const char *number,
                                               const char *country_code);
gboolean     chatty_phone_utils_is_possible   (const char *number,
                                               const char *country_code);
G_END_DECLS

