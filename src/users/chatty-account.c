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

static const char *
chatty_account_real_get_username (ChattyAccount *self)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  return "";
}

static void
chatty_account_real_set_username (ChattyAccount *self,
                                  const char    *username)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  /* Do nothing */
}

static GListModel *
chatty_account_real_get_buddies (ChattyAccount *self)
{
  g_assert (CHATTY_IS_ACCOUNT (self));

  return NULL;
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
  klass->get_username = chatty_account_real_get_username;
  klass->set_username = chatty_account_real_set_username;
  klass->get_buddies  = chatty_account_real_get_buddies;
  klass->get_enabled  = chatty_account_real_get_enabled;
  klass->set_enabled  = chatty_account_real_set_enabled;
  klass->get_password = chatty_account_real_get_password;
  klass->set_password = chatty_account_real_set_password;
  klass->get_remember_password = chatty_account_real_get_remember_password;
  klass->set_remember_password = chatty_account_real_set_remember_password;
  klass->save = chatty_account_real_save;
  klass->delete = chatty_account_real_delete;

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
                      CHATTY_DISCONNECTED,
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

const char *
chatty_account_get_username (ChattyAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_ACCOUNT (self), "");

  return CHATTY_ACCOUNT_GET_CLASS (self)->get_username (self);
}

void
chatty_account_set_username (ChattyAccount *self,
                             const char    *username)
{
  g_return_if_fail (CHATTY_IS_ACCOUNT (self));

  CHATTY_ACCOUNT_GET_CLASS (self)->set_username (self, username);
}

GListModel *
chatty_account_get_buddies (ChattyAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_ACCOUNT (self), NULL);

  return CHATTY_ACCOUNT_GET_CLASS (self)->get_buddies (self);
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
