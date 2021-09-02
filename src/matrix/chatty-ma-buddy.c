/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-ma-buddy.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-ma-buddy"

#define _GNU_SOURCE
#include <string.h>
#include <glib/gi18n.h>

#include "matrix-utils.h"
#include "chatty-ma-buddy.h"

struct _ChattyMaBuddy
{
  ChattyItem      parent_instance;

  char           *matrix_id;
  char           *name;
  GList          *devices;

  MatrixApi      *matrix_api;
  MatrixEnc      *matrix_enc;

  /* generated using g_str_hash for faster comparison */
  guint           id_hash;
  gboolean        is_self;
};

struct _BuddyDevice
{

  char *device_id;
  char *device_name;
  char *curve_key; /* Public part Curve25519 identity key pair */
  char *ed_key;    /* Public part of Ed25519 fingerprint key pair */
  char *one_time_key;

  gboolean meagolm_v1;
  gboolean olm_v1;
};


G_DEFINE_TYPE (ChattyMaBuddy, chatty_ma_buddy, CHATTY_TYPE_ITEM)

enum {
  CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
chatty_ma_device_free (BuddyDevice *device)
{
  if (!device)
    return;

  g_free (device->device_id);
  g_free (device->device_name);
  g_free (device->curve_key);
  g_free (device->ed_key);
  g_free (device->one_time_key);
  g_free (device);
}

static ChattyProtocol
chatty_ma_buddy_get_protocols (ChattyItem *item)
{
  return CHATTY_PROTOCOL_MATRIX;
}

static gboolean
chatty_ma_buddy_matches (ChattyItem     *item,
                         const char     *needle,
                         ChattyProtocol  protocols,
                         gboolean        match_name)
{
  ChattyMaBuddy *self = (ChattyMaBuddy *)item;

  if (needle == self->matrix_id)
    return TRUE;

  if (!needle || !self->matrix_id)
    return FALSE;

  return strcasestr (needle, self->matrix_id) != NULL;
}

static const char *
chatty_ma_buddy_get_name (ChattyItem *item)
{
  ChattyMaBuddy *self = (ChattyMaBuddy *)item;

  g_assert (CHATTY_IS_MA_BUDDY (self));

  if (self->name)
    return self->name;

  if (self->matrix_id)
    return self->matrix_id;

  return "";
}

static void
chatty_ma_buddy_set_name (ChattyItem *item,
                          const char *name)
{
  ChattyMaBuddy *self = (ChattyMaBuddy *)item;

  g_assert (CHATTY_IS_MA_BUDDY (self));

  g_free (self->name);

  if (!name || !*name)
    self->name = NULL;
  else
    self->name = g_strdup (name);
}

/*
 * chatty_ma_buddy_get_username:
 *
 * Get the user id of @item. The id is usually a
 * fully qualified Matrix ID (@user:example.com),
 * but it can also be the username alone (user).
 *
 * Returns: (transfer none): the id of Buddy.
 * or an empty string if not found or on error.
 */
static const char *
chatty_ma_buddy_get_username (ChattyItem *item)
{
  ChattyMaBuddy *self = (ChattyMaBuddy *)item;

  g_assert (CHATTY_IS_MA_BUDDY (self));

  if (self->matrix_id)
    return self->matrix_id;

  return "";
}

static GdkPixbuf *
chatty_ma_buddy_get_avatar (ChattyItem *item)
{
  ChattyMaBuddy *self = (ChattyMaBuddy *)item;

  g_assert (CHATTY_IS_MA_BUDDY (self));

  return NULL;
}

static void
chatty_ma_buddy_dispose (GObject *object)
{
  ChattyMaBuddy *self = (ChattyMaBuddy *)object;

  g_clear_pointer (&self->matrix_id, g_free);
  g_clear_pointer (&self->name, g_free);

  g_clear_object (&self->matrix_api);
  g_clear_object (&self->matrix_enc);

  g_list_free_full (self->devices, (GDestroyNotify)chatty_ma_device_free);
  self->devices = NULL;

  G_OBJECT_CLASS (chatty_ma_buddy_parent_class)->dispose (object);
}

static void
chatty_ma_buddy_class_init (ChattyMaBuddyClass *klass)
{
  GObjectClass *object_class  = G_OBJECT_CLASS (klass);
  ChattyItemClass *item_class = CHATTY_ITEM_CLASS (klass);

  object_class->dispose = chatty_ma_buddy_dispose;

  item_class->get_protocols = chatty_ma_buddy_get_protocols;
  item_class->matches  = chatty_ma_buddy_matches;
  item_class->get_name = chatty_ma_buddy_get_name;
  item_class->set_name = chatty_ma_buddy_set_name;
  item_class->get_username = chatty_ma_buddy_get_username;
  item_class->get_avatar = chatty_ma_buddy_get_avatar;

  /**
   * ChattyMaBuddy::changed:
   * @self: a #ChattyMaBuddy
   *
   * changed signal is emitted when any detail
   * of the buddy changes.
   */
  signals [CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
chatty_ma_buddy_init (ChattyMaBuddy *self)
{
}

ChattyMaBuddy *
chatty_ma_buddy_new (const char *matrix_id,
                     MatrixApi  *api,
                     MatrixEnc  *enc)
{
  ChattyMaBuddy *self;

  g_return_val_if_fail (matrix_id && *matrix_id == '@', NULL);
  g_return_val_if_fail (MATRIX_IS_API (api), NULL);
  g_return_val_if_fail (MATRIX_IS_ENC (enc), NULL);

  self = g_object_new (CHATTY_TYPE_MA_BUDDY, NULL);
  self->matrix_id = g_strdup (matrix_id);
  self->matrix_api = g_object_ref (api);
  self->matrix_enc = g_object_ref (enc);

  if (g_str_equal (matrix_id, matrix_api_get_username (api)))
    self->is_self = TRUE;

  return self;
}

guint
chatty_ma_buddy_get_id_hash (ChattyMaBuddy *self)
{
  g_return_val_if_fail (CHATTY_IS_MA_BUDDY (self), 0);

  if (!self->id_hash && self->matrix_id)
    self->id_hash = g_str_hash (self->matrix_id);

  return self->id_hash;
}

void
chatty_ma_buddy_add_devices (ChattyMaBuddy *self,
                             JsonObject    *root)
{
  g_autoptr(GList) members = NULL;
  JsonObject *object, *child;
  BuddyDevice *device;

  g_return_if_fail (CHATTY_IS_MA_BUDDY (self));
  g_return_if_fail (root);

  members = json_object_get_members (root);

  for (GList *member = members; member; member = member->next) {
    g_autofree char *device_name = NULL;
    const char *device_id, *user, *key;
    JsonArray *array;
    char *key_name;

    child = matrix_utils_json_object_get_object (root, member->data);
    device_id = matrix_utils_json_object_get_string (child, "device_id");
    user = matrix_utils_json_object_get_string (child, "user_id");

    if (g_strcmp0 (user, self->matrix_id) != 0) {
      g_warning ("‘%s’ and ‘%s’ are not the same users", user, self->matrix_id);
      continue;
    }

    if (self->is_self &&
        g_strcmp0 (device_id, matrix_api_get_device_id (self->matrix_api)) == 0)
      continue;

    if (g_strcmp0 (member->data, device_id) != 0) {
      g_warning ("‘%s’ and ‘%s’ are not the same device", (char *)member->data, device_id);
      continue;
    }

    object = matrix_utils_json_object_get_object (child, "unsigned");
    device_name = g_strdup (matrix_utils_json_object_get_string (object, "device_display_name"));

    key_name = g_strconcat ("ed25519:", device_id, NULL);
    object = matrix_utils_json_object_get_object (child, "keys");
    key = matrix_utils_json_object_get_string (object, key_name);
    g_free (key_name);

    if (!matrix_enc_verify (self->matrix_enc, child, self->matrix_id, device_id, key)) {
      g_warning ("failed to verify signature for %s with device %s", self->matrix_id, device_id);
      continue;
    }

    device = g_new0 (BuddyDevice, 1);
    device->device_id = g_strdup (device_id);
    device->device_name = g_steal_pointer (&device_name);
    device->ed_key = g_strdup (key);

    key_name = g_strconcat ("curve25519:", device_id, NULL);
    object = matrix_utils_json_object_get_object (child, "keys");
    key = matrix_utils_json_object_get_string (object, key_name);
    device->curve_key = g_strdup (key);
    g_free (key_name);

    array = matrix_utils_json_object_get_array (child, "algorithms");
    for (guint i = 0; array && i < json_array_get_length (array); i++) {
      const char *algorithm;

      algorithm = json_array_get_string_element (array, i);
      if (g_strcmp0 (algorithm, ALGORITHM_MEGOLM) == 0)
        device->meagolm_v1 = TRUE;
      else if (g_strcmp0 (algorithm, ALGORITHM_OLM) == 0)
        device->olm_v1 = TRUE;
    }

    self->devices = g_list_prepend (self->devices, device);
  }
}

GList *
chatty_ma_buddy_get_devices (ChattyMaBuddy *self)
{
  g_return_val_if_fail (CHATTY_IS_MA_BUDDY (self), NULL);

  return g_list_copy (self->devices);
}

/**
 * chatty_ma_buddy_device_key_json:
 * @self: A #ChattyMaBuddy
 *
 * Get A JSON object with all the devices
 * that we don't have an one time key for.
 *
 * The JSON created will have the following format:
 *
 *  {
 *    "@alice:example.com": {
 *      "JLAFKJWSCS": "signed_curve25519"
 *     },
 *    "@bob:example.com": {
 *      "JOJOAEWBZY": "signed_curve25519"
 *  }
 *
 * Returns: (transfer full): A #JsonObject
 */
JsonObject *
chatty_ma_buddy_device_key_json (ChattyMaBuddy *self)
{
  JsonObject *object;

  g_return_val_if_fail (CHATTY_IS_MA_BUDDY (self), NULL);

  if (!self->devices)
    return NULL;

  object = json_object_new ();

  for (GList *node = self->devices; node; node = node->next) {
    BuddyDevice *device = node->data;

    if (!device->one_time_key)
      json_object_set_string_member (object, device->device_id, "signed_curve25519");
  }

  return object;
}

void
chatty_ma_buddy_add_one_time_keys (ChattyMaBuddy *self,
                                   JsonObject    *root)
{
  JsonObject *object, *child;

  g_return_if_fail (CHATTY_IS_MA_BUDDY (self));
  g_return_if_fail (root);

  for (GList *item = self->devices; item; item = item->next) {
    g_autoptr(GList) members = NULL;
    BuddyDevice *device = item->data;

    child = matrix_utils_json_object_get_object (root, device->device_id);

    if (!child) {
      g_warning ("device '%s' not found", device->device_id);
      continue;
    }

    members = json_object_get_members (child);

    for (GList *node = members; node; node = node->next) {
      object = matrix_utils_json_object_get_object (child, node->data);

      if (matrix_enc_verify (self->matrix_enc, object, self->matrix_id,
                             device->device_id, device->ed_key)) {
        const char *key;

        key = matrix_utils_json_object_get_string (object, "key");
        g_free (device->one_time_key);
        device->one_time_key = g_strdup (key);
      }
    }
  }
}

const char *
chatty_ma_device_get_id (BuddyDevice *device)
{
  g_return_val_if_fail (device, "");

  return device->device_id;
}

const char *
chatty_ma_device_get_ed_key (BuddyDevice *device)
{
  g_return_val_if_fail (device, "");

  return device->ed_key;
}

const char *
chatty_ma_device_get_curve_key (BuddyDevice *device)
{
  g_return_val_if_fail (device, "");

  return device->curve_key;
}

char *
chatty_ma_device_get_one_time_key (BuddyDevice *device)
{
  g_return_val_if_fail (device, g_strdup (""));

  return g_steal_pointer (&device->one_time_key);
}
