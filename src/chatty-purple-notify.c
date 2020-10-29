/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */



#include <glib/gi18n.h>
#include "chatty-purple-notify.h"
#include "chatty-application.h"
#include "chatty-utils.h"


static void
cb_message_response_cb (GtkDialog *dialog,
                        gint       id,
                        GtkWidget *widget)
{
  purple_notify_close (PURPLE_NOTIFY_MESSAGE, widget);
}


static void *
chatty_notify_message (PurpleNotifyMsgType  type,
                       const char          *title,
                       const char          *primary,
                       const char          *secondary)
{
  GtkApplication *app;
  GtkWindow *window;
  GtkWidget *dialog;

  app = GTK_APPLICATION (g_application_get_default ());
  window = gtk_application_get_active_window (app);

  dialog = gtk_message_dialog_new (window,
                                   GTK_DIALOG_MODAL,
                                   GTK_MESSAGE_INFO,
                                   GTK_BUTTONS_NONE,
                                   "%s", primary ? primary : title);

  gtk_dialog_add_button (GTK_DIALOG(dialog),
                         _("Close"),
                         GTK_RESPONSE_CLOSE);

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog),
                                            "%s",
                                            secondary);

  g_signal_connect (G_OBJECT(dialog),
                    "response",
                    G_CALLBACK(cb_message_response_cb),
                    dialog);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), window);

  gtk_widget_show_all (dialog);

  return dialog;
}



static void
chatty_close_notify (PurpleNotifyType  type,
                     void             *ui_handle)
{
  gtk_widget_destroy (GTK_WIDGET(ui_handle));
}


static PurpleNotifyUiOps ops =
{
  chatty_notify_message,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  chatty_close_notify,
  NULL,
  NULL,
  NULL,
  NULL
};


PurpleNotifyUiOps *
chatty_notify_get_ui_ops (void)
{
  return &ops;
}
