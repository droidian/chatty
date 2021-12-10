/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-manager.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-manager"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define LIBFEEDBACK_USE_UNSTABLE_API
#include <libfeedback.h>
#include <glib/gi18n.h>
#include "contrib/gtk.h"

#include "chatty-settings.h"
#include "chatty-contact-provider.h"
#include "chatty-utils.h"
#include "chatty-mm-account.h"
#include "chatty-ma-account.h"
#include "chatty-chat.h"
#include "chatty-history.h"
#include "chatty-matrix.h"
#include "chatty-purple.h"
#include "chatty-manager.h"
#include "chatty-log.h"

/**
 * SECTION: chatty-manager
 * @title: ChattyManager
 * @short_description: A class to manage various providers and accounts
 * @include: "chatty-manager.h"
 */

struct _ChattyManager
{
  GObject          parent_instance;

  ChattyHistory   *history;
  ChattyEds       *chatty_eds;

  GtkFlattenListModel *accounts;
  GtkFlattenListModel *contact_list;
  GtkFlattenListModel *chat_list;

  GtkFilter           *chat_filter;
  GtkFilterListModel  *filtered_chat_list;
  GtkSortListModel    *sorted_chat_list;

  ChattyMatrix    *matrix;
#ifdef PURPLE_ENABLED
  ChattyPurple    *purple;
#endif
  /* We have exactly one MM account */
  ChattyMmAccount *mm_account;

  gboolean         disable_auto_login;
  gboolean         has_loaded;
};

G_DEFINE_TYPE (ChattyManager, chatty_manager, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_ACTIVE_PROTOCOLS,
  N_PROPS
};

enum {
  CHAT_DELETED,
  OPEN_CHAT,
  N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

static GtkFlattenListModel *
manager_new_flatten_list (GType type)
{
  g_autoptr(GListStore) list_of_list = NULL;

  list_of_list = g_list_store_new (G_TYPE_LIST_MODEL);

  return gtk_flatten_list_model_new (type, G_LIST_MODEL (list_of_list));
}

static void
manager_add_to_flat_model (GtkFlattenListModel *flatten_model,
                           GListModel          *model)
{
  GListModel *parent;

  parent = gtk_flatten_list_model_get_model (flatten_model);
  g_list_store_append (G_LIST_STORE (parent), model);
}

static int
manager_sort_chat_item (ChattyChat *a,
                        ChattyChat *b,
                        gpointer    user_data)
{
  time_t a_time, b_time;

  g_assert (CHATTY_IS_CHAT (a));
  g_assert (CHATTY_IS_CHAT (b));

  a_time = chatty_chat_get_last_msg_time (a);
  b_time = chatty_chat_get_last_msg_time (b);

  return difftime (b_time, a_time);
}

static gboolean
manager_filter_chat_item (ChattyItem *item)
{
  ChattyProtocol protocol;

  protocol = chatty_item_get_protocols (item);

  /* We always show SMS/MMS chats regardless of the modem status */
  if (protocol & (CHATTY_PROTOCOL_MMS_SMS | CHATTY_PROTOCOL_MMS))
    return TRUE;

#ifdef PURPLE_ENABLED
  if (CHATTY_IS_PP_CHAT (item) &&
      !chatty_pp_chat_get_auto_join (CHATTY_PP_CHAT (item)))
    return FALSE;
#endif

  if (CHATTY_IS_CHAT (item)) {
    ChattyAccount *account;

    account = chatty_chat_get_account (CHATTY_CHAT (item));

    if (!account || chatty_account_get_status (account) != CHATTY_CONNECTED)
      return FALSE;
  }

  return TRUE;
}

static void
manager_active_protocols_changed_cb (ChattyManager *self)
{
  g_assert (CHATTY_IS_MANAGER (self));

  gtk_filter_changed (self->chat_filter, GTK_FILTER_CHANGE_DIFFERENT);
}

static gboolean
manager_mm_set_eds (gpointer user_data)
{
  g_autoptr(ChattyManager) self = user_data;

  chatty_mm_account_set_eds (self->mm_account, self->chatty_eds);

  return G_SOURCE_REMOVE;
}

static void
manager_eds_is_ready (ChattyManager *self)
{
  g_assert (CHATTY_IS_MANAGER (self));

  /* Set eds after some timeout so that most contacts are loaded */
  g_timeout_add (200, manager_mm_set_eds, g_object_ref (self));
}

static void
chatty_manager_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  ChattyManager *self = (ChattyManager *)object;

  switch (prop_id)
    {
    case PROP_ACTIVE_PROTOCOLS:
      g_value_set_int (value, chatty_manager_get_active_protocols (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}


static void
chatty_manager_dispose (GObject *object)
{
  ChattyManager *self = (ChattyManager *)object;

  g_clear_object (&self->chatty_eds);
  g_clear_object (&self->chat_list);
  g_clear_object (&self->filtered_chat_list);
  g_clear_object (&self->sorted_chat_list);

  g_clear_object (&self->contact_list);
  g_clear_object (&self->accounts);

  G_OBJECT_CLASS (chatty_manager_parent_class)->dispose (object);
}

static void
chatty_manager_finalize (GObject *object)
{
  ChattyManager *self = (ChattyManager *)object;

  g_clear_object (&self->history);
  g_clear_object (&self->matrix);

  G_OBJECT_CLASS (chatty_manager_parent_class)->finalize (object);
}

static void
chatty_manager_class_init (ChattyManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = chatty_manager_get_property;
  object_class->dispose = chatty_manager_dispose;
  object_class->finalize = chatty_manager_finalize;

  /**
   * ChattyUser:active-protocols:
   *
   * Protocols currently available for use.  This is a
   * flag of protocols currently connected and available
   * for use.
   */
  properties[PROP_ACTIVE_PROTOCOLS] =
    g_param_spec_int ("active-protocols",
                      "Active protocols",
                      "Protocols currently active and connected",
                      CHATTY_PROTOCOL_NONE,
                      CHATTY_PROTOCOL_TELEGRAM,
                      CHATTY_PROTOCOL_NONE,
                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * ChattyManager::chat-deleted:
   * @self: a #ChattyManager
   * @chat: A #ChattyChat
   *
   * Emitted when a chat is deleted.  ‘chat-deleted’ is
   * emitted just before the chat is actually deleted
   * and thus @chat will still point to a valid memory.
   */
  signals [CHAT_DELETED] =
    g_signal_new ("chat-deleted",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1, CHATTY_TYPE_CHAT);

  /**
   * ChattyManager::open-chat:
   * @self: A #ChattyManager
   * @chat: A #ChattyChat
   *
   * Emitted when user requests to open a chat.  UI can hook
   * to this signal to do whatever appropriate.
   */
  signals [OPEN_CHAT] =
    g_signal_new ("open-chat",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1, CHATTY_TYPE_CHAT);
}

static void
manager_mm_account_changed_cb (ChattyManager *self)
{
  g_assert (CHATTY_IS_MANAGER (self));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_PROTOCOLS]);
}

static void
manager_chat_message_added_cb (ChattyManager *self,
                               ChattyChat    *chat)
{
  GListModel *model;
  guint position;

  g_assert (CHATTY_IS_MANAGER (self));

  model = gtk_sort_list_model_get_model (self->sorted_chat_list);

  if (chatty_utils_get_item_position (model, chat, &position))
    g_list_model_items_changed (model, position, 1, 1);
}

static void
manager_chat_list_items_changed (ChattyManager *self,
                                 guint          position,
                                 guint          removed,
                                 guint          added,
                                 GListModel    *model)
{
  if (!added)
    return;

  g_assert (CHATTY_IS_MANAGER (self));

  for (guint i = position; i < position + added; i++) {
    g_autoptr(ChattyChat) chat = NULL;
    gulong signal;

    chat = g_list_model_get_item (model, i);
    if (g_object_get_data (G_OBJECT (chat), "message-added-id"))
      continue;

    signal = g_signal_connect_object (chat, "message-added",
                                      G_CALLBACK (manager_chat_message_added_cb),
                                      self, G_CONNECT_SWAPPED);
    g_object_set_data (G_OBJECT (chat), "message-added-id", GINT_TO_POINTER (signal));
  }
}

static void
chatty_manager_init (ChattyManager *self)
{
  g_autoptr(GtkSorter) sorter = NULL;

  self->chatty_eds = chatty_eds_new (CHATTY_PROTOCOL_MMS_SMS);
  self->mm_account = chatty_mm_account_new ();

  g_signal_connect_object (self->mm_account, "notify::status",
                           G_CALLBACK (manager_mm_account_changed_cb), self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->chatty_eds, "notify::is-ready",
                           G_CALLBACK (manager_eds_is_ready), self,
                           G_CONNECT_SWAPPED);

  self->accounts = manager_new_flatten_list (CHATTY_TYPE_ACCOUNT);
  self->contact_list = manager_new_flatten_list (G_TYPE_OBJECT);
  self->chat_list = manager_new_flatten_list (G_TYPE_OBJECT);
  manager_add_to_flat_model (self->contact_list, chatty_eds_get_model (self->chatty_eds));

  sorter = gtk_custom_sorter_new ((GCompareDataFunc)manager_sort_chat_item, NULL, NULL);
  self->sorted_chat_list = gtk_sort_list_model_new (G_LIST_MODEL (self->chat_list), sorter);

  self->chat_filter = gtk_custom_filter_new ((GtkCustomFilterFunc)manager_filter_chat_item, NULL, NULL);
  self->filtered_chat_list = gtk_filter_list_model_new (G_LIST_MODEL(self->sorted_chat_list),
                                                        self->chat_filter);

  g_signal_connect_object (self->chat_list, "items-changed",
                           G_CALLBACK (manager_chat_list_items_changed),
                           self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self, "notify::active-protocols",
                           G_CALLBACK (manager_active_protocols_changed_cb),
                           self, G_CONNECT_SWAPPED);
}

ChattyManager *
chatty_manager_get_default (void)
{
  static ChattyManager *self;

  if (!self)
    {
      self = g_object_new (CHATTY_TYPE_MANAGER, NULL);
      g_object_add_weak_pointer (G_OBJECT (self), (gpointer *)&self);
    }

  return self;
}

void
chatty_manager_load (ChattyManager *self)
{
  g_autoptr(GListStore) accounts = NULL;

  g_return_if_fail (CHATTY_IS_MANAGER (self));

  if (self->has_loaded)
    return;

  self->has_loaded = TRUE;

#ifdef PURPLE_ENABLED
  if (!self->purple) {
    self->purple = chatty_purple_get_default ();
    manager_add_to_flat_model (self->accounts,
                               chatty_purple_get_accounts (self->purple));
    manager_add_to_flat_model (self->chat_list,
                               chatty_purple_get_chat_list (self->purple));
    manager_add_to_flat_model (self->contact_list,
                               chatty_purple_get_chat_list (self->purple));
    manager_add_to_flat_model (self->contact_list,
                               chatty_purple_get_user_list (self->purple));
    chatty_purple_set_history_db (self->purple, self->history);
    chatty_purple_load (self->purple, self->disable_auto_login);
  }
#endif

  chatty_mm_account_set_history_db (self->mm_account,
                                    chatty_manager_get_history (self));
  manager_add_to_flat_model (self->chat_list,
                             chatty_mm_account_get_chat_list (self->mm_account));

  chatty_mm_account_load_async (self->mm_account, NULL, NULL);

  /* Matrix Setup */
  self->matrix = chatty_matrix_new (chatty_manager_get_history (self),
                                    self->disable_auto_login);
  manager_add_to_flat_model (self->accounts,
                             chatty_matrix_get_account_list (self->matrix));
  manager_add_to_flat_model (self->chat_list,
                             chatty_matrix_get_chat_list (self->matrix));
  chatty_matrix_load (self->matrix);

  accounts = g_list_store_new (CHATTY_TYPE_ACCOUNT);
  g_list_store_append (accounts, self->mm_account);
  manager_add_to_flat_model (self->accounts, G_LIST_MODEL (accounts));
}

GListModel *
chatty_manager_get_accounts (ChattyManager *self)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), NULL);

  return G_LIST_MODEL (self->accounts);
}

GListModel *
chatty_manager_get_contact_list (ChattyManager *self)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), NULL);

  return G_LIST_MODEL (self->contact_list);
}


GListModel *
chatty_manager_get_chat_list (ChattyManager *self)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), NULL);

  return G_LIST_MODEL (self->filtered_chat_list);
}

/**
 * chatty_manager_disable_auto_login:
 * @self: A #ChattyManager
 * @disable: whether to disable auto-login
 *
 * Set whether to disable automatic login when accounts are
 * loaded/added.  By default, auto-login is enabled if the
 * account is enabled with chatty_pp_account_set_enabled().
 *
 * This is not applicable to SMS accounts.
 */
void
chatty_manager_disable_auto_login (ChattyManager *self,
                                   gboolean       disable)
{
  g_return_if_fail (CHATTY_IS_MANAGER (self));

  self->disable_auto_login = !!disable;
}

gboolean
chatty_manager_get_disable_auto_login (ChattyManager *self)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), TRUE);

  return self->disable_auto_login;
}

ChattyProtocol
chatty_manager_get_active_protocols (ChattyManager *self)
{
  ChattyProtocol protocols = CHATTY_PROTOCOL_NONE;

  g_return_val_if_fail (CHATTY_IS_MANAGER (self), CHATTY_PROTOCOL_NONE);

  if (chatty_account_get_status (CHATTY_ACCOUNT (self->mm_account)) == CHATTY_CONNECTED)
    protocols = protocols | CHATTY_PROTOCOL_MMS_SMS;

#ifdef PURPLE_ENABLED
  if (self->purple)
    protocols |= chatty_purple_get_protocols (self->purple);
#endif

  return protocols;
}


ChattyEds *
chatty_manager_get_eds (ChattyManager *self)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), NULL);

  return self->chatty_eds;
}

static void
manager_delete_account_cb (GObject      *object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  ChattyManager *self;
  ChattyMaAccount *account;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;
  gboolean success = FALSE;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  account = g_task_get_task_data (task);

  g_assert (CHATTY_IS_MANAGER (self));
  g_assert (CHATTY_IS_ACCOUNT (account));

  if (CHATTY_IS_MA_ACCOUNT (account))
    success = chatty_matrix_delete_account_finish (self->matrix, result, &error);
#ifdef PURPLE_ENABLED
  else if (CHATTY_IS_PP_ACCOUNT (account))
    success = chatty_purple_delete_account_finish (self->purple, result, &error);
#endif

  if (error)
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, success);
}

void
chatty_manager_delete_account_async (ChattyManager       *self,
                                     ChattyAccount       *account,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (CHATTY_IS_MANAGER (self));
  g_return_if_fail (CHATTY_IS_ACCOUNT (account));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_task_data (task, g_object_ref (account), g_object_unref);

  if (CHATTY_IS_MA_ACCOUNT (account))
    chatty_matrix_delete_account_async (self->matrix, account, cancellable,
                                        manager_delete_account_cb,
                                        g_steal_pointer (&task));
#ifdef PURPLE_ENABLED
  else if (CHATTY_IS_PP_ACCOUNT (account))
    chatty_purple_delete_account_async (self->purple, account, cancellable,
                                        manager_delete_account_cb,
                                        g_steal_pointer (&task));
#endif
  else
    g_return_if_reached ();
}

gboolean
chatty_manager_delete_account_finish  (ChattyManager  *self,
                                       GAsyncResult   *result,
                                       GError        **error)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), FALSE);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
manager_save_account_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  ChattyManager *self;
  ChattyAccount *account;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;
  gboolean saved = FALSE;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  account = g_task_get_task_data (task);

  g_assert (CHATTY_IS_MANAGER (self));
  g_assert (CHATTY_IS_ACCOUNT (account));

  if (CHATTY_IS_MA_ACCOUNT (account))
    saved = chatty_matrix_save_account_finish (self->matrix, result, &error);

  if (error)
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, saved);
}

void
chatty_manager_save_account_async (ChattyManager       *self,
                                   ChattyAccount       *account,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (CHATTY_IS_MANAGER (self));
  g_return_if_fail (CHATTY_IS_ACCOUNT (account));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_task_data (task, g_object_ref (account), g_object_unref);

  if (CHATTY_IS_MA_ACCOUNT (account))
    chatty_matrix_save_account_async (self->matrix, account, cancellable,
                                      manager_save_account_cb,
                                      g_steal_pointer (&task));
  else
    g_return_if_reached ();
}

gboolean
chatty_manager_save_account_finish (ChattyManager  *self,
                                    GAsyncResult   *result,
                                    GError        **error)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), FALSE);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

ChattyAccount *
chatty_manager_get_mm_account (ChattyManager *self)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), NULL);

  return CHATTY_ACCOUNT (self->mm_account);
}

ChattyAccount *
chatty_manager_find_account_with_name (ChattyManager  *self,
                                       ChattyProtocol  protocol,
                                       const char     *account_id)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), NULL);
  g_return_val_if_fail (account_id && *account_id, NULL);

  if (protocol & CHATTY_PROTOCOL_MATRIX &&
      chatty_settings_get_experimental_features (chatty_settings_get_default ()))
    return chatty_matrix_find_account_with_name (self->matrix, account_id);

#ifdef PURPLE_ENABLED
  return chatty_purple_find_account_with_name (self->purple, protocol, account_id);
#else
  return NULL;
#endif
}

ChattyChat *
chatty_manager_find_chat_with_name (ChattyManager  *self,
                                    ChattyProtocol  protocol,
                                    const char     *account_id,
                                    const char     *chat_id)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), NULL);
  g_return_val_if_fail (chat_id && *chat_id, NULL);

  if (protocol & (CHATTY_PROTOCOL_MMS | CHATTY_PROTOCOL_MMS_SMS))
    return chatty_mm_account_find_chat (self->mm_account, chat_id);

#ifdef PURPLE_ENABLED
  if (protocol & (CHATTY_PROTOCOL_XMPP | CHATTY_PROTOCOL_TELEGRAM) ||
      (!chatty_settings_get_experimental_features (chatty_settings_get_default ()) &&
       protocol & CHATTY_PROTOCOL_MATRIX))
    return chatty_purple_find_chat_with_name (chatty_purple_get_default (),
                                              protocol, account_id, chat_id);
#endif

  if (chatty_settings_get_experimental_features (chatty_settings_get_default ())
      && protocol == CHATTY_PROTOCOL_MATRIX)
    return chatty_matrix_find_chat_with_name (self->matrix, protocol, account_id, chat_id);

  return NULL;
}

gboolean
chatty_manager_set_uri (ChattyManager *self,
                        const char    *uri)
{
  ChattyChat *chat = NULL;
  g_autofree char *who = NULL;
  ChattyAccount *account;
  const char *country_code;
  g_auto(GStrv) recipients = NULL;
  guint num;

  if (!uri || !*uri)
    return FALSE;

  account = chatty_manager_get_mm_account (self);
  if (chatty_account_get_status (account) != CHATTY_CONNECTED)
    return FALSE;

  country_code = chatty_settings_get_country_iso_code (chatty_settings_get_default ());
  recipients = g_strsplit (uri, ",", -1);
  num = g_strv_length (recipients);
  for (int i = 0; i < num; i++) {
    who = chatty_utils_check_phonenumber (recipients[i], country_code);
    if (!who)
      return FALSE;
  }
  if (num == 1)
    chat = chatty_mm_account_start_chat (CHATTY_MM_ACCOUNT (account), who);
  else
    chat = chatty_mm_account_start_chat (CHATTY_MM_ACCOUNT (account), uri);

  g_signal_emit (self, signals[OPEN_CHAT], 0, chat);

  return TRUE;
}

ChattyHistory *
chatty_manager_get_history (ChattyManager *self)
{
  g_return_val_if_fail (CHATTY_IS_MANAGER (self), NULL);

  if (!self->history)
    self->history = chatty_history_new ();

  return self->history;
}
