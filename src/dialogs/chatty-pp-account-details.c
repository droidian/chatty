/* -*- mode: c; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* chatty-pp-account-details.c
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

#define G_LOG_DOMAIN "chatty-pp-account-details"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>

#include "chatty-avatar.h"
#include "chatty-fp-row.h"
#include "matrix/chatty-ma-account.h"
#include "chatty-pp-account-details.h"
#include "chatty-log.h"

struct _ChattyPpAccountDetails
{
  HdyPreferencesPage parent_instance;

  ChattyAccount *account;

  GtkWidget     *avatar_image;
  GtkWidget     *account_id_label;
  GtkWidget     *account_protocol_label;
  GtkWidget     *status_label;
  GtkWidget     *password_entry;
  GtkWidget     *delete_account_button;

  GtkWidget     *device_fp;
  GtkWidget     *device_fp_list;

  guint          modified : 1;
  gulong         status_id;
};

enum {
  DELETE_CLICKED,
  N_SIGNALS
};

enum {
  PROP_0,
  PROP_MODIFIED,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_TYPE (ChattyPpAccountDetails, chatty_pp_account_details, HDY_TYPE_PREFERENCES_PAGE)

static char *
pp_account_show_dialog_load_avatar (ChattyPpAccountDetails *self)
{
  g_autoptr(GtkFileChooserNative) dialog = NULL;
  GtkWidget *window;
  int response;

  window = gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
  dialog = gtk_file_chooser_native_new (_("Set Avatar"),
                                        GTK_WINDOW (window),
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        _("Open"),
                                        _("Cancel"));

  response = gtk_native_dialog_run (GTK_NATIVE_DIALOG (dialog));

  if (response == GTK_RESPONSE_ACCEPT)
    return gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

  return NULL;
}

static void
pp_details_avatar_button_clicked_cb (ChattyPpAccountDetails *self)
{
  g_autofree char *file_name = NULL;

  file_name = pp_account_show_dialog_load_avatar (self);

  if (file_name)
    chatty_item_set_avatar_async (CHATTY_ITEM (self->account),
                                  file_name, NULL, NULL, NULL);
}

static void
pa_details_pw_entry_icon_clicked_cb (ChattyPpAccountDetails *self)
{
  GtkEntry *entry;
  const char *icon;
  gboolean visible;

  g_assert (CHATTY_IS_PP_ACCOUNT_DETAILS (self));

  entry = GTK_ENTRY (self->password_entry);
  visible = !gtk_entry_get_visibility (entry);
  gtk_entry_set_visibility (entry, visible);

  if (visible)
    icon = "eye-open-negative-filled-symbolic";
  else
    icon = "eye-not-looking-symbolic";

  gtk_entry_set_icon_from_icon_name (entry, GTK_ENTRY_ICON_SECONDARY, icon);
}

static void
pa_details_pw_entry_changed_cb (ChattyPpAccountDetails *self)
{
  g_assert (CHATTY_IS_PP_ACCOUNT_DETAILS (self));

  self->modified = TRUE;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODIFIED]);
}

static void
pa_details_delete_account_clicked_cb (ChattyPpAccountDetails *self)
{
  g_assert (CHATTY_IS_PP_ACCOUNT_DETAILS (self));

  g_signal_emit (self, signals[DELETE_CLICKED], 0);
}

static void
pp_details_status_changed_cb (ChattyPpAccountDetails *self)
{
  const char *status_text;
  ChattyStatus status;

  g_assert (CHATTY_IS_PP_ACCOUNT_DETAILS (self));

  status = chatty_account_get_status (self->account);

  if (status == CHATTY_CONNECTED)
    status_text = _("connected");
  else if (status == CHATTY_CONNECTING)
    status_text = _("connecting…");
  else
    status_text = _("disconnected");

  gtk_label_set_text (GTK_LABEL (self->status_label), status_text);
}

static void
pp_details_get_fingerprints_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  g_autoptr(ChattyPpAccountDetails) self = user_data;
  ChattyAccount *account = CHATTY_ACCOUNT (object);
  g_autoptr(GError) error = NULL;
  GListModel *fp_list;
  HdyValueObject *device_fp;

  chatty_account_load_fp_finish (account, result, &error);

  if (error) {
    g_warning ("error: %s", error->message);
    return;
  }

  device_fp = chatty_account_get_device_fp (account);
  fp_list = chatty_account_get_fp_list (account);

  gtk_widget_set_visible (self->device_fp_list, !!device_fp);
  gtk_widget_set_visible (self->device_fp, !!device_fp);

  gtk_widget_set_visible (self->device_fp_list,
                          fp_list && g_list_model_get_n_items (fp_list));

  gtk_list_box_bind_model (GTK_LIST_BOX (self->device_fp_list),
                           fp_list,
                           (GtkListBoxCreateWidgetFunc) chatty_fp_row_new,
                           NULL, NULL);
  if (device_fp)
    chatty_fp_row_set_item (CHATTY_FP_ROW (self->device_fp), device_fp);
}

static void
chatty_pp_account_details_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  ChattyPpAccountDetails *self = (ChattyPpAccountDetails *)object;

  switch (prop_id)
    {
    case PROP_MODIFIED:
      g_value_set_boolean (value, self->modified);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
chatty_pp_account_details_finalize (GObject *object)
{
  ChattyPpAccountDetails *self = (ChattyPpAccountDetails *)object;

  g_clear_object (&self->account);

  G_OBJECT_CLASS (chatty_pp_account_details_parent_class)->finalize (object);
}

static void
chatty_pp_account_details_class_init (ChattyPpAccountDetailsClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = chatty_pp_account_details_get_property;
  object_class->finalize = chatty_pp_account_details_finalize;

  signals [DELETE_CLICKED] =
    g_signal_new ("delete-clicked",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  properties[PROP_MODIFIED] =
    g_param_spec_boolean ("modified",
                          "Modified",
                          "If Settings is modified",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-pp-account-details.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyPpAccountDetails, avatar_image);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpAccountDetails, account_id_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpAccountDetails, account_protocol_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpAccountDetails, status_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpAccountDetails, password_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpAccountDetails, delete_account_button);

  gtk_widget_class_bind_template_child (widget_class, ChattyPpAccountDetails, device_fp);
  gtk_widget_class_bind_template_child (widget_class, ChattyPpAccountDetails, device_fp_list);

  gtk_widget_class_bind_template_callback (widget_class, pp_details_avatar_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, pa_details_pw_entry_icon_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, pa_details_pw_entry_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, pa_details_delete_account_clicked_cb);
}

static void
chatty_pp_account_details_init (ChattyPpAccountDetails *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
chatty_pp_account_details_new (void)
{
  return g_object_new (CHATTY_TYPE_PP_ACCOUNT_DETAILS, NULL);
}

void
chatty_pp_account_save_async (ChattyPpAccountDetails *self,
                              GAsyncReadyCallback     callback,
                              gpointer                user_data)
{
  g_autoptr(GTask) task = NULL;
  GtkEntry *entry;

  g_return_if_fail (CHATTY_IS_PP_ACCOUNT_DETAILS (self));
  g_return_if_fail (callback);

  entry = (GtkEntry *)self->password_entry;
  chatty_account_set_password (self->account, gtk_entry_get_text (entry));

  chatty_account_set_remember_password (self->account, TRUE);
  chatty_account_set_enabled (self->account, TRUE);

  gtk_entry_set_text (entry, "");

  self->modified = FALSE;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODIFIED]);

  task = g_task_new (self, NULL, callback, user_data);
  g_task_return_boolean (task, TRUE);
}

gboolean
chatty_pp_account_save_finish (ChattyPpAccountDetails  *self,
                               GAsyncResult            *result,
                               GError                 **error)
{
  g_return_val_if_fail (CHATTY_IS_PP_ACCOUNT_DETAILS (self), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

ChattyAccount *
chatty_pp_account_details_get_item (ChattyPpAccountDetails *self)
{
  g_return_val_if_fail (CHATTY_IS_PP_ACCOUNT_DETAILS (self), NULL);

  return self->account;
}

void
chatty_pp_account_details_set_item (ChattyPpAccountDetails *self,
                                    ChattyAccount          *account)
{
  const char *account_name, *protocol_name;

  g_return_if_fail (CHATTY_IS_PP_ACCOUNT_DETAILS (self));
  g_return_if_fail (!account || CHATTY_IS_ACCOUNT (account));

  gtk_entry_set_text (GTK_ENTRY (self->password_entry), "");

  if (self->account != account) {
    g_clear_signal_handler (&self->status_id, self->account);
    gtk_list_box_bind_model (GTK_LIST_BOX (self->device_fp_list),
                             NULL, NULL, NULL, NULL);
    gtk_widget_hide (self->device_fp_list);
    gtk_widget_hide (self->device_fp);
  }

  if (!g_set_object (&self->account, account) || !account)
    return;

  account_name = chatty_account_get_username (account);
  protocol_name = chatty_account_get_protocol_name (account);

  gtk_label_set_text (GTK_LABEL (self->account_id_label), account_name);
  gtk_label_set_text (GTK_LABEL (self->account_protocol_label), protocol_name);

  chatty_avatar_set_item (CHATTY_AVATAR (self->avatar_image), CHATTY_ITEM (account));

  self->status_id = g_signal_connect_object (self->account, "notify::status",
                                             G_CALLBACK (pp_details_status_changed_cb),
                                             self, G_CONNECT_SWAPPED);
  pp_details_status_changed_cb (self);

  if (chatty_item_get_protocols (CHATTY_ITEM (self->account)) == CHATTY_PROTOCOL_XMPP ||
      CHATTY_IS_MA_ACCOUNT (self->account))
    chatty_account_load_fp_async (self->account,
                                  pp_details_get_fingerprints_cb,
                                  g_object_ref (self));
}
