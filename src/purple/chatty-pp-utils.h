/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <purple.h>

#include "chatty-enums.h"

PurpleBlistNode     *chatty_pp_utils_get_conv_blist_node   (PurpleConversation *conv);
ChattyMsgDirection   chatty_pp_utils_direction_from_flag   (PurpleMessageFlags flag);
