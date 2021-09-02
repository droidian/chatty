/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-utils"

#include <glib.h>
#include <glib/gi18n.h>
#include "chatty-manager.h"
#include "chatty-settings.h"
#include "chatty-phone-utils.h"
#include "chatty-utils.h"
#include <libebook-contacts/libebook-contacts.h>
#include <gdesktop-enums.h>

#include "chatty-log.h"

#define DIGITS      "0123456789"
#define ASCII_CAPS  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define ASCII_SMALL "abcdefghijklmnopqrstuvwxyz"

/*
 * matrix_id_is_valid:
 * @name: A string
 * @prefix: The allowed prefix
 *
 * Check if @name is a valid username
 * or channel name
 *
 * @prefix should be one of ‘#’ or ‘@’.
 *
 * See https://matrix.org/docs/spec/appendices#id12
 */
static gboolean
matrix_id_is_valid (const char *name,
                    char        prefix)
{
  guint len;

  if (!name || !*name)
    return FALSE;

  if (prefix != '@' && prefix != '#')
    return FALSE;

  if (*(name + 1) == ':')
    return FALSE;

  if (prefix == '@' && *name != '@')
    return FALSE;

  /* Group name can have '#' or '!' (Group id) as prefix */
  if (prefix == '#' && *name != '#' && *name != '!')
    return FALSE;

  len = strlen (name);

  if (len > 255)
    return FALSE;

  if (strspn (name + 1, DIGITS ASCII_CAPS ASCII_SMALL ":._=/-") != len - 1)
    return FALSE;

  if (len >= 4 &&
      *(name + len - 1) != ':' &&
      !strchr (name + 1, prefix) &&
      strchr (name, ':'))
    return TRUE;

  return FALSE;
}

char *
chatty_utils_check_phonenumber (const char *phone_number,
                                const char *country)
{
  EPhoneNumber      *number;
  g_autofree char   *raw = NULL;
  char              *stripped;
  char              *result;
  g_autoptr(GError)  err = NULL;

  g_debug ("%s number %s", G_STRLOC, phone_number);

  if (!phone_number || !*phone_number)
    return NULL;

  raw = g_uri_unescape_string (phone_number, NULL);

  if (g_str_has_prefix (raw, "sms:"))
    stripped = raw + strlen ("sms:");
  else
    stripped = raw;

  /* Skip sms:// */
  while (*stripped == '/')
    stripped++;

  if (strspn (stripped, "+()- 0123456789") != strlen (stripped))
    return NULL;

  if (!e_phone_number_is_supported ()) {
    g_warning ("evolution-data-server built without libphonenumber support");
    return NULL;
  }

  number = e_phone_number_from_string (stripped, country, &err);

  if (!number) {
    g_debug ("Error parsing ‘%s’ for country ‘%s’: %s", phone_number, country, err->message);

    return NULL;
  }

  if (*phone_number != '+' &&
      !chatty_phone_utils_is_valid (phone_number, country))
    result = e_phone_number_to_string (number, E_PHONE_NUMBER_FORMAT_NATIONAL);
  else
    result = e_phone_number_to_string (number, E_PHONE_NUMBER_FORMAT_E164);

  e_phone_number_free (number);

  return result;
}

/**
 * chatty_utils_username_is_valid:
 * @name: A string
 * @protocol: A #ChattyProtocol flag
 *
 * Check if @name is a valid username for the given
 * @protocol(s). Please note that only rudimentary
 * checks are done for the validation process.
 *
 * Currently, %CHATTY_PROTOCOL_XMPP, %CHATTY_PROTOCOL_SMS
 * and %CHATTY_PROTOCOL_MATRIX or their combinations are
 * supported for @protocol.
 *
 * Returns: A #ChattyProtocol with all valid protocols
 * set.
 */
ChattyProtocol
chatty_utils_username_is_valid (const char     *name,
                                ChattyProtocol  protocol)
{
  ChattyProtocol valid = 0;
  guint len;

  if (!name)
    return valid;

  len = strlen (name);
  if (len < 3)
    return valid;

  if (protocol & CHATTY_PROTOCOL_XMPP) {
    const char *at_char, *at_char_end;

    at_char = strchr (name, '@');
    at_char_end = strrchr (name, '@');

    /* Consider valid if @name has only one ‘@’ and @name
     * doesn’t start nor end with a ‘@’
     * See https://xmpp.org/rfcs/rfc3920.html#addressing
     */
    /* XXX: We are ignoring one valid case.  ie, domain alone
     * or domain/resource */
    if (at_char &&
        /* Should not begin with ‘@’ */
        *name != '@' &&
        /* should not end with ‘@’ */
        *(at_char + 1) &&
        /* We require exact one ‘@’ */
        at_char == at_char_end)
      valid |= CHATTY_PROTOCOL_XMPP;
  }

  if (protocol & CHATTY_PROTOCOL_MATRIX) {
    if (matrix_id_is_valid (name, '@'))
      valid |= CHATTY_PROTOCOL_MATRIX;
  }

  if (protocol & CHATTY_PROTOCOL_TELEGRAM && *name == '+') {
    /* country code doesn't matter as we use international format numbers */
    if (chatty_phone_utils_is_valid (name, "US"))
      valid |= CHATTY_PROTOCOL_TELEGRAM;
  }

  if (protocol & CHATTY_PROTOCOL_MMS_SMS && len < 20) {
    const char *end;
    guint end_len;

    end = name;
    if (*end == '+')
      end++;

    end_len = strspn (end, "0123456789- ()");

    if (*name == '+')
      end_len++;

    if (end_len == len)
      valid |= CHATTY_PROTOCOL_MMS_SMS;
  }

  return valid;
}

/**
 * chatty_utils_groupname_is_valid:
 * @name: A string
 * @protocol: A #ChattyProtocol flag
 *
 * Check if @name is a valid group name for the given
 * @protocol(s).  Please note that only rudimentary checks
 * are done for the validation process.
 *
 * Currently %CHATTY_PROTOCOL_XMPP and %CHATTY_PROTOCOL_MATRIX
 * or their combinations are supported for @protocol.
 *
 * Returns: A #ChattyProtocol with all valid protocols
 * set.
 */
ChattyProtocol
chatty_utils_groupname_is_valid (const char     *name,
                                 ChattyProtocol  protocol)
{
  ChattyProtocol valid = 0;
  guint len;

  if (!name)
    return valid;

  len = strlen (name);
  if (len < 3)
    return valid;

  if (protocol & CHATTY_PROTOCOL_XMPP) {
    if (chatty_utils_username_is_valid (name, CHATTY_PROTOCOL_XMPP))
      valid |= CHATTY_PROTOCOL_XMPP;
  }

  if (protocol & CHATTY_PROTOCOL_MATRIX) {
    /* Consider valid if @name starts with ‘#’ and has only one
     * ‘#’, has ‘:’, and has atleast 4 chars*/
    if (matrix_id_is_valid (name, '#'))
      valid |= CHATTY_PROTOCOL_MATRIX;
  }

  return valid;
}

char *
chatty_utils_jabber_id_strip (const char *name)
{
  char ** split;
  char *  stripped;

  split = g_strsplit (name, "/", -1);
  stripped = g_strdup (split[0]);

  g_strfreev (split);

  return stripped;
}


gboolean
chatty_utils_get_item_position (GListModel *list,
                                gpointer    item,
                                guint      *position)
{
  guint n_items;

  g_return_val_if_fail (G_IS_LIST_MODEL (list), FALSE);
  g_return_val_if_fail (item != NULL, FALSE);

  n_items = g_list_model_get_n_items (list);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(GObject) object = NULL;

      object = g_list_model_get_item (list, i);

      if (object == item)
        {
          if (position)
            *position = i;

          return TRUE;
        }
    }

  return FALSE;
}

/**
 * chatty_utils_remove_list_item:
 * @store: a #GListStore
 * @item: A #GObject derived object
 *
 * Remove first found @item from @store.
 *
 * Returns: %TRUE if found and removed. %FALSE otherwise.
 */
gboolean
chatty_utils_remove_list_item (GListStore *store,
                               gpointer    item)
{
  GListModel *model;
  guint n_items;

  g_return_val_if_fail (G_IS_LIST_STORE (store), FALSE);
  g_return_val_if_fail (item, FALSE);

  model = G_LIST_MODEL (store);
  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(GObject) object = NULL;

      object = g_list_model_get_item (model, i);

      if (object == item)
        {
          g_list_store_remove (store, i);

          return TRUE;
        }
    }

  return FALSE;
}

char *
chatty_utils_get_human_time (time_t unix_time)
{
  g_autoptr(GDateTime) now = NULL;
  g_autoptr(GDateTime) utc_time = NULL;
  g_autoptr(GDateTime) local_time = NULL;
  gint year_now, month_now, day_now;
  gint year, month, day;

  g_return_val_if_fail (unix_time >= 0, g_strdup (""));

  now = g_date_time_new_now_local ();
  utc_time = g_date_time_new_from_unix_utc (unix_time);
  local_time = g_date_time_to_local (utc_time);

  g_date_time_get_ymd (now, &year_now, &month_now, &day_now);
  g_date_time_get_ymd (local_time, &year, &month, &day);

  /* If the message is from the current month */
  if (year  == year_now && month == month_now) {
    g_autoptr(GSettings) settings = NULL;
    GDesktopClockFormat clock_format;

    settings = g_settings_new ("org.gnome.desktop.interface");
    clock_format = g_settings_get_enum (settings, "clock-format");

    /* If the message was today */
    if (day == day_now) {
      if (clock_format == G_DESKTOP_CLOCK_FORMAT_24H)
        return g_date_time_format (local_time, "%H∶%M");
      else
        return g_date_time_format (local_time, "%I∶%M %p");
    }

    /* If the message was in the last 7 days */
    if (day_now - day <= 7) {
      if (clock_format == G_DESKTOP_CLOCK_FORMAT_24H)
        /* TRANSLATORS: Timestamp from the last week with 24 hour time, e.g. “Tuesday 18∶42”.
           See https://developer.gnome.org/glib/stable/glib-GDateTime.html#g-date-time-format
         */
        return g_date_time_format (local_time, _("%A %H∶%M"));
      else
        /* TRANSLATORS: Timestamp from the last week with 12 hour time, e.g. “Tuesday 06∶42 PM”.
          See https://developer.gnome.org/glib/stable/glib-GDateTime.html#g-date-time-format
         */
        return g_date_time_format (local_time, _("%A %I∶%M %p"));
    }
  }

  /* TRANSLATORS: Timestamp from more than 7 days ago, e.g. “2020-08-11”.
     See https://developer.gnome.org/glib/stable/glib-GDateTime.html#g-date-time-format
   */
  return g_date_time_format (local_time, _("%Y-%m-%d"));
}


PurpleBlistNode *
chatty_utils_get_conv_blist_node (PurpleConversation *conv)
{
  PurpleBlistNode *node = NULL;

  switch (purple_conversation_get_type (conv)) {
  case PURPLE_CONV_TYPE_IM:
    node = PURPLE_BLIST_NODE (purple_find_buddy (conv->account,
                                                 conv->name));
    break;
  case PURPLE_CONV_TYPE_CHAT:
    node = PURPLE_BLIST_NODE (purple_blist_find_chat (conv->account,
                                                      conv->name));
    break;
  case PURPLE_CONV_TYPE_UNKNOWN:
  case PURPLE_CONV_TYPE_MISC:
  case PURPLE_CONV_TYPE_ANY:
  default:
    g_warning ("Unhandled conversation type %d",
               purple_conversation_get_type (conv));
    break;
  }
  return node;
}

GdkPixbuf *
chatty_utils_get_pixbuf_from_data (const guchar *buf,
                                   gsize         count)
{
  g_autoptr(GdkPixbufLoader) loader = NULL;
  g_autoptr(GError) error = NULL;
  GdkPixbuf *pixbuf;

  if (!buf || !count)
    return NULL;

  loader = gdk_pixbuf_loader_new ();
  gdk_pixbuf_loader_write (loader, buf, count, &error);

  if (!error)
    gdk_pixbuf_loader_close (loader, &error);

  if (error) {
    CHATTY_TRACE_MSG ("%s", error->message);
    return NULL;
  }

  pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);

  if (!pixbuf) {
    CHATTY_TRACE_MSG ("pixbuf creation failed");
    return NULL;
  }

  return g_object_ref (pixbuf);
}


ChattyMsgDirection
chatty_utils_direction_from_flag (PurpleMessageFlags flag)
{
  if (flag & PURPLE_MESSAGE_SYSTEM)
    return CHATTY_DIRECTION_SYSTEM;

  if (flag & PURPLE_MESSAGE_SEND)
    return CHATTY_DIRECTION_OUT;

  if (flag & PURPLE_MESSAGE_RECV)
    return CHATTY_DIRECTION_IN;

  g_return_val_if_reached (CHATTY_DIRECTION_UNKNOWN);
}

void
chatty_file_info_free (ChattyFileInfo *file_info)
{
  if (!file_info)
    return;

  g_free (file_info->file_name);
  g_free (file_info->url);
  g_free (file_info->path);
  g_free (file_info->mime_type);
  g_free (file_info);
}
