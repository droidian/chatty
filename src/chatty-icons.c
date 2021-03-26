/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-icons"

#include <glib.h>
#include <gtk/gtk.h>
#include <math.h>
#include "purple.h"
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
chatty_icon_shape_pixbuf_circular (GdkPixbuf *pixbuf)
{
  cairo_format_t   format;
  cairo_surface_t *surface;
  cairo_t         *cr;
  GdkPixbuf       *ret;
  int              width, height, size;

  format = CAIRO_FORMAT_ARGB32;

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);

  size = (width >= height) ? width : height;

  surface = cairo_image_surface_create (format, size, size);

  cr = cairo_create (surface);

  cairo_set_antialias (cr, CAIRO_ANTIALIAS_SUBPIXEL); 

  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);

  cairo_arc (cr,
             size / 2,
             size / 2,
             size / 2,
             0,
             2 * M_PI);

  cairo_fill (cr);

  gdk_cairo_set_source_pixbuf (cr, pixbuf, 0, 0);

  cairo_arc (cr,
             size / 2,
             size / 2,
             size / 2,
             0,
             2 * M_PI);

  cairo_clip (cr);
  cairo_paint (cr);

  ret = gdk_pixbuf_get_from_surface (surface,
                                     0,
                                     0,
                                     size,
                                     size);

  cairo_surface_destroy (surface);
  cairo_destroy (cr);

  return ret;
}


GdkPixbuf *
chatty_icon_get_buddy_icon (PurpleBlistNode *node,
                            const char      *name,
                            guint            size,
                            const char      *color,
                            gboolean         greyed)
{
  // TODO needs to be detangled and segregated

  gsize                      len;
  PurpleBuddy               *buddy = NULL;
  PurpleGroup               *group = NULL;
  const guchar              *data = NULL;
  GdkPixbuf                 *buf = NULL, *ret = NULL;
  cairo_surface_t           *surface;
  cairo_t                   *cr;
  PurpleBuddyIcon           *icon = NULL;
  PurpleAccount             *account = NULL;
  PurpleContact             *contact = NULL;
  PurpleStoredImage         *custom_img;
  PurplePluginProtocolInfo  *prpl_info = NULL;
  const char                *symbol = NULL;
  gint                       orig_width,
                             orig_height,
                             scale_width,
                             scale_height;
  float                      scale_size;
  gchar                     *sub_str;
  gdouble                    color_r;
  gdouble                    color_g;
  gdouble                    color_b;

  g_return_val_if_fail (color, NULL);

  // convert colors for drawing the cairo background
  if (color) {
    sub_str = g_utf8_substring (color, 0, 2);
    color_r = (gdouble)g_ascii_strtoll (sub_str, NULL, 16) / 255;
    g_free (sub_str);

    sub_str = g_utf8_substring (color, 2, 4);
    color_g = (gdouble)g_ascii_strtoll (sub_str, NULL, 16) / 255;
    g_free (sub_str);

    sub_str = g_utf8_substring (color, 4, 6);
    color_b = (gdouble)g_ascii_strtoll (sub_str, NULL, 16) / 255;
    g_free (sub_str);
  }

  // get the buddy and retrieve an icon if available
  if (node) {
    if (PURPLE_BLIST_NODE_IS_CONTACT (node)) {
      buddy = purple_contact_get_priority_buddy ((PurpleContact*)node);
      symbol = "avatar-default-symbolic";
      contact = (PurpleContact*)node;
    } else if (PURPLE_BLIST_NODE_IS_BUDDY(node)) {
      buddy = (PurpleBuddy*)node;
      symbol = "avatar-default-symbolic";
      contact = purple_buddy_get_contact (buddy);
    } else if (PURPLE_BLIST_NODE_IS_GROUP(node)) {
      group = (PurpleGroup*)node;
    } else if (PURPLE_BLIST_NODE_IS_CHAT(node)) {
      buddy = (PurpleBuddy*)node;
      symbol = "system-users-symbolic";
    } else {
      return NULL;
    }

    if(account && account->gc) {
      prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO(account->gc->prpl);
    }

    if (contact) {
      custom_img = purple_buddy_icons_node_find_custom_icon ((PurpleBlistNode*)contact);
    } else {
      custom_img = purple_buddy_icons_node_find_custom_icon (node);
    }

    if (custom_img) {
      data = purple_imgstore_get_data (custom_img);
      len = purple_imgstore_get_size (custom_img);
    }

    purple_imgstore_unref (custom_img);

    if (data == NULL && buddy && PURPLE_BLIST_NODE_IS_BUDDY(node)) {
      icon = purple_buddy_icons_find (buddy->account, buddy->name);

      if (icon) {
        data = purple_buddy_icon_get_data (icon, &len);
      }
    }

    if (data != NULL) {
      buf = chatty_icon_pixbuf_from_data (data, len);
      purple_buddy_icon_unref (icon);
      // create a grey background to make buddy icons
      // look nicer that don't have square format
      color_r = color_g = color_b = 0.7;
    } else {
      GtkIconTheme *icon_theme;

      icon_theme = gtk_icon_theme_get_default ();

      buf = gtk_icon_theme_load_icon (icon_theme,
                                      symbol,
                                      size,
                                      0,
                                      NULL);
    }
  }

  // create an avatar with the initial of the
  // buddy name if there is no icon available
  if (data == NULL && name != NULL) {
    PangoFontDescription *font_desc;
    PangoLayout          *layout;
    int                  width = size;
    int                  height = size;
    int                  pango_width, pango_height;
    char                 tmp[4];
    char                *initial_char;
    g_autofree char     *font = NULL;

    g_utf8_strncpy (tmp, name, 1);
    initial_char = g_utf8_strup (tmp, 1);

    surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24, width, height);
    cr = cairo_create (surface);

    cairo_set_source_rgb (cr, color_r, color_g, color_b);
    cairo_paint (cr);

    cairo_set_source_rgb (cr, 0.95, 0.95, 0.95);

    font = g_strdup_printf ("Sans %d", (int)ceil (size / 2.5));
    layout = pango_cairo_create_layout (cr);
    pango_layout_set_text (layout, initial_char, -1);
    font_desc = pango_font_description_from_string (font);
    pango_layout_set_font_description (layout, font_desc);
    pango_font_description_free (font_desc);

    pango_layout_get_size (layout, &pango_width, &pango_height);
    cairo_translate (cr, size/2, size/2);
    cairo_move_to (cr,
                   -((double)pango_width / PANGO_SCALE) / 2,
                   -((double)pango_height / PANGO_SCALE) / 2);
    pango_cairo_show_layout (cr, layout);

    buf = gdk_pixbuf_get_from_surface (surface,
                                       0,
                                       0,
                                       width,
                                       height);

    cairo_surface_destroy (surface);
    g_object_unref (layout);
    cairo_destroy (cr);
    g_free (initial_char);
  }

  if (!buf) {
    return NULL;
  }

  if (greyed) {
    gboolean offline = FALSE, idle = FALSE;

    if (buddy) {
      PurplePresence *presence = purple_buddy_get_presence(buddy);
      if (!PURPLE_BUDDY_IS_ONLINE(buddy))
        offline = TRUE;
      if (purple_presence_is_idle(presence))
        idle = TRUE;
    } else if (group) {
      if (purple_blist_get_group_online_count (group) == 0)
        offline = TRUE;
    }

    if (offline) {
      gdk_pixbuf_saturate_and_pixelate (buf, buf, 0.0, FALSE);
    }

    if (idle) {
      gdk_pixbuf_saturate_and_pixelate (buf, buf, 0.25, FALSE);
    }
  }

  scale_width = orig_width = gdk_pixbuf_get_width (buf);
  scale_height = orig_height = gdk_pixbuf_get_height (buf);

  if (prpl_info &&
      prpl_info->icon_spec.scale_rules & PURPLE_ICON_SCALE_DISPLAY) {
    purple_buddy_icon_get_scale_size (&prpl_info->icon_spec,
                                      &scale_width,
                                      &scale_height);
  }

  scale_size = (float)size;

  if (size) {
    GdkPixbuf *tmpbuf;

    if (scale_height > scale_width) {
      scale_width = scale_size * (double)scale_width / (double)scale_height;
      scale_height = scale_size;
    } else {
      scale_height = scale_size * (double)scale_height / (double)scale_width;
      scale_width = scale_size;
    }

    tmpbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, scale_width, scale_height);
    gdk_pixbuf_fill (tmpbuf, 0x00000000);
    gdk_pixbuf_scale (buf, tmpbuf, 0, 0,
                      scale_width, scale_height,
                      0, 0,
                      (double)scale_width / (double)orig_width,
                      (double)scale_height / (double)orig_height,
                      GDK_INTERP_BILINEAR);

    ret = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, scale_size, scale_size);
    gdk_pixbuf_fill (ret, 0x00000000);
    gdk_pixbuf_copy_area (tmpbuf,
                          0, 0,
                          scale_width, scale_height,
                          ret,
                          (scale_size - scale_width) / 2,
                          (scale_size - scale_height) / 2);

    g_object_unref (G_OBJECT(tmpbuf));
  } else {
    ret = gdk_pixbuf_scale_simple (buf,
                                   scale_width,
                                   scale_height,
                                   GDK_INTERP_HYPER);
  }

  g_object_unref (G_OBJECT(buf));

  return chatty_icon_shape_pixbuf_circular (ret);
}


/* Altered from do_colorshift in gnome-panel */
void
chatty_icon_do_alphashift (GdkPixbuf *pixbuf,
                           int        shift)
{
  gint    i, j;
  gint    width, height, padding;
  guchar *pixels;
  int     val;

  if (!gdk_pixbuf_get_has_alpha (pixbuf)) {
    return;
  }

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  padding = gdk_pixbuf_get_rowstride (pixbuf) - width * 4;
  pixels = gdk_pixbuf_get_pixels (pixbuf);

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      pixels++;
      pixels++;
      pixels++;
      val = *pixels - shift;
      *(pixels++) = CLAMP(val, 0, 255);
    }

    pixels += padding;
  }
}


GIcon * 
chatty_icon_get_gicon_from_pixbuf (GdkPixbuf *pixbuf)
{
  GIcon  *icon;
  g_autoptr(GBytes) bytes = NULL;
  g_autofree char *buffer = NULL;
  gsize   size;
  g_autoptr(GError) error = NULL;

  gdk_pixbuf_save_to_buffer (pixbuf, &buffer, &size, "png", &error, NULL);

  if (error != NULL) {
    g_debug ("%s: Could not save pixbuf to buffer: %s", __func__, error->message);

    return NULL;
  }

  bytes = g_bytes_new (buffer, size);
  icon = g_bytes_icon_new (bytes);

  return icon;
}
