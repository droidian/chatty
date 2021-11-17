/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-media.c
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

#include <math.h>
#include <glib-object.h>
#include "chatty-utils.h"

G_BEGIN_DECLS

ChattyFileInfo *chatty_media_scale_image_to_size_sync   (ChattyFileInfo *input_file,
                                                         gsize           desired_size,
                                                         gboolean        use_temp_file);
void            chatty_media_scale_image_to_size_async  (ChattyFileInfo *input_file,
                                                         gsize           desired_size,
                                                         gboolean        use_temp_file,
                                                         GCancellable   *cancellable,
                                                         GAsyncReadyCallback callback,
                                                         gpointer        user_data);
ChattyFileInfo *chatty_media_scale_image_to_size_finish (GAsyncResult   *result,
                                                         GError        **error);

G_END_DECLS
