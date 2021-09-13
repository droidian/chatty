/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-account.c
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-account"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "chatty-chat.h"
#include "chatty-account.h"

/**
 * SECTION: chatty-account
 * @title: ChattyAccount
 * @short_description: The base class for Purple and ModemManager Accounts
 */

typedef struct
{
  gint dummy;
} ChattyAccountPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ChattyAccount, chatty_account, CHATTY_TYPE_ITEM)

enum {
  PROP_0,
  PROP_ENABLED,
  PROP_STATUS,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static const char *
chatty_account_real_get_protocol_name (ChattyAccount *self)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  return "";
}

static ChattyStatus
chatty_account_real_get_status (ChattyAccount *self)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  return CHATTY_DISCONNECTED;
}

static GListModel *
chatty_account_real_get_buddies (ChattyAccount *self)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  return NULL;
}

static gboolean
chatty_account_real_buddy_exists (ChattyAccount *self,
                                  const char    *buddy_username)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  return FALSE;
}

static gboolean
chatty_account_real_get_enabled (ChattyAccount *self)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  return FALSE;
}

static void
chatty_account_real_set_enabled (ChattyAccount *self,
                                 gboolean       enabled)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  /* Do Nothing */
}

static void
chatty_account_real_connect (ChattyAccount *self,
                             gboolean       delay)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  /* Do Nothing */
}

static void
chatty_account_real_disconnect (ChattyAccount *self)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  /* Do Nothing */
}

static const char *
chatty_account_real_get_password (ChattyAccount *self)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  return "";
}

static void
chatty_account_real_set_password (ChattyAccount *self,
                                  const char    *password)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  /* Do Nothing */
}

static gboolean
chatty_account_real_get_remember_password (ChattyAccount *self)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  return FALSE;
}

static void
chatty_account_real_set_remember_password (ChattyAccount *self,
                                           gboolean       remember)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  /* Do Nothing */
}

static void
chatty_account_real_save (ChattyAccount *self)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  /* Do Nothing */
}

static void
chatty_account_real_delete (ChattyAccount *self)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  /* Do Nothing */
}

static HdyValueObject *
chatty_account_real_get_device_fp (ChattyAccount *self)
{
  return NULL;
}

static GListModel *
chatty_account_real_get_fp_list (ChattyAccount *self)
{
  return NULL;
}

static void
chatty_account_real_load_fp_async (ChattyAccount       *self,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  g_task_report_new_error (self, callback, user_data,
                           chatty_account_real_load_fp_async,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "Loading finger print list not supported");
}

static gboolean
chatty_account_real_load_fp_finish (ChattyAccount *self,
                                    GAsyncResult  *result,
                                    GError        **error)
{
  g_assert (CHATTY_IS_ACCOUNT (self));
  g_assert (G_IS_TASK (result));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
chatty_account_real_leave_chat_async (ChattyAccount       *self,
                                      ChattyChat          *chat,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  g_task_report_new_error (self, callback, user_data,
                           chatty_account_real_leave_chat_async,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "Leaving chat not supported");
}

static gboolean
chatty_account_real_leave_chat_finish (ChattyAccount *self,
                                       GAsyncResult  *result,
                                       GError        **error)
{
  g_assert (CHATTY_IS_ACCOUNT (self));
  g_assert (G_IS_TASK (result));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
chatty_account_real_join_chat_async (ChattyAccount       *self,
                                     ChattyChat          *chat,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  g_task_report_new_error (self, callback, user_data,
                           chatty_account_real_join_chat_async,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "Joining chat not supported");
}

static gboolean
chatty_account_real_join_chat_finish (ChattyAccount  *self,
                                      GAsyncResult   *result,
                                      GError        **error)
{
  g_assert (CHATTY_IS_ACCOUNT (self));
  g_assert (G_IS_TASK (result));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
chatty_account_real_start_direct_chat_async (ChattyAccount       *self,
                                             GPtrArray           *buddies,
                                             GAsyncReadyCallback  callback,
                                             gpointer             user_data)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  g_task_report_new_error (self, callback, user_data,
                           chatty_account_real_start_direct_chat_async,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "Starting direct chat not supported");
}

static gboolean
chatty_account_real_start_direct_chat_finish (ChattyAccount *self,
                                              GAsyncResult  *result,
                                              GError        **error)
{
  g_assert (CHATTY_IS_ACCOUNT (self));
  g_assert (G_IS_TASK (result));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
chatty_account_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  ChattyAccount *self = (ChattyAccount *)object;

  switch (prop_id)
    {
    case PROP_ENABLED:
      g_value_set_boolean (value, chatty_account_get_enabled (self));
      break;

    case PROP_STATUS:
      g_value_set_int (value, chatty_account_get_status (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
chatty_account_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  ChattyAccount *self = (ChattyAccount *)object;

  switch (prop_id)
    {
    case PROP_ENABLED:
      chatty_account_set_enabled (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
chatty_account_class_init (ChattyAccountClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = chatty_account_get_property;
  object_class->set_property = chatty_account_set_property;

  klass->get_protocol_name = chatty_account_real_get_protocol_name;
  klass->get_status   = chatty_account_real_get_status;
  klass->get_buddies  = chatty_account_real_get_buddies;
  klass->buddy_exists = chatty_account_real_buddy_exists;
  klass->get_enabled  = chatty_account_real_get_enabled;
  klass->set_enabled  = chatty_account_real_set_enabled;
  klass->connect      = chatty_account_real_connect;
  klass->disconnect   = chatty_account_real_disconnect;
  klass->get_password = chatty_account_real_get_password;
  klass->set_password = chatty_account_real_set_password;
  klass->get_remember_password = chatty_account_real_get_remember_password;
  klass->set_remember_password = chatty_account_real_set_remember_password;
  klass->save = chatty_account_real_save;
  klass->delete = chatty_account_real_delete;
  klass->get_device_fp = chatty_account_real_get_device_fp;
  klass->get_fp_list = chatty_account_real_get_fp_list;
  klass->load_fp_async = chatty_account_real_load_fp_async;
  klass->load_fp_finish = chatty_account_real_load_fp_finish;
  klass->join_chat_async = chatty_account_real_join_chat_async;
  klass->join_chat_finish = chatty_account_real_join_chat_finish;
  klass->leave_chat_async = chatty_account_real_leave_chat_async;
  klass->leave_chat_finish = chatty_account_real_leave_chat_finish;
  klass->start_direct_chat_async = chatty_account_real_start_direct_chat_async;
  klass->start_direct_chat_finish = chatty_account_real_start_direct_chat_finish;

  /**
   * ChattyAccount:enabled:
   */
  properties[PROP_ENABLED] =
    g_param_spec_boolean ("enabled",
                          "Enabled",
                          "If Account is enabled or not",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * ChattyAccount:status:
   *
   * The Account status %ChattyStatus
   */
  properties[PROP_STATUS] =
    g_param_spec_int ("status",
                      "status",
                      "The status of the Account",
                      CHATTY_UNKNOWN,
                      CHATTY_CONNECTED,
                      CHATTY_DISCONNECTED,
                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
chatty_account_init (ChattyAccount *self)
{
}

const char *
chatty_account_get_protocol_name (ChattyAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_ACCOUNT (self), "");

  return CHATTY_ACCOUNT_GET_CLASS (self)->get_protocol_name (self);
}

ChattyStatus
chatty_account_get_status (ChattyAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_ACCOUNT (self), CHATTY_DISCONNECTED);

  return CHATTY_ACCOUNT_GET_CLASS (self)->get_status (self);
}

GListModel *
chatty_account_get_buddies (ChattyAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_ACCOUNT (self), NULL);

  return CHATTY_ACCOUNT_GET_CLASS (self)->get_buddies (self);
}

/**
 * chatty_account_buddy_exists:
 * @self: A #ChattyAccount
 * @buddy_username: Username of the buddy
 *
 * Check if @buddy_username exists in the buddy list
 * of @self.  The result is undefined if an invalid
 * username is provided.  To validate username see
 * chatty_utils_username_is_valid()
 *
 * Returns: %TRUE if @buddy_username exists in the
 * local buddy list of @self.  %FALSE otherwise
 */
gboolean
chatty_account_buddy_exists (ChattyAccount *self,
                             const char    *buddy_username)
{
  g_return_val_if_fail (CHATTY_IS_ACCOUNT (self), FALSE);

  return CHATTY_ACCOUNT_GET_CLASS (self)->buddy_exists (self, buddy_username);
}

gboolean
chatty_account_get_enabled (ChattyAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_ACCOUNT (self), FALSE);

  return CHATTY_ACCOUNT_GET_CLASS (self)->get_enabled (self);
}

void
chatty_account_set_enabled (ChattyAccount *self,
                            gboolean       enable)
{
  g_return_if_fail (CHATTY_IS_ACCOUNT (self));

  CHATTY_ACCOUNT_GET_CLASS (self)->set_enabled (self, !!enable);
}

/**
 * chatty_account_connect:
 * @self: A #ChattyAccount
 * @delay: Whether to delay connection
 *
 * connection to @self.  If @delay is %TRUE, the connection
 * is initiated after some delay, which can be useful when
 * trying to connect after a connection failure.
 *
 * If the account is not enabled, or if account status is
 * set to offline, or if already connected, the function
 * simply returns.
 */
void
chatty_account_connect (ChattyAccount *self,
                        gboolean       delay)
{
  g_return_if_fail (CHATTY_IS_ACCOUNT (self));

  CHATTY_ACCOUNT_GET_CLASS (self)->connect (self, delay);
}

void
chatty_account_disconnect (ChattyAccount *self)
{
  g_return_if_fail (CHATTY_IS_ACCOUNT (self));

  CHATTY_ACCOUNT_GET_CLASS (self)->disconnect (self);
}

const char *
chatty_account_get_password (ChattyAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_ACCOUNT (self), "");

  return CHATTY_ACCOUNT_GET_CLASS (self)->get_password (self);
}

void
chatty_account_set_password (ChattyAccount *self,
                             const char    *password)
{
  g_return_if_fail (CHATTY_IS_ACCOUNT (self));

  CHATTY_ACCOUNT_GET_CLASS (self)->set_password (self, password);
}

gboolean
chatty_account_get_remember_password (ChattyAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_ACCOUNT (self), FALSE);

  return CHATTY_ACCOUNT_GET_CLASS (self)->get_remember_password (self);
}

void
chatty_account_set_remember_password (ChattyAccount *self,
                                      gboolean       remember)
{
  g_return_if_fail (CHATTY_IS_ACCOUNT (self));

  CHATTY_ACCOUNT_GET_CLASS (self)->set_remember_password (self, remember);
}

/**
 * chatty_account_save:
 * @self: A #ChattyAccount
 *
 * Save @self to accounts store, which is saved to disk.
 * If the account is already saved, the function simply
 * returns.
 */
void
chatty_account_save (ChattyAccount *self)
{
  g_return_if_fail (CHATTY_IS_ACCOUNT (self));

  CHATTY_ACCOUNT_GET_CLASS (self)->save (self);
}

void
chatty_account_delete (ChattyAccount *self)
{
  g_return_if_fail (CHATTY_IS_ACCOUNT (self));

  CHATTY_ACCOUNT_GET_CLASS (self)->delete (self);
}

HdyValueObject *
chatty_account_get_device_fp (ChattyAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_ACCOUNT (self), NULL);

  return CHATTY_ACCOUNT_GET_CLASS (self)->get_device_fp (self);
}

GListModel *
chatty_account_get_fp_list (ChattyAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_ACCOUNT (self), NULL);

  return CHATTY_ACCOUNT_GET_CLASS (self)->get_fp_list (self);
}

void
chatty_account_load_fp_async (ChattyAccount       *self,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  g_return_if_fail (CHATTY_IS_ACCOUNT (self));

  CHATTY_ACCOUNT_GET_CLASS (self)->load_fp_async (self, callback, user_data);
}

gboolean
chatty_account_load_fp_finish (ChattyAccount  *self,
                               GAsyncResult   *result,
                               GError        **error)
{
  g_return_val_if_fail (CHATTY_IS_ACCOUNT (self), FALSE);

  return CHATTY_ACCOUNT_GET_CLASS (self)->load_fp_finish (self, result, error);
}

void
chatty_account_join_chat_async (ChattyAccount       *self,
                                ChattyChat          *chat,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  g_return_if_fail (CHATTY_IS_ACCOUNT (self));
  g_return_if_fail (CHATTY_IS_CHAT (chat));

  CHATTY_ACCOUNT_GET_CLASS (self)->join_chat_async (self, chat, callback, user_data);
}

gboolean
chatty_account_join_chat_finish (ChattyAccount  *self,
                                 GAsyncResult   *result,
                                 GError        **error)
{
  g_return_val_if_fail (CHATTY_IS_ACCOUNT (self), FALSE);

  return CHATTY_ACCOUNT_GET_CLASS (self)->join_chat_finish (self, result, error);
}

void
chatty_account_leave_chat_async (ChattyAccount       *self,
                                 ChattyChat          *chat,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
  g_return_if_fail (CHATTY_IS_ACCOUNT (self));
  g_return_if_fail (CHATTY_IS_CHAT (chat));

  CHATTY_ACCOUNT_GET_CLASS (self)->leave_chat_async (self, chat, callback, user_data);
}

gboolean
chatty_account_leave_chat_finish (ChattyAccount  *self,
                                  GAsyncResult   *result,
                                  GError        **error)
{
  g_return_val_if_fail (CHATTY_IS_ACCOUNT (self), FALSE);

  return CHATTY_ACCOUNT_GET_CLASS (self)->leave_chat_finish (self, result, error);
}

/**
 * chatty_account_start_direct_chat_async:
 * @self: A #ChattyAccount
 * @buddies: (transfer full): A #GPtrArray an array of strings
 * @callback: A #GAsyncReadyCallback
 * @user_data: The user data for @callback
 *
 * Try to create a direct chat with the @buddies.  Different
 * protocols may have different requirements for this.  Eg:
 * currently purple XMPP requires @buddies to have exactly
 * one buddy.  The GUI is present to the user when ready.
 */
void
chatty_account_start_direct_chat_async (ChattyAccount       *self,
                                        GPtrArray           *buddies,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
  g_return_if_fail (CHATTY_IS_ACCOUNT (self));
  g_return_if_fail (buddies && buddies->len);

  CHATTY_ACCOUNT_GET_CLASS (self)->start_direct_chat_async (self, buddies, callback, user_data);
}

gboolean
chatty_account_start_direct_chat_finish (ChattyAccount  *self,
                                         GAsyncResult   *result,
                                         GError        **error)
{
  g_return_val_if_fail (CHATTY_IS_ACCOUNT (self), FALSE);

  return CHATTY_ACCOUNT_GET_CLASS (self)->start_direct_chat_finish (self, result, error);
}
