/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-pp-account.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-pp-account"

#include <purple.h>

#include "chatty-config.h"
#include "chatty-settings.h"
#include "chatty-account.h"
#include "chatty-window.h"
#include "chatty-pp-account.h"

/**
 * SECTION: chatty-pp-account
 * @title: ChattyPpAccount
 * @short_description: An abstraction over #PurpleAccount
 * @include: "chatty-pp-account.h"
 *
 * libpurple doesn’t have a nice OOP interface for managing anything.
 * This class hides all the complexities surrounding it.
 */

#define RECONNECT_DELAY 5000 /* milliseconds */

struct _ChattyPpAccount
{
  ChattyAccount   parent_instance;

  gchar          *username;
  gchar          *server_url;
  GListStore     *buddy_list;

  PurpleAccount  *pp_account;
  PurpleStoredImage *pp_avatar;
  GdkPixbuf         *avatar;
  guint           connect_id;
  ChattyProtocol  protocol;

  ChattyPpAccountFeatures features;
};

G_DEFINE_TYPE (ChattyPpAccount, chatty_pp_account, CHATTY_TYPE_ACCOUNT)

enum {
  PROP_0,
  PROP_USERNAME,
  PROP_SERVER_URL,
  PROP_PURPLE_ACCOUNT,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gpointer
chatty_icon_get_data_from_image (const char  *file_name,
                                 int          width,
                                 int          height,
                                 size_t      *len,
                                 GError     **error)
{
  g_autoptr(GdkPixbuf) pixbuf = NULL;
  GdkPixbufFormat *format;
  gchar *buffer = NULL;
  gsize size = 0;
  int icon_width, icon_height;

  format = gdk_pixbuf_get_file_info (file_name, &icon_width, &icon_height);
  if (!format) {
    if (error)
      *error = g_error_new (G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "File format of %s not supported", file_name);
    return NULL;
  }

  pixbuf = gdk_pixbuf_new_from_file_at_scale (file_name,
                                              MIN (width, icon_width),
                                              MIN (height, icon_height),
                                              TRUE, error);

  if (!pixbuf)
    return NULL;

  if (!gdk_pixbuf_save_to_buffer (pixbuf, &buffer, &size, "png", error, NULL))
    return NULL;

  if (len)
    *len = size;

  return buffer;
}

static gboolean
account_connect (ChattyPpAccount *self)
{
  PurpleStatus *pp_status;
  ChattyStatus status;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  g_clear_handle_id (&self->connect_id, g_source_remove);

  if (!chatty_account_get_enabled (CHATTY_ACCOUNT (self)))
    return G_SOURCE_REMOVE;

  status = chatty_account_get_status (CHATTY_ACCOUNT (self));

  if (status == CHATTY_CONNECTED ||
      status == CHATTY_CONNECTING)
    return G_SOURCE_REMOVE;

  pp_status = chatty_pp_account_get_active_status (self);

  if (!purple_status_is_online (pp_status))
    return G_SOURCE_REMOVE;

  g_debug ("connecting to %s", chatty_pp_account_get_username (self));
  purple_account_connect (self->pp_account);

  return G_SOURCE_REMOVE;
}

static void
chatty_pp_account_create (ChattyPpAccount *self)
{
  const char *protocol_id;
  ChattyProtocol protocol;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  protocol = chatty_item_get_protocols (CHATTY_ITEM (self));

  if (protocol == CHATTY_PROTOCOL_XMPP)
    protocol_id = "prpl-jabber";
  else if (protocol == CHATTY_PROTOCOL_MATRIX)
    protocol_id = "prpl-matrix";
  else if (protocol == CHATTY_PROTOCOL_SMS)
    protocol_id = "prpl-mm-sms";
  else if (protocol == CHATTY_PROTOCOL_TELEGRAM)
    protocol_id = "prpl-telegram";

  self->pp_account = purple_account_new (self->username, protocol_id);

    if (protocol == CHATTY_PROTOCOL_MATRIX)
      {
        purple_account_set_string (self->pp_account, "home_server", self->server_url);
      }
    else if (protocol == CHATTY_PROTOCOL_SMS)
      {
        purple_account_set_password (self->pp_account, NULL);
        purple_account_set_remember_password (self->pp_account, TRUE);
      }
}

static void
chatty_pp_load_protocol (ChattyPpAccount *self)
{
  const char *protocol_id = NULL;
  ChattyProtocol protocol = CHATTY_PROTOCOL_NONE;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));
  g_assert (self->pp_account);

  protocol_id = purple_account_get_protocol_id (self->pp_account);
  g_return_if_fail (protocol_id);

  if (g_str_equal (protocol_id, "prpl-jabber"))
    protocol = CHATTY_PROTOCOL_XMPP;
  else if (g_str_equal (protocol_id, "prpl-matrix"))
    protocol = CHATTY_PROTOCOL_MATRIX;
  else if (g_str_equal (protocol_id, "prpl-mm-sms"))
    protocol = CHATTY_PROTOCOL_SMS;
  else if (g_str_equal (protocol_id, "prpl-telegram"))
    protocol = CHATTY_PROTOCOL_TELEGRAM;
  else if (g_str_equal (protocol_id, "prpl-delta"))
    protocol = CHATTY_PROTOCOL_DELTA;
  else if (g_str_equal (protocol_id, "prpl-threepl"))
    protocol = CHATTY_PROTOCOL_THREEPL;

  self->protocol = protocol;
}

static ChattyStatus
chatty_pp_account_get_status (ChattyAccount *account)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  if (purple_account_is_connected (self->pp_account))
    return CHATTY_CONNECTED;
  if (purple_account_is_connecting (self->pp_account))
    return CHATTY_CONNECTING;

  return CHATTY_DISCONNECTED;
}

static gboolean
chatty_pp_account_get_enabled (ChattyAccount *account)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  return purple_account_get_enabled (self->pp_account,
                                     purple_core_get_ui ());
}

static void
chatty_pp_account_set_enabled (ChattyAccount *account,
                               gboolean       enable)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  purple_account_set_enabled (self->pp_account,
                              purple_core_get_ui (), !!enable);
}

static const char *
chatty_pp_account_get_password (ChattyAccount *account)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  return purple_account_get_password (self->pp_account);
}

static void
chatty_pp_account_set_password (ChattyAccount *account,
                                const char    *password)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;
  ChattyProtocol protocol;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  protocol = chatty_item_get_protocols (CHATTY_ITEM (account));

  if (protocol == CHATTY_PROTOCOL_TELEGRAM)
    purple_account_set_string (self->pp_account, "password-two-factor", password);
  else
    purple_account_set_password (self->pp_account, password);
}

static void
chatty_pp_account_set_remember_password (ChattyAccount *account,
                                         gboolean       remember)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  purple_account_set_remember_password (self->pp_account, !!remember);
}

static gboolean
chatty_pp_account_get_remember_password (ChattyAccount *account)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  return purple_account_get_remember_password (self->pp_account);
}

static ChattyProtocol
chatty_pp_account_get_protocols (ChattyItem *item)
{
  ChattyPpAccount *self = (ChattyPpAccount *)item;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  if (self->protocol != CHATTY_PROTOCOL_NONE)
    return self->protocol;

  return CHATTY_ITEM_CLASS (chatty_pp_account_parent_class)->get_protocols (item);
}

static const char *
chatty_pp_account_get_name (ChattyItem *item)
{
  ChattyPpAccount *self = (ChattyPpAccount *)item;
  const char *name;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  name = purple_account_get_alias (self->pp_account);

  if (name && *name)
    return name;

  return purple_account_get_username (self->pp_account);
}

static void
chatty_pp_account_set_name (ChattyItem *item,
                            const char *name)
{
  ChattyPpAccount *self = (ChattyPpAccount *)item;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  purple_account_set_alias (self->pp_account, name);
}

static GdkPixbuf *
chatty_icon_from_data (const guchar *buf,
                       gsize         size)
{
  g_autoptr(GdkPixbufLoader) loader = NULL;
  g_autoptr(GError) error = NULL;
  GdkPixbuf *pixbuf = NULL;

  loader = gdk_pixbuf_loader_new ();
  gdk_pixbuf_loader_write (loader, buf, size, &error);

  if (!error)
    gdk_pixbuf_loader_close (loader, &error);

  if (error)
    g_warning ("Error: %s: %s", __func__, error->message);
  else
    pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);

  if (!pixbuf)
    g_warning ("%s: pixbuf creation failed", __func__);

  return g_object_ref (pixbuf);
}

static GdkPixbuf *
chatty_pp_account_get_avatar (ChattyItem *item)
{
  ChattyPpAccount *self = (ChattyPpAccount *)item;
  PurpleStoredImage *img;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  img = purple_buddy_icons_find_account_icon (self->pp_account);

  if (img == NULL)
    return NULL;

  if (img == self->pp_avatar && self->avatar)
    return self->avatar;

  purple_imgstore_unref (self->pp_avatar);
  g_clear_object (&self->avatar);
  self->pp_avatar = img;

  if (img != NULL)
    self->avatar = chatty_icon_from_data (purple_imgstore_get_data (img),
                                          purple_imgstore_get_size (img));
  return self->avatar;
}

static void
chatty_pp_account_set_avatar_async (ChattyItem          *item,
                                    const char          *file_name,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  ChattyPpAccount *self = (ChattyPpAccount *)item;
  PurplePluginProtocolInfo *prpl_info;
  g_autoptr(GTask) task = NULL;
  g_autoptr(GError) error = NULL;
  const char *protocol_id;
  guchar *data;
  int width, height;
  size_t len;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, chatty_item_set_avatar_async);

  protocol_id = purple_account_get_protocol_id (self->pp_account);
  prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO (purple_find_prpl (protocol_id));
  width  = prpl_info->icon_spec.max_width;
  height = prpl_info->icon_spec.max_height;
  data   = chatty_icon_get_data_from_image (file_name, width, height, &len, &error);

  if (!data)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      g_debug ("Error: %s", error->message);

      return;
    }

  /* Purple does not support multi-thread. So do it sync */
  purple_buddy_icons_set_account_icon (self->pp_account, data, len);

  g_signal_emit_by_name (self, "avatar-changed");
  g_task_return_boolean (task, TRUE);
}

static void
chatty_pp_account_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  ChattyPpAccount *self = (ChattyPpAccount *)object;

  switch (prop_id)
    {
    case PROP_USERNAME:
      self->username = g_value_dup_string (value);
      break;

    case PROP_SERVER_URL:
      self->server_url = g_value_dup_string (value);
      break;

    case PROP_PURPLE_ACCOUNT:
      self->pp_account = g_value_get_pointer (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
chatty_pp_account_constructed (GObject *object)
{
  ChattyPpAccount *self = (ChattyPpAccount *)object;

  G_OBJECT_CLASS (chatty_pp_account_parent_class)->constructed (object);

  if (!self->pp_account)
    chatty_pp_account_create (self);
  else
    chatty_pp_load_protocol (self);

  /*
   * ‘ui_data’ is a field provided by libpurple to be used by UI.
   * It is never used by libpurple core.
   * See: https://web.archive.org/web/20160104101051/https://pidgin.im/pipermail/devel/2011-October/021972.html
   *
   * As it’s used here, ‘ui_data’ shouldn’t be used elsewhere in UI.
   */
  self->pp_account->ui_data = self;
  g_object_add_weak_pointer (G_OBJECT (self), (gpointer *)&self->pp_account->ui_data);
}

static void
chatty_pp_account_finalize (GObject *object)
{
  ChattyPpAccount *self = (ChattyPpAccount *)object;

  g_clear_handle_id (&self->connect_id, g_source_remove);

  if (self->pp_avatar)
    purple_imgstore_unref (self->pp_avatar);

  g_clear_object (&self->buddy_list);
  g_clear_object (&self->avatar);
  g_free (self->username);
  g_free (self->server_url);

  G_OBJECT_CLASS (chatty_pp_account_parent_class)->finalize (object);
}

static void
chatty_pp_account_class_init (ChattyPpAccountClass *klass)
{
  GObjectClass *object_class  = G_OBJECT_CLASS (klass);
  ChattyItemClass *item_class = CHATTY_ITEM_CLASS (klass);
  ChattyAccountClass *account_class = CHATTY_ACCOUNT_CLASS (klass);

  object_class->set_property = chatty_pp_account_set_property;
  object_class->constructed = chatty_pp_account_constructed;
  object_class->finalize = chatty_pp_account_finalize;

  item_class->get_protocols = chatty_pp_account_get_protocols;
  item_class->get_name = chatty_pp_account_get_name;
  item_class->set_name = chatty_pp_account_set_name;
  item_class->get_avatar = chatty_pp_account_get_avatar;
  item_class->set_avatar_async = chatty_pp_account_set_avatar_async;

  account_class->get_status   = chatty_pp_account_get_status;
  account_class->get_enabled  = chatty_pp_account_get_enabled;
  account_class->set_enabled  = chatty_pp_account_set_enabled;
  account_class->get_password = chatty_pp_account_get_password;
  account_class->set_password = chatty_pp_account_set_password;
  account_class->get_remember_password = chatty_pp_account_get_remember_password;
  account_class->set_remember_password = chatty_pp_account_set_remember_password;

  properties[PROP_USERNAME] =
    g_param_spec_string ("username",
                         "Username",
                         "Username of the Purple Account",
                         NULL,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_SERVER_URL] =
    g_param_spec_string ("server-url",
                         "Server URL",
                         "The Server URL of the Purple account",
                         NULL,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_PURPLE_ACCOUNT] =
    g_param_spec_pointer ("purple-account",
                         "Purple Account",
                         "The PurpleAccount to be used to create the object",
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
chatty_pp_account_init (ChattyPpAccount *self)
{
  self->buddy_list = g_list_store_new (CHATTY_TYPE_PP_BUDDY);
}

/**
 * chatty_pp_account_get_object:
 * @account: A #PurpleAccount
 *
 * Get the #ChattyPpAccount associated with @account.
 *
 * Returns: (transfer none) (nullable): A #ChattyPpAccount.
 */
ChattyPpAccount *
chatty_pp_account_get_object (PurpleAccount *account)
{
  g_return_val_if_fail (account, NULL);

  /*
   * ‘ui_data’ is a field provided by libpurple to be used by UI.
   * It is never used by libpurple core.
   * See: https://web.archive.org/web/20160104101051/https://pidgin.im/pipermail/devel/2011-October/021972.html
   *
   * As it’s used here, ‘ui_data’ shouldn’t be used elsewhere in UI.
   */
  return account->ui_data;
}

ChattyPpAccount *
chatty_pp_account_new (ChattyProtocol  protocol,
                       const char     *username,
                       const char     *server_url)
{
  g_return_val_if_fail (protocol & (CHATTY_PROTOCOL_SMS |
                                    CHATTY_PROTOCOL_XMPP |
                                    CHATTY_PROTOCOL_MATRIX |
                                    CHATTY_PROTOCOL_TELEGRAM), NULL);
  g_return_val_if_fail (username && *username, NULL);

  return g_object_new (CHATTY_TYPE_PP_ACCOUNT,
                       "protocols", protocol,
                       "username", username,
                       "server-url", server_url,
                       NULL);
}

ChattyPpAccount *
chatty_pp_account_new_purple (PurpleAccount *account)
{
  g_return_val_if_fail (account, NULL);
  g_return_val_if_fail (!account->ui_data, NULL);

  return g_object_new (CHATTY_TYPE_PP_ACCOUNT,
                       "purple-account", account,
                       NULL);
}

ChattyPpBuddy *
chatty_pp_account_add_buddy (ChattyPpAccount *self,
                             const char      *username,
                             const char      *name)
{
  g_autoptr(ChattyPpBuddy) buddy = NULL;

  g_return_val_if_fail (CHATTY_IS_PP_ACCOUNT (self), NULL);
  g_return_val_if_fail (username && *username, NULL);

  buddy = g_object_new (CHATTY_TYPE_PP_BUDDY,
                        "purple-account", self->pp_account,
                        "username", username,
                        "name", name,
                        NULL);

  return buddy;
}

ChattyPpBuddy *
chatty_pp_account_add_purple_buddy (ChattyPpAccount *self,
                                    PurpleBuddy     *pp_buddy)
{
  g_autoptr(ChattyPpBuddy) buddy = NULL;

  g_return_val_if_fail (CHATTY_IS_PP_ACCOUNT (self), NULL);
  g_return_val_if_fail (pp_buddy, NULL);

  buddy = g_object_new (CHATTY_TYPE_PP_BUDDY,
                        "purple-buddy", pp_buddy,
                        NULL);

  return buddy;
}

GListModel *
chatty_pp_account_get_buddy_list (ChattyPpAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_PP_ACCOUNT (self), NULL);

  return G_LIST_MODEL (self->buddy_list);
}

/**
 * chatty_pp_account_save:
 * @self: A #ChattyPpAccount
 *
 * Save @self to accounts store, which is saved to disk.
 * If the account is already saved, the function simply
 * returns.
 */
void
chatty_pp_account_save (ChattyPpAccount *self)
{
  g_return_if_fail (CHATTY_IS_PP_ACCOUNT (self));

  /* purple adds the account only if not yet added */
  purple_accounts_add (self->pp_account);
}

/* XXX: a helper API till the dust settles */
PurpleAccount *
chatty_pp_account_get_account (ChattyPpAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_PP_ACCOUNT (self), NULL);

  return self->pp_account;
}

/* XXX: a helper API till the dust settles */
PurpleStatus *
chatty_pp_account_get_active_status (ChattyPpAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_PP_ACCOUNT (self), NULL);

  return purple_account_get_active_status (self->pp_account);
}

gboolean
chatty_pp_account_is_sms (ChattyPpAccount *self)
{
  const char *id;

  g_return_val_if_fail (CHATTY_IS_PP_ACCOUNT (self), FALSE);

  id = chatty_pp_account_get_protocol_id (self);

  return g_str_equal (id, "prpl-mm-sms");
}

const char *
chatty_pp_account_get_protocol_id (ChattyPpAccount *self)
{
  const char *id;

  g_return_val_if_fail (CHATTY_IS_PP_ACCOUNT (self), "");

  id = purple_account_get_protocol_id (self->pp_account);

  return id ? id : "";
}

const char *
chatty_pp_account_get_protocol_name (ChattyPpAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_PP_ACCOUNT (self), NULL);

  return purple_account_get_protocol_name (self->pp_account);
}

void
chatty_pp_account_set_username (ChattyPpAccount *self,
                                const char      *username)
{
  g_return_if_fail (CHATTY_IS_PP_ACCOUNT (self));

  purple_account_set_username (self->pp_account, username);
}

const gchar *
chatty_pp_account_get_username (ChattyPpAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_PP_ACCOUNT (self), NULL);

  return purple_account_get_username (self->pp_account);
}

/**
 * chatty_pp_account_connect:
 * @self: A #ChattyPpAccount
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
chatty_pp_account_connect (ChattyPpAccount *self,
                           gboolean         delay)
{
  g_return_if_fail (CHATTY_IS_PP_ACCOUNT (self));

  g_clear_handle_id (&self->connect_id, g_source_remove);

  if (!delay)
    account_connect (self);
  else
    self->connect_id = g_timeout_add (RECONNECT_DELAY,
                                      G_SOURCE_FUNC (account_connect),
                                      self);
}

void
chatty_pp_account_disconnect (ChattyPpAccount *self)
{
  g_autofree char *password = NULL;
  ChattyStatus status;

  g_return_if_fail (CHATTY_IS_PP_ACCOUNT (self));

  if (chatty_pp_account_is_sms (self))
    return;

  status = chatty_account_get_status (CHATTY_ACCOUNT (self));

  if (status == CHATTY_DISCONNECTED)
    return;

  password = g_strdup (chatty_account_get_password (CHATTY_ACCOUNT (self)));
  purple_account_disconnect (self->pp_account);
  chatty_account_set_password (CHATTY_ACCOUNT (self), password);
}

void
chatty_pp_account_set_features (ChattyPpAccount *self,
                                ChattyPpAccountFeatures features)
{
  g_return_if_fail (CHATTY_IS_PP_ACCOUNT (self));

  self->features = features;
}

void
chatty_pp_account_update_features (ChattyPpAccount *self,
                                   ChattyPpAccountFeatures features)

{
  g_return_if_fail (CHATTY_IS_PP_ACCOUNT (self));

  self->features |= features;
}

gboolean
chatty_pp_account_has_features (ChattyPpAccount *self,
                                ChattyPpAccountFeatures features)
{
  g_return_val_if_fail (CHATTY_IS_PP_ACCOUNT (self), CHATTY_PP_ACCOUNT_FEATURES_NONE);

  return !!(self->features & features);
}

ChattyPpAccountFeatures
chatty_pp_account_get_features (ChattyPpAccount *self)
{
  g_return_val_if_fail (CHATTY_IS_PP_ACCOUNT (self), CHATTY_PP_ACCOUNT_FEATURES_NONE);

  return self->features;
}
