/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-avatar.c
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-avatar"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <handy.h>

#include "chatty-chat.h"
#include "chatty-mm-chat.h"
#include "chatty-avatar.h"

/**
 * SECTION: chatty-avatar
 * @title: ChattyAvatar
 * @short_description: Avatar Image widget for an Item
 * @include: "chatty-avatar.h"
 */

#define DEFAULT_SIZE 32
struct _ChattyAvatar
{
  GtkBin      parent_instance;

  GtkWidget  *avatar;
  ChattyItem *item;
};

G_DEFINE_TYPE (ChattyAvatar, chatty_avatar, GTK_TYPE_BIN)

enum {
  PROP_0,
  PROP_SIZE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
avatar_changed_cb (ChattyAvatar *self)
{
  GLoadableIcon *avatar = NULL;

  g_assert (CHATTY_IS_AVATAR (self));

  if (self->item)
    avatar = (GLoadableIcon *)chatty_item_get_avatar (self->item);

  hdy_avatar_set_loadable_icon (HDY_AVATAR (self->avatar), avatar);
}

static void
item_name_changed_cb (ChattyAvatar *self)
{
  chatty_avatar_set_title (self, chatty_item_get_name (self->item));

  if (CHATTY_IS_MM_CHAT (self->item)) {
    gboolean has_name;

    has_name = !g_str_equal (chatty_item_get_name (self->item),
                             chatty_chat_get_chat_name (CHATTY_CHAT (self->item)));
    hdy_avatar_set_show_initials (HDY_AVATAR (self->avatar), has_name);
  }
}

static void
chatty_avatar_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  ChattyAvatar *self = (ChattyAvatar *)object;

  switch (prop_id)
    {
    case PROP_SIZE:
      hdy_avatar_set_size (HDY_AVATAR (self->avatar), g_value_get_int (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
chatty_avatar_dispose (GObject *object)
{
  ChattyAvatar *self = (ChattyAvatar *)object;

  g_clear_object (&self->item);

  G_OBJECT_CLASS (chatty_avatar_parent_class)->dispose (object);
}

static void
chatty_avatar_class_init (ChattyAvatarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = chatty_avatar_set_property;
  object_class->dispose = chatty_avatar_dispose;

  properties[PROP_SIZE] =
    g_param_spec_int ("size",
                      "Size",
                      "Size of avatar",
                      0, 96, 32,
                      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
chatty_avatar_init (ChattyAvatar *self)
{
  self->avatar = g_object_new (HDY_TYPE_AVATAR,
                               "visible", TRUE,
                               "show-initials", TRUE,
                               NULL);

  gtk_container_add (GTK_CONTAINER (self), self->avatar);
}

/**
 * chatty_avatar_set_title:
 * @self: A #ChattyManager
 * @title: The title to be used to create avatar
 *
 * If @title is a non-empty string, it will be preferred
 * as the name to create avatar if a #ChattyItem isn’t
 * set, or the item doesn’t have an avatar set.
 */
void
chatty_avatar_set_title (ChattyAvatar *self,
                         const char   *title)
{
  g_return_if_fail (CHATTY_IS_AVATAR (self));

  /* Skip '@' prefix common in matrix usernames */
  if (title && title[0] == '@' && title[1])
    title++;

  /* We use dummy contact as a placeholder to create new chat */
  if (CHATTY_IS_CONTACT (self->item) &&
      chatty_contact_is_dummy (CHATTY_CONTACT (self->item)))
    title = "+";

  hdy_avatar_set_text (HDY_AVATAR (self->avatar), title);
}

void
chatty_avatar_set_item (ChattyAvatar *self,
                        ChattyItem   *item)
{
  g_return_if_fail (CHATTY_IS_AVATAR (self));
  g_return_if_fail (!item || CHATTY_IS_ITEM (item));

  if (self->item) {
    g_signal_handlers_disconnect_by_func (self->item,
                                          g_clear_object,
                                          &self->item);
    g_signal_handlers_disconnect_by_func (self->item,
                                          avatar_changed_cb,
                                          self);
    g_signal_handlers_disconnect_by_func (self->item,
                                          item_name_changed_cb,
                                          self);
  }

  if (!g_set_object (&self->item, item))
    return;

  gtk_widget_set_visible (GTK_WIDGET (self), !!self->item);
  chatty_avatar_set_title (self, NULL);
  avatar_changed_cb (self);

  /* We don’t emit notify signals as we don’t need it */
  if (self->item)
    {
      g_signal_connect_swapped (self->item, "deleted",
                                G_CALLBACK (g_clear_object), &self->item);
      g_signal_connect_object (self->item, "notify::name",
                               G_CALLBACK (item_name_changed_cb), self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (self->item, "avatar-changed",
                               G_CALLBACK (avatar_changed_cb), self,
                               G_CONNECT_SWAPPED);
      item_name_changed_cb (self);
    }
}
