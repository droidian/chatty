/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include <glib/gi18n.h>
#include "chatty-purple-request.h"
#include "chatty-manager.h"


static void
cb_action_response (GtkDialog         *dialog,
                    gint               id,
                    ChattyRequestData *data)
{
  if (id >= 0 && (gsize)id < data->cb_count && data->cbs[id] != NULL) {
    ((PurpleRequestActionCb)data->cbs[id])(data->user_data, id);
  }

  purple_request_close (PURPLE_REQUEST_INPUT, data);
}


static void
cb_file_exists (GtkWidget         *widget,
                gint               response,
                ChattyRequestData *data)
{
  if (response != GTK_RESPONSE_ACCEPT) {
    if (data->cbs[0] != NULL) {
      ((PurpleRequestFileCb)data->cbs[0])(data->user_data, NULL);
    }

    purple_request_close (data->type, data);

    return;
  }

  data->file_name = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(data->dialog));

  if (data->cbs[1] != NULL) {
    ((PurpleRequestFileCb)data->cbs[1])(data->user_data, data->file_name);
  }

  purple_request_close (data->type, data);
}


static void *
chatty_request_action (const char         *title,
                       const char         *primary,
                       const char         *secondary,
                       int                 default_action,
                       PurpleAccount      *account,
                       const char         *who,
                       PurpleConversation *conv,
                       void               *user_data,
                       size_t              action_count,
                       va_list             actions)
{
  ChattyRequestData  *data;
  GtkWindow          *window;
  GtkWidget          *dialog;
  void              **buttons;
  gsize               i;

  if (account || conv || who) {
    // we only handle libpurple request-actions
    // for certificates
    return NULL;
  }

  data            = g_new0 (ChattyRequestData, 1);
  data->type      = PURPLE_REQUEST_ACTION;
  data->user_data = user_data;
  data->cb_count  = action_count;
  data->cbs       = g_new0 (GCallback, action_count);

  window = gtk_application_get_active_window (GTK_APPLICATION (g_application_get_default ()));

  dialog = gtk_message_dialog_new (window,
                                   GTK_DIALOG_MODAL,
                                   GTK_MESSAGE_INFO,
                                   GTK_BUTTONS_NONE,
                                   "%s", primary ? primary : title);

  g_signal_connect (G_OBJECT(dialog),
                    "response",
                    G_CALLBACK(cb_action_response),
                    data);

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog),
                                            "%s",
                                            secondary);

  buttons = g_new0 (void *, action_count * 2);

  for (i = 0; i < action_count * 2; i += 2) {
    buttons[(action_count * 2) - i - 2] = va_arg(actions, char *);
    buttons[(action_count * 2) - i - 1] = va_arg(actions, GCallback);
  }

  for (i = 0; i < action_count; i++) {
    gtk_dialog_add_button (GTK_DIALOG(dialog), buttons[2 * i], i);

    data->cbs[i] = buttons[2 * i + 1];
  }

  g_free (buttons);

  gtk_dialog_set_default_response (GTK_DIALOG(dialog), action_count - 1 - default_action);

  window = gtk_application_get_active_window (GTK_APPLICATION (g_application_get_default ()));
  gtk_window_set_transient_for (GTK_WINDOW(dialog), window);

  gtk_widget_show_all (dialog);

  data->dialog = dialog;

  return data;
}


static void
chatty_request_close (PurpleRequestType  type,
                      void              *ui_handle)
{
  ChattyRequestData *data = (ChattyRequestData *)ui_handle;

  if (!data)
    return;

  g_free(data->cbs);
  g_free(data->file_name);

  gtk_widget_destroy (data->dialog);

  g_free (data);
}


static void *
chatty_request_file (const char         *title,
                     const char         *filename,
                     gboolean            save_dialog,
                     GCallback           ok_cb,
                     GCallback           cancel_cb,
                     PurpleAccount      *account,
                     const char         *who,
                     PurpleConversation *conv,
                     void               *user_data)
{
  ChattyRequestData *data;
  GtkWindow         *window;
  GtkWidget         *dialog;

  data = g_new0 (ChattyRequestData, 1);
  data->type = PURPLE_REQUEST_FILE;
  data->user_data = user_data;
  data->cb_count = 2;
  data->cbs = g_new0 (GCallback, 2);
  data->cbs[0] = cancel_cb;
  data->cbs[1] = ok_cb;
  data->save_dialog = save_dialog;

  window = gtk_application_get_active_window (GTK_APPLICATION (g_application_get_default ()));

  dialog = gtk_file_chooser_dialog_new (title ? title : (save_dialog ? _("Save File...") :
                                          _("Open File...")),
                                        window,
                                        save_dialog ? GTK_FILE_CHOOSER_ACTION_SAVE :
                                          GTK_FILE_CHOOSER_ACTION_OPEN,
                                        _("Cancel"),
                                        GTK_RESPONSE_CANCEL,
                                        save_dialog ? _("Save") : _("Open"),
                                        GTK_RESPONSE_ACCEPT,
                                        NULL);

  gtk_dialog_set_default_response (GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

  if ((filename != NULL) && (*filename != '\0')) {
    if (save_dialog) {
      gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER(dialog), filename);
    } else if (g_file_test(filename, G_FILE_TEST_EXISTS)) {
      gtk_file_chooser_set_filename (GTK_FILE_CHOOSER(dialog), filename);
    }
  }

  g_signal_connect (G_OBJECT(GTK_FILE_CHOOSER(dialog)),
                    "response",
                    G_CALLBACK(cb_file_exists),
                    data);

  window = gtk_application_get_active_window (GTK_APPLICATION (g_application_get_default ()));
  gtk_window_set_transient_for (GTK_WINDOW(dialog), window);

  data->dialog = dialog;

  gtk_widget_show (dialog);

  return (void *)data;
}


static PurpleRequestUiOps ops =
{
  NULL,
  NULL,
  chatty_request_action,
  NULL,
  chatty_request_file,
  chatty_request_close,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};


PurpleRequestUiOps *
chatty_request_get_ui_ops(void)
{
  return &ops;
}
