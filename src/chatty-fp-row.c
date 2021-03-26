/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-fp-row.c
 *
 * Copyright 2021 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-fp-row"

#include <glib/gi18n.h>

#include "chatty-fp-row.h"

struct _ChattyFpRow
{
  GtkListBoxRow   parent_instance;

  HdyValueObject *item;

  GtkWidget      *device_id;
  GtkWidget      *device_fp;
};

G_DEFINE_TYPE (ChattyFpRow, chatty_fp_row, GTK_TYPE_LIST_BOX_ROW)


static void
chatty_fp_row_finalize (GObject *object)
{
  ChattyFpRow *self = (ChattyFpRow *)object;

  g_clear_object (&self->item);

  G_OBJECT_CLASS (chatty_fp_row_parent_class)->finalize (object);
}

static void
chatty_fp_row_class_init (ChattyFpRowClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = chatty_fp_row_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/sm/puri/Chatty/"
                                               "ui/chatty-fp-row.ui");

  gtk_widget_class_bind_template_child (widget_class, ChattyFpRow, device_id);
  gtk_widget_class_bind_template_child (widget_class, ChattyFpRow, device_fp);
}

static void
chatty_fp_row_init (ChattyFpRow *self)
{
  PangoAttrList *list;
  PangoAttribute *attribute;

  gtk_widget_init_template (GTK_WIDGET (self));

  list = pango_attr_list_new ();

  attribute = pango_attr_size_new (PANGO_SCALE * 9);
  pango_attr_list_insert (list, attribute);

  attribute = pango_attr_family_new ("monospace");
  pango_attr_list_insert (list, attribute);

  /* Set color for alternate blocks */
  for (guint i = 0; i < 8; i++, i++) {
    attribute = pango_attr_foreground_new (30000, 30000, 30000);
    attribute->start_index = i * 9 ;
    attribute->end_index = i *  9 + 9;
    pango_attr_list_insert (list, attribute);
  }

  gtk_label_set_attributes (GTK_LABEL (self->device_fp), list);

  pango_attr_list_unref (list);
}

GtkWidget *
chatty_fp_row_new (HdyValueObject *item)
{
  ChattyFpRow *self;

  g_return_val_if_fail (HDY_IS_VALUE_OBJECT (item), NULL);

  self = g_object_new (CHATTY_TYPE_FP_ROW, NULL);
  chatty_fp_row_set_item (self, item);

  return GTK_WIDGET (self);
}

void
chatty_fp_row_set_item (ChattyFpRow    *self,
                        HdyValueObject *item)
{
  const char *device_id;
  char *value;
  size_t len;

  g_return_if_fail (CHATTY_IS_FP_ROW (self));
  g_return_if_fail (HDY_IS_VALUE_OBJECT (item));

  if (!g_set_object (&self->item, item))
    return;

  value = hdy_value_object_dup_string (item);
  len = strlen (value);
  /* If we have at least 4 chunks, split the rest into a new line */
  if (len >= 4 * 8) {
    for (guint i = 4 * 8; value[i]; i++) {
      if (value[i] != ' ')
        continue;

      value[i] = '\n';
      break;
    }
  }

  gtk_label_set_text (GTK_LABEL (self->device_fp), value);
  g_free (value);

  device_id = g_object_get_data (G_OBJECT (item), "device-id");
  /* TRANSLATORS: %s is the Device ID */
  value = g_strdup_printf (_("Device ID %s fingerprint:"), device_id);
  gtk_label_set_text (GTK_LABEL (self->device_id), value);
  g_free (value);
}
