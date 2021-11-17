/* -*- mode: c; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* chatty-pp-account-details.h
 *
 * Copyright 2021 Purism SPC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "chatty-account.h"

G_BEGIN_DECLS

#define CHATTY_TYPE_PP_ACCOUNT_DETAILS (chatty_pp_account_details_get_type ())

G_DECLARE_FINAL_TYPE (ChattyPpAccountDetails, chatty_pp_account_details, CHATTY, PP_ACCOUNT_DETAILS, HdyPreferencesPage)

GtkWidget       *chatty_pp_account_details_new      (void);
void             chatty_pp_account_save_async       (ChattyPpAccountDetails *self,
                                                     GAsyncReadyCallback     callback,
                                                     gpointer                user_data);
gboolean         chatty_pp_account_save_finish      (ChattyPpAccountDetails *self,
                                                     GAsyncResult           *result,
                                                     GError                **error);
ChattyAccount   *chatty_pp_account_details_get_item (ChattyPpAccountDetails *self);
void             chatty_pp_account_details_set_item (ChattyPpAccountDetails *self,
                                                     ChattyAccount          *account);

G_END_DECLS
