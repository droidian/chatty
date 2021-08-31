/* -*- mode: c; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* chatty-ma-account-details.c
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

#define G_LOG_DOMAIN "chatty-ma-account-details"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>

#include "chatty-avatar.h"
#include "chatty-fp-row.h"
#include "matrix/chatty-ma-account.h"
#include "chatty-ma-account-details.h"
#include "chatty-log.h"

struct _ChattyMaAccountDetails
{
  HdyPreferencesPage parent_instance;

  ChattyAccount *account;

  GtkWidget     *avatar_image;
  GtkWidget     *delete_avatar_button;
  GtkWidget     *delete_button_stack;
  GtkWidget     *delete_button_image;
  GtkWidget     *delete_avatar_spinner;

  GtkWidget     *status_label;
  GtkWidget     *name_entry;
  GtkWidget     *email_box;
  GtkWidget     *phone_box;

  GtkWidget     *homeserver_label;
  GtkWidget     *matrix_id_label;
  GtkWidget     *device_id_label;

  guint          modified : 1;
  guint          is_deleting_avatar : 1;
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

G_DEFINE_TYPE (ChattyMaAccountDetails, chatty_ma_account_details, HDY_TYPE_PREFERENCES_PAGE)

static void
update_delete_avatar_button_state (ChattyMaAccountDetails *self)
{
  GtkStack *button_stack;
  ChattyStatus status;
  gboolean has_avatar = FALSE, can_delete;

  if (CHATTY_IS_ITEM (self->account) &&
      chatty_item_get_avatar_file (CHATTY_ITEM (self->account)))
    has_avatar = TRUE;

  status = chatty_account_get_status (CHATTY_ACCOUNT (self->account));
  can_delete = has_avatar && !self->is_deleting_avatar && status == CHATTY_CONNECTED;
  gtk_widget_set_sensitive (self->delete_avatar_button, can_delete);

  button_stack = GTK_STACK (self->delete_button_stack);

  if (self->is_deleting_avatar)
    gtk_stack_set_visible_child (button_stack, self->delete_avatar_spinner);
  else
    gtk_stack_set_visible_child (button_stack, self->delete_button_image);

  g_object_set (self->delete_avatar_spinner, "active", self->is_deleting_avatar, NULL);
}

static char *
ma_account_show_dialog_load_avatar (ChattyMaAccountDetails *self)
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
ma_details_avatar_button_clicked_cb (ChattyMaAccountDetails *self)
{
  g_autofree char *file_name = NULL;

  file_name = ma_account_show_dialog_load_avatar (self);

  if (file_name)
    chatty_item_set_avatar_async (CHATTY_ITEM (self->account),
                                  file_name, NULL, NULL, NULL);
}

static void
ma_details_delete_avatar_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  g_autoptr(ChattyMaAccountDetails) self = user_data;

  self->is_deleting_avatar = FALSE;
  update_delete_avatar_button_state (self);
}

static void
ma_details_delete_avatar_button_clicked_cb (ChattyMaAccountDetails *self)
{
  g_assert (CHATTY_IS_MA_ACCOUNT_DETAILS (self));

  if (self->is_deleting_avatar)
    return;

  g_warning ("xxxxxxxxxx");
  self->is_deleting_avatar = TRUE;
  update_delete_avatar_button_state (self);
  chatty_item_set_avatar_async (CHATTY_ITEM (self->account), NULL, NULL,
                                ma_details_delete_avatar_cb,
                                g_object_ref (self));
}

static void
ma_details_name_entry_changed_cb (ChattyMaAccountDetails *self,
                                  GtkEntry               *entry)
{
  const char *old, *new;

  g_assert (CHATTY_IS_MA_ACCOUNT_DETAILS (self));
  g_assert (GTK_IS_ENTRY (entry));

  old = g_object_get_data (G_OBJECT (entry), "name");
  new = gtk_entry_get_text (entry);

  if (g_strcmp0 (old, new) == 0)
    self->modified = FALSE;
  else
    self->modified = TRUE;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODIFIED]);
}

static void
ma_details_delete_account_clicked_cb (ChattyMaAccountDetails *self)
{
  g_assert (CHATTY_IS_MA_ACCOUNT_DETAILS (self));

  g_signal_emit (self, signals[DELETE_CLICKED], 0);
}

static void
ma_account_details_update (ChattyMaAccountDetails *self)
{
  const char *name;

  g_assert (CHATTY_IS_MA_ACCOUNT_DETAILS (self));

  name = chatty_item_get_name (CHATTY_ITEM (self->account));

  g_object_set_data_full (G_OBJECT (self->name_entry),
                          "name", g_strdup (name), g_free);
  gtk_entry_set_text (GTK_ENTRY (self->name_entry), name);
  gtk_widget_set_sensitive (self->name_entry, TRUE);
}

static void
ma_account_details_get_cb (GObject      *object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  g_autoptr(ChattyMaAccountDetails) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (CHATTY_IS_MA_ACCOUNT_DETAILS (self));

  chatty_ma_account_get_details_finish (CHATTY_MA_ACCOUNT (self->account),
                                        result, &error);

  if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    g_warning ("Error getting details: %s", error->message);

  ma_account_details_update (self);
}


static void
ma_details_delete_3pid_cb (GObject      *object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  ChattyMaAccountDetails *self;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;
  GtkWidget *button, *row;
  ChattyMaAccount *account;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (CHATTY_IS_MA_ACCOUNT_DETAILS (self));

  button = g_task_get_task_data (task);
  row = gtk_widget_get_parent (button);
  account = CHATTY_MA_ACCOUNT (self->account);

  if (chatty_ma_account_delete_3pid_finish (account, result, &error))
    gtk_widget_destroy (row);
  else {
    GtkWidget *stack, *child;

    stack = gtk_bin_get_child (GTK_BIN (button));
    child = gtk_stack_get_child_by_name (GTK_STACK (stack), "spinner");
    gtk_spinner_stop (GTK_SPINNER (child));

    gtk_stack_set_visible_child_name (GTK_STACK (stack), "trash-image");
    gtk_widget_set_sensitive (GTK_WIDGET (button), TRUE);
  }
}

static void
ma_details_delete_3pid_clicked (ChattyMaAccountDetails *self,
                                GObject                *button)
{
  GtkWidget *stack, *child;
  const char *value;
  GTask *task;
  ChattyIdType type;

  g_assert (CHATTY_IS_MA_ACCOUNT_DETAILS (self));
  g_assert (GTK_IS_BUTTON (button));

  value = g_object_get_data (button, "value");
  type = GPOINTER_TO_INT (g_object_get_data (button, "type"));

  g_assert (value);
  g_assert (type);

  stack = gtk_bin_get_child (GTK_BIN (button));
  child = gtk_stack_get_child_by_name (GTK_STACK (stack), "spinner");
  gtk_stack_set_visible_child_name (GTK_STACK (stack), "trash-image");

  gtk_spinner_start (GTK_SPINNER (child));
  gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);

  task = g_task_new (self, NULL, NULL, NULL);
  g_task_set_task_data (task, g_object_ref (button), g_object_unref);
  chatty_ma_account_delete_3pid_async (CHATTY_MA_ACCOUNT (self->account),
                                       value, type, NULL,
                                       ma_details_delete_3pid_cb,
                                       task);
}

static void
ma_account_details_add_entry (ChattyMaAccountDetails *self,
                              GtkWidget              *box,
                              const char             *value)
{
  GtkWidget *entry, *row, *button, *stack, *spinner, *image;
  GtkStyleContext *context;

  row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  context = gtk_widget_get_style_context (row);
  gtk_style_context_add_class (context, "linked");
  /* gtk_style_context_add_class (context, "dim-label"); */

  entry = gtk_entry_new ();
  gtk_widget_show (entry);
  gtk_widget_set_hexpand (entry, TRUE);
  gtk_widget_set_sensitive (entry, FALSE);
  gtk_entry_set_text (GTK_ENTRY (entry), value);
  gtk_container_add (GTK_CONTAINER (row), entry);

  stack = gtk_stack_new ();
  image = gtk_image_new_from_icon_name ("user-trash-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_stack_add_named (GTK_STACK (stack), image, "trash-image");

  spinner = gtk_spinner_new ();
  gtk_stack_add_named (GTK_STACK (stack), spinner, "spinner");

  button = gtk_button_new ();
  g_object_set_data_full (G_OBJECT (button), "value", g_strdup (value), g_free);
  if (box == self->phone_box)
    g_object_set_data (G_OBJECT (button), "type", GINT_TO_POINTER (CHATTY_ID_PHONE));
  else
    g_object_set_data (G_OBJECT (button), "type", GINT_TO_POINTER (CHATTY_ID_EMAIL));
  gtk_container_add (GTK_CONTAINER (button), stack);
  gtk_container_add (GTK_CONTAINER (row), button);

  g_signal_connect_swapped (button, "clicked",
                            (GCallback)ma_details_delete_3pid_clicked, self);
  /* context = gtk_widget_get_style_context (button); */
  /* gtk_style_context_add_class (context, "dim-label"); */
  /* gtk_stack_add_named (GTK_STACK (stack), button, "delete-button"); */


  gtk_widget_show_all (row);
  gtk_container_add (GTK_CONTAINER (box), row);
}

static void
ma_account_get_3pid_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  g_autoptr(ChattyMaAccountDetails) self = user_data;
  g_autoptr(GPtrArray) emails = NULL;
  g_autoptr(GPtrArray) phones = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (CHATTY_IS_MA_ACCOUNT_DETAILS (self));

  chatty_ma_account_get_3pid_finish (CHATTY_MA_ACCOUNT (self->account),
                                     &emails, &phones, result, &error);

  if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    g_warning ("Error getting 3pid: %s", error->message);

  for (guint i = 0; emails && i < emails->len; i++)
    ma_account_details_add_entry (self, self->email_box, emails->pdata[i]);

  for (guint i = 0; phones && i < phones->len; i++)
    ma_account_details_add_entry (self, self->phone_box, phones->pdata[i]);

  gtk_widget_set_visible (self->email_box, emails && emails->len > 0);
  gtk_widget_set_visible (self->phone_box, phones && phones->len > 0);
}

static void
ma_details_status_changed_cb (ChattyMaAccountDetails *self)
{
  const char *status_text;
  ChattyStatus status;

  g_assert (CHATTY_IS_MA_ACCOUNT_DETAILS (self));

  status = chatty_account_get_status (self->account);

  if (status == CHATTY_CONNECTED)
    status_text = _("connected");
  else if (status == CHATTY_CONNECTING)
    status_text = _("connecting…");
  else
    status_text = _("disconnected");

  gtk_label_set_text (GTK_LABEL (self->status_label), status_text);
  update_delete_avatar_button_state (self);

  if (status == CHATTY_CONNECTED) {
    chatty_ma_account_get_details_async (CHATTY_MA_ACCOUNT (self->account), NULL,
                                         ma_account_details_get_cb,
                                         g_object_ref (self));
    chatty_ma_account_get_3pid_async (CHATTY_MA_ACCOUNT (self->account), NULL,
                                      ma_account_get_3pid_cb,
                                      g_object_ref (self));
  }

  gtk_label_set_text (GTK_LABEL (self->device_id_label),
                      chatty_ma_account_get_device_id (CHATTY_MA_ACCOUNT (self->account)));

}

static void
chatty_ma_account_details_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  ChattyMaAccountDetails *self = (ChattyMaAccountDetails *)object;

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
chatty_ma_account_details_finalize (GObject *object)
{
  ChattyMaAccountDetails *self = (ChattyMaAccountDetails *)object;

  g_clear_object (&self->account);

  G_OBJECT_CLASS (chatty_ma_account_details_parent_class)->finalize (object);
}

static void
chatty_ma_account_details_class_init (ChattyMaAccountDetailsClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = chatty_ma_account_details_get_property;
  object_class->finalize = chatty_ma_account_details_finalize;

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
                                               "ui/chatty-ma-account-details.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyMaAccountDetails, avatar_image);
  gtk_widget_class_bind_template_child (widget_class, ChattyMaAccountDetails, delete_avatar_button);
  gtk_widget_class_bind_template_child (widget_class, ChattyMaAccountDetails, delete_button_stack);
  gtk_widget_class_bind_template_child (widget_class, ChattyMaAccountDetails, delete_button_image);
  gtk_widget_class_bind_template_child (widget_class, ChattyMaAccountDetails, delete_avatar_spinner);

  gtk_widget_class_bind_template_child (widget_class, ChattyMaAccountDetails, status_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyMaAccountDetails, name_entry);
  gtk_widget_class_bind_template_child (widget_class, ChattyMaAccountDetails, email_box);
  gtk_widget_class_bind_template_child (widget_class, ChattyMaAccountDetails, phone_box);

  gtk_widget_class_bind_template_child (widget_class, ChattyMaAccountDetails, homeserver_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyMaAccountDetails, matrix_id_label);
  gtk_widget_class_bind_template_child (widget_class, ChattyMaAccountDetails, device_id_label);

  gtk_widget_class_bind_template_callback (widget_class, ma_details_avatar_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, ma_details_delete_avatar_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, ma_details_name_entry_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, ma_details_delete_account_clicked_cb);
}

static void
chatty_ma_account_details_init (ChattyMaAccountDetails *self)
{
  GtkWidget *clamp;

  gtk_widget_init_template (GTK_WIDGET (self));

  clamp = gtk_widget_get_ancestor (self->avatar_image, HDY_TYPE_CLAMP);

  if (clamp) {
    hdy_clamp_set_maximum_size (HDY_CLAMP (clamp), 360);
    hdy_clamp_set_tightening_threshold (HDY_CLAMP (clamp), 320);
  }
}

GtkWidget *
chatty_ma_account_details_new (void)
{
  return g_object_new (CHATTY_TYPE_MA_ACCOUNT_DETAILS, NULL);
}

static void
ma_details_set_name_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  ChattyMaAccountDetails *self;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;
  const char *name;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (CHATTY_IS_MA_ACCOUNT_DETAILS (self));

  chatty_ma_account_set_name_finish (CHATTY_MA_ACCOUNT (self->account),
                                     result, &error);

  if (error)
    g_warning ("Error setting name: %s", error->message);

  if (error)
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, TRUE);

  name = chatty_item_get_name (CHATTY_ITEM (self->account));
  g_object_set_data_full (G_OBJECT (self->name_entry),
                          "name", g_strdup (name), g_free);
  self->modified = FALSE;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODIFIED]);
}

void
chatty_ma_account_details_save_async (ChattyMaAccountDetails *self,
                                      GAsyncReadyCallback     callback,
                                      gpointer                user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (CHATTY_IS_MA_ACCOUNT_DETAILS (self));
  g_return_if_fail (callback);

  task = g_task_new (self, NULL, callback, user_data);

  if (self->modified) {
    const char *name;

    name = gtk_entry_get_text (GTK_ENTRY (self->name_entry));
    chatty_ma_account_set_name_async (CHATTY_MA_ACCOUNT (self->account),
                                      name, NULL,
                                      ma_details_set_name_cb,
                                      g_steal_pointer (&task));
  } else
    g_task_return_boolean (task, TRUE);
}

gboolean
chatty_ma_account_details_save_finish (ChattyMaAccountDetails *self,
                                       GAsyncResult           *result,
                                       GError                 **error)
{
  g_return_val_if_fail (CHATTY_IS_MA_ACCOUNT_DETAILS (self), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

ChattyAccount *
chatty_ma_account_details_get_item (ChattyMaAccountDetails *self)
{
  g_return_val_if_fail (CHATTY_IS_MA_ACCOUNT_DETAILS (self), NULL);

  return self->account;
}

void
chatty_ma_account_details_set_item (ChattyMaAccountDetails *self,
                                    ChattyAccount          *account)
{
  g_return_if_fail (CHATTY_IS_MA_ACCOUNT_DETAILS (self));
  g_return_if_fail (!account || CHATTY_IS_MA_ACCOUNT (account));

  if (self->account != account) {
    g_clear_signal_handler (&self->status_id, self->account);
    gtk_entry_set_text (GTK_ENTRY (self->name_entry), "");
    gtk_widget_hide (self->email_box);
    gtk_widget_hide (self->phone_box);

    gtk_container_foreach (GTK_CONTAINER (self->email_box),
                           (GtkCallback)gtk_widget_destroy, NULL);
    gtk_container_foreach (GTK_CONTAINER (self->phone_box),
                           (GtkCallback)gtk_widget_destroy, NULL);
  }

  if (!g_set_object (&self->account, account) || !account)
    return;

  gtk_label_set_text (GTK_LABEL (self->homeserver_label),
                      chatty_ma_account_get_homeserver (CHATTY_MA_ACCOUNT (self->account)));
  gtk_label_set_text (GTK_LABEL (self->matrix_id_label),
                      chatty_item_get_username (CHATTY_ITEM (account)));

  chatty_avatar_set_item (CHATTY_AVATAR (self->avatar_image), CHATTY_ITEM (account));

  self->status_id = g_signal_connect_object (self->account, "notify::status",
                                             G_CALLBACK (ma_details_status_changed_cb),
                                             self, G_CONNECT_SWAPPED);
  ma_details_status_changed_cb (self);
}
