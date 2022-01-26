/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-text-item.c
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "config.h"

#include <glib/gi18n.h>
#include <ctype.h>


#ifdef PURPLE_ENABLED
# include <purple.h>
#endif

#include "chatty-settings.h"
#include "chatty-text-item.h"


#define MAX_GMT_ISO_SIZE 256

struct _ChattyTextItem
{
  GtkBin         parent_instance;

  GtkWidget     *content_label;

  ChattyMessage *message;
  ChattyProtocol protocol;
  gboolean       is_im;
};

G_DEFINE_TYPE (ChattyTextItem, chatty_text_item, GTK_TYPE_BIN)

#define URL_WWW "www"
#define URL_HTTP "http"
#define URL_HTTPS "https"
#define URL_FILE "file"

#define get_url_start(_start, _match, _type, _suffix, _out)             \
  do {                                                                  \
    char *_uri = _match - strlen (_type);                               \
    size_t len = strlen (_type _suffix);                                \
                                                                        \
    if (*_out)                                                          \
      break;                                                            \
                                                                        \
    if (_match - _start >= strlen (_type) &&                            \
        g_ascii_strncasecmp (_uri, _type _suffix, len) == 0 &&          \
        (isalnum (_uri[len]) || _uri[len] == '/'))                      \
      *_out = match - strlen (_type);                                   \
  } while (0)

static char *
find_url (const char  *buffer,
          char       **end)
{
  char *match, *url = NULL;
  const char *start;

  start = buffer;

  /*
   * linkify http://,  https://, file://, and www.
   */
  while ((match = strpbrk (start, ":."))) {
    get_url_start (start, match, URL_HTTP, "://", &url);
    get_url_start (start, match, URL_HTTPS, "://", &url);
    get_url_start (start, match, URL_FILE, "://", &url);
    get_url_start (start, match, URL_WWW, ".", &url);

    start = match + 1;

    if (url && url > buffer &&
        !isspace (url[-1]) && !ispunct (url[-1]))
      url = NULL;

    if (url)
      break;
  }

  if (url) {
    size_t url_end;

    url_end = strcspn (url, " \n()[],\t\r");
    if (url_end)
      *end = url + url_end;
    else
      *end = url + strlen (url);
  }

  return url;
}

static char *
text_item_linkify (ChattyTextItem *self,
                   const char     *message)
{
  g_autoptr(GString) link_str = NULL;
  GString *str = NULL;
  char *start, *end, *url;

  if (!message || !*message)
    return NULL;

  str = g_string_sized_new (256);
  link_str = g_string_sized_new (256);
  start = end = (char *)message;

  while ((url = find_url (start, &end))) {
    g_autofree char *link = NULL;
    g_autofree char *escaped_link = NULL;
    char *escaped = NULL;

    escaped = g_markup_escape_text (start, url - start);
    g_string_append (str, escaped);
    g_free (escaped);

    link = g_strndup (url, end - url);
    escaped_link = g_markup_escape_text (url, end - url);
    g_string_set_size (link_str, 0);
    g_string_append_uri_escaped (link_str, link, ":/", TRUE);
    escaped = g_markup_escape_text (link_str->str, link_str->len);
    g_string_append_printf (str, "<a href=\"%s\">%s</a>", escaped, escaped_link);
    g_free (escaped);

    start = end;
  }

  /* Append rest of the string, only if we there is already content */
  if (str->len && start && *start) {
    g_autofree char *escaped = NULL;

    escaped = g_markup_escape_text (start, -1);
    g_string_append (str, escaped);
  }

  return g_string_free (str, FALSE);
}

static gchar *
chatty_msg_list_escape_message (ChattyTextItem *self,
                                const char       *message)
{
#ifdef PURPLE_ENABLED
  g_autofree char *nl_2_br = NULL;
  g_autofree char *striped = NULL;
  g_autofree char *escaped = NULL;
  g_autofree char *linkified = NULL;
  char *result;

  nl_2_br = purple_strdup_withhtml (message);
  striped = purple_markup_strip_html (nl_2_br);
  escaped = purple_markup_escape_text (striped, -1);
  linkified = purple_markup_linkify (escaped);
  // convert all tags to lowercase for GtkLabel markup parser
  purple_markup_html_to_xhtml (linkified, &result, NULL);

  return result;
#endif

  return g_strdup ("");
}

static void
text_item_update_quotes (ChattyTextItem *self)
{
  const char *text, *end;
  char *quote;

  text = gtk_label_get_text (GTK_LABEL (self->content_label));
  end = text;

  if (!gtk_label_get_attributes (GTK_LABEL (self->content_label))) {
    PangoAttrList *list;

    list = pango_attr_list_new ();
    gtk_label_set_attributes (GTK_LABEL (self->content_label), list);
    pango_attr_list_unref (list);

  }

  if (!text || !*text)
    return;

  do {
    quote = strchr (end, '>');

    if (quote &&
        (quote == text ||
         *(quote - 1) == '\n')) {
      PangoAttrList *list;
      PangoAttribute *attribute;

      list = gtk_label_get_attributes (GTK_LABEL (self->content_label));
      end = strchr (quote, '\n');

      if (!end)
        end = quote + strlen (quote);

      attribute = pango_attr_foreground_new (30000, 30000, 30000);
      attribute->start_index = quote - text;
      attribute->end_index = end - text + 1;
      pango_attr_list_insert (list, attribute);
    } else if (quote && *quote) {
      /* Move forward one character if '>' happend midst a line */
      end = end + 1;
    }
  } while (quote && *quote);
}

static void
text_item_update_message (ChattyTextItem *self)
{
  g_autoptr(GString) str = NULL;
  ChattySettings *settings;
  const char *text;

  g_assert (CHATTY_IS_TEXT_ITEM (self));
  g_assert (self->message);

  settings = chatty_settings_get_default ();
  text = chatty_message_get_text (self->message);
  str = g_string_sized_new (256);

  if (chatty_message_get_files (self->message)) {
    GList *files;

    files = chatty_message_get_files (self->message);

    for (GList *item = files; item; item = item->next) {
      ChattyFileInfo *file = item->data;

      if (!file || !file->url)
        continue;

      /* file->path is the path to locally saved file */
      if (file->path) {
        if (g_str_has_prefix (file->path, "file://"))
          g_string_append (str, file->path);
        else if (self->protocol & (CHATTY_PROTOCOL_MMS_SMS | CHATTY_PROTOCOL_MMS))
          g_string_append_printf (str, "file://%s/%s/%s", g_get_user_data_dir (), "chatty", file->path);
        else
          g_string_append_printf (str, "file://%s", file->path);
      } else {
        g_string_append (str, file->url);
      }

      if (item->next)
        g_string_append (str, "\n\n");
    }
  }

  if ((self->protocol == CHATTY_PROTOCOL_MATRIX &&
       chatty_settings_get_experimental_features (settings)) ||
      self->protocol & (CHATTY_PROTOCOL_MMS_SMS | CHATTY_PROTOCOL_MMS)) {
    g_autofree char *content = NULL;
    const char *subject;

    subject = chatty_message_get_subject (self->message);

    if (text && *text) {
      if (str->len)
        g_string_prepend (str, "\n\n");
      g_string_prepend (str, text);

      if (subject && *subject)
        g_string_prepend (str, "\n\nMessage: ");
    }

    if (subject && *subject) {
      g_string_prepend (str, subject);
      g_string_prepend (str, "Subject: ");
    }

    content = text_item_linkify (self, str->str);

    /* The string is generated only if there is at least one url, hence set as markup */
    if (content && *content)
      gtk_label_set_markup (GTK_LABEL (self->content_label), content);
    else
      gtk_label_set_text (GTK_LABEL (self->content_label), str->str ?: "");
  } else {
    /* This happens only for purple messages */
    g_autofree char *message = NULL;

    message = chatty_msg_list_escape_message (self, text);
    gtk_label_set_markup (GTK_LABEL (self->content_label), message);
  }

  text_item_update_quotes (self);
}

static void
chatty_text_item_dispose (GObject *object)
{
  ChattyTextItem *self = (ChattyTextItem *)object;

  g_clear_object (&self->message);

  G_OBJECT_CLASS (chatty_text_item_parent_class)->dispose (object);
}

static void
chatty_text_item_class_init (ChattyTextItemClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = chatty_text_item_dispose;
}

static void
chatty_text_item_init (ChattyTextItem *self)
{
  self->content_label = g_object_new (GTK_TYPE_LABEL,
                                      "visible", TRUE,
                                      "margin", 2,
                                      "wrap", TRUE,
                                      "wrap-mode", PANGO_WRAP_WORD_CHAR,
                                      "xalign", 0.0,
                                      NULL);
  gtk_label_set_selectable (GTK_LABEL (self->content_label), TRUE);
  gtk_container_add (GTK_CONTAINER (self), self->content_label);
}

GtkWidget *
chatty_text_item_new (ChattyMessage  *message,
                      ChattyProtocol  protocol)
{
  ChattyTextItem *self;

  g_return_val_if_fail (CHATTY_IS_MESSAGE (message), NULL);

  self = g_object_new (CHATTY_TYPE_TEXT_ITEM, NULL);
  self->protocol = protocol;
  self->message = g_object_ref (message);

  g_signal_connect_object (message, "updated",
                           G_CALLBACK (text_item_update_message),
                           self, G_CONNECT_SWAPPED);
  text_item_update_message (self);

  return GTK_WIDGET (self);
}

GtkStyleContext *
chatty_text_item_get_style (ChattyTextItem *self)
{
  g_return_val_if_fail (CHATTY_IS_TEXT_ITEM (self), NULL);

  return gtk_widget_get_style_context (self->content_label);

}

ChattyMessage *
chatty_text_item_get_item (ChattyTextItem *self)
{
  g_return_val_if_fail (CHATTY_IS_TEXT_ITEM (self), NULL);

  return self->message;
}

const char *
chatty_text_item_get_text (ChattyTextItem *self)
{
  g_return_val_if_fail (CHATTY_IS_TEXT_ITEM (self), "");

  return gtk_label_get_text (GTK_LABEL (self->content_label));
}
