/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#ifndef __ICON_H_INCLUDE__
#define __ICON_H_INCLUDE__

#include "purple.h"

GdkPixbuf *chatty_icon_pixbuf_from_data (const guchar *buf, gsize count);

GdkPixbuf *chatty_icon_get_buddy_icon (PurpleBlistNode *node);

#endif
