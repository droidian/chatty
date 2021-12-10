/* -*- mode: c; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* chatty-settings-dialog.c
 *
 * Copyright 2020 Purism SPC
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
 *   Andrea Schäfer <mosibasu@me.com>
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-settings-dialog"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>

#include "chatty-utils.h"
#include "matrix-utils.h"
#include "chatty-ma-account.h"
#include "chatty-mm-account.h"
#include "chatty-manager.h"
#include "chatty-purple.h"
#include "chatty-fp-row.h"
#include "chatty-avatar.h"
#include "chatty-settings.h"
#include "chatty-secret-store.h"
#include "chatty-ma-account-details.h"
#include "chatty-pp-account-details.h"
#include "chatty-settings-dialog.h"
#include "chatty-log.h"

/**
 * @short_description: Chatty settings Dialog
 */

/* Several code has been copied from chatty-dialogs.c with modifications
 * which was written by Andrea Schäfer. */
struct _ChattySettingsDialog
{
  HdyWindow      parent_instance;

  GtkWidget      *back_button;
  GtkWidget      *cancel_button;
  GtkWidget      *add_button;
  GtkWidget      *matrix_spinner;
  GtkWidget      *save_button;
  GtkWidget      *mms_cancel_button;
  GtkWidget      *mms_save_button;

  GtkWidget      *notification_revealer;
  GtkWidget      *notification_label;

  GtkWidget      *main_stack;
  GtkWidget      *accounts_list_box;
  GtkWidget      *add_account_row;

  GtkWidget      *enable_purple_row;
  GtkWidget      *enable_purple_switch;

  GtkWidget      *account_details_stack;
  GtkWidget      *pp_account_details;
  GtkWidget      *ma_account_details;

  GtkWidget      *protocol_list_group;
  GtkWidget      *protocol_list;
  GtkWidget      *xmpp_radio_button;
  GtkWidget      *matrix_row;
  GtkWidget      *matrix_radio_button;
  GtkWidget      *telegram_row;
  GtkWidget      *telegram_radio_button;
  GtkWidget      *new_account_settings_list;
  GtkWidget      *new_account_id_entry;
  GtkWidget      *new_password_entry;

  GtkWidget      *delivery_reports_switch;
  GtkWidget      *handle_smil_switch;
  GtkWidget      *carrier_mmsc_entry;
  GtkWidget      *mms_apn_entry;
  GtkWidget      *mms_proxy_entry;

  GtkWidget      *send_receipts_switch;
  GtkWidget      *message_archive_switch;
  GtkWidget      *message_carbons_row;
  GtkWidget      *message_carbons_switch;
  GtkWidget      *typing_notification_switch;

  GtkWidget      *convert_smileys_switch;
  GtkWidget      *return_sends_switch;

  GtkWidget      *purple_settings_row;

  GtkWidget      *matrix_homeserver_entry;

  ChattyMmAccount *mm_account;
  ChattySettings *settings;
  ChattyAccount  *selected_account;
  GCancellable   *cancellable;

  gboolean visible;
  gboolean mms_settings_loaded;
  guint    revealer_timeout_id;
};

G_DEFINE_TYPE (ChattySettingsDialog, chatty_settings_dialog, HDY_TYPE_WINDOW)

static void
finish_cb (GObject      *object,
           GAsyncResult *result,
           gpointer      user_data)
{
  GError *error = NULL;
  gboolean status;

  status = g_task_propagate_boolean (G_TASK (result), &error);

  if (error)
    g_task_return_error (user_data, error);
  else
    g_task_return_boolean (user_data, status);
}

static void
settings_dialog_set_save_state (ChattySettingsDialog *self,
                                gboolean              in_progress)
{
  g_object_set (self->matrix_spinner, "active", in_progress, NULL);
  gtk_widget_set_sensitive (self->main_stack, !in_progress);
  gtk_widget_set_sensitive (self->add_button, !in_progress);
  gtk_widget_set_visible (self->back_button, !in_progress);
  gtk_widget_set_visible (self->cancel_button, in_progress);
}

static void
settings_save_account_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  ChattySettingsDialog *self = user_data;
  ChattyManager *manager = (gpointer)object;
  g_autoptr(GError) error = NULL;

  chatty_manager_save_account_finish (manager, result, &error);
  settings_dialog_set_save_state (self, FALSE);

  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      GtkWidget *dialog;

      dialog = gtk_message_dialog_new (GTK_WINDOW (self),
                                       GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_WARNING,
                                       GTK_BUTTONS_CLOSE,
                                       "Error saving account: %s", error->message);
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
    }
  } else {
    gtk_widget_hide (self->add_button);
    gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack), "main-settings");
  }
}

static gboolean
dialog_notification_timeout_cb (gpointer user_data)
{
  ChattySettingsDialog *self = user_data;

  gtk_revealer_set_reveal_child (GTK_REVEALER (self->notification_revealer), FALSE);
  g_clear_handle_id (&self->revealer_timeout_id, g_source_remove);

  return G_SOURCE_REMOVE;
}

static void
matrix_home_server_verify_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  ChattySettingsDialog *self = user_data;
  g_autoptr(ChattyMaAccount) account = NULL;
  g_autoptr(GError) error = NULL;
  const char *username, *password, *server;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  settings_dialog_set_save_state (self, FALSE);
  server = gtk_entry_get_text (GTK_ENTRY (self->matrix_homeserver_entry));

  if (!matrix_utils_verify_homeserver_finish (result, &error)) {
    gtk_widget_set_sensitive (self->add_button, FALSE);
    g_clear_handle_id (&self->revealer_timeout_id, g_source_remove);
    gtk_label_set_text (GTK_LABEL (self->notification_label),
                        _("Failed to verify server"));
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->notification_revealer), TRUE);
    self->revealer_timeout_id = g_timeout_add_seconds (5, dialog_notification_timeout_cb, self);
    return;
  }

  username = gtk_entry_get_text (GTK_ENTRY (self->new_account_id_entry));
  password = gtk_entry_get_text (GTK_ENTRY (self->new_password_entry));

  account = chatty_ma_account_new (username, password);
  chatty_ma_account_set_homeserver (account, server);
  chatty_manager_save_account_async (chatty_manager_get_default (), CHATTY_ACCOUNT (account),
                                     NULL, settings_save_account_cb, self);
}

static void
chatty_settings_save_matrix (ChattySettingsDialog *self,
                             const char           *user_id,
                             const char           *password);
static void
matrix_home_server_got_cb (GObject      *object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  ChattySettingsDialog *self = user_data;
  g_autoptr(ChattyMaAccount) account = NULL;
  g_autofree char *home_server = NULL;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  home_server = matrix_utils_get_homeserver_finish (result, NULL);

  if (g_cancellable_is_cancelled (self->cancellable)) {
    settings_dialog_set_save_state (self, FALSE);
    return;
  }

  if (home_server && *home_server) {
    const char *username, *password;

    username = gtk_entry_get_text (GTK_ENTRY (self->new_account_id_entry));
    password = gtk_entry_get_text (GTK_ENTRY (self->new_password_entry));

    gtk_entry_set_text (GTK_ENTRY (self->matrix_homeserver_entry), home_server);
    chatty_settings_save_matrix (self, username, password);
  } else {
    settings_dialog_set_save_state (self, FALSE);
    gtk_widget_set_sensitive (self->add_button, FALSE);

    g_clear_handle_id (&self->revealer_timeout_id, g_source_remove);
    gtk_label_set_text (GTK_LABEL (self->notification_label),
                        _("Couldn't get Home server address"));
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->notification_revealer), TRUE);
    self->revealer_timeout_id = g_timeout_add_seconds (5, dialog_notification_timeout_cb, self);

    gtk_widget_show (self->matrix_homeserver_entry);
    gtk_entry_grab_focus_without_selecting (GTK_ENTRY (self->matrix_homeserver_entry));
    gtk_editable_set_position (GTK_EDITABLE (self->matrix_homeserver_entry), -1);
  }
}

static void
chatty_settings_save_matrix (ChattySettingsDialog *self,
                             const char           *user_id,
                             const char           *password)
{
  GtkEntry *entry;
  const char *uri;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));
  g_return_if_fail (user_id && *user_id);
  g_return_if_fail (password && *password);

  settings_dialog_set_save_state (self, TRUE);

  g_clear_object (&self->cancellable);
  self->cancellable = g_cancellable_new ();
  entry = GTK_ENTRY (self->matrix_homeserver_entry);
  uri = gtk_entry_get_text (entry);

  if (uri && *uri)
    matrix_utils_verify_homeserver_async (uri, 30, self->cancellable,
                                          matrix_home_server_verify_cb, self);
  else
    matrix_utils_get_homeserver_async (user_id, 10, self->cancellable,
                                       matrix_home_server_got_cb, self);
}

static void
chatty_settings_add_clicked_cb (ChattySettingsDialog *self)
{
  ChattyManager *manager;
  g_autoptr (ChattyAccount) account = NULL;
  const char *user_id, *password;
  gboolean is_matrix, is_telegram;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  manager  = chatty_manager_get_default ();
  user_id  = gtk_entry_get_text (GTK_ENTRY (self->new_account_id_entry));
  password = gtk_entry_get_text (GTK_ENTRY (self->new_password_entry));

  is_matrix = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->matrix_radio_button));
  is_telegram = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->telegram_radio_button));

  if (is_matrix) {
    GtkEntry *entry;
    const char *server;

    entry = GTK_ENTRY (self->matrix_homeserver_entry);
    server = gtk_entry_get_text (entry);

    if (!server || !*server) {
      if (g_str_has_suffix (user_id, ":librem.one") ||
          g_str_has_suffix (user_id, "@librem.one"))
        gtk_entry_set_text (entry, "https://chat.librem.one");
      else if (g_str_has_suffix (user_id, "talk.puri.sm") ||
               g_str_has_suffix (user_id, "@puri.sm"))
        gtk_entry_set_text (entry, "https://talk.puri.sm");
    }
  }

  if (is_matrix && chatty_settings_get_experimental_features (chatty_settings_get_default ())) {
    chatty_settings_save_matrix (self, user_id, password);
    return;
  }

#ifdef PURPLE_ENABLED
  if (is_matrix) {
    g_autoptr(GTask) task = NULL;
    GtkEntry *entry;
    const char *server_url;

    entry = GTK_ENTRY (self->matrix_homeserver_entry);
    server_url = gtk_entry_get_text (entry);

    if (!server_url || !*server_url) {
      gtk_widget_show (GTK_WIDGET (entry));
      gtk_entry_grab_focus_without_selecting (entry);
      gtk_editable_set_position (GTK_EDITABLE (entry), -1);

      gtk_widget_set_sensitive (self->add_button, FALSE);
      g_clear_handle_id (&self->revealer_timeout_id, g_source_remove);
      gtk_label_set_text (GTK_LABEL (self->notification_label),
                          _("Couldn't get Home server address"));
      gtk_revealer_set_reveal_child (GTK_REVEALER (self->notification_revealer), TRUE);
      self->revealer_timeout_id = g_timeout_add_seconds (5, dialog_notification_timeout_cb, self);
      return;
    }

    task = g_task_new (self, NULL, NULL, NULL);
    settings_dialog_set_save_state (self, TRUE);
    matrix_utils_verify_homeserver_async (server_url, 10, self->cancellable,
                                          finish_cb, task);

    while (!g_task_get_completed (task))
      g_main_context_iteration (NULL, TRUE);

    settings_dialog_set_save_state (self, FALSE);

    if (!g_task_propagate_boolean (task, NULL)) {
      gtk_widget_set_sensitive (self->add_button, FALSE);
      g_clear_handle_id (&self->revealer_timeout_id, g_source_remove);
      gtk_label_set_text (GTK_LABEL (self->notification_label),
                          _("Failed to verify server"));
      gtk_revealer_set_reveal_child (GTK_REVEALER (self->notification_revealer), TRUE);
      self->revealer_timeout_id = g_timeout_add_seconds (5, dialog_notification_timeout_cb, self);
      return;
    }

    account = (ChattyAccount *)chatty_pp_account_new (CHATTY_PROTOCOL_MATRIX, user_id, server_url, FALSE);
  } else if (is_telegram) {
    account = (ChattyAccount *)chatty_pp_account_new (CHATTY_PROTOCOL_TELEGRAM, user_id, NULL, FALSE);
  } else {/* XMPP */
    gboolean has_encryption;

    has_encryption = chatty_purple_has_encryption (chatty_purple_get_default ());
    account = (ChattyAccount *)chatty_pp_account_new (CHATTY_PROTOCOL_XMPP,
                                                      user_id, NULL, has_encryption);
  }
#endif

  g_return_if_fail (account);

  if (password)
    {
      chatty_account_set_password (CHATTY_ACCOUNT (account), password);

      if (!is_telegram)
        chatty_account_set_remember_password (CHATTY_ACCOUNT (account), TRUE);
    }

  chatty_account_save (CHATTY_ACCOUNT (account));

  if (!chatty_manager_get_disable_auto_login (manager))
    chatty_account_set_enabled (CHATTY_ACCOUNT (account), TRUE);

  gtk_widget_hide (self->add_button);
  gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack), "main-settings");
}

static void
pp_account_save_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr(ChattySettingsDialog) self = user_data;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  g_object_set (self->matrix_spinner, "active", FALSE, NULL);
  gtk_widget_set_sensitive (self->account_details_stack, TRUE);

  gtk_widget_hide (self->save_button);
  gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack), "main-settings");
}

static void
chatty_settings_save_clicked_cb (ChattySettingsDialog *self)
{
  GtkStack *stack;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  stack = GTK_STACK (self->account_details_stack);

  g_object_set (self->matrix_spinner, "active", TRUE, NULL);
  gtk_widget_set_sensitive (self->account_details_stack, FALSE);

  if (gtk_stack_get_visible_child (stack) == self->pp_account_details)
    chatty_pp_account_save_async (CHATTY_PP_ACCOUNT_DETAILS (self->pp_account_details),
                                  pp_account_save_cb, g_object_ref (self));
  else if (gtk_stack_get_visible_child (stack) == self->ma_account_details)
    chatty_ma_account_details_save_async (CHATTY_MA_ACCOUNT_DETAILS (self->ma_account_details),
                                          pp_account_save_cb, g_object_ref (self));
}

static void
settings_pp_details_changed_cb (ChattySettingsDialog *self,
                                GParamSpec           *pspec,
                                GtkWidget            *details)
{
  gboolean modified;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));
  g_assert (GTK_IS_WIDGET (details));

  g_object_get (details, "modified", &modified, NULL);
  gtk_widget_set_sensitive (self->save_button, modified);
}

static void
settings_delete_account_clicked_cb (ChattySettingsDialog *self)
{
  GtkWidget *dialog;
  const char *username;
  int response;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  if (CHATTY_IS_MA_ACCOUNT (self->selected_account))
    username = chatty_ma_account_get_login_username (CHATTY_MA_ACCOUNT (self->selected_account));
  else
    username = chatty_item_get_username (CHATTY_ITEM (self->selected_account));

  dialog = gtk_message_dialog_new ((GtkWindow*)self,
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_WARNING,
                                   GTK_BUTTONS_OK_CANCEL,
                                   _("Delete Account"));

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("Delete account %s?"), username);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ON_PARENT);

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if (response == GTK_RESPONSE_OK)
    {
      chatty_manager_delete_account_async (chatty_manager_get_default (),
                                           g_steal_pointer (&self->selected_account),
                                           NULL, NULL, NULL);

      gtk_widget_hide (self->save_button);
      gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack), "main-settings");
    }

  gtk_widget_destroy (dialog);
}

static void
settings_pp_details_delete_cb (ChattySettingsDialog *self)
{
  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  settings_delete_account_clicked_cb (self);
}

static void
settings_dialog_load_mms_settings (ChattySettingsDialog *self)
{
  const char *apn, *mmsc, *proxy;
  gboolean use_smil;

  if (chatty_mm_account_get_mms_settings (self->mm_account, &apn, &mmsc, &proxy, &use_smil)) {
    gtk_entry_set_text (GTK_ENTRY (self->mms_apn_entry), apn);
    gtk_entry_set_text (GTK_ENTRY (self->carrier_mmsc_entry), mmsc);
    gtk_entry_set_text (GTK_ENTRY (self->mms_proxy_entry), proxy);
    gtk_switch_set_state (GTK_SWITCH (self->handle_smil_switch), use_smil);

    self->mms_settings_loaded = TRUE;
  }
}

static void
sms_mms_settings_row_activated_cb (ChattySettingsDialog *self)
{
  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  gtk_widget_show (self->mms_save_button);
  gtk_widget_show (self->mms_cancel_button);
  gtk_widget_hide (self->back_button);

  settings_dialog_load_mms_settings (self);

  gtk_switch_set_state (GTK_SWITCH (self->delivery_reports_switch),
                        chatty_settings_request_sms_delivery_reports (self->settings));

  gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack), "message-settings-view");
}

static void
purple_settings_row_activated_cb (ChattySettingsDialog *self)
{
  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack), "purple-settings-view");
}

static void
settings_pw_entry_icon_clicked_cb (ChattySettingsDialog *self,
                                   GtkEntryIconPosition  icon_pos,
                                   GdkEvent             *event,
                                   GtkEntry             *entry)
{
  const char *icon_name = "eye-not-looking-symbolic";

  g_return_if_fail (CHATTY_IS_SETTINGS_DIALOG (self));
  g_return_if_fail (GTK_IS_ENTRY (entry));
  g_return_if_fail (icon_pos == GTK_ENTRY_ICON_SECONDARY);

  self->visible = !self->visible;

  gtk_entry_set_visibility (entry, self->visible);

  if (self->visible)
    icon_name = "eye-open-negative-filled-symbolic";

  gtk_entry_set_icon_from_icon_name (entry,
                                     GTK_ENTRY_ICON_SECONDARY,
                                     icon_name);
}

static void
settings_homeserver_entry_changed (ChattySettingsDialog *self,
                                   GtkEntry             *entry)
{
  const char *server;
  gboolean valid = FALSE;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));
  g_assert (GTK_IS_ENTRY (entry));

  if (!gtk_widget_is_visible (GTK_WIDGET (entry)))
    return;

  server = gtk_entry_get_text (entry);

  if (server && *server) {
    g_autoptr(SoupURI) uri = NULL;

    uri = soup_uri_new (gtk_entry_get_text (entry));

    valid = SOUP_URI_VALID_FOR_HTTP (uri);
    /* We need an absolute path URI */
    valid = valid && *uri->host && g_str_equal (soup_uri_get_path (uri), "/");
  }

  gtk_widget_set_sensitive (self->add_button, valid);
}

static void
settings_dialog_purple_changed_cb (ChattySettingsDialog *self)
{
#ifdef PURPLE_ENABLED
  ChattyPurple *purple;
  HdyActionRow *row;
  gboolean active;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  row = HDY_ACTION_ROW (self->enable_purple_row);
  purple = chatty_purple_get_default ();
  active = gtk_switch_get_active (GTK_SWITCH (self->enable_purple_switch));

  if (!active && chatty_purple_is_loaded (purple))
    hdy_action_row_set_subtitle (row, _("Restart chatty to disable purple"));
  else
    hdy_action_row_set_subtitle (row, _("Enable purple plugin"));
#endif
}

static void
settings_dialog_page_changed_cb (ChattySettingsDialog *self)
{
  const char *name;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  name = gtk_stack_get_visible_child_name (GTK_STACK (self->main_stack));

  if (g_strcmp0 (name, "message-settings-view") == 0)
    gtk_window_set_title (GTK_WINDOW (self), _("SMS and MMS Settings"));
  else if (g_strcmp0 (name, "purple-settings-view") == 0)
    gtk_window_set_title (GTK_WINDOW (self), _("Purple Settings"));
  else if (g_strcmp0 (name, "add-account-view") == 0)
    gtk_window_set_title (GTK_WINDOW (self), _("New Account"));
  else
    gtk_window_set_title (GTK_WINDOW (self), _("Preferences"));
}

static void
settings_update_new_account_view (ChattySettingsDialog *self)
{
  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  gtk_widget_hide (self->matrix_homeserver_entry);

  gtk_entry_set_text (GTK_ENTRY (self->matrix_homeserver_entry), "");
  gtk_entry_set_text (GTK_ENTRY (self->new_account_id_entry), "");
  gtk_entry_set_text (GTK_ENTRY (self->new_password_entry), "");

  self->selected_account = NULL;
  gtk_widget_grab_focus (self->new_account_id_entry);
  gtk_widget_show (self->add_button);

  if (chatty_settings_get_experimental_features (chatty_settings_get_default ()))
    gtk_widget_set_visible (self->matrix_row, TRUE);

#ifdef PURPLE_ENABLED
  if (purple_find_prpl ("prpl-matrix"))
    gtk_widget_set_visible (self->matrix_row, TRUE);

  gtk_widget_set_visible (self->telegram_row, purple_find_prpl ("prpl-telegram") != NULL);
#endif

  hdy_preferences_group_set_title (HDY_PREFERENCES_GROUP (self->protocol_list_group),
                                   _("Select Protocol"));
  gtk_widget_show (self->protocol_list);

  if (gtk_widget_is_visible (self->xmpp_radio_button))
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->xmpp_radio_button), TRUE);
  else if (gtk_widget_is_visible (self->matrix_radio_button))
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->matrix_radio_button), TRUE);

  gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack), "add-account-view");
}

static void
chatty_settings_dialog_update_status (GtkListBoxRow *row)
{
  ChattyAccount *account;
  GtkSpinner *spinner;
  ChattyStatus status;

  g_assert (GTK_IS_LIST_BOX_ROW (row));

  spinner = g_object_get_data (G_OBJECT(row), "row-prefix");
  account = g_object_get_data (G_OBJECT (row), "row-account");
  status  = chatty_account_get_status (CHATTY_ACCOUNT (account));

  g_object_set (spinner,
                "active", status == CHATTY_CONNECTING,
                NULL);
}

static void
mms_carrier_settings_apply_button_clicked_cb (ChattySettingsDialog *self)
{
  ChattyAccount *mm_account;
  const char *apn, *mmsc, *proxy;
  gboolean use_smil;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  mm_account = chatty_manager_get_mm_account (chatty_manager_get_default ());
  apn = gtk_entry_get_text (GTK_ENTRY (self->mms_apn_entry));
  mmsc = gtk_entry_get_text (GTK_ENTRY (self->carrier_mmsc_entry));
  proxy = gtk_entry_get_text (GTK_ENTRY (self->mms_proxy_entry));
  use_smil = gtk_switch_get_state (GTK_SWITCH (self->handle_smil_switch));

  chatty_mm_account_set_mms_settings_async (CHATTY_MM_ACCOUNT (mm_account),
                                            apn, mmsc, proxy, use_smil,
                                            NULL, NULL, NULL);

  g_object_set (self->settings,
                "request-sms-delivery-reports",
                gtk_switch_get_state (GTK_SWITCH (self->delivery_reports_switch)),
                NULL);

  gtk_widget_hide (self->mms_cancel_button);
  gtk_widget_hide (self->mms_save_button);
  gtk_widget_show (self->back_button);
  gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack), "main-settings");
}

static void
mms_carrier_settings_cancel_button_clicked_cb (ChattySettingsDialog *self)
{
  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  gtk_widget_hide (self->mms_cancel_button);
  gtk_widget_hide (self->mms_save_button);
  gtk_widget_show (self->back_button);
  gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack), "main-settings");
}

static void
account_list_row_activated_cb (ChattySettingsDialog *self,
                               GtkListBoxRow        *row,
                               GtkListBox           *box)
{
  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));
  g_assert (GTK_IS_LIST_BOX_ROW (row));
  g_assert (GTK_IS_LIST_BOX (box));

  gtk_widget_set_sensitive (self->add_button, FALSE);
  gtk_widget_set_sensitive (self->save_button, FALSE);

  if (GTK_WIDGET (row) == self->add_account_row)
    {
      settings_update_new_account_view (self);
    }
  else
    {
      gtk_widget_show (self->save_button);
      self->selected_account = g_object_get_data (G_OBJECT (row), "row-account");
      g_assert (self->selected_account != NULL);

      if (CHATTY_IS_MA_ACCOUNT (self->selected_account)) {
        chatty_ma_account_details_set_item (CHATTY_MA_ACCOUNT_DETAILS (self->ma_account_details),
                                            self->selected_account);
        chatty_pp_account_details_set_item (CHATTY_PP_ACCOUNT_DETAILS (self->pp_account_details), NULL);
        gtk_stack_set_visible_child (GTK_STACK (self->account_details_stack), self->ma_account_details);
      } else {
        chatty_pp_account_details_set_item (CHATTY_PP_ACCOUNT_DETAILS (self->pp_account_details),
                                            self->selected_account);
        chatty_ma_account_details_set_item (CHATTY_MA_ACCOUNT_DETAILS (self->ma_account_details), NULL);
        gtk_stack_set_visible_child (GTK_STACK (self->account_details_stack), self->pp_account_details);
      }

      chatty_settings_dialog_update_status (row);
      gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack),
                                        "edit-account-view");
    }
}

static void
chatty_settings_back_clicked_cb (ChattySettingsDialog *self)
{
  const gchar *visible_child;

  visible_child = gtk_stack_get_visible_child_name (GTK_STACK (self->main_stack));

  if (g_str_equal (visible_child, "main-settings"))
    {
        gtk_widget_hide (GTK_WIDGET (self));
    }
  else
    {
      gtk_widget_hide (self->add_button);
      gtk_widget_hide (self->save_button);
      gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack), "main-settings");
    }
}

static void
chatty_settings_cancel_clicked_cb (ChattySettingsDialog *self)
{
  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  if (self->cancellable)
    g_cancellable_cancel (self->cancellable);

  g_object_set (self->matrix_spinner, "active", FALSE, NULL);
  gtk_widget_set_sensitive (self->main_stack, TRUE);
  gtk_widget_set_sensitive (self->add_button, TRUE);
  gtk_widget_hide (self->cancel_button);
  gtk_widget_show (self->back_button);
}

static void
settings_new_detail_changed_cb (ChattySettingsDialog *self)
{
  const gchar *id, *password;
  ChattyProtocol protocol;
  gboolean valid = TRUE;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  id = gtk_entry_get_text (GTK_ENTRY (self->new_account_id_entry));
  password = gtk_entry_get_text (GTK_ENTRY (self->new_password_entry));

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->matrix_radio_button)))
    protocol = CHATTY_PROTOCOL_MATRIX;
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->telegram_radio_button)))
    protocol = CHATTY_PROTOCOL_TELEGRAM;
  else
    protocol = CHATTY_PROTOCOL_XMPP;

  if (chatty_settings_get_experimental_features (chatty_settings_get_default ()) &&
      gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->matrix_radio_button)))
    protocol = CHATTY_PROTOCOL_MATRIX | CHATTY_PROTOCOL_EMAIL;

  /* Allow empty passwords for telegram accounts */
  if (protocol != CHATTY_PROTOCOL_TELEGRAM)
    valid = valid && password && *password;

  valid = valid && chatty_utils_username_is_valid (id, protocol);

  /* Don’t allow adding if an account with same id exists */
  if (valid &&
      chatty_manager_find_account_with_name (chatty_manager_get_default (), protocol, id))
    valid = FALSE;

  gtk_widget_set_sensitive (self->add_button, valid);
}

static void
settings_protocol_changed_cb (ChattySettingsDialog *self,
                              GtkWidget            *button)
{
  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));
  g_assert (GTK_IS_TOGGLE_BUTTON (button));

  gtk_widget_grab_focus (self->new_account_id_entry);

  if (button == self->xmpp_radio_button)
    gtk_entry_set_placeholder_text (GTK_ENTRY (self->new_account_id_entry),
                                    "user@example.com");
  else if (button == self->matrix_radio_button)
    gtk_entry_set_placeholder_text (GTK_ENTRY (self->new_account_id_entry),
                                    /* TRANSLATORS: Only translate 'or' */
                                    _("@user:matrix.org or user@example.com"));
  else /* Telegram */
    gtk_entry_set_placeholder_text (GTK_ENTRY (self->new_account_id_entry),
                                    "+1123456789");

  /* Force re-check if id is valid */
  settings_new_detail_changed_cb (self);
}

static GtkWidget *
chatty_account_row_new (ChattyAccount *account)
{
  HdyActionRow   *row;
  GtkWidget      *account_enabled_switch;
  GtkWidget      *spinner;
  const char     *username;

  row = HDY_ACTION_ROW (hdy_action_row_new ());
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
  gtk_widget_show (GTK_WIDGET (row));
  g_object_set_data (G_OBJECT(row),
                     "row-account",
                     (gpointer) account);

  spinner = gtk_spinner_new ();
  gtk_widget_show (spinner);
  hdy_action_row_add_prefix (row, spinner);

  g_object_set_data (G_OBJECT(row),
                     "row-prefix",
                     (gpointer)spinner);

  account_enabled_switch = gtk_switch_new ();
  gtk_widget_show (account_enabled_switch);

  g_object_set  (G_OBJECT(account_enabled_switch),
                 "valign", GTK_ALIGN_CENTER,
                 "halign", GTK_ALIGN_END,
                 NULL);

  g_object_bind_property (account, "enabled",
                          account_enabled_switch, "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

  if (CHATTY_IS_MA_ACCOUNT (account))
    username = chatty_ma_account_get_login_username (CHATTY_MA_ACCOUNT (account));
  else
    username = chatty_item_get_username (CHATTY_ITEM (account));

  hdy_preferences_row_set_title (HDY_PREFERENCES_ROW (row), username);
  hdy_action_row_set_subtitle (row, chatty_account_get_protocol_name (CHATTY_ACCOUNT (account)));
  gtk_container_add (GTK_CONTAINER (row), account_enabled_switch);
  hdy_action_row_set_activatable_widget (row, NULL);

  g_signal_connect_object (account, "notify::status",
                           G_CALLBACK (chatty_settings_dialog_update_status),
                           row, G_CONNECT_SWAPPED);
  chatty_settings_dialog_update_status (GTK_LIST_BOX_ROW (row));

  return GTK_WIDGET (row);
}

static GtkWidget *
chatty_settings_account_row_new (ChattyAccount        *account,
                                 ChattySettingsDialog *self)
{
  /* mm account is the last item, and we use the row to show new account row */
  if (CHATTY_IS_MM_ACCOUNT (account))
    return self->add_account_row;
  else
    return chatty_account_row_new (account);
}

static void
settings_dialog_mm_status_changed (ChattySettingsDialog *self)
{
  gboolean has_mms;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  has_mms = chatty_mm_account_has_mms_feature (self->mm_account);
  gtk_widget_set_sensitive (self->handle_smil_switch, has_mms);
  gtk_widget_set_sensitive (self->carrier_mmsc_entry, has_mms);
  gtk_widget_set_sensitive (self->mms_apn_entry, has_mms);
  gtk_widget_set_sensitive (self->mms_proxy_entry, has_mms);

  if (has_mms && !self->mms_settings_loaded)
    settings_dialog_load_mms_settings (self);
}

static void
chatty_settings_dialog_constructed (GObject *object)
{
  ChattySettingsDialog *self = (ChattySettingsDialog *)object;
  ChattySettings *settings;

  G_OBJECT_CLASS (chatty_settings_dialog_parent_class)->constructed (object);

  settings = chatty_settings_get_default ();
  self->settings = g_object_ref (settings);

  g_object_bind_property (settings, "message-carbons",
                          self->message_carbons_switch, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (settings, "purple-enabled",
                          self->enable_purple_switch, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (settings, "send-receipts",
                          self->send_receipts_switch, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (settings, "mam-enabled",
                          self->message_archive_switch, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (settings, "send-typing",
                          self->typing_notification_switch, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (settings, "convert-emoticons",
                          self->convert_smileys_switch, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (settings, "return-sends-message",
                          self->return_sends_switch, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
}

static void
chatty_settings_dialog_finalize (GObject *object)
{
  ChattySettingsDialog *self = (ChattySettingsDialog *)object;

  g_clear_handle_id (&self->revealer_timeout_id, g_source_remove);
  if (self->cancellable)
    g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->settings);

  G_OBJECT_CLASS (chatty_settings_dialog_parent_class)->finalize (object);
}

static void
chatty_settings_dialog_class_init (ChattySettingsDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed  = chatty_settings_dialog_constructed;
  object_class->finalize = chatty_settings_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-settings-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, back_button);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, cancel_button);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, add_button);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, matrix_spinner);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, save_button);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, mms_cancel_button);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, mms_save_button);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, notification_revealer);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, notification_label);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, main_stack);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, accounts_list_box);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, add_account_row);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, enable_purple_row);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, enable_purple_switch);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, account_details_stack);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, pp_account_details);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, ma_account_details);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, protocol_list_group);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, protocol_list);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, xmpp_radio_button);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, matrix_row);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, matrix_radio_button);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, telegram_row);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, telegram_radio_button);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, new_account_settings_list);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, new_account_id_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, new_password_entry);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, delivery_reports_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, handle_smil_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, carrier_mmsc_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, mms_apn_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, mms_proxy_entry);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, send_receipts_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, message_archive_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, message_carbons_row);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, message_carbons_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, typing_notification_switch);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, convert_smileys_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, return_sends_switch);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, purple_settings_row);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, matrix_homeserver_entry);

  gtk_widget_class_bind_template_callback (widget_class, chatty_settings_add_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, chatty_settings_save_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_pp_details_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_pp_details_delete_cb);
  gtk_widget_class_bind_template_callback (widget_class, sms_mms_settings_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, purple_settings_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, mms_carrier_settings_cancel_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, mms_carrier_settings_apply_button_clicked_cb);

  gtk_widget_class_bind_template_callback (widget_class, account_list_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, chatty_settings_back_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, chatty_settings_cancel_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_new_detail_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_protocol_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_pw_entry_icon_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_homeserver_entry_changed);

  gtk_widget_class_bind_template_callback (widget_class, settings_dialog_purple_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_dialog_page_changed_cb);
}

static void
chatty_settings_dialog_init (ChattySettingsDialog *self)
{
  ChattyManager *manager;
  gboolean show_account_box = FALSE;

  manager = chatty_manager_get_default ();
  self->mm_account = (ChattyMmAccount *)chatty_manager_get_mm_account (manager);

  gtk_widget_init_template (GTK_WIDGET (self));

#ifdef PURPLE_ENABLED
  {
    ChattyPurple *purple;

    purple = chatty_purple_get_default ();
    gtk_widget_show (self->purple_settings_row);
    gtk_widget_set_visible (self->message_carbons_row,
                            chatty_purple_has_carbon_plugin (purple));
    g_object_bind_property (purple, "enabled",
                            self->xmpp_radio_button, "visible",
                            G_BINDING_SYNC_CREATE);
    g_object_bind_property (purple, "enabled",
                            self->accounts_list_box, "visible",
                            G_BINDING_SYNC_CREATE);
    if (chatty_purple_is_loaded (purple))
      show_account_box = TRUE;
  }
#else
  gtk_widget_hide (self->xmpp_radio_button);
#endif

  if (chatty_settings_get_experimental_features (chatty_settings_get_default ()))
    show_account_box = TRUE;

  gtk_widget_set_visible (self->accounts_list_box, show_account_box);

  gtk_list_box_bind_model (GTK_LIST_BOX (self->accounts_list_box),
                           chatty_manager_get_accounts (manager),
                           (GtkListBoxCreateWidgetFunc)chatty_settings_account_row_new,
                           g_object_ref (self), g_object_unref);
  g_signal_connect_object (self->mm_account, "notify::status",
                           G_CALLBACK (settings_dialog_mm_status_changed),
                           self, G_CONNECT_SWAPPED);
  settings_dialog_mm_status_changed (self);
}

GtkWidget *
chatty_settings_dialog_new (GtkWindow *parent_window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (parent_window), NULL);

  return g_object_new (CHATTY_TYPE_SETTINGS_DIALOG,
                       "transient-for", parent_window,
                       "modal", TRUE,
                       NULL);
}
