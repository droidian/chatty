/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-sms-uri.c
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#define G_LOG_DOMAIN "chatty-sms-uri"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define _GNU_SOURCE
#include <string.h>
#include "chatty-settings.h"
#include "chatty-utils.h"
#include "chatty-sms-uri.h"
#include "chatty-log.h"

/**
 * SECTION: chatty-sms-uri
 * @title: ChattySmsUri
 * @short_description: RFC 5724 SMS URI parser
 * @include: "chatty-sms-uri.h"
 *
 * See https://www.rfc-editor.org/rfc/rfc5724
 *
 * Please note that this parser implements only a minimal set of the
 * RFC as required for chatty (ie, most of RFC 3966 is missing), and
 * does NOT follow the RFC where it breaks the old behavior of chatty,
 * eg: "phone-context" for local numbers
 */

struct _ChattySmsUri
{
  GObject    parent_instance;

  char      *uri;
  GPtrArray *numbers;
  char      *numbers_str;
  char      *body;

  /* If @uri_parsed is set, @uri_valid should cache the URI state */
  gboolean   uri_parsed;
  gboolean   uri_valid;
  gboolean   numbers_parsed;
  gboolean   numbers_valid;
};

G_DEFINE_TYPE (ChattySmsUri, chatty_sms_uri, G_TYPE_OBJECT)

static int
sort_strv (gconstpointer a,
           gconstpointer b)
{
  char **str_a = (gpointer) a;
  char **str_b = (gpointer) b;

  return g_strcmp0 (*str_a, *str_b);
}

static void
chatty_sms_parse_numbers (ChattySmsUri *self)
{
  GPtrArray *array;

  g_assert (CHATTY_IS_SMS_URI (self));

  array = self->numbers;
  g_clear_pointer (&self->numbers_str, g_free);

  if (array->len == 1)
    self->numbers_str = g_strdup (array->pdata[0]);

  if (array->len <= 1)
    return;

  g_ptr_array_sort (self->numbers, sort_strv);

  /* Make the array bigger so that we can assure it's NULL terminated */
  g_ptr_array_set_size (array, array->len + 1);

  for (guint i = 0; i < array->len - 1; i++) {
    if (g_strcmp0 (array->pdata[i], array->pdata[i + 1]) == 0)
      g_ptr_array_remove_index (array, i);
  }

  self->numbers_str = g_strjoinv (",", (char **)array->pdata);

  /* Resize back to original size */
  g_ptr_array_set_size (array, array->len - 1);
}

/* See https://www.rfc-editor.org/rfc/rfc5724#section-2.2 */
static void
chatty_sms_validate (ChattySmsUri *self)
{
  g_auto(GStrv) recipients = NULL;
  g_autofree char *numbers = NULL;
  const char *number_start;
  ChattySettings *settings;
  const char *end;

  g_assert (CHATTY_IS_SMS_URI (self));

  if (self->uri_parsed)
    return;

  self->uri_parsed = TRUE;
  self->uri_valid = FALSE;

  if (!self->uri || !*self->uri)
    return;

  self->numbers_valid = TRUE;
  end = strchrnul (self->uri, '?');
  numbers = g_strndup (self->uri, end - self->uri);
  number_start = numbers;

  if (g_str_has_prefix (number_start, "sms:"))
    number_start += strlen ("sms:");

  while (*number_start == '/')
    number_start++;

  settings = chatty_settings_get_default ();
  recipients = g_strsplit (number_start, ",", -1);
  g_ptr_array_set_size (self->numbers, 0);
  self->numbers->pdata[0] = NULL;


  for (int i = 0; recipients[i]; i++) {
    char *who;

    who = chatty_utils_check_phonenumber (recipients[i],
                                          chatty_settings_get_country_iso_code (settings));

    if (!who) {
      who = g_strdup (recipients[i]);
      self->numbers_valid = FALSE;
    }

    g_ptr_array_add (self->numbers, who);
  }

  /*
   * If there are 0 or 2+ numbers, with atleast one invalid, the URI is invalid
   * because this can't happen in an incoming nor outgoing message.
   */
  if (!self->numbers->len ||
      (self->numbers->len >= 2 && !self->numbers_valid)) {
    self->uri_valid = FALSE;
    return;
  }

  g_clear_pointer (&self->body, g_free);

  if (end) {
    g_autofree char *text = NULL;
    const char *body;

    body = strstr (end, "body=");

    if (!body)
      goto end;

    body = body + strlen ("body=");
    end = strchrnul (body, '&');
    text = g_strndup (body, end - body);

    self->body = g_uri_unescape_string (text, NULL);
  }

 end:
  /* A URI with exactly one non numeric sender is considered valid
   * As it can happen in incoming messages, but in that case, body
   * should be empty as a message can't be send to the given number
   */
  if (self->numbers_valid || !self->body)
    self->uri_valid = TRUE;
}

static void
chatty_sms_uri_finalize (GObject *object)
{
  ChattySmsUri *self = (ChattySmsUri *)object;

  g_free (self->uri);
  g_free (self->numbers_str);
  g_free (self->body);
  g_ptr_array_free (self->numbers, TRUE);

  G_OBJECT_CLASS (chatty_sms_uri_parent_class)->finalize (object);
}

static void
chatty_sms_uri_class_init (ChattySmsUriClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = chatty_sms_uri_finalize;
}

static void
chatty_sms_uri_init (ChattySmsUri *self)
{
  self->numbers = g_ptr_array_new_full (1, g_free);
}

ChattySmsUri *
chatty_sms_uri_new (const char *uri)
{
  ChattySmsUri *self;

  self = g_object_new (CHATTY_TYPE_SMS_URI, NULL);
  self->uri = g_strdup (uri);

  return self;
}

void
chatty_sms_uri_set_uri (ChattySmsUri *self,
                        const char   *uri)
{
  g_return_if_fail (CHATTY_IS_SMS_URI (self));

  g_free (self->uri);
  self->uri = g_strdup (uri);

  self->uri_parsed = FALSE;
}

gboolean
chatty_sms_uri_is_valid (ChattySmsUri *self)
{
  g_return_val_if_fail (CHATTY_IS_SMS_URI (self), FALSE);

  chatty_sms_validate (self);

  return self->uri_valid;
}

gboolean
chatty_sms_uri_can_send (ChattySmsUri *self)
{
  g_return_val_if_fail (CHATTY_IS_SMS_URI (self), FALSE);

  return self->numbers_valid;
}

GPtrArray *
chatty_sms_uri_get_numbers (ChattySmsUri *self)
{
  g_return_val_if_fail (CHATTY_IS_SMS_URI (self), NULL);

  chatty_sms_validate (self);
  chatty_sms_parse_numbers (self);

  return self->numbers;
}

const char *
chatty_sms_uri_get_numbers_str (ChattySmsUri *self)
{
  g_return_val_if_fail (CHATTY_IS_SMS_URI (self), NULL);

  chatty_sms_validate (self);
  chatty_sms_parse_numbers (self);

  if (self->numbers_str)
    return self->numbers_str;

  return "";
}

const char *
chatty_sms_uri_get_body (ChattySmsUri *self)
{
  g_return_val_if_fail (CHATTY_IS_SMS_URI (self), "");

  chatty_sms_validate (self);

  if (self->body && self->numbers_valid)
    return self->body;

  return "";
}
