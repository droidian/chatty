/* chatty-settings.h
 *
 * Copyright 2019 Purism SPC
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

G_BEGIN_DECLS

#define CHATTY_TYPE_SETTINGS (chatty_settings_get_type ())

G_DECLARE_FINAL_TYPE (ChattySettings, chatty_settings, CHATTY, SETTINGS, GObject)

ChattySettings *chatty_settings_get_default                  (void);
gboolean        chatty_settings_get_first_start              (ChattySettings *self);
gboolean        chatty_settings_get_send_receipts            (ChattySettings *self);
gboolean        chatty_settings_get_message_carbons          (ChattySettings *self);
gboolean        chatty_settings_get_send_typing              (ChattySettings *self);
gboolean        chatty_settings_get_greyout_offline_buddies  (ChattySettings *self);
gboolean        chatty_settings_get_blur_idle_buddies        (ChattySettings *self);
gboolean        chatty_settings_get_indicate_unknown_contacts (ChattySettings *self);
gboolean        chatty_settings_get_convert_emoticons        (ChattySettings *self);
gboolean        chatty_settings_get_return_sends_message     (ChattySettings *self);
gboolean        chatty_settings_get_mam_enabled              (ChattySettings *self);
gboolean        chatty_settings_get_window_maximized         (ChattySettings *self);
void            chatty_settings_set_window_maximized         (ChattySettings *self,
                                                              gboolean        maximized);
void            chatty_settings_get_window_geometry          (ChattySettings *self,
                                                              GdkRectangle   *geometry);
void            chatty_settings_set_window_geometry          (ChattySettings *self,
                                                              GdkRectangle   *geometry);
const char     *chatty_settings_get_country_iso_code         (ChattySettings *self);
void            chatty_settings_set_country_iso_code         (ChattySettings *self,
                                                              const char     *iso_code);

G_END_DECLS
