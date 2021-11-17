/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#ifndef __CHATTY_REQUEST_H_INCLUDE__
#define __CHATTY_REQUEST_H_INCLUDE__

#include <gtk/gtk.h>
#include <gtk/gtkwidget.h>
#include "request.h"

typedef struct
{
	PurpleRequestType type;

	GtkWidget         *dialog;
	GtkWidget         *ok_button;

	gpointer          *user_data;
	size_t             cb_count;
	GCallback         *cbs;

  gboolean           save_dialog;
  gchar             *file_name;

} ChattyRequestData;

PurpleRequestUiOps *chatty_request_get_ui_ops (void);

#endif
