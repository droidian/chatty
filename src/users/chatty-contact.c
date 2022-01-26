/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-contact.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-contact"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libebook-contacts/libebook-contacts.h>

#include "chatty-settings.h"
#include "chatty-utils.h"
#include "chatty-contact.h"
#include "chatty-contact-private.h"

#define ICON_SIZE 96

/**
 * SECTION: chatty-contact
 * @title: ChattyContact
 * @short_description: An abstraction over #FolksIndividual
 * @include: "chatty-contact.h"
 */

struct _ChattyContact
{
  ChattyItem       parent_instance;

  EContact        *e_contact;
  EVCardAttribute *attribute;
  ChattyProtocol   protocol;

  char       *name;
  char       *value;
  GdkPixbuf *avatar;
};

G_DEFINE_TYPE (ChattyContact, chatty_contact, CHATTY_TYPE_ITEM)


static ChattyProtocol
chatty_contact_get_protocols (ChattyItem *item)
{
  ChattyContact *self = (ChattyContact *)item;

  g_assert (CHATTY_IS_CONTACT (self));

  if (self->protocol != CHATTY_PROTOCOL_NONE)
    return self->protocol;

  return CHATTY_ITEM_CLASS (chatty_contact_parent_class)->get_protocols (item);
}


static gboolean
chatty_contact_matches (ChattyItem     *item,
                        const char     *needle,
                        ChattyProtocol  protocols,
                        gboolean        match_name)
{
  ChattyContact *self = (ChattyContact *)item;
  const char *value;
  ChattyProtocol protocol;

  g_assert (CHATTY_IS_CONTACT (self));

  value = chatty_item_get_username (item);
  protocol = chatty_item_get_protocols (item);

  if (protocol == CHATTY_PROTOCOL_MMS_SMS &&
      protocols & CHATTY_PROTOCOL_MMS_SMS) {
    ChattySettings *settings;
    const char *country;
    EPhoneNumberMatch match;

    if (strstr (value, needle))
      return TRUE;

    settings = chatty_settings_get_default ();
    country = chatty_settings_get_country_iso_code (settings);
    match = e_phone_number_compare_strings_with_region (value, needle, country, NULL);

    if (match == E_PHONE_NUMBER_MATCH_EXACT ||
        match == E_PHONE_NUMBER_MATCH_NATIONAL)
      return TRUE;

    if (g_str_equal (value, needle))
      return TRUE;
  }

  return FALSE;
}


static const char *
chatty_contact_get_name (ChattyItem *item)
{
  ChattyContact *self = (ChattyContact *)item;

  g_assert (CHATTY_IS_CONTACT (self));

  if (self->name)
    return self->name;

  if (self->e_contact) {
    const char *value;

    value = e_contact_get_const (self->e_contact, E_CONTACT_FULL_NAME);

    if (value)
      return value;
  }

  return "";
}

static const char *
chatty_contact_get_username (ChattyItem *item)
{
  ChattyContact *self = (ChattyContact *)item;

  g_assert (CHATTY_IS_CONTACT (self));

  if (!self->value && self->attribute)
    self->value = e_vcard_attribute_get_value (self->attribute);

  if (self->value)
    return self->value;

  return "";
}

static GdkPixbuf *
chatty_contact_get_avatar (ChattyItem *item)
{
  ChattyContact *self = (ChattyContact *)item;
  EContactPhoto *photo;

  g_assert (CHATTY_IS_CONTACT (self));

  if (self->avatar)
    return self->avatar;

  if (!self->e_contact)
    return NULL;

  photo = e_contact_get (self->e_contact, E_CONTACT_PHOTO);

  if (!photo)
    return NULL;

  if (photo->type == E_CONTACT_PHOTO_TYPE_URI) {
    g_autoptr(GFileInputStream) stream = NULL;
    g_autoptr(GFile) file = NULL;

    file = g_file_new_for_uri (e_contact_photo_get_uri (photo));
    stream = g_file_read (file, NULL, NULL);

    if (stream)
      self->avatar = gdk_pixbuf_new_from_stream (G_INPUT_STREAM (stream), NULL, NULL);
  } else {
    const guchar *data;
    gsize len;

    data = e_contact_photo_get_inlined (photo, &len);

    if (data)
      self->avatar = chatty_utils_get_pixbuf_from_data (data, len);
  }

  e_contact_photo_free (photo);

  return self->avatar;
}

static void
chatty_contact_get_avatar_async (ChattyItem          *item,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
  ChattyContact *self = (ChattyContact *)item;
  g_autoptr(GTask) task = NULL;

  g_assert (CHATTY_IS_CONTACT (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);

  self->avatar = chatty_contact_get_avatar (item);
  g_task_return_pointer (task, self->avatar, NULL);
}

static void
chatty_contact_dispose (GObject *object)
{
  ChattyContact *self = (ChattyContact *)object;

  g_clear_object (&self->e_contact);
  g_clear_object (&self->avatar);
  g_clear_pointer (&self->attribute, e_vcard_attribute_free);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->value, g_free);

  G_OBJECT_CLASS (chatty_contact_parent_class)->dispose (object);
}


static void
chatty_contact_class_init (ChattyContactClass *klass)
{
  GObjectClass *object_class  = G_OBJECT_CLASS (klass);
  ChattyItemClass *item_class = CHATTY_ITEM_CLASS (klass);

  object_class->dispose = chatty_contact_dispose;

  item_class->get_protocols = chatty_contact_get_protocols;
  item_class->matches  = chatty_contact_matches;
  item_class->get_name = chatty_contact_get_name;
  item_class->get_username = chatty_contact_get_username;
  item_class->get_avatar = chatty_contact_get_avatar;
  item_class->get_avatar_async  = chatty_contact_get_avatar_async;
}


static void
chatty_contact_init (ChattyContact *self)
{
}


/**
 * chatty_contact_new:
 * @contact: A #EContact
 * @attr: (transfer full): A #EvCardAttribute
 * @protocol: A #ChattyProtocol for the attribute @attr
 *
 * Create a new contact which represents the @attr of
 * the @contact.
 *
 * Currently, only %CHATTY_PROTOCOL_CALL and %CHATTY_PROTOCOL_SMS
 * is supported as @protocol.
 *
 * Returns: (transfer full): A #ChattyContact
 */
ChattyContact *
chatty_contact_new (EContact        *contact,
                    EVCardAttribute *attr,
                    ChattyProtocol   protocol)
{
  ChattyContact *self;

  self = g_object_new (CHATTY_TYPE_CONTACT, NULL);
  g_set_object (&self->e_contact, contact);
  self->attribute = attr;
  self->protocol  = protocol;

  return self;
}

ChattyContact *
chatty_contact_dummy_new (const char *name,
                          const char *value)
{
  ChattyContact *self;

  self = g_object_new (CHATTY_TYPE_CONTACT, NULL);
  self->name = g_strdup (name);
  self->value = g_strdup (value);
  g_object_set_data (G_OBJECT (self), "dummy", GINT_TO_POINTER (TRUE));

  return self;
}

void
chatty_contact_set_name (ChattyContact *self,
                         const char    *name)
{
  g_return_if_fail (CHATTY_IS_CONTACT (self));

  g_free (self->name);
  self->name = g_strdup (name);
}

void
chatty_contact_set_value (ChattyContact *self,
                          const char    *value)
{
  g_return_if_fail (CHATTY_IS_CONTACT (self));

  g_free (self->value);
  self->value = g_strdup (value);
}

/**
 * chatty_contact_get_value:
 * @self: A #ChattyContact
 *
 * Get the type of value stored in @self.
 * Eg: “Mobile”, “Work”, etc. translated to
 * the current locale.
 *
 * Returns: (transfer none): The value type of @self.
 */
const char *
chatty_contact_get_value_type (ChattyContact *self)
{
  g_return_val_if_fail (CHATTY_IS_CONTACT (self), NULL);

  if (!self->attribute)
    return "";

  if (e_vcard_attribute_has_type (self->attribute, "cell"))
    return _("Mobile: ");
  if (e_vcard_attribute_has_type (self->attribute, "work"))
    return _("Work: ");
  if (e_vcard_attribute_has_type (self->attribute, "other"))
    return _("Other: ");

  return "";
}


/**
 * chatty_contact_get_uid:
 * @self: A #ChattyContact
 *
 * A unique ID reperesenting the contact.  This
 * ID won’t change unless the contact is modified.
 *
 * Returns: (transfer none): A unique ID of @self.
 */
const char *
chatty_contact_get_uid (ChattyContact *self)
{
  g_return_val_if_fail (CHATTY_IS_CONTACT (self), "");

  if (self->e_contact)
    return e_contact_get_const (self->e_contact, E_CONTACT_UID);

  return "";
}

gboolean
chatty_contact_is_exact_match (ChattyContact  *self,
                               const char     *value,
                               ChattyProtocol  protocols)
{
  const char *contact_value;
  ChattyProtocol protocol;

  g_assert (CHATTY_IS_CONTACT (self));

  contact_value = chatty_item_get_username (CHATTY_ITEM (self));
  protocol = chatty_item_get_protocols (CHATTY_ITEM (self));

  if (protocol & (CHATTY_PROTOCOL_MMS_SMS | CHATTY_PROTOCOL_MMS) &&
      protocols & (CHATTY_PROTOCOL_MMS_SMS | CHATTY_PROTOCOL_MMS)) {
    ChattySettings *settings;
    const char *country;
    EPhoneNumberMatch match;

    if (g_str_equal (contact_value, value))
      return TRUE;

    settings = chatty_settings_get_default ();
    country = chatty_settings_get_country_iso_code (settings);
    match = e_phone_number_compare_strings_with_region (contact_value, value, country, NULL);

    if (match == E_PHONE_NUMBER_MATCH_EXACT ||
        match == E_PHONE_NUMBER_MATCH_NATIONAL)
      return TRUE;
  }

  return FALSE;
}

/**
 * chatty_contact_clear_cache:
 * @self: #ChattyContact
 *
 * Reset the values cached in @self.
 * This API is only to be used by contact-provider.
 */
void
chatty_contact_clear_cache (ChattyContact *self)
{
  g_return_if_fail (CHATTY_IS_CONTACT (self));

  g_clear_object (&self->avatar);
}

gboolean
chatty_contact_is_dummy (ChattyContact *self)
{
  g_return_val_if_fail (CHATTY_IS_CONTACT (self), TRUE);

  return !!g_object_get_data (G_OBJECT (self), "dummy");
}
