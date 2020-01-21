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

#include <math.h>

#include "chatty-avatar.h"

/**
 * SECTION: chatty-avatar
 * @title: ChattyAvatar
 * @short_description: Avatar Image widget for a User
 * @include: "chatty-avatar.h"
 */

struct _ChattyAvatar
{
  GtkImage    parent_instance;

  ChattyUser *user;
};

G_DEFINE_TYPE (ChattyAvatar, chatty_avatar, GTK_TYPE_IMAGE)

enum {
  PROP_0,
  PROP_USER,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static GdkRGBA
get_rgba_for_str (const gchar *str)
{
  /* XXX: Remove colors with contrast issues */
  /* https://gitlab.gnome.org/Teams/Design/HIG-app-icons/blob/master/GNOME%20HIG.gpl */
  static guchar color_palette[][3] = {
    {153, 193, 241}, /* Blue 1 */
    {98,  160, 234}, /* Blue 2 */
    {53,  132, 228}, /* Blue 3 */
    {28,  113, 216}, /* Blue 4 */
    {26,   95, 180}, /* Blue 5 */
    {143, 240, 164}, /* Green 1 */
    {87,  227, 137}, /* Green 2 */
    {51,  209, 122}, /* Green 3 */
    {46,  194, 126}, /* Green 4 */
    {38,  162, 105}, /* Green 5 */
    {249, 240, 107}, /* Yellow 1 */
    {248, 228,  92}, /* Yellow 2 */
    {246, 211,  45}, /* Yellow 3 */
    {245, 194,  17}, /* Yellow 4 */
    {229, 165,  10}, /* Yellow 5 */
    {255, 190, 111}, /* Orange 1 */
    {255, 163,  72}, /* Orange 2 */
    {255, 120,   0}, /* Orange 3 */
    {230,  97,   0}, /* Orange 4 */
    {198,  70,   0}, /* Orange 5 */
    {246,  97,  81}, /* Red 1 */
    {237,  51,  59}, /* Red 2 */
    {224,  27,  36}, /* Red 3 */
    {192,  28,  40}, /* Red 4 */
    {165,  29,  45}, /* Red 5 */
    {220, 138, 221}, /* Purple 1 */
    {192,  97, 203}, /* Purple 2 */
    {145,  65, 172}, /* Purple 3 */
    {129,  61, 156}, /* Purple 4 */
    {97,   53, 131}, /* Purple 5 */
    {205, 171, 143}, /* Brown 1 */
    {181, 131,  90}, /* Brown 2 */
    {152, 106,  68}, /* Brown 3 */
    {134,  94,  60}, /* Brown 4 */
    {99,   69,  44}  /* Brown 5 */
  };

  GdkRGBA rgba = { 0.0, 0.0, 0.0, 1.0 };
  guint hash;
  int n_colors;
  int index;

  if (!str || !*str)
    return rgba;

  hash = g_str_hash (str);
  n_colors = G_N_ELEMENTS (color_palette);
  index = hash % n_colors;

  rgba.red   = color_palette[index][0] / 255.0;
  rgba.green = color_palette[index][1] / 255.0;
  rgba.blue  = color_palette[index][2] / 255.0;

  return rgba;
}

static void
chatty_avatar_draw_pixbuf (cairo_t   *cr,
                           GdkPixbuf *pixbuf,
                           gint       size)
{
  g_autoptr(GdkPixbuf) image = NULL;
  int width, height;

  width  = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);

  image = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, size, size);
  gdk_pixbuf_scale (pixbuf, image, 0, 0,
                    size, size,
                    0, 0,
                    (double)size / width,
                    (double)size / height,
                    GDK_INTERP_BILINEAR);

  gdk_cairo_set_source_pixbuf (cr, image, 0, 0);

  cairo_arc (cr, size / 2.0, size / 2.0, size / 2.0, 0, 2 * G_PI);
  cairo_clip (cr);
  cairo_paint (cr);
}

static void
chatty_avatar_draw_label (ChattyAvatar *self,
                          cairo_t      *cr,
                          const char   *label)
{
  PangoFontDescription *font_desc;
  PangoLayout *layout;
  const char *text_end;
  g_autofree char *font = NULL;
  g_autofree char *upcase_str = NULL;
  GdkRGBA rgba;
  int pango_width, pango_height;
  int width, height;
  guint size;

  width = gtk_widget_get_allocated_width (GTK_WIDGET (self));
  height = gtk_widget_get_allocated_width (GTK_WIDGET (self));
  size = MIN (width, height);

  /* Paint background circle */
  rgba = get_rgba_for_str (label);
  cairo_arc (cr, size / 2.0, size / 2.0, size / 2.0, 0, 2 * G_PI);
  cairo_set_source_rgb (cr, rgba.red, rgba.green, rgba.blue);
  cairo_fill (cr);

  font = g_strdup_printf ("Sans %d", (int)ceil (size / 2.5));
  layout = pango_cairo_create_layout (cr);
  font_desc = pango_font_description_from_string (font);
  pango_layout_set_font_description (layout, font_desc);
  pango_font_description_free (font_desc);

  /* Use the first utf-8 letter from name */
  text_end = g_utf8_next_char (label);
  upcase_str = g_utf8_strup (label, text_end - label);
  pango_layout_set_text (layout, upcase_str, -1);

  pango_layout_get_size (layout, &pango_width, &pango_height);
  cairo_set_source_rgb (cr, 0.95, 0.95, 0.95);
  cairo_translate (cr, size / 2.0, size / 2.0);
  /* XXX: Adjusted to 2px off in vertical orientation, but why? */
  cairo_move_to (cr,
                 -((double)pango_width / PANGO_SCALE) / 2,
                 -((double)pango_height / PANGO_SCALE) / 2 + 2);
  pango_cairo_show_layout (cr, layout);
}

static gboolean
chatty_avatar_draw (GtkWidget *widget,
                    cairo_t   *cr)
{
  ChattyAvatar *self = (ChattyAvatar *)widget;
  GdkPixbuf *avatar = NULL;
  const gchar *name = NULL;
  int width, height;
  guint size;

  width  = gtk_widget_get_allocated_width (GTK_WIDGET (self));
  height = gtk_widget_get_allocated_width (GTK_WIDGET (self));
  size = MIN (width, height);

  if (self->user)
    {
      avatar = chatty_user_get_avatar (self->user);

      if (!avatar)
        name = chatty_user_get_name (self->user);
    }

  if (avatar)
    chatty_avatar_draw_pixbuf (cr, avatar, size);
  else if (name && *name)
    chatty_avatar_draw_label (self, cr, name);

  return GTK_WIDGET_CLASS (chatty_avatar_parent_class)->draw (widget, cr);
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
    case PROP_USER:
      chatty_avatar_set_user (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
chatty_avatar_dispose (GObject *object)
{
  ChattyAvatar *self = (ChattyAvatar *)object;

  g_clear_object (&self->user);

  G_OBJECT_CLASS (chatty_avatar_parent_class)->dispose (object);
}

static void
chatty_avatar_class_init (ChattyAvatarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = chatty_avatar_set_property;
  object_class->dispose = chatty_avatar_dispose;

  widget_class->draw = chatty_avatar_draw;

  properties[PROP_USER] =
    g_param_spec_object ("user",
                         "User",
                         "An Account, Buddy, or a Contact",
                         CHATTY_TYPE_USER,
                         G_PARAM_WRITABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
chatty_avatar_init (ChattyAvatar *self)
{
}

GtkWidget *
chatty_avatar_new (ChattyUser *user)
{
  g_return_val_if_fail (CHATTY_IS_USER (user), NULL);

  return g_object_new (CHATTY_TYPE_AVATAR,
                       "user", user,
                       NULL);
}

void
chatty_avatar_set_user (ChattyAvatar *self,
                        ChattyUser   *user)
{
  g_return_if_fail (CHATTY_IS_AVATAR (self));
  g_return_if_fail (!user || CHATTY_IS_USER (user));

  if (!g_set_object (&self->user, user))
    return;

  /* We don’t emit notify signals as we don’t need it */
  g_signal_connect_object (self->user, "avatar-changed",
                           G_CALLBACK (gtk_widget_queue_draw), self,
                           G_CONNECT_SWAPPED);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}