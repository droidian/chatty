/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* message-text-item.c
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#undef NDEBUG
#undef G_DISABLE_ASSERT
#undef G_DISABLE_CHECKS
#undef G_DISABLE_CAST_CHECKS
#undef G_LOG_DOMAIN

#include "chatty-text-item.c"

static void
test_message_text_markup (void)
{
  typedef struct data {
    char *text;
    char *markup;
  } data;
  data array[] = {
    {"",""},
    {"abc","abc"},
    {".abc",".abc"},
    {"www.","www."},
    {"www. ","www. "},
    {"www.a ","<a href=\"www.a\">www.a</a> "},
    {"http:// ","http:// "},
    {"http://w ","<a href=\"http://w\">http://w</a> "},
    {"::: ","::: "},
    {
     "അത് http://www.example.com/മലയാളം ആണ്",
     "അത് <a href=\"http://www.example.com/മലയാളം\">http://www.example.com/മലയാളം</a> ആണ്"},
    {
     "http://www.example.com/user's-image.png",
     "<a href=\"http://www.example.com/user%27s-image.png\">http://www.example.com/user&apos;s-image.png</a>"},
    {
     "www.puri.sm www.gnu.org www.fsf.org ",
     "<a href=\"www.puri.sm\">www.puri.sm</a> "
     "<a href=\"www.gnu.org\">www.gnu.org</a> "
     "<a href=\"www.fsf.org\">www.fsf.org</a> "
    },
    {
     "file:///home/user/good&bad-file.png ",
     "<a href=\"file:///home/user/good%26bad-file.png\">"
     "file:///home/user/good&amp;bad-file.png</a> "
    },
  };

  for (guint i = 0; i < G_N_ELEMENTS (array); i++) {
    g_autoptr(ChattyMessage) message = NULL;
    g_autoptr(GtkWidget) item = NULL;
    GtkLabel *label;
    const char *str;

    message = chatty_message_new (NULL, array[i].text, NULL, 0, CHATTY_MESSAGE_TEXT, 0, 0);
    item = chatty_text_item_new (message, CHATTY_PROTOCOL_MMS_SMS);
    label = GTK_LABEL (CHATTY_TEXT_ITEM (item)->content_label);
    g_object_ref_sink (item);

    str = gtk_label_get_text (label);
    g_assert_cmpstr (str, ==, array[i].text);

    str = gtk_label_get_label (label);
    g_assert_cmpstr (str, ==, array[i].markup);
  }
}

int
main (int   argc,
      char *argv[])
{
  gtk_test_init (&argc, &argv);

  g_test_add_func ("/message-text/markup", test_message_text_markup);

  return g_test_run ();
}
