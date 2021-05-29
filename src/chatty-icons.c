/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-icons"

#include <glib.h>
#include <gtk/gtk.h>
#include "chatty-icons.h"


GdkPixbuf *
chatty_icon_pixbuf_from_data (const guchar *buf,
                              gsize         count)
{
  GdkPixbuf       *pixbuf;
  g_autoptr(GdkPixbufLoader) loader = NULL;
  g_autoptr(GError) error = NULL;

  loader = gdk_pixbuf_loader_new ();

  if (!gdk_pixbuf_loader_write (loader, buf, count, &error)) {
    g_debug ("%s: pixbuf_loder_write failed: %s", __func__, error->message);
    return NULL;
  }

  if (!gdk_pixbuf_loader_close (loader, &error)) {
    g_debug ("%s: pixbuf_loder_close failed: %s", __func__, error->message);
    return NULL;
  }

  pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);

  if (!pixbuf) {
    g_debug ("%s: pixbuf creation failed", __func__);
    return NULL;
  }

  return g_object_ref (pixbuf);
}

GdkPixbuf *
chatty_icon_get_buddy_icon (PurpleBlistNode *node)
{
  gsize                      len;
  const guchar              *data = NULL;
  GdkPixbuf                 *buf = NULL;
  PurpleBuddyIcon           *icon = NULL;
  PurpleStoredImage         *custom_img;

  if (!node || !PURPLE_BLIST_NODE_IS_CHAT (node))
    return NULL;

  custom_img = purple_buddy_icons_node_find_custom_icon (node);

  if (custom_img) {
    data = purple_imgstore_get_data (custom_img);
    len = purple_imgstore_get_size (custom_img);
  }

  purple_imgstore_unref (custom_img);

  if (data != NULL) {
    buf = chatty_icon_pixbuf_from_data (data, len);
    purple_buddy_icon_unref (icon);

    return buf;
  }

  return NULL;
}
