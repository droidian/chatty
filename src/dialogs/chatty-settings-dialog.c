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

#include <glib/gi18n.h>

#include "chatty-config.h"
#include "chatty-utils.h"
#include "users/chatty-pp-account.h"
#include "chatty-manager.h"
#include "chatty-icons.h"
#include "chatty-avatar.h"
#include "chatty-settings.h"
#include "chatty-settings-dialog.h"

/**
 * @short_description: Chatty settings Dialog
 */

/* Several code has been copied from chatty-dialogs.c with modifications
 * which was written by Andrea Schäfer. */
struct _ChattySettingsDialog
{
  GtkDialog       parent_instance;

  GtkWidget      *add_button;
  GtkWidget      *save_button;

  GtkWidget      *main_stack;
  GtkWidget      *accounts_list_box;
  GtkWidget      *add_account_row;

  GtkWidget      *avatar_button;
  GtkWidget      *avatar_image;
  GtkWidget      *account_id_entry;
  GtkWidget      *account_protocol_label;
  GtkWidget      *status_label;
  GtkWidget      *password_entry;
  GtkWidget      *edit_password_button;

  GtkWidget      *protocol_list_group;
  GtkWidget      *protocol_list;
  GtkWidget      *xmpp_radio_button;
  GtkWidget      *matrix_row;
  GtkWidget      *matrix_radio_button;
  GtkWidget      *telegram_row;
  GtkWidget      *telegram_radio_button;
  GtkWidget      *new_account_settings_list;
  GtkWidget      *new_account_id_entry;
  GtkWidget      *server_url_entry;
  GtkWidget      *new_password_entry;

  GtkWidget      *fingerprint_list;
  GtkWidget      *fingerprint_device_list;

  GtkWidget      *send_receipts_switch;
  GtkWidget      *message_archive_switch;
  GtkWidget      *message_carbons_row;
  GtkWidget      *message_carbons_switch;
  GtkWidget      *typing_notification_switch;

  GtkWidget      *indicate_offline_switch;
  GtkWidget      *indicate_idle_switch;
  GtkWidget      *indicate_unknown_switch;

  GtkWidget      *convert_smileys_switch;
  GtkWidget      *return_sends_switch;

  ChattySettings *settings;
  ChattyAccount  *selected_account;

  gboolean visible;
};

G_DEFINE_TYPE (ChattySettingsDialog, chatty_settings_dialog, GTK_TYPE_DIALOG)


static void
get_fp_list_own_cb (int         err,
                    GHashTable *id_fp_table,
                    gpointer    user_data)
{
  GList       *key_list = NULL;
  GList       *filtered_list = NULL;
  const GList *curr_p = NULL;
  const char  *fp = NULL;
  GtkWidget   *row;

  ChattySettingsDialog *self = (ChattySettingsDialog *)user_data;

  if (err || !id_fp_table) {
    gtk_widget_hide (GTK_WIDGET(self->fingerprint_list));
    gtk_widget_hide (GTK_WIDGET(self->fingerprint_device_list));

    return;
  }

  if (self->fingerprint_list && self->fingerprint_device_list) {

    key_list = g_hash_table_get_keys (id_fp_table);

    for (curr_p = key_list; curr_p; curr_p = curr_p->next) {
      fp = (char *) g_hash_table_lookup(id_fp_table, curr_p->data);

      if (fp) {
        filtered_list = g_list_append (filtered_list, curr_p->data);
      }
    }

    gtk_container_foreach (GTK_CONTAINER (self->fingerprint_list),
                           (GtkCallback)gtk_widget_destroy, NULL);
    gtk_container_foreach (GTK_CONTAINER (self->fingerprint_device_list),
                           (GtkCallback)gtk_widget_destroy, NULL);

    for (curr_p = filtered_list; curr_p; curr_p = curr_p->next) {
      fp = (char *) g_hash_table_lookup(id_fp_table, curr_p->data);

      g_debug ("DeviceId: %i fingerprint: %s", *((guint32 *) curr_p->data),
               fp ? fp : "(no session)");

      row = chatty_utils_create_fingerprint_row (fp, *((guint32 *) curr_p->data));

      if (row) {
        if (curr_p == g_list_first (filtered_list)) {
          gtk_container_add (GTK_CONTAINER(self->fingerprint_list), row);
        } else {
          gtk_container_add (GTK_CONTAINER(self->fingerprint_device_list), row);
        }
      }
    }

    if (g_list_length (filtered_list) == 1) {
      gtk_widget_hide (GTK_WIDGET(self->fingerprint_device_list));
    }
  }

  g_list_free (key_list);
  g_list_free (filtered_list);
}


static void
chatty_settings_dialog_get_fp_list_own (ChattySettingsDialog *self,
                                        PurpleAccount        *account)
{
  void * plugins_handle = purple_plugins_get_handle();

  purple_signal_emit (plugins_handle,
                      "lurch-fp-list",
                      account,
                      get_fp_list_own_cb,
                      self);
}


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
settings_update_account_details (ChattySettingsDialog *self)
{
  ChattyAccount *account;
  const char *account_name, *protocol_name;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  account = self->selected_account;
  account_name = chatty_account_get_username (CHATTY_ACCOUNT (account));
  protocol_name = chatty_account_get_protocol_name (CHATTY_ACCOUNT (account));

  gtk_entry_set_text (GTK_ENTRY (self->account_id_entry), account_name);
  gtk_label_set_text (GTK_LABEL (self->account_protocol_label), protocol_name);
  chatty_avatar_set_item (CHATTY_AVATAR (self->avatar_image), CHATTY_ITEM (account));
}

static void
chatty_settings_add_clicked_cb (ChattySettingsDialog *self)
{
  ChattyManager *manager;
  g_autoptr (ChattyAccount) account = NULL;
  const char *user_id, *password, *server_url;
  gboolean is_matrix, is_telegram;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  manager  = chatty_manager_get_default ();
  user_id  = gtk_entry_get_text (GTK_ENTRY (self->new_account_id_entry));
  password = gtk_entry_get_text (GTK_ENTRY (self->new_password_entry));
  server_url = gtk_entry_get_text (GTK_ENTRY (self->server_url_entry));

  is_matrix = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->matrix_radio_button));
  is_telegram = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->telegram_radio_button));

  if (is_matrix)
    account = (ChattyAccount *)chatty_pp_account_new (CHATTY_PROTOCOL_MATRIX, user_id, server_url);
  else if (is_telegram)
    account = (ChattyAccount *)chatty_pp_account_new (CHATTY_PROTOCOL_TELEGRAM, user_id, NULL);
  else /* XMPP */
    account = (ChattyAccount *)chatty_pp_account_new (CHATTY_PROTOCOL_XMPP, user_id, NULL);

  if (password)
    {
      chatty_account_set_password (CHATTY_ACCOUNT (account), password);

      if (!is_telegram)
        chatty_account_set_remember_password (CHATTY_ACCOUNT (account), TRUE);
    }

  chatty_account_save (account);

  if (!chatty_manager_get_disable_auto_login (manager))
    chatty_account_set_enabled (CHATTY_ACCOUNT (account), TRUE);

  gtk_widget_hide (self->add_button);
  gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack), "main-settings");
}

static void
chatty_settings_save_clicked_cb (ChattySettingsDialog *self)
{
  GtkEntry *password_entry;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  chatty_account_set_username (self->selected_account,
                               gtk_entry_get_text (GTK_ENTRY (self->account_id_entry)));

  password_entry = (GtkEntry *)self->password_entry;
  chatty_account_set_password (CHATTY_ACCOUNT (self->selected_account),
                               gtk_entry_get_text (password_entry));

  chatty_account_set_remember_password (CHATTY_ACCOUNT (self->selected_account), TRUE);
  chatty_account_set_enabled (CHATTY_ACCOUNT (self->selected_account), TRUE);

  gtk_widget_hide (self->save_button);
  gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack), "main-settings");
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
settings_update_new_account_view (ChattySettingsDialog *self)
{
  PurplePlugin *protocol;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  gtk_entry_set_text (GTK_ENTRY (self->new_account_id_entry), "");
  gtk_entry_set_text (GTK_ENTRY (self->new_password_entry), "");
  gtk_entry_set_text (GTK_ENTRY (self->server_url_entry), "");

  self->selected_account = NULL;
  gtk_widget_grab_focus (self->new_account_id_entry);
  gtk_widget_show (self->add_button);

  protocol = purple_find_prpl ("prpl-matrix");
  gtk_widget_set_visible (self->matrix_row, protocol != NULL);

  protocol = purple_find_prpl ("prpl-telegram");
  gtk_widget_set_visible (self->telegram_row, protocol != NULL);

  if (gtk_widget_get_visible (self->matrix_row) ||
      gtk_widget_get_visible (self->telegram_row))
    {
      hdy_preferences_group_set_title (HDY_PREFERENCES_GROUP (self->protocol_list_group),
                                       _("Select Protocol"));
      gtk_widget_show (self->protocol_list);
    }
  else
    {
      hdy_preferences_group_set_title (HDY_PREFERENCES_GROUP (self->protocol_list_group),
                                       _("Add XMPP account"));
      gtk_widget_hide (self->protocol_list);
    }

  gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack), "add-account-view");
}

static void
chatty_settings_dialog_update_status (GtkListBoxRow *row)
{
  ChattySettingsDialog *self;
  ChattyAccount *account;
  GtkWidget *dialog;
  GtkSpinner *spinner;
  ChattyStatus status;
  const gchar *status_text;

  g_assert (GTK_IS_LIST_BOX_ROW (row));

  dialog = gtk_widget_get_toplevel (GTK_WIDGET (row));
  self = CHATTY_SETTINGS_DIALOG (dialog);

  spinner = g_object_get_data (G_OBJECT(row), "row-prefix");
  account = g_object_get_data (G_OBJECT (row), "row-account");
  status  = chatty_account_get_status (CHATTY_ACCOUNT (account));

  g_object_set (spinner,
                "active", status == CHATTY_CONNECTING,
                NULL);

  if (status == CHATTY_CONNECTED)
    status_text = _("connected");
  else if (status == CHATTY_CONNECTING)
    status_text = _("connecting…");
  else
    status_text = _("disconnected");

  if (self->selected_account == account)
    gtk_label_set_text (GTK_LABEL (self->status_label), status_text);
}

static void
account_list_row_activated_cb (ChattySettingsDialog *self,
                               GtkListBoxRow        *row,
                               GtkListBox           *box)
{
  ChattyManager *manager;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));
  g_assert (GTK_IS_LIST_BOX_ROW (row));
  g_assert (GTK_IS_LIST_BOX (box));

  manager = chatty_manager_get_default ();
  gtk_widget_set_sensitive (self->add_button, FALSE);
  gtk_widget_set_sensitive (self->save_button, FALSE);
  gtk_widget_set_sensitive (self->password_entry, FALSE);

  if (GTK_WIDGET (row) == self->add_account_row)
    {
      settings_update_new_account_view (self);
    }
  else
    {
      gtk_widget_show (self->save_button);
      self->selected_account = g_object_get_data (G_OBJECT (row), "row-account");
      g_assert (self->selected_account != NULL);

      chatty_settings_dialog_update_status (row);
      gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack),
                                        "edit-account-view");

      if (chatty_manager_lurch_plugin_is_loaded (manager) &&
          chatty_item_get_protocols (CHATTY_ITEM (self->selected_account)) == CHATTY_PROTOCOL_XMPP)
        {
          PurpleAccount *account;

          account = chatty_pp_account_get_account (CHATTY_PP_ACCOUNT (self->selected_account));
          gtk_widget_show (GTK_WIDGET (self->fingerprint_list));
          gtk_widget_show (GTK_WIDGET (self->fingerprint_device_list));
          chatty_settings_dialog_get_fp_list_own (self, account);
        }

      settings_update_account_details (self);
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

static char *
settings_show_dialog_load_avatar (void)
{
  GtkFileChooserNative *dialog;
  GtkWindow *window;
  char *file_name = NULL;
  int response;

  window = gtk_application_get_active_window (GTK_APPLICATION (g_application_get_default ()));

  dialog = gtk_file_chooser_native_new (_("Set Avatar"),
                                        window,
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        _("Open"),
                                        _("Cancel"));

  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dialog), getenv ("HOME"));

  // TODO: add preview widget when available in portrait mode

  response = gtk_native_dialog_run (GTK_NATIVE_DIALOG(dialog));

  if (response == GTK_RESPONSE_ACCEPT)
    file_name = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(dialog));

  g_object_unref (dialog);

  return file_name;
}

static void
settings_avatar_button_clicked_cb (ChattySettingsDialog *self)
{
  g_autofree char *file_name = NULL;

  file_name = settings_show_dialog_load_avatar ();

  if (file_name)
    chatty_item_set_avatar_async (CHATTY_ITEM (self->selected_account),
                                  file_name, NULL, NULL, NULL);
}

static void
settings_account_id_changed_cb (ChattySettingsDialog *self)
{
  const gchar *id;
  ChattyProtocol protocol;

  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  id = gtk_entry_get_text (GTK_ENTRY (self->new_account_id_entry));

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->matrix_radio_button)))
    protocol = CHATTY_PROTOCOL_MATRIX;
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->telegram_radio_button)))
    protocol = CHATTY_PROTOCOL_TELEGRAM;
  else
    protocol = CHATTY_PROTOCOL_XMPP;

  gtk_widget_set_sensitive (self->add_button, chatty_utils_username_is_valid (id, protocol));
}

static void
settings_edit_password_clicked_cb (ChattySettingsDialog *self)
{
  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));

  gtk_widget_set_sensitive (self->save_button, TRUE);
  gtk_widget_set_sensitive (self->password_entry, TRUE);
  gtk_widget_grab_focus (self->password_entry);
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

  response = gtk_dialog_run (GTK_DIALOG(dialog));

  if (response == GTK_RESPONSE_OK)
    {
      ChattyAccount *account;

      account = g_steal_pointer (&self->selected_account);
      chatty_account_delete (account);

      chatty_settings_dialog_populate_account_list (self);
      gtk_widget_hide (self->save_button);
      gtk_stack_set_visible_child_name (GTK_STACK (self->main_stack), "main-settings");
    }

  gtk_widget_destroy (dialog);
}

static void
settings_protocol_changed_cb (ChattySettingsDialog *self,
                              GtkWidget            *button)
{
  g_assert (CHATTY_IS_SETTINGS_DIALOG (self));
  g_assert (GTK_IS_TOGGLE_BUTTON (button));

  /* Show URL entry only for Matrix accounts */
  gtk_widget_set_visible (self->server_url_entry, button == self->matrix_radio_button);

  if (button == self->xmpp_radio_button)
    gtk_entry_set_text (GTK_ENTRY (self->server_url_entry), "");
  else if (button == self->matrix_radio_button)
    gtk_entry_set_text (GTK_ENTRY (self->server_url_entry), "https://chat.librem.one");

  gtk_widget_grab_focus (self->new_account_id_entry);

  /* Force re-check if id is valid */
  settings_account_id_changed_cb (self);
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

  g_object_bind_property (settings, "greyout-offline-buddies",
                          self->indicate_offline_switch, "active",
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

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, add_button);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, save_button);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, main_stack);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, accounts_list_box);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, add_account_row);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, avatar_button);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, avatar_image);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, account_id_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, account_protocol_label);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, status_label);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, password_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, edit_password_button);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, protocol_list_group);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, protocol_list);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, xmpp_radio_button);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, matrix_row);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, matrix_radio_button);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, telegram_row);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, telegram_radio_button);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, new_account_settings_list);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, new_account_id_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, server_url_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, new_password_entry);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, fingerprint_list);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, fingerprint_device_list);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, send_receipts_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, message_archive_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, message_carbons_row);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, message_carbons_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, typing_notification_switch);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, indicate_offline_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, indicate_idle_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, indicate_unknown_switch);

  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, convert_smileys_switch);
  gtk_widget_class_bind_template_child (widget_class, ChattySettingsDialog, return_sends_switch);

  gtk_widget_class_bind_template_callback (widget_class, chatty_settings_add_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, chatty_settings_save_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, account_list_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, chatty_settings_back_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_avatar_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_account_id_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_edit_password_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_delete_account_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_protocol_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, settings_pw_entry_icon_clicked_cb);
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
