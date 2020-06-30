/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include <glib.h>
#include <glib/gi18n.h>
#include "chatty-manager.h"
#include "chatty-utils.h"
#include <libebook-contacts/libebook-contacts.h>

/* https://gitlab.gnome.org/GNOME/gtk/-/blob/gtk-3-24/gtk/org.gtk.Settings.FileChooser.gschema.xml#L42 */
#define CLOCK_FORMAT_24H 0
#define CLOCK_FORMAT_12H 1

static const char *avatar_colors[] = {
  "E57373", "F06292", "BA68C8", "9575CD",
  "7986CB", "64B5F6", "4FC3F7", "4DD0E1",
  "4DB6AC", "81C784", "AED581", "DCE775",
  "FFD54F", "FFB74D", "FF8A65", "A1887F"
};

char *
chatty_utils_strip_blanks (const char *string)
{
  char *result;
  char **chunks;

  chunks = g_strsplit (string, "%20", 0);

  result = g_strjoinv(NULL, chunks);
  
  g_strstrip (result);

  g_strfreev (chunks);

  return result;
}


char* 
chatty_utils_check_phonenumber (const char *phone_number)
{
  EPhoneNumber      *number;
  g_autofree char   *stripped = NULL;
  char              *result;
  g_autoptr(GError)  err = NULL;

  stripped = chatty_utils_strip_blanks (phone_number);

  number = e_phone_number_from_string (stripped, NULL, &err);

  if (!number || !e_phone_number_is_supported ()) {
    g_debug ("%s %s: %s", __func__, phone_number, err->message);

    result = NULL;
  } else {
    if (g_strrstr (phone_number, "+")) {
      result = e_phone_number_to_string (number, E_PHONE_NUMBER_FORMAT_E164);
    } else {
      result = e_phone_number_to_string (number, E_PHONE_NUMBER_FORMAT_NATIONAL);
    }
  }

  e_phone_number_free (number);

  return result;
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

GtkWidget*
chatty_utils_create_fingerprint_row (const char *fp,
                                     guint       id)
{
  GtkWidget     *row;
  GtkBox        *vbox;
  GtkLabel      *label_fp;
  GtkLabel      *label_id;
  g_auto(GStrv)  line_split = NULL;
  char          *markup_fp = NULL;
  char          *markup_id = NULL;
  char          *device_id;

  if (!fp) {
    return NULL;
  }

  line_split = g_strsplit (fp, " ", -1);

  markup_fp = "<span font_family='monospace' font='9'>";

  for (int i = 0; i < 8; i++) {
    markup_fp = g_strconcat (markup_fp,
                             i % 2 ? "<span color='DarkGrey'>"
                                   : "<span color='DimGrey'>",
                             line_split[i],
                             i == 3 ? "\n" : " ",
                             "</span>",
                             i == 7 ? "</span>" : "\0",
                             NULL);
  }

  device_id = g_strdup_printf ("%i", id);

  markup_id = g_strconcat ("<span font='9'>"
                           "<span color='DimGrey'>",
                           "Device ID ",
                           device_id,
                           " fingerprint:",
                           "</span></span>",
                           NULL);

  row = GTK_WIDGET(gtk_list_box_row_new ());
  g_object_set (G_OBJECT(row),
                "selectable", FALSE,
                "activatable", FALSE,
                NULL);

  vbox = GTK_BOX(gtk_box_new (GTK_ORIENTATION_VERTICAL, 0));
  g_object_set (G_OBJECT(vbox),
                "margin_top", 6,
                "margin_bottom", 6,
                "margin_start", 12,
                "margin_end", 6,
                NULL);

  label_id = GTK_LABEL(gtk_label_new (NULL));
  gtk_label_set_markup (GTK_LABEL(label_id), g_strdup (markup_id));
  g_object_set (G_OBJECT(label_id),
                "can_focus", FALSE,
                "use_markup", TRUE,
                "ellipsize", PANGO_ELLIPSIZE_END,
                "halign", GTK_ALIGN_START,
                "hexpand", TRUE,
                "xalign", 0.0,
                NULL);

  label_fp = GTK_LABEL(gtk_label_new (NULL));
  gtk_label_set_markup (GTK_LABEL(label_fp), g_strdup (markup_fp));
  g_object_set (G_OBJECT(label_fp),
                "can_focus", FALSE,
                "use_markup", TRUE,
                "ellipsize", PANGO_ELLIPSIZE_END,
                "halign", GTK_ALIGN_START,
                "hexpand", TRUE,
                "margin_top", 8,
                "xalign", 0.0,
                NULL);

  gtk_box_pack_start (vbox, GTK_WIDGET(label_id), FALSE, FALSE, 0);
  gtk_box_pack_start (vbox, GTK_WIDGET(label_fp), FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER(row), GTK_WIDGET(vbox));
  gtk_widget_show_all (GTK_WIDGET(row));

  g_free (markup_fp);
  g_free (markup_id);
  g_free (device_id);

  return GTK_WIDGET(row);
}

gpointer
chatty_utils_get_node_object (PurpleBlistNode *node)
{
  return node->ui_data;
}


const char *
chatty_utils_get_color_for_str (const char *str)
{
  guint hash;

  if (!str)
    str = "";

  hash = g_str_hash (str);

  return avatar_colors[hash % G_N_ELEMENTS (avatar_colors)];
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

  if (year  == year_now &&
      month == month_now)
    {
      g_autoptr(GSettings) gtk_settings = NULL;
      gint clock_format;

      gtk_settings = g_settings_new ("org.gnome.desktop.interface");
      clock_format = g_settings_get_enum (gtk_settings, "clock-format");

      /* Time Format */
      if (day == day_now && clock_format == CLOCK_FORMAT_24H)
        return g_date_time_format (local_time, "%R");
      else if (day == day_now)
        /* TRANSLATORS: Time format with time in AM/PM format */
        return g_date_time_format (local_time, _("%I:%M %p"));

      /* Localized day name */
      if (day_now - day <= 7 && clock_format == CLOCK_FORMAT_24H)
        /* TRANSLATORS: Time format as supported by g_date_time_format() */
        return g_date_time_format (local_time, _("%A %R"));
      else if (day_now - day <= 7)
        /* TRANSLATORS: Time format with day and time in AM/PM format */
        return g_date_time_format (local_time, _("%A %I:%M %p"));
    }

  /* TRANSLATORS: Year format as supported by g_date_time_format() */
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
