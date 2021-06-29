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
#include "users/chatty-pp-account.h"
#include "matrix/matrix-utils.h"
#include "matrix/chatty-ma-account.h"
#include "chatty-manager.h"
#include "chatty-fp-row.h"
#include "chatty-avatar.h"
#include "chatty-settings.h"
#include "chatty-secret-store.h"
#include "chatty-pp-account-details.h"
#include "chatty-settings-dialog.h"

/**
 * @short_description: Chatty settings Dialog
 */

/* Several code has been copied from chatty-dialogs.c with modifications
 * which was written by Andrea Schäfer. */
struct _ChattySettingsDialog
{
  GtkDialog       parent_instance;

  GtkWidget      *back_button;
  GtkWidget      *cancel_button;
  GtkWidget      *add_button;
  GtkWidget      *matrix_spinner;
  GtkWidget      *save_button;

  GtkWidget      *main_stack;
  GtkWidget      *accounts_list_box;
  GtkWidget      *add_account_row;

  GtkWidget      *account_details_stack;
  GtkWidget      *pp_account_details;

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

  GtkWidget      *send_receipts_switch;
  GtkWidget      *message_archive_switch;
  GtkWidget      *message_carbons_row;
  GtkWidget      *message_carbons_switch;
  GtkWidget      *typing_notification_switch;

  GtkWidget      *indicate_idle_switch;
  GtkWidget      *indicate_unknown_switch;

  GtkWidget      *convert_smileys_switch;
  GtkWidget      *return_sends_switch;

  GtkWidget      *matrix_homeserver_dialog;
  GtkWidget      *matrix_homeserver_entry;
  GtkWidget      *matrix_accept_button;
  GtkWidget      *matrix_cancel_button;

  ChattySettings *settings;
  ChattyAccount  *selected_account;
  GCancellable   *cancellable;

  gboolean visible;
};

G_DEFINE_TYPE (ChattySettingsDialog, chatty_settings_dialog, GTK_TYPE_DIALOG)


static void
chatty_account_list_clear (ChattySettingsDialog *self,
                           GtkListBox           *list)
{
  g_autoptr(GList) children = NULL;
  GList *iter;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));
  g_assert (GTK_IS_LIST_BOX (list));

  children = gtk_container_get_children (GTK_CONTAINER (list));

  for (iter = children; iter != NULL; iter = iter->next)
    if ((GtkWidget *)iter->data != self->add_account_row)
      gtk_container_remove (GTK_CONTAINER (list), GTK_WIDGET(iter->data));
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

  g_object_set (self->matrix_spinner, "active", FALSE, NULL);
  gtk_widget_set_sensitive (self->main_stack, TRUE);
  gtk_widget_set_sensitive (self->add_button, TRUE);
  gtk_widget_hide (self->cancel_button);
  gtk_widget_show (self->back_button);

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

static void
matrix_home_server_got_cb (GObject      *object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  ChattySettingsDialog *self = user_data;
  g_autoptr(ChattyMaAccount) account = NULL;
  g_autofree char *home_server = NULL;
  const char *username, *password;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  home_server = matrix_utils_get_homeserver_finish (result, NULL);

  if (g_cancellable_is_cancelled (self->cancellable))
    return;

  username = gtk_entry_get_text (GTK_ENTRY (self->new_account_id_entry));
  password = gtk_entry_get_text (GTK_ENTRY (self->new_password_entry));

  if (!home_server || !*home_server) {
    GtkEntry *entry;
    const char *url;
    int response;

    url = matrix_utils_get_url_from_username (username);
    entry = GTK_ENTRY (self->matrix_homeserver_entry);

    if (g_strcmp0 (url, "librem.one") == 0)
      gtk_entry_set_text (entry, "https://chat.librem.one");
    else
      gtk_entry_set_text (entry, "https://");

    gtk_entry_grab_focus_without_selecting (entry);
    gtk_editable_set_position (GTK_EDITABLE (entry), -1);
    response = gtk_dialog_run (GTK_DIALOG (self->matrix_homeserver_dialog));
    gtk_widget_hide (self->matrix_homeserver_dialog);

    if (response == GTK_RESPONSE_ACCEPT)
      home_server = g_strdup (gtk_entry_get_text (GTK_ENTRY (self->matrix_homeserver_entry)));
  }

  if (!home_server || !*home_server) {
    g_object_set (self->matrix_spinner, "active", FALSE, NULL);
    gtk_widget_set_sensitive (self->main_stack, TRUE);
    gtk_widget_set_sensitive (self->add_button, TRUE);
    gtk_widget_hide (self->cancel_button);
    gtk_widget_show (self->back_button);
    return;
  }

  account = chatty_ma_account_new (username, password);
  chatty_ma_account_set_homeserver (account, home_server);
  chatty_manager_save_account_async (chatty_manager_get_default (), CHATTY_ACCOUNT (account),
                                     NULL, settings_save_account_cb, self);
}

static void
chatty_settings_save_matrix (ChattySettingsDialog *self,
                             const char           *user_id,
                             const char           *password)
{
  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));
  g_return_if_fail (user_id && *user_id);
  g_return_if_fail (password && *password);

  if (!matrix_utils_get_url_from_username (user_id))
    g_return_if_reached ();

  gtk_widget_set_sensitive (self->main_stack, FALSE);
  gtk_widget_set_sensitive (self->add_button, FALSE);
  gtk_widget_show (self->cancel_button);
  gtk_widget_hide (self->back_button);

  g_object_set (self->matrix_spinner, "active", TRUE, NULL);

  g_clear_object (&self->cancellable);
  self->cancellable = g_cancellable_new ();
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

  if (is_matrix && chatty_settings_get_experimental_features (chatty_settings_get_default ())) {
    chatty_settings_save_matrix (self, user_id, password);
    return;
  }

  if (is_matrix) {
    GtkEntry *entry;
    const char *server_url;
    int response;

    entry = GTK_ENTRY (self->matrix_homeserver_entry);

    if (g_str_has_suffix (user_id, ":librem.one"))
      gtk_entry_set_text (entry, "https://chat.librem.one");
    else
      gtk_entry_set_text (entry, "https://");
       gtk_entry_grab_focus_without_selecting (entry);
    gtk_editable_set_position (GTK_EDITABLE (entry), -1);
    response = gtk_dialog_run (GTK_DIALOG (self->matrix_homeserver_dialog));
    gtk_widget_hide (self->matrix_homeserver_dialog);

    if (response == GTK_RESPONSE_ACCEPT)
      server_url = gtk_entry_get_text (GTK_ENTRY (self->matrix_homeserver_entry));
    else
      return;

    account = (ChattyAccount *)chatty_pp_account_new (CHATTY_PROTOCOL_MATRIX, user_id, server_url, FALSE);
  } else if (is_telegram) {
    account = (ChattyAccount *)chatty_pp_account_new (CHATTY_PROTOCOL_TELEGRAM, user_id, NULL, FALSE);
  } else {/* XMPP */
    gboolean has_encryption;

    has_encryption = chatty_manager_lurch_plugin_is_loaded (chatty_manager_get_default ());
    account = (ChattyAccount *)chatty_pp_account_new (CHATTY_PROTOCOL_XMPP,
                                                      user_id, NULL, has_encryption);
  }

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
chatty_settings_save_clicked_cb (ChattySettingsDialog *self)
{
  GtkStack *stack;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  stack = GTK_STACK (self->account_details_stack);

  if (gtk_stack_get_visible_child (stack) == self->pp_account_details)
    chatty_pp_account_save (CHATTY_PP_ACCOUNT_DETAILS (self->pp_account_details));

  gtk_widget_hide (self->save_button);
  gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack), "main-settings");
}

static void
settings_pp_details_changed_cb (ChattySettingsDialog *self)
{
  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  gtk_widget_set_sensitive (self->save_button, TRUE);
}

static void chatty_settings_dialog_populate_account_list (ChattySettingsDialog *self);

static void
settings_delete_account_clicked_cb (ChattySettingsDialog *self)
{
  GtkWidget *dialog;
  int response;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  dialog = gtk_message_dialog_new ((GtkWindow*)self,
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_WARNING,
                                   GTK_BUTTONS_OK_CANCEL,
                                   _("Delete Account"));

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("Delete account %s?"),
                                            chatty_account_get_username (CHATTY_ACCOUNT (self->selected_account)));

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ON_PARENT);

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if (response == GTK_RESPONSE_OK)
    {
      ChattyAccount *account;

      account = g_steal_pointer (&self->selected_account);
      chatty_account_delete (account);
      chatty_manager_delete_account_async (chatty_manager_get_default (), account, NULL, NULL, NULL);

      chatty_settings_dialog_populate_account_list (self);
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
  gboolean valid = TRUE;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));
  g_assert (GTK_IS_ENTRY (entry));

  server = gtk_entry_get_text (entry);
  valid = server && g_str_has_prefix (server, "https://");

  if (valid) {
    g_autoptr(SoupURI) uri = NULL;

    uri = soup_uri_new (gtk_entry_get_text (entry));
    valid = uri && uri->host && *uri->host && g_str_equal (soup_uri_get_path (uri), "/");
  }

  gtk_widget_set_sensitive (self->matrix_accept_button, valid);
}

static void
settings_update_new_account_view (ChattySettingsDialog *self)
{
  PurplePlugin *protocol;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  gtk_entry_set_text (GTK_ENTRY (self->new_account_id_entry), "");
  gtk_entry_set_text (GTK_ENTRY (self->new_password_entry), "");

  self->selected_account = NULL;
  gtk_widget_grab_focus (self->new_account_id_entry);
  gtk_widget_show (self->add_button);

  if (chatty_settings_get_experimental_features (chatty_settings_get_default ()) ||
      purple_find_prpl ("prpl-matrix"))
    gtk_widget_set_visible (self->matrix_row, TRUE);

  protocol = purple_find_prpl ("prpl-telegram");
  gtk_widget_set_visible (self->telegram_row, protocol != NULL);

  hdy_preferences_group_set_title (HDY_PREFERENCES_GROUP (self->protocol_list_group),
                                   _("Select Protocol"));
  gtk_widget_show (self->protocol_list);

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

      chatty_pp_account_details_set_item (CHATTY_PP_ACCOUNT_DETAILS (self->pp_account_details),
                                          self->selected_account);
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

  /* Allow empty passwords for telegram accounts */
  if (protocol != CHATTY_PROTOCOL_TELEGRAM)
    valid = valid && password && *password;

  valid = valid && chatty_utils_username_is_valid (id, protocol);

  gtk_widget_set_sensitive (self->add_button, valid);
}

static void
settings_protocol_changed_cb (ChattySettingsDialog *self,
                              GtkWidget            *button)
{
  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));
  g_assert (GTK_IS_TOGGLE_BUTTON (button));

  gtk_widget_grab_focus (self->new_account_id_entry);

  /* Force re-check if id is valid */
  settings_new_detail_changed_cb (self);
}

static GtkWidget *
chatty_account_row_new (ChattyAccount *account)
{
  HdyActionRow   *row;
  GtkWidget      *account_enabled_switch;
  GtkWidget      *spinner;
  ChattyProtocol protocol;

  row = HDY_ACTION_ROW (hdy_action_row_new ());
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
  gtk_widget_show (GTK_WIDGET (row));
  g_object_set_data (G_OBJECT(row),
                     "row-account",
                     (gpointer) account);

  protocol = chatty_item_get_protocols (CHATTY_ITEM (account));
  if (protocol & ~(CHATTY_PROTOCOL_XMPP |
                   CHATTY_PROTOCOL_MATRIX |
                   CHATTY_PROTOCOL_TELEGRAM |
                   CHATTY_PROTOCOL_DELTA |
                   CHATTY_PROTOCOL_THREEPL |
                   CHATTY_PROTOCOL_SMS))
    return NULL;

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

  hdy_preferences_row_set_title (HDY_PREFERENCES_ROW (row), chatty_account_get_username (CHATTY_ACCOUNT (account)));
  hdy_action_row_set_subtitle (row, chatty_account_get_protocol_name (CHATTY_ACCOUNT (account)));
  gtk_container_add (GTK_CONTAINER (row), account_enabled_switch);
  hdy_action_row_set_activatable_widget (row, NULL);

  return GTK_WIDGET (row);
}

static void
chatty_settings_dialog_populate_account_list (ChattySettingsDialog *self)
{
  GListModel *model;
  guint n_items;
  gint index = 0;

  chatty_account_list_clear (self, GTK_LIST_BOX (self->accounts_list_box));

  model = chatty_manager_get_accounts (chatty_manager_get_default ());
  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(ChattyAccount) account = NULL;
      GtkWidget *row;

      account = g_list_model_get_item (model, i);

      if (chatty_item_is_sms (CHATTY_ITEM (account)))
        continue;

      row = chatty_account_row_new (account);

      if (!row)
        continue;

      gtk_list_box_insert (GTK_LIST_BOX (self->accounts_list_box), row, index);
      g_signal_connect_object (account, "notify::status",
                               G_CALLBACK (chatty_settings_dialog_update_status),
                               row, G_CONNECT_SWAPPED);
      chatty_settings_dialog_update_status (GTK_LIST_BOX_ROW (row));
      index++;
    }
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

  g_object_bind_property (settings, "send-receipts",
                          self->send_receipts_switch, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (settings, "mam-enabled",
                          self->message_archive_switch, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (settings, "message-carbons",
                          self->message_carbons_switch, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (settings, "send-typing",
                          self->typing_notification_switch, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (settings, "blur-idle-buddies",
                          self->indicate_idle_switch, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (settings, "indicate-unknown-contacts",
                          self->indicate_unknown_switch, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (settings, "convert-emoticons",
                          self->convert_smileys_switch, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (settings, "return-sends-message",
                          self->return_sends_switch, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  chatty_settings_dialog_populate_account_list (self);
}

static void
chatty_settings_dialog_finalize (GObject *object)
{
  ChattySettingsDialog *self = (ChattySettingsDialog *)object;

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

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, main_stack);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, accounts_list_box);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, add_account_row);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, account_details_stack);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, pp_account_details);

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

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, send_receipts_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, message_archive_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, message_carbons_row);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, message_carbons_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, typing_notification_switch);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, indicate_idle_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, indicate_unknown_switch);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, convert_smileys_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, return_sends_switch);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, matrix_homeserver_dialog);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, matrix_homeserver_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, matrix_accept_button);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, matrix_cancel_button);

  gtk_widget_class_bind_template_callback (widget_class, chatty_settings_add_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, chatty_settings_save_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_pp_details_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_pp_details_delete_cb);
  gtk_widget_class_bind_template_callback (widget_class, account_list_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, chatty_settings_back_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, chatty_settings_cancel_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_new_detail_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_protocol_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_pw_entry_icon_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_homeserver_entry_changed);
}

static void
chatty_settings_dialog_init (ChattySettingsDialog *self)
{
  ChattyManager *manager;

  manager = chatty_manager_get_default ();

  g_signal_connect_object (G_OBJECT (chatty_manager_get_accounts (manager)),
                           "items-changed",
                           G_CALLBACK (chatty_settings_dialog_populate_account_list),
                           self, G_CONNECT_SWAPPED);

  gtk_widget_init_template (GTK_WIDGET (self));
  gtk_window_set_transient_for (GTK_WINDOW (self->matrix_homeserver_dialog), GTK_WINDOW (self));

  gtk_widget_set_visible (self->message_carbons_row,
                          chatty_manager_has_carbons_plugin (manager));
}

GtkWidget *
chatty_settings_dialog_new (GtkWindow *parent_window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (parent_window), NULL);

  return g_object_new (CHATTY_TYPE_SETTINGS_DIALOG,
                       "transient-for", parent_window,
                       "use-header-bar", 1,
                       NULL);
}
