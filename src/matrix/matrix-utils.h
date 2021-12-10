/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* matrix-utils.h
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#pragma once

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#define MATRIX_ERROR (matrix_error_quark ())

GQuark        matrix_error_quark                    (void);
GError       *matrix_utils_json_node_get_error      (JsonNode      *node);
void          matrix_utils_clear                    (char          *buffer,
                                                     size_t         length);
void          matrix_utils_free_buffer              (char          *buffer);
gboolean      matrix_utils_username_is_complete     (const char    *username);
const char   *matrix_utils_get_url_from_username    (const char    *username);
char         *matrix_utils_json_object_to_string    (JsonObject    *object,
                                                     gboolean       prettify);
GString      *matrix_utils_json_get_canonical       (JsonObject    *object,
                                                     GString       *out);
JsonObject   *matrix_utils_string_to_json_object    (const char    *json_str);
gint64        matrix_utils_json_object_get_int      (JsonObject    *object,
                                                     const char    *member);
gboolean      matrix_utils_json_object_get_bool     (JsonObject    *object,
                                                     const char    *member);
const char   *matrix_utils_json_object_get_string   (JsonObject    *object,
                                                     const char    *member);
JsonObject   *matrix_utils_json_object_get_object   (JsonObject    *object,
                                                     const char    *member);
JsonArray    *matrix_utils_json_object_get_array    (JsonObject    *object,
                                                     const char    *member);

JsonObject   *matrix_utils_get_message_json_object  (SoupMessage   *message,
                                                     const char    *member);

void          matrix_utils_read_uri_async           (const char    *uri,
                                                     guint          timeout,
                                                     GCancellable  *cancellable,
                                                     GAsyncReadyCallback callback,
                                                     gpointer       user_data);
JsonNode     *matrix_utils_read_uri_finish          (GAsyncResult  *result,
                                                     GError       **error);
void          matrix_utils_get_homeserver_async     (const char    *username,
                                                     guint          timeout,
                                                     GCancellable  *cancellable,
                                                     GAsyncReadyCallback callback,
                                                     gpointer       user_data);
void          matrix_utils_verify_homeserver_async  (const char    *server,
                                                     guint          timeout,
                                                     GCancellable  *cancellable,
                                                     GAsyncReadyCallback callback,
                                                     gpointer       user_data);
gboolean      matrix_utils_verify_homeserver_finish (GAsyncResult  *result,
                                                     GError       **error);
char         *matrix_utils_get_homeserver_finish    (GAsyncResult  *result,
                                                     GError       **error);
void          matrix_utils_get_pixbuf_async         (const char    *file,
                                                     GCancellable  *cancellable,
                                                     GAsyncReadyCallback callback,
                                                     gpointer       user_data);
GdkPixbuf    *matrix_utils_get_pixbuf_finish        (GAsyncResult  *result,
                                                     GError       **error);
