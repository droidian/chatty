/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* matrix-enc.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-matrix-enc"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <json-glib/json-glib.h>
#include <olm/olm.h>
#include <sys/random.h>

#include "chatty-settings.h"
#include "chatty-ma-buddy.h"
#include "matrix-utils.h"
#include "matrix-db.h"
#include "matrix-enc.h"
#include "chatty-log.h"

#define KEY_LABEL_SIZE    6
#define STRING_ALLOCATION 512

/**
 * SECTION: chatty-contact
 * @title: MatrixEnc
 * @short_description: An abstraction over #FolksIndividual
 * @include: "chatty-contact.h"
 */

/*
 * Documentations:
 *   https://matrix.org/docs/guides/end-to-end-encryption-implementation-guide
 *
 * Other:
 *  * We use g_malloc(size) instead of g_malloc(size * sizeof(type)) for all ‘char’
 *    and ‘[u]int8_t’, unless there is a possibility that the type can change.
 */
struct _MatrixEnc
{
  GObject parent_instance;

  OlmAccount *account;
  OlmUtility *utility;
  char       *pickle_key;

  GHashTable *in_olm_sessions;
  GHashTable *out_olm_sessions;
  GHashTable *in_group_sessions;
  GHashTable *out_group_sessions;

  /* Use something better, like sqlite */
  MatrixDb   *matrix_db;

  char *user_id;
  char *device_id;

  char *curve_key; /* Public part of Curve25519 identity key */
  char *ed_key;    /* Public part of Ed25519 fingerprint key */
};

G_DEFINE_TYPE (MatrixEnc, matrix_enc, G_TYPE_OBJECT)

static void
free_olm_session (gpointer data)
{
  olm_clear_session (data);
  g_free (data);
}

static void
free_in_group_session (gpointer data)
{
  olm_clear_inbound_group_session (data);
  g_free (data);
}

static void
free_out_group_session (gpointer data)
{
  olm_clear_outbound_group_session (data);
  g_free (data);
}

static void
free_all_details (MatrixEnc *self)
{
  if (self->account)
    olm_clear_account (self->account);

  g_clear_pointer (&self->account, g_free);
  g_hash_table_remove_all (self->in_olm_sessions);
  g_hash_table_remove_all (self->out_olm_sessions);
  g_hash_table_remove_all (self->in_group_sessions);
  g_hash_table_remove_all (self->out_group_sessions);
}

static char *
ma_olm_encrypt (OlmSession *session,
                const char *plain_text)
{
  g_autofree char *encrypted = NULL;
  g_autofree void *random = NULL;
  size_t length, rand_len;

  g_assert (session);

  if (!plain_text)
    return NULL;

  rand_len = olm_encrypt_random_length (session);
  random = g_malloc (rand_len);
  getrandom (random, rand_len, GRND_NONBLOCK);

  length = olm_encrypt_message_length (session, strlen (plain_text));
  encrypted = g_malloc (length + 1);
  length = olm_encrypt (session, plain_text, strlen (plain_text),
                        random, rand_len,
                        encrypted, length);

  if (length == olm_error ()) {
    g_warning ("Error encrypting: %s", olm_session_last_error (session));

    return NULL;
  }
  encrypted[length] = '\0';

  return g_steal_pointer (&encrypted);
}

static OlmSession *
ma_create_olm_out_session (MatrixEnc  *self,
                           const char *curve_key,
                           const char *one_time_key)
{
  g_autofree OlmSession *session = NULL;
  g_autofree void *buffer = NULL;
  size_t length, error;

  g_assert (MATRIX_ENC (self));

  if (!curve_key || !one_time_key)
    return NULL;

  session = g_malloc (olm_session_size ());
  olm_session (session);

  length = olm_create_outbound_session_random_length (session);
  buffer = g_malloc (length);
  getrandom (buffer, length, GRND_NONBLOCK);

  error = olm_create_outbound_session (session,
                                       self->account,
                                       curve_key, strlen (curve_key),
                                       one_time_key, strlen (one_time_key),
                                       buffer, length);
  olm_encrypt_message_type (session);
  if (error == olm_error ()) {
    g_warning ("Error creating outbound olm session: %s",
               olm_session_last_error (session));
    return NULL;
  }

  return g_steal_pointer (&session);
}

/*
 * matrix_enc_load_identity_keys:
 * @self: A #MatrixEnc
 *
 * Load the public part of Ed25519 fingerprint
 * key pair and Curve25519 identity key pair.
 */
static void
matrix_enc_load_identity_keys (MatrixEnc *self)
{
  g_autoptr(JsonParser) parser = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *key = NULL;
  JsonObject *object;
  JsonNode *node;
  size_t length, err;

  length = olm_account_identity_keys_length (self->account);
  key = malloc (length + 1);
  err = olm_account_identity_keys (self->account, key, length);
  key[length] = '\0';

  if (err == olm_error ()) {
    g_warning ("error getting identity keys: %s", olm_account_last_error (self->account));
    return;
  }

  parser = json_parser_new ();
  json_parser_load_from_data (parser, key, length, &error);

  if (error) {
    g_warning ("error parsing keys: %s", error->message);
    return;
  }

  node = json_parser_get_root (parser);
  object = json_node_get_object (node);

  g_free (self->curve_key);
  g_free (self->ed_key);

  self->curve_key = g_strdup (json_object_get_string_member (object, "curve25519"));
  self->ed_key = g_strdup (json_object_get_string_member (object, "ed25519"));
}

static void
create_new_details (MatrixEnc *self)
{
  g_autofree void *buffer = NULL;
  size_t length, err;

  g_assert (MATRIX_ENC (self));

  CHATTY_TRACE_MSG ("Creating new encryption keys");

  free_all_details (self);

  self->account = g_malloc (olm_account_size ());
  olm_account (self->account);

  matrix_utils_free_buffer (self->pickle_key);
  self->pickle_key = g_uuid_string_random ();

  length = olm_create_account_random_length (self->account);
  buffer = g_malloc (length);
  getrandom (buffer, length, GRND_NONBLOCK);
  err = olm_create_account (self->account, buffer, length);
  if (err == olm_error ())
    g_warning ("Error creating account: %s", olm_account_last_error (self->account));
}

static void
matrix_enc_sign_json_object (MatrixEnc  *self,
                             JsonObject *object)
{
  g_autoptr(GString) str = NULL;
  g_autofree char *signature = NULL;
  g_autofree char *label = NULL;
  JsonObject *sign, *child;

  g_assert (MATRIX_IS_ENC (self));
  g_assert (object);

  /* The JSON is in canonical form.  Required for signing */
  /* https://matrix.org/docs/spec/appendices#signing-json */
  str = matrix_utils_json_get_canonical (object, NULL);
  signature = matrix_enc_sign_string (self, str->str, str->len);

  sign = json_object_new ();
  label = g_strconcat ("ed25519:", self->device_id, NULL);
  json_object_set_string_member (sign, label, signature);

  child = json_object_new ();
  json_object_set_object_member (child, self->user_id, sign);
  json_object_set_object_member (object, "signatures", child);
}

static void
matrix_enc_finalize (GObject *object)
{
  MatrixEnc *self = (MatrixEnc *)object;

  olm_clear_account (self->account);
  g_free (self->account);

  olm_clear_utility (self->utility);
  g_free (self->utility);

  g_hash_table_unref (self->in_olm_sessions);
  g_hash_table_unref (self->out_olm_sessions);
  g_hash_table_unref (self->in_group_sessions);
  g_hash_table_unref (self->out_group_sessions);
  g_free (self->user_id);
  g_free (self->device_id);
  matrix_utils_free_buffer (self->pickle_key);
  matrix_utils_free_buffer (self->curve_key);
  matrix_utils_free_buffer (self->ed_key);
  g_clear_object (&self->matrix_db);

  G_OBJECT_CLASS (matrix_enc_parent_class)->finalize (object);
}


static void
matrix_enc_class_init (MatrixEncClass *klass)
{
  GObjectClass *object_class  = G_OBJECT_CLASS (klass);

  object_class->finalize = matrix_enc_finalize;
}


static void
matrix_enc_init (MatrixEnc *self)
{
  self->utility = g_malloc (olm_utility_size ());
  olm_utility (self->utility);

  self->in_olm_sessions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                 g_free, free_olm_session);
  self->out_olm_sessions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, free_olm_session);
  self->in_group_sessions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   g_free, free_in_group_session);
  self->out_group_sessions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                    g_free, free_out_group_session);
}

/**
 * matrix_enc_new:
 * @pickle: (nullable): The account pickle
 * @key: @pickle key, can be %NULL if @pickle is %NULL
 *
 * If @pickle is non-null, the olm account is created
 * using the pickled data.  Otherwise a new olm account
 * is created. If @pickle is non-null and invalid
 * %NULL is returned.
 *
 * For @self to be ready for use, the details of @self
 * should be set with matrix_enc_set_details().
 *
 * Also see matrix_enc_get_pickle().
 *
 * Returns: (transfer full) (nullable): A new #MatrixEnc.
 * Free with g_object_unref()
 */
MatrixEnc *
matrix_enc_new (gpointer    matrix_db,
                const char *pickle,
                const char *key)
{
  g_autoptr(MatrixEnc) self = NULL;

  g_return_val_if_fail (!pickle || (*pickle && key && *key), NULL);

  self = g_object_new (MATRIX_TYPE_ENC, NULL);
  g_set_object (&self->matrix_db, matrix_db);

  /* Deserialize the pickle to create the account */
  if (pickle && *pickle) {
    g_autofree char *duped = NULL;
    size_t err;

    self->pickle_key = g_strdup (key);
    self->account = g_malloc (olm_account_size ());
    olm_account (self->account);

    duped = g_strdup (pickle);
    err = olm_unpickle_account (self->account, key, strlen (key),
                                duped, strlen (duped));

    if (err == olm_error ()) {
      g_warning ("Error account unpickle: %s", olm_account_last_error (self->account));
      return NULL;
    }
  } else {
    create_new_details (self);
  }

  matrix_enc_load_identity_keys (self);

  return g_steal_pointer (&self);
}

/**
 * matrix_enc_set_details:
 * @self: A #MatrixEnc
 * @user_id: Fully qualified Matrix user ID
 * @device_id: The device id string
 *
 * Set user id and device id of @self.  @user_id
 * should be fully qualified Matrix user ID
 * (ie, @user:example.com)
 */
void
matrix_enc_set_details (MatrixEnc  *self,
                        const char *user_id,
                        const char *device_id)
{
  g_autofree char *old_user = NULL;
  g_autofree char *old_device = NULL;

  g_return_if_fail (MATRIX_IS_ENC (self));
  g_return_if_fail (!user_id || *user_id == '@');

  old_user = self->user_id;
  old_device = self->device_id;

  self->user_id = g_strdup (user_id);
  self->device_id = g_strdup (device_id);

  if (self->user_id && old_device &&
      g_strcmp0 (device_id, old_device) == 0) {
    create_new_details (self);
    matrix_enc_load_identity_keys (self);
  }
}

char *
matrix_enc_get_account_pickle (MatrixEnc *self)
{
  g_autofree char *pickle = NULL;
  size_t length, err;

  g_return_val_if_fail (MATRIX_IS_ENC (self), NULL);

  length = olm_pickle_account_length (self->account);
  pickle = malloc (length + 1);
  err = olm_pickle_account (self->account, self->pickle_key,
                            strlen (self->pickle_key), pickle, length);
  pickle[length] = '\0';

  if (err == olm_error ()) {
    g_warning ("Error getting account pickle: %s", olm_account_last_error (self->account));

    return NULL;
  }

  return g_steal_pointer (&pickle);
}

char *
matrix_enc_get_pickle_key (MatrixEnc *self)
{
  g_return_val_if_fail (MATRIX_ENC (self), NULL);

  return g_strdup (self->pickle_key);
}

/**
 * matrix_enc_sign_string:
 * @self: A #MatrixEnc
 * @str: A string to sign
 * @len: The length of @str, or -1
 *
 * Sign @str and return the signature.
 * Returns %NULL on error.
 *
 * Returns: (transfer full): The signature string.
 * Free with g_free()
 */
char *
matrix_enc_sign_string (MatrixEnc  *self,
                        const char *str,
                        size_t      len)
{
  char *signature;
  size_t length, err;

  g_return_val_if_fail (MATRIX_IS_ENC (self), NULL);
  g_return_val_if_fail (str, NULL);
  g_return_val_if_fail (*str, NULL);

  if (len == (size_t) -1)
    len = strlen (str);

  length = olm_account_signature_length (self->account);
  signature = malloc (length + 1);
  err = olm_account_sign (self->account, str, len, signature, length);
  signature[length] = '\0';

  if (err == olm_error ()) {
    g_warning ("Error signing data: %s", olm_account_last_error (self->account));

    return NULL;
  }

  return signature;
}

/**
 * matrix_enc_verify:
 * @self: A #MatrixEnc
 * @object: A #JsonObject
 * @matrix_id: A Fully qualified Matrix ID
 * @device_id: The device id string.
 * @ed_key: The ED25519 key of @matrix_id
 *
 * Verify if the content in @object is signed by
 * the user @matrix_id with device @device_id.
 *
 * This function may modify @object by removing
 * "signatures" and "unsigned" members.
 *
 * Returns; %TRUE if verification succeeded.  Or
 * %FALSE otherwise.
 */
gboolean
matrix_enc_verify (MatrixEnc  *self,
                   JsonObject *object,
                   const char *matrix_id,
                   const char *device_id,
                   const char *ed_key)
{
  JsonNode *signatures, *non_signed;
  g_autoptr(GString) json_str = NULL;
  g_autofree char *signature = NULL;
  g_autofree char *key_name = NULL;
  JsonObject *child;
  size_t error;

  if (!object)
    return FALSE;

  g_return_val_if_fail (MATRIX_IS_ENC (self), FALSE);
  g_return_val_if_fail (matrix_id && *matrix_id == '@', FALSE);
  g_return_val_if_fail (device_id && *device_id, FALSE);
  g_return_val_if_fail (ed_key && *ed_key, FALSE);

  /* https://matrix.org/docs/spec/appendices#checking-for-a-signature */
  key_name = g_strconcat ("ed25519:", device_id, NULL);
  child = matrix_utils_json_object_get_object (object, "signatures");
  child = matrix_utils_json_object_get_object (child, matrix_id);
  signature = g_strdup (matrix_utils_json_object_get_string (child, key_name));

  if (!signature)
    return FALSE;

  signatures = json_object_dup_member (object, "signatures");
  non_signed = json_object_dup_member (object, "signatures");
  json_object_remove_member (object, "signatures");
  json_object_remove_member (object, "unsigned");

  json_str = matrix_utils_json_get_canonical (object, NULL);

  if (signatures)
    json_object_set_member (object, "signatures", signatures);
  if (non_signed)
    json_object_set_member (object, "unsigned", non_signed);

  error = olm_ed25519_verify (self->utility,
                              ed_key, ED25519_SIZE,
                              json_str->str, json_str->len,
                              signature, strlen (signature));

  /* XXX: the libolm documentation is not much clear on this */
  if (error == olm_error ()) {
    g_debug ("Error verifying signature: %s", olm_utility_last_error (self->utility));
    return FALSE;
  }

  return TRUE;
}

/**
 * matrix_enc_max_one_time_keys:
 * @self: A #MatrixEnc
 *
 * Get the maximum number of one time keys Olm
 * library can handle.
 *
 * Returns: The number of maximum one-time keys.
 */
size_t
matrix_enc_max_one_time_keys (MatrixEnc *self)
{
  g_return_val_if_fail (MATRIX_IS_ENC (self), 0);

  return olm_account_max_number_of_one_time_keys (self->account);
}

/**
 * matrix_enc_create_one_time_keys:
 * @self: A #MatrixEnc
 * @count: A non-zero number
 *
 * Generate @count number of curve25519 one time keys.
 * @count is capped to the half of what Olm library
 * can handle.
 *
 * Returns: The number of one-time keys generated.
 * It will be <= @count.
 */
size_t
matrix_enc_create_one_time_keys (MatrixEnc *self,
                                 size_t     count)
{
  g_autofree void *buffer = NULL;
  size_t length, err;

  g_return_val_if_fail (MATRIX_IS_ENC (self), 0);
  g_return_val_if_fail (count, 0);

  /* doc: The maximum number of active keys supported by libolm
     is returned by olm_account_max_number_of_one_time_keys.
     The client should try to maintain about half this number on the homeserver. */
  count = MIN (count, olm_account_max_number_of_one_time_keys (self->account) / 2);

  length = olm_account_generate_one_time_keys_random_length (self->account, count);
  buffer = g_malloc (length);
  getrandom (buffer, length, GRND_NONBLOCK);
  err = olm_account_generate_one_time_keys (self->account, count, buffer, length);

  if (err == olm_error ()) {
    g_warning ("Error creating one time keys: %s", olm_account_last_error (self->account));

    return 0;
  }

  return count;
}

/**
 * matrix_enc_publish_one_time_keys:
 * @self: A #MatrixEnc
 *
 * Mark current set of one-time keys as published
 */
void
matrix_enc_publish_one_time_keys (MatrixEnc *self)
{

  g_return_if_fail (MATRIX_IS_ENC (self));

  olm_account_mark_keys_as_published (self->account);
}

/**
 * matrix_enc_get_one_time_keys:
 * @self: A #MatrixEnc
 *
 * Get public part of unpublished Curve25519 one-time keys in @self.
 *
 * The returned data is a JSON-formatted object with the single
 * property curve25519, which is itself an object mapping key id
 * to base64-encoded Curve25519 key. For example:
 *
 * {
 *     "curve25519": {
 *         "AAAAAA": "wo76WcYtb0Vk/pBOdmduiGJ0wIEjW4IBMbbQn7aSnTo",
 *         "AAAAAB": "LRvjo46L1X2vx69sS9QNFD29HWulxrmW11Up5AfAjgU"
 *     }
 * }
 *
 * Returns: (nullable) (transfer full): The unpublished one time keys.
 * Free with g_free()
 */
JsonObject *
matrix_enc_get_one_time_keys (MatrixEnc *self)
{
  g_autoptr(JsonParser) parser = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *buffer = NULL;
  size_t length, err;

  g_return_val_if_fail (MATRIX_IS_ENC (self), NULL);

  length = olm_account_one_time_keys_length (self->account);
  buffer = g_malloc (length + 1);
  err = olm_account_one_time_keys (self->account, buffer, length);
  buffer[length] = '\0';

  if (err == olm_error ()) {
    g_warning ("Error getting one time keys: %s", olm_account_last_error (self->account));

    return NULL;
  }

  /* Return NULL if there are no keys */
  if (g_str_equal (buffer, "{\"curve25519\":{}}"))
    return NULL;

  parser = json_parser_new ();
  json_parser_load_from_data (parser, buffer, length, &error);

  if (error) {
    g_warning ("error parsing keys: %s", error->message);
    return NULL;
  }

  return json_node_dup_object (json_parser_get_root (parser));
}

/**
 * matrix_enc_get_one_time_keys_json:
 * @self: A #MatrixEnc
 *
 * Get the signed Curve25519 one-time keys JSON.  The JSON shall
 * be in the following format:
 *
 * {
 *   "signed_curve25519:AAAAHg": {
 *     "key": "zKbLg+NrIjpnagy+pIY6uPL4ZwEG2v+8F9lmgsnlZzs",
 *     "signatures": {
 *       "@alice:example.com": {
 *         "ed25519:JLAFKJWSCS": "FLWxXqGbwrb8SM3Y795eB6OA8bwBcoMZFXBqnTn58AYWZSqiD45tlBVcDa2L7RwdKXebW/VzDlnfVJ+9jok1Bw"
 *       }
 *     }
 *   },
 *   "signed_curve25519:AAAAHQ": {
 *     "key": "j3fR3HemM16M7CWhoI4Sk5ZsdmdfQHsKL1xuSft6MSw",
 *     "signatures": {
 *       "@alice:example.com": {
 *         "ed25519:JLAFKJWSCS": "IQeCEPb9HFk217cU9kw9EOiusC6kMIkoIRnbnfOh5Oc63S1ghgyjShBGpu34blQomoalCyXWyhaaT3MrLZYQAA"
 *       }
 *     }
 *   }
 * }
 *
 * Returns: (transfer full): A JSON encoded string.
 * Free with g_free()
 */
char *
matrix_enc_get_one_time_keys_json (MatrixEnc *self)
{
  g_autoptr(JsonObject) object = NULL;
  g_autoptr(JsonObject) root = NULL;
  g_autoptr(GList) members = NULL;
  JsonObject *keys, *child;

  g_return_val_if_fail (MATRIX_IS_ENC (self), NULL);

  object = matrix_enc_get_one_time_keys (self);

  if (!object)
    return NULL;

  keys = json_object_new ();
  object = json_object_get_object_member (object, "curve25519");
  members = json_object_get_members (object);

  for (GList *item = members; item; item = item->next) {
    g_autofree char *label = NULL;
    const char *value;

    child = json_object_new ();
    value = json_object_get_string_member (object, item->data);
    json_object_set_string_member (child, "key", value);
    matrix_enc_sign_json_object (self, child);

    label = g_strconcat ("signed_curve25519:", item->data, NULL);
    json_object_set_object_member (keys, label, child);
  }

  root = json_object_new ();
  json_object_set_object_member (root, "one_time_keys", keys);

  return matrix_utils_json_object_to_string (root, FALSE);
}

/**
 * matrix_enc_get_one_time_keys:
 * @self: A #MatrixEnc
 *
 * Get the signed device key JSON.  The JSON shall
 * be in the following format:
 *
 * {
 *   "user_id": "@alice:example.com",
 *   "device_id": "JLAFKJWSCS",
 *   "algorithms": [
 *     "m.olm.curve25519-aes-sha256",
 *     "m.megolm.v1.aes-sha2"
 *   ],
 *   "keys": {
 *     "curve25519:JLAFKJWSCS": "3C5BFWi2Y8MaVvjM8M22DBmh24PmgR0nPvJOIArzgyI",
 *     "ed25519:JLAFKJWSCS": "lEuiRJBit0IG6nUf5pUzWTUEsRVVe/HJkoKuEww9ULI"
 *   },
 *   "signatures": {
 *     "@alice:example.com": {
 *       "ed25519:JLAFKJWSCS": "dSO80A01XiigH3uBiDVx/EjzaoycHcjq9lfQX0uWsqxl2giMIiSPR8a4d291W1ihKJL/a+myXS367WT6NAIcBA"
 *     }
 *   }
 * }
 *
 * Returns: (nullable): A JSON encoded string.
 * Free with g_free()
 */
char *
matrix_enc_get_device_keys_json (MatrixEnc *self)
{
  g_autoptr(JsonObject) root = NULL;
  JsonObject *keys, *device_keys;
  JsonArray *array;
  char *label;

  g_return_val_if_fail (MATRIX_IS_ENC (self), NULL);
  g_return_val_if_fail (self->user_id, NULL);
  g_return_val_if_fail (self->device_id, NULL);

  device_keys = json_object_new ();
  json_object_set_string_member (device_keys, "user_id", self->user_id);
  json_object_set_string_member (device_keys, "device_id", self->device_id);

  array = json_array_new ();
  json_array_add_string_element (array, ALGORITHM_OLM);
  json_array_add_string_element (array, ALGORITHM_MEGOLM);
  json_object_set_array_member (device_keys, "algorithms", array);

  keys = json_object_new ();

  label = g_strconcat ("curve25519:", self->device_id, NULL);
  json_object_set_string_member (keys, label, self->curve_key);
  g_free (label);

  label = g_strconcat ("ed25519:", self->device_id, NULL);
  json_object_set_string_member (keys, label, self->ed_key);
  g_free (label);

  json_object_set_object_member (device_keys, "keys", keys);
  matrix_enc_sign_json_object (self, device_keys);

  root = json_object_new ();
  json_object_set_object_member (root, "device_keys", device_keys);

  return matrix_utils_json_object_to_string (root, FALSE);
}

static gboolean
in_olm_matches (gpointer key,
                gpointer value,
                gpointer user_data)
{
  g_autofree char *body = NULL;
  size_t match;

  body = g_strdup (user_data);
  match = olm_matches_inbound_session (value, body, strlen (body));

  if (match == olm_error ()) {
    g_warning ("Error matching inbound session: %s", olm_session_last_error (key));
    return FALSE;
  }

  return match;
}

static void
handle_m_room_key (MatrixEnc  *self,
                   JsonObject *root,
                   const char *sender_key)
{
  g_autofree OlmInboundGroupSession *session = NULL;
  JsonObject *object;
  const char *session_key, *session_id, *room_id;

  g_assert (MATRIX_IS_ENC (self));
  g_assert (root);

  session = g_malloc (olm_inbound_group_session_size ());
  olm_inbound_group_session (session);

  object = matrix_utils_json_object_get_object (root, "content");
  session_key = matrix_utils_json_object_get_string (object, "session_key");
  session_id = matrix_utils_json_object_get_string (object, "session_id");
  room_id = matrix_utils_json_object_get_string (object, "room_id");

  if (session_key) {
    size_t error;

    error = olm_init_inbound_group_session (session, (gpointer)session_key,
                                            strlen (session_key));
    if (error == olm_error ())
      g_warning ("Error creating group session from key: %s", olm_inbound_group_session_last_error (session));

    if (!error) {
      g_autofree char *pickle = NULL;
      int length;

      length = olm_pickle_inbound_group_session_length (session);
      pickle = g_malloc (length + 1);
      olm_pickle_inbound_group_session (session, self->pickle_key,
                                        strlen (self->pickle_key),
                                        pickle, length);
      pickle[length] = '\0';
      CHATTY_TRACE (room_id, "saving session, room id: ");
      if (self->matrix_db)
        matrix_db_add_session_async (self->matrix_db, self->user_id, self->device_id,
                                     room_id, session_id, sender_key,
                                     g_steal_pointer (&pickle),
                                     SESSION_MEGOLM_V1_IN, NULL, NULL);
      g_hash_table_insert (self->in_group_sessions, g_strdup (session_id),
                           g_steal_pointer (&session));

    }
  }
}

void
matrix_enc_handle_room_encrypted (MatrixEnc  *self,
                                  JsonObject *object)
{
  const char *algorithm, *sender, *sender_key;
  g_autofree char *plaintext = NULL;
  g_autofree char *body = NULL;
  g_autofree char *copy = NULL;
  OlmSession *session;
  size_t error, length;
  int type;

  g_return_if_fail (MATRIX_IS_ENC (self));
  g_return_if_fail (object);

  sender = matrix_utils_json_object_get_string (object, "sender");
  object = matrix_utils_json_object_get_object (object, "content");
  algorithm = matrix_utils_json_object_get_string (object, "algorithm");
  sender_key = matrix_utils_json_object_get_string (object, "sender_key");

  if (!algorithm || !sender_key || !sender)
    g_return_if_reached ();

  if (!g_str_equal (algorithm, ALGORITHM_MEGOLM) &&
      !g_str_equal (algorithm, ALGORITHM_OLM))
    g_return_if_reached ();

  object = matrix_utils_json_object_get_object (object, "ciphertext");
  object = matrix_utils_json_object_get_object (object, self->curve_key);

  body = g_strdup (matrix_utils_json_object_get_string (object, "body"));
  type = matrix_utils_json_object_get_int (object, "type");

  if (!body)
    return;

  if (type == OLM_MESSAGE_TYPE_PRE_KEY) {
    session = g_hash_table_find (self->in_olm_sessions, in_olm_matches, body);
    CHATTY_TRACE_MSG ("message with pre key received, session exits: %d", !!session);

    if (!session) {
      g_autofree char *body_copy = NULL;
      session = g_malloc (olm_session_size ());
      olm_session (session);

      body_copy = g_strdup (body);
      error = olm_create_inbound_session_from (session, self->account,
                                               sender_key, strlen (sender_key),
                                               body_copy, strlen (body_copy));
      if (error == olm_error ()) {
        g_warning ("Error creating session: %s", olm_session_last_error (session));
        free_olm_session (session);

        return;
      }

      /* Remove old used keys */
      error = olm_remove_one_time_keys (self->account, session);
      if (error == olm_error ())
        g_warning ("Error removing key: %s", olm_account_last_error (self->account));

      g_hash_table_insert (self->in_olm_sessions, g_strdup (sender_key), session);
    }
  } else {
    CHATTY_TRACE_MSG ("normal message received ");

    session = g_hash_table_lookup (self->in_olm_sessions, sender_key);

    if (!session)
      session = g_hash_table_lookup (self->out_olm_sessions, sender_key);

    if (!session) {
      g_warning ("Couldn't find session for normal message");
      return;
    }
  }

  copy = g_strdup (body);
  length = olm_decrypt_max_plaintext_length (session, type, copy, strlen (copy));

  if (length == olm_error ()) {
    g_warning ("Error getting max length: %s", olm_session_last_error (session));

    return;
  }

  plaintext = g_malloc (length + 1);
  length = olm_decrypt (session, type, body, strlen (body), plaintext, length);
  if (length == olm_error ()) {
    g_warning ("Error decrypt session: %s", olm_session_last_error (session));

    return;
  }

  plaintext[length] = '\0';
  if (plaintext) {
    g_autoptr(JsonObject) content = NULL;
    const char *message_type;

    content = matrix_utils_string_to_json_object (plaintext);
    message_type = matrix_utils_json_object_get_string (content, "type");

    CHATTY_TRACE_MSG ("message decrypted. type: %s", message_type);

    if (g_strcmp0 (sender, matrix_utils_json_object_get_string (content, "sender")) != 0) {
      g_warning ("Sender mismatch in encrypted content");
      return;
    }

    if (g_strcmp0 (message_type, "m.room_key") == 0)
      handle_m_room_key (self, content, sender_key);
  }
}

char *
matrix_enc_handle_join_room_encrypted (MatrixEnc  *self,
                                       const char *room_id,
                                       JsonObject *object)
{
  OlmInboundGroupSession *session = NULL;
  const char *sender_key;
  const char *ciphertext, *session_id;
  g_autofree char *plaintext = NULL;
  char *body;
  size_t length;

  g_return_val_if_fail (MATRIX_IS_ENC (self), NULL);
  g_return_val_if_fail (object, NULL);

  sender_key = matrix_utils_json_object_get_string (object, "sender_key");

  ciphertext = matrix_utils_json_object_get_string (object, "ciphertext");
  session_id = matrix_utils_json_object_get_string (object, "session_id");
  g_return_val_if_fail (ciphertext, NULL);

  if (session_id)
    session = g_hash_table_lookup (self->in_group_sessions, session_id);

  CHATTY_TRACE_MSG ("Got room encrypted. session exits: %d", !!session);

  if (!session) {
    g_autofree char *pickle = NULL;

    if (self->matrix_db)
      pickle = matrix_db_lookup_session (self->matrix_db, self->user_id,
                                         self->device_id, session_id,
                                         sender_key, SESSION_MEGOLM_V1_IN);
    if (pickle) {
      int err;
      session = g_malloc (olm_inbound_group_session_size ());
      err = olm_unpickle_inbound_group_session (session, self->pickle_key,
                                                strlen (self->pickle_key),
                                                pickle, strlen (pickle));
      if (err == olm_error ()) {
        g_debug ("Error in group unpickle: %s", olm_inbound_group_session_last_error (session));
        g_free (session);
        session = NULL;
      } else {
        g_hash_table_insert (self->in_group_sessions, g_strdup (session_id), session);
        CHATTY_TRACE_MSG ("Got session from matrix db");
      }
    }
  }

  if (!session)
    return NULL;

  g_return_val_if_fail (session, NULL);

  body = g_strdup (ciphertext);
  length = olm_group_decrypt_max_plaintext_length (session, (gpointer)body, strlen (body));
  g_free (body);

  plaintext = g_malloc (length + 1);
  body = g_strdup (ciphertext);
  length = olm_group_decrypt (session, (gpointer)body, strlen (body),
                              (gpointer)plaintext, length, NULL);
  g_free (body);

  if (length == olm_error ()) {
    g_warning ("Error decrypting: %s", olm_inbound_group_session_last_error (session));
    return NULL;
  }

  plaintext[length] = '\0';

  return g_steal_pointer (&plaintext);
}

JsonObject *
matrix_enc_encrypt_for_chat (MatrixEnc  *self,
                             const char *room_id,
                             const char *message)
{
  OlmOutboundGroupSession *session;
  g_autofree char *encrypted = NULL;
  g_autofree char *session_id = NULL;
  JsonObject *root;
  size_t message_len, length;

  g_return_val_if_fail (MATRIX_IS_ENC (self), NULL);
  g_return_val_if_fail (message && *message, NULL);

  session = g_hash_table_lookup (self->out_group_sessions, room_id);
  g_return_val_if_fail (session, NULL);

  message_len = strlen (message);
  length = olm_group_encrypt_message_length (session, message_len);
  encrypted = g_malloc (length + 1);
  length = olm_group_encrypt (session, (gpointer)message, message_len,
                              (gpointer)encrypted, length);

  if (length == olm_error ()) {
    g_warning ("Error encryption: %s", olm_outbound_group_session_last_error (session));
    return NULL;
  }

  encrypted[length] = '\0';

  length = olm_outbound_group_session_id_length (session);
  session_id = g_malloc (length + 1);
  length = olm_outbound_group_session_id (session, (gpointer)session_id, length);
  session_id[length] = '\0';

  root = json_object_new ();
  json_object_set_string_member (root, "algorithm", ALGORITHM_MEGOLM);
  json_object_set_string_member (root, "sender_key", self->curve_key);
  json_object_set_string_member (root, "ciphertext", encrypted);
  json_object_set_string_member (root, "session_id", session_id);
  json_object_set_string_member (root, "device_id", self->device_id);

  return root;
}

JsonObject *
matrix_enc_create_out_group_keys (MatrixEnc  *self,
                                  const char *room_id,
                                  GListModel *members_list)
{
  g_autofree OlmOutboundGroupSession *session = NULL;
  g_autofree uint8_t *session_key = NULL;
  g_autofree uint8_t *session_id = NULL;
  g_autofree uint8_t *random = NULL;
  JsonObject *root, *child;
  BuddyDevice *device;
  size_t length;
  size_t error;

  g_return_val_if_fail (MATRIX_IS_ENC (self), FALSE);

  /* Return early if the chat has an existing outbound group session */
  if (g_hash_table_contains (self->out_group_sessions, room_id))
    return NULL;

  /* Initialize session */
  session = g_malloc (olm_outbound_group_session_size ());
  olm_outbound_group_session (session);

  /* Feed in random bits */
  length = olm_init_outbound_group_session_random_length (session);
  random = g_malloc (length);
  getrandom (random, length, GRND_NONBLOCK);
  error = olm_init_outbound_group_session (session, random, length);
  if (error == olm_error ()) {
    g_warning ("Error init out group session: %s", olm_outbound_group_session_last_error (session));

    return NULL;
  }

  /* Get session id */
  length = olm_outbound_group_session_id_length (session);
  session_id = g_malloc (length + 1);
  length = olm_outbound_group_session_id (session, session_id, length);
  if (length == olm_error ()) {
    g_warning ("Error decrypt session: %s", olm_outbound_group_session_last_error (session));

    return NULL;
  }
  session_id[length] = '\0';

  /* Get session key */
  length = olm_outbound_group_session_key_length (session);
  session_key = g_malloc (length + 1);
  length = olm_outbound_group_session_key (session, session_key, length);
  if (length == olm_error ()) {
    g_warning ("Error getting session key: %s", olm_outbound_group_session_last_error (session));

    return NULL;
  }
  session_key[length] = '\0';

  root = json_object_new ();

  /* https://matrix.org/docs/spec/client_server/r0.6.1#m-room-key */
  for (guint i = 0; i < g_list_model_get_n_items (members_list); i++) {
    g_autoptr(ChattyMaBuddy) buddy = NULL;
    g_autoptr(GList) devices = NULL;
    const char *curve_key/* , *ed_key */;
    JsonObject *user;

    buddy = g_list_model_get_item (members_list, i);
    devices = chatty_ma_buddy_get_devices (buddy);

    user = json_object_new ();
    json_object_set_object_member (root, chatty_item_get_username (CHATTY_ITEM (buddy)), user);

    for (GList *node = devices; node; node = node->next) {
      OlmSession *olm_session = NULL;
      g_autofree char *one_time_key = NULL;
      JsonObject *content;

      device = node->data;
      curve_key = chatty_ma_device_get_curve_key (device);

      one_time_key = chatty_ma_device_get_one_time_key (device);
      olm_session = ma_create_olm_out_session (self, curve_key, one_time_key);

      if (!one_time_key || !curve_key || !olm_session)
        continue;

      g_hash_table_insert (self->out_olm_sessions, g_strdup (curve_key), olm_session);

      /* Create per device object */
      child = json_object_new ();
      json_object_set_object_member (user, chatty_ma_device_get_id (device), child);

      json_object_set_string_member (child, "algorithm", ALGORITHM_OLM);
      json_object_set_string_member (child, "sender_key", self->curve_key);
      json_object_set_object_member (child, "ciphertext", json_object_new ());

      content = json_object_new ();
      child = json_object_get_object_member (child, "ciphertext");
      g_assert (child);
      json_object_set_object_member (child, curve_key, content);

      /* Body to be encrypted */
      {
        g_autoptr(JsonObject) object = NULL;
        g_autofree char *encrypted = NULL;
        g_autofree char *data = NULL;

        /* Create a json object with common data */
        object = json_object_new ();
        json_object_set_string_member (object, "type", "m.room_key");
        json_object_set_string_member (object, "sender", self->user_id);
        json_object_set_string_member (object, "sender_device", self->device_id);

        child = json_object_new ();
        json_object_set_string_member (child, "ed25519", self->ed_key);
        json_object_set_object_member (object, "keys", child);

        child = json_object_new ();
        json_object_set_string_member (child, "algorithm", "m.megolm.v1.aes-sha2");
        json_object_set_string_member (child, "room_id", room_id);
        json_object_set_string_member (child, "session_id", (char *)session_id);
        json_object_set_string_member (child, "session_key", (char *)session_key);
        json_object_set_int_member (child, "chain_index", olm_outbound_group_session_message_index (session));
        json_object_set_object_member (object, "content", child);

        /* User specific data */
        json_object_set_string_member (object, "recipient", chatty_item_get_username (CHATTY_ITEM (buddy)));

        /* Device specific data */
        child = json_object_new ();
        json_object_set_string_member (child, "ed25519", chatty_ma_device_get_ed_key (device));
        json_object_set_object_member (object, "recipient_keys", child);

        /* Now encrypt the above JSON */
        data = matrix_utils_json_object_to_string (object, FALSE);
        encrypted = ma_olm_encrypt (olm_session, data);

        /* Add the encrypted data as the content */
        json_object_set_int_member (content, "type", olm_encrypt_message_type (olm_session));
        json_object_set_string_member (content, "body", encrypted);
      }
    }
  }

  /*
   * We should also create an inbound session with the same key so
   * that we we'll be able to decrypt the messages we sent (when
   * we receive them via sync requests)
   */
  {
    OlmInboundGroupSession *in_session;

    in_session = g_malloc (olm_inbound_group_session_size ());
    olm_inbound_group_session (in_session);
    olm_init_inbound_group_session (in_session, (gpointer)session_key,
                                    strlen ((char *)session_key));
    g_hash_table_insert (self->in_group_sessions,
                         g_strdup ((char *)session_id), in_session);
  }

  g_hash_table_insert (self->out_group_sessions,
                       g_strdup (room_id), g_steal_pointer (&session));

  matrix_utils_clear ((char *)session_key, strlen ((char *)session_key));
  matrix_utils_clear ((char *)session_id, strlen ((char *)session_id));

  return root;
}

void
matrix_file_enc_info_free (MatrixFileEncInfo *enc_info)
{
  if (!enc_info)
    return;

  matrix_utils_free_buffer (enc_info->aes_iv_base64);
  matrix_utils_free_buffer (enc_info->aes_key_base64);
  matrix_utils_free_buffer (enc_info->sha256_base64);

  matrix_utils_clear ((char *)enc_info->aes_iv, enc_info->aes_iv_len);
  matrix_utils_clear ((char *)enc_info->aes_key, enc_info->aes_key_len);
  matrix_utils_clear ((char *)enc_info->sha256, enc_info->sha256_len);
  g_free (enc_info->aes_iv);
  g_free (enc_info->aes_key);
  g_free (enc_info->sha256);

  g_free (enc_info);
}

const char *
matrix_enc_get_curve25519_key (MatrixEnc *self)
{
  g_return_val_if_fail (MATRIX_IS_ENC (self), NULL);

  return self->curve_key;
}

const char *
matrix_enc_get_ed25519_key (MatrixEnc *self)
{
  g_return_val_if_fail (MATRIX_IS_ENC (self), NULL);

  return self->ed_key;
}
