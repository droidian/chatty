/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include <glib.h>
#include <glib/gi18n.h>
#include "chatty-popover-actions.h"
#include "chatty-window.h"
#include "chatty-buddy-list.h"

static void
view_chat_list_cmd_group_chat (GSimpleAction *action,
                               GVariant      *parameter,
                               gpointer       user_data)
{
  chatty_window_change_view (CHATTY_VIEW_JOIN_CHAT);
}


static void
view_chat_list_cmd_direct_chat (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
  chatty_window_change_view (CHATTY_VIEW_NEW_CHAT);
}


static void
view_chat_list_cmd_settings (GSimpleAction *action,
                             GVariant      *parameter,
                             gpointer       user_data)
{
  chatty_window_change_view (CHATTY_VIEW_SETTINGS);
}


static void
view_msg_list_cmd_delete (GSimpleAction *action,
                          GVariant      *parameter,
                          gpointer       user_data)
{
  chatty_blist_chat_list_remove_buddy ();
}

static void
view_msg_list_cmd_chat_info (GSimpleAction *action,
                             GVariant      *parameter,
                             gpointer       user_data)
{
  chatty_window_change_view (CHATTY_VIEW_CHAT_INFO);
}


static const GActionEntry view_chat_list_entries [] =
{
  { "group-chat", view_chat_list_cmd_group_chat },
  { "direct-chat", view_chat_list_cmd_direct_chat },
  { "settings", view_chat_list_cmd_settings }
};


static const GActionEntry view_msg_list_entries [] =
{
  { "delete-chat", view_msg_list_cmd_delete },
  { "chat-info", view_msg_list_cmd_chat_info }
};


void
chatty_popover_actions_init (GtkWindow *window)
{
  GSimpleActionGroup *simple_action_group;

  simple_action_group = g_simple_action_group_new ();
  g_action_map_add_action_entries (G_ACTION_MAP (simple_action_group),
                                   view_chat_list_entries,
                                   G_N_ELEMENTS (view_chat_list_entries),
                                   window);

  gtk_widget_insert_action_group (GTK_WIDGET (window),
                                  "chat_list",
                                  G_ACTION_GROUP (simple_action_group));

  simple_action_group = g_simple_action_group_new ();
  g_action_map_add_action_entries (G_ACTION_MAP (simple_action_group),
                                   view_msg_list_entries,
                                   G_N_ELEMENTS (view_msg_list_entries),
                                   window);

  gtk_widget_insert_action_group (GTK_WIDGET (window),
                                  "msg_view",
                                  G_ACTION_GROUP (simple_action_group));
}
