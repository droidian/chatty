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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <handy.h>
#include <purple.h>

#include "chatty-settings.h"
#include "chatty-account.h"
#include "chatty-window.h"
#include "chatty-pp-chat.h"
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
  HdyValueObject *device_fp;
  GListStore     *fp_list;

  PurpleAccount  *pp_account;
  PurpleStoredImage *pp_avatar;
  GdkPixbuf         *avatar;
  guint           connect_id;
  ChattyProtocol  protocol;
  gboolean        has_encryption;

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

  g_debug ("connecting to %s", chatty_account_get_username (CHATTY_ACCOUNT (self)));
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

static const char *
chatty_pp_account_get_protocol_name (ChattyAccount *account)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  return purple_account_get_protocol_name (self->pp_account);
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

static const gchar *
chatty_pp_account_get_username (ChattyAccount *account)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  return purple_account_get_username (self->pp_account);
}

static void
chatty_pp_account_set_username (ChattyAccount *account,
                                const char    *username)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  purple_account_set_username (self->pp_account, username);
}

static GListModel *
chatty_pp_account_get_buddies (ChattyAccount *account)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  return G_LIST_MODEL (self->buddy_list);
}

static gboolean
chatty_pp_account_buddy_exists (ChattyAccount *account,
                                const char    *buddy_username)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;
  PurpleBuddy *buddy;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  if (!buddy_username || !*buddy_username)
    return FALSE;

  buddy = purple_find_buddy (self->pp_account, buddy_username);

  return buddy != NULL;
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
chatty_pp_account_connect (ChattyAccount *account,
                           gboolean       delay)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  g_clear_handle_id (&self->connect_id, g_source_remove);

  if (!delay)
    account_connect (self);
  else
    self->connect_id = g_timeout_add (RECONNECT_DELAY,
                                      G_SOURCE_FUNC (account_connect),
                                      self);
}

static void
chatty_pp_account_disconnect (ChattyAccount *account)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;
  g_autofree char *password = NULL;
  ChattyStatus status;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  if (chatty_item_is_sms (CHATTY_ITEM (self)))
    return;

  status = chatty_account_get_status (CHATTY_ACCOUNT (self));

  if (status == CHATTY_DISCONNECTED)
    return;

  password = g_strdup (chatty_account_get_password (CHATTY_ACCOUNT (self)));
  purple_account_disconnect (self->pp_account);
  chatty_account_set_password (CHATTY_ACCOUNT (self), password);
}

static void
chatty_pp_account_set_remember_password (ChattyAccount *account,
                                         gboolean       remember)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  purple_account_set_remember_password (self->pp_account, !!remember);
}

static void
chatty_pp_account_save (ChattyAccount *account)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;

  g_return_if_fail (CHATTY_IS_PP_ACCOUNT (self));

  /* purple adds the account only if not yet added */
  purple_accounts_add (self->pp_account);
}

static void
chatty_pp_account_delete (ChattyAccount *account)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;

  g_return_if_fail (CHATTY_IS_PP_ACCOUNT (self));

  purple_accounts_delete (self->pp_account);
}

static HdyValueObject *
chatty_pp_account_get_device_fp (ChattyAccount *account)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  return self->device_fp;
}

static GListModel *
chatty_pp_account_get_fp_list (ChattyAccount *account)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  return G_LIST_MODEL (self->fp_list);
}

static void
get_fp_list_cb (int         error,
                GHashTable *fp_table,
                gpointer    user_data)
{
  g_autoptr(GList) key_list = NULL;
  g_autoptr(GTask) task = user_data;
  ChattyPpAccount *self;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  if (error || !fp_table) {
    g_warning ("has error: %d", !!error);
    g_task_return_boolean (task, FALSE);

    return;
  }

  key_list = g_hash_table_get_keys (fp_table);
  g_clear_object (&self->device_fp);
  g_list_store_remove_all (self->fp_list);

  for (GList *item = key_list; item; item = item->next) {
    const char *fp = NULL;

    fp = g_hash_table_lookup (fp_table, item->data);

    g_debug ("DeviceId: %i fingerprint: %s", *((guint32 *) item->data),
             fp ? fp : "(no session)");

    if (fp) {
      char *id;

      id = g_strdup_printf ("%u", *((guint32 *) item->data));
      /* The first fingerprint is current device fingerprint */
      if (!self->device_fp) {
        self->device_fp = hdy_value_object_new_string (fp);
        g_object_set_data_full (G_OBJECT (self->device_fp), "device-id", id, g_free);
      } else {
        g_autoptr(HdyValueObject) object = NULL;

        object = hdy_value_object_new_string (fp);
        g_object_set_data_full (G_OBJECT (object), "device-id", id, g_free);
        g_list_store_append (self->fp_list, object);
      }
    }
  }

  g_task_return_boolean (task, TRUE);
}

static void
chatty_pp_account_load_fp_async (ChattyAccount       *account,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;
  GTask *task;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  if (!self->has_encryption) {
    CHATTY_ACCOUNT_CLASS (chatty_pp_account_parent_class)->load_fp_async (account, callback, user_data);

    return;
  }

  task = g_task_new (self, NULL, callback, user_data);

  purple_signal_emit (purple_plugins_get_handle (),
                      "lurch-fp-list",
                      self->pp_account,
                      get_fp_list_cb,
                      task);
}

static void
chatty_pp_account_join_chat_async (ChattyAccount       *account,
                                   ChattyChat          *chat,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;
  g_autoptr(GTask) task = NULL;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));
  g_return_if_fail (CHATTY_IS_PP_CHAT (chat));

  chatty_pp_chat_join (CHATTY_PP_CHAT (chat));

  /* Assume we succeeded! */
  task = g_task_new (self, NULL, callback, user_data);
  g_task_return_boolean (task, TRUE);
}

static void
chatty_pp_account_leave_chat_async (ChattyAccount       *account,
                                    ChattyChat          *chat,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;
  g_autoptr(GTask) task = NULL;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  chatty_pp_chat_leave (CHATTY_PP_CHAT (chat));

  /* Assume we succeeded! */
  task = g_task_new (self, NULL, callback, user_data);
  g_task_return_boolean (task, TRUE);
}

static void
chatty_pp_account_start_direct_chat_async (ChattyAccount       *account,
                                           GPtrArray           *buddies,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
  ChattyPpAccount *self = (ChattyPpAccount *)account;
  PurpleConversation *conv;
  const char *name;
  g_autoptr(GTask) task = NULL;

  g_assert (CHATTY_IS_PP_ACCOUNT (self));

  g_return_if_fail (buddies->len == 1);
  g_return_if_fail (purple_account_is_connected (self->pp_account));

  task = g_task_new (self, NULL, callback, user_data);
  name = buddies->pdata[0];
  conv = purple_find_conversation_with_account (PURPLE_CONV_TYPE_IM, name, self->pp_account);

  if (!conv)
    conv = purple_conversation_new (PURPLE_CONV_TYPE_IM, self->pp_account, name);

  purple_conversation_present (conv);
  g_ptr_array_unref (buddies);

  /* Just assume we succeeded, what else can we do! */
  g_task_return_boolean (task, TRUE);
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

  g_list_store_remove_all (self->fp_list);
  g_clear_object (&self->fp_list);
  g_clear_object (&self->device_fp);
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

  account_class->get_protocol_name = chatty_pp_account_get_protocol_name;
  account_class->get_status   = chatty_pp_account_get_status;
  account_class->get_username = chatty_pp_account_get_username;
  account_class->set_username = chatty_pp_account_set_username;
  account_class->get_buddies  = chatty_pp_account_get_buddies;
  account_class->buddy_exists = chatty_pp_account_buddy_exists;
  account_class->get_enabled  = chatty_pp_account_get_enabled;
  account_class->set_enabled  = chatty_pp_account_set_enabled;
  account_class->get_password = chatty_pp_account_get_password;
  account_class->set_password = chatty_pp_account_set_password;
  account_class->connect      = chatty_pp_account_connect;
  account_class->disconnect   = chatty_pp_account_disconnect;
  account_class->get_remember_password = chatty_pp_account_get_remember_password;
  account_class->set_remember_password = chatty_pp_account_set_remember_password;
  account_class->save = chatty_pp_account_save;
  account_class->delete = chatty_pp_account_delete;
  account_class->get_device_fp = chatty_pp_account_get_device_fp;
  account_class->get_fp_list = chatty_pp_account_get_fp_list;
  account_class->load_fp_async = chatty_pp_account_load_fp_async;
  account_class->join_chat_async = chatty_pp_account_join_chat_async;
  account_class->leave_chat_async = chatty_pp_account_leave_chat_async;
  account_class->start_direct_chat_async = chatty_pp_account_start_direct_chat_async;

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
  self->fp_list = g_list_store_new (HDY_TYPE_VALUE_OBJECT);
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
                       const char     *server_url,
                       gboolean        has_encryption)
{
  ChattyPpAccount *self;

  g_return_val_if_fail (protocol & (CHATTY_PROTOCOL_SMS |
                                    CHATTY_PROTOCOL_XMPP |
                                    CHATTY_PROTOCOL_MATRIX |
                                    CHATTY_PROTOCOL_TELEGRAM), NULL);
  g_return_val_if_fail (username && *username, NULL);

  self = g_object_new (CHATTY_TYPE_PP_ACCOUNT,
                       "protocols", protocol,
                       "username", username,
                       "server-url", server_url,
                       NULL);
  self->has_encryption = !!has_encryption;

  return self;
}

ChattyPpAccount *
chatty_pp_account_new_purple (PurpleAccount *account,
                              gboolean       has_encryption)
{
  ChattyPpAccount *self;

  g_return_val_if_fail (account, NULL);
  g_return_val_if_fail (!account->ui_data, NULL);

  self = g_object_new (CHATTY_TYPE_PP_ACCOUNT,
                       "purple-account", account,
                       NULL);
  self->has_encryption = !!has_encryption;

  return self;
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

ChattyChat *
chatty_pp_account_join_chat (ChattyPpAccount *self,
                             const char      *chat_id,
                             const char      *room_alias,
                             const char      *user_alias,
                             const char      *password)
{
  PurplePluginProtocolInfo *info;
  PurpleConnection *gc;
  PurpleGroup *group;
  PurpleChat *chat;
  GHashTable *hash = NULL;

  g_return_val_if_fail (CHATTY_IS_PP_ACCOUNT (self), NULL);
  g_return_val_if_fail (chat_id && *chat_id, NULL);

  if (!purple_account_is_connected (self->pp_account))
    return NULL;

  gc = purple_account_get_connection (self->pp_account);
  info = PURPLE_PLUGIN_PROTOCOL_INFO (purple_connection_get_prpl (gc));

  if (info->chat_info_defaults != NULL)
    hash = info->chat_info_defaults (gc, chat_id);

  if (user_alias && *user_alias)
    g_hash_table_replace (hash, "handle", g_strdup (user_alias));

  chat = purple_chat_new (self->pp_account, chat_id, hash);
  if ((group = purple_find_group ("Chats")) == NULL) {
    group = purple_group_new ("Chats");
    purple_blist_add_group (group, NULL);
  }

  purple_blist_add_chat (chat, group, NULL);
  purple_blist_alias_chat (chat, room_alias);
  purple_blist_node_set_bool ((PurpleBlistNode*)chat, "chatty-autojoin", TRUE);

  return chatty_pp_chat_get_object (chat);
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

const char *
chatty_pp_account_get_protocol_id (ChattyPpAccount *self)
{
  const char *id;

  g_return_val_if_fail (CHATTY_IS_PP_ACCOUNT (self), "");

  id = purple_account_get_protocol_id (self->pp_account);

  return id ? id : "";
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
