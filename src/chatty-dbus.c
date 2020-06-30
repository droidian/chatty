/*
 * Copyright (C) 2019 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-dbus"

#include <glib.h>
#include <gtk/gtk.h>
#include "chatty-dbus.h"


static void
cb_action_group (GActionGroup *action_group,
                 gchar        *action_name,
                 gpointer      user_data)
{
  if (g_strcmp0 (action_name, "new-contact-data") != 0)
    return;

  g_debug ("Call new-contact-data action %s", action_name);

  g_action_group_activate_action (action_group, action_name, (GVariant *) user_data);
  g_variant_unref ((GVariant *) user_data);
}


static void
cb_bus_connected (GObject      *source_object,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  g_autoptr (GDBusConnection) connection = NULL;
  g_autoptr (GDBusActionGroup) action_group = NULL;
  GError *error = NULL;

  connection = g_bus_get_finish (res, &error);

  if (connection == NULL || error != NULL) {
    g_debug ("%s Could not get dbus connection: %s", __func__, error->message);
    g_error_free (error);
    
    return;
  }

  action_group = g_dbus_action_group_get (connection,
                                          "org.gnome.Contacts",
                                          "/org/gnome/Contacts");

  g_signal_connect (action_group, "action-added", (GCallback) cb_action_group, user_data);

  // For some reason we need to ask for the full list or "action-added" isn't triggered
  g_action_group_list_actions (G_ACTION_GROUP (action_group));
}


void
chatty_dbus_gc_write_contact (const char *contact_name,
                              const char *phone_number)
{
  GVariant *contact;

  contact =  g_variant_new_parsed ("[('full-name', %s), ('phone-numbers', %s)]",
                                   contact_name,
                                   phone_number);
  g_variant_ref_sink (contact);

  g_bus_get (G_BUS_TYPE_SESSION,
             NULL,
             (GAsyncReadyCallback) cb_bus_connected,
             contact);
}
