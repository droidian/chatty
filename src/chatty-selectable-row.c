/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-selectable-row.c
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-selectable-row"

#include <glib/gi18n.h>

#include "chatty-selectable-row.h"

struct _ChattySelectableRow
{
  GtkListBoxRow   parent_instance;

  GtkWidget      *row_title;
  GtkWidget      *icon;
};

G_DEFINE_TYPE (ChattySelectableRow, chatty_selectable_row, GTK_TYPE_LIST_BOX_ROW)

enum {
  PROP_0,
  PROP_TITLE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
chatty_selectable_row_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  ChattySelectableRow *self = (ChattySelectableRow *)object;

  switch (prop_id)
    {
    case PROP_TITLE:
      gtk_label_set_label (GTK_LABEL (self->row_title), g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
chatty_selectable_row_class_init (ChattySelectableRowClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = chatty_selectable_row_set_property;

  properties[PROP_TITLE] =
    g_param_spec_string ("title",
                         "Title",
                         "List row primary title",
                         NULL,
                         G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-selectable-row.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattySelectableRow, row_title);
  gtk_widget_class_bind_template_child (widget_class, ChattySelectableRow, icon);
}

static void
chatty_selectable_row_init (ChattySelectableRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
chatty_selectable_row_new (const char *title)
{
  return g_object_new (CHATTY_TYPE_SELECTABLE_ROW,
                       "title", title,
                       NULL);
}

gboolean
chatty_selectable_row_get_selected (ChattySelectableRow *self)
{
  const char *name;

  g_return_val_if_fail (CHATTY_IS_SELECTABLE_ROW (self), FALSE);

  gtk_image_get_icon_name (GTK_IMAGE (self->icon), &name, NULL);

  if (!name || !*name)
    return FALSE;

  return TRUE;
}

void
chatty_selectable_row_set_selected (ChattySelectableRow *self,
                                    gboolean             is_selected)
{
  const char *name = "";

  g_return_if_fail (CHATTY_IS_SELECTABLE_ROW (self));

  if (is_selected)
    name = "emblem-ok-symbolic";

  g_object_set (self->icon, "icon-name", name, NULL);
}

const char *
chatty_selectable_row_get_title (ChattySelectableRow *self)
{
  g_return_val_if_fail (CHATTY_IS_SELECTABLE_ROW (self), "");

  return gtk_label_get_label (GTK_LABEL (self->row_title));
}
