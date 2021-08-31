/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-contact-provider.c
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-folks"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <libebook/libebook.h>

#include "users/chatty-contact.h"
#include "users/chatty-contact-private.h"
#include "chatty-contact-provider.h"

/**
 * SECTION: chatty-eds
 * @title: ChattyEds
 * @short_description: A Contact List populated with evolution-data-server
 * @include: "chatty-contact-provider.h"
 */

#define PHONE_SEXP    "(contains 'phone' '')"
#define IM_SEXP       "(contains 'im_jabber' '')"
#define CHATTY_SEXP   "(or " PHONE_SEXP  IM_SEXP ")"

struct _ChattyEds
{
  GObject           parent_instance;

  ESourceRegistry  *source_registry;
  GCancellable     *cancellable;

  /* contacts to be saved to contacts_list */
  GPtrArray        *contacts_array;
  GListStore       *eds_view_list;
  GListStore       *contacts_list;
  guint             providers_to_load;
  ChattyProtocol    protocols;
  gboolean          is_ready;
};

G_DEFINE_TYPE (ChattyEds, chatty_eds, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_IS_READY,
  PROP_PROTOCOLS,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
chatty_app_run_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  GDBusConnection *connection = (GDBusConnection *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GVariant) variant = NULL;
  GError *error = NULL;

  g_assert (G_IS_DBUS_CONNECTION (connection));
  g_assert (G_IS_TASK (task));

  variant = g_dbus_connection_call_finish (connection, result, &error);

  if (error) {
    g_dbus_error_strip_remote_error (error);
    g_task_return_error (task, error);
  } else {
    g_task_return_boolean (task, TRUE);
  }
}

static void
chatty_eds_bus_got (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(GDBusConnection) connection = NULL;
  GCancellable *cancellable;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  connection  = g_bus_get_finish (result, &error);
  cancellable = g_task_get_cancellable (task);

  if (error)
    g_task_return_error (task, error);
  else
    g_dbus_connection_call (connection,
                            "org.gnome.Contacts",
                            "/org/gnome/Contacts",
                            "org.gtk.Application",
                            "Activate",
                            g_variant_new ("(a{sv})", NULL),
                            NULL,
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            cancellable,
                            chatty_app_run_cb,
                            g_steal_pointer (&task));
}

static ChattyContact *
chatty_contact_provider_get_match (ChattyEds      *self,
                                   const char     *value,
                                   ChattyProtocol  protocols)
{
  GListModel *model;
  guint n_items;
  gboolean match;

  model = G_LIST_MODEL (self->contacts_list);
  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(ChattyContact) contact = NULL;

      contact = g_list_model_get_item (model, i);
      match = chatty_contact_is_exact_match (contact, value, protocols);

      if (match)
        return contact;
    }

  return NULL;
}

static void
eds_find_contact_index (ChattyEds  *self,
                        const char *uid,
                        guint      *position,
                        guint      *count)
{
  const char *old_uid;
  guint n_items, i;
  gboolean match;

  g_assert (CHATTY_IS_EDS (self));
  g_assert (position && count);
  g_assert (uid && *uid);

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->contacts_list));
  *position = *count = 0;

  for (i = 0; i < n_items; i++) {
    g_autoptr(ChattyContact) contact = NULL;

    contact = g_list_model_get_item (G_LIST_MODEL (self->contacts_list), i);
    old_uid = chatty_contact_get_uid (contact);
    match = g_str_equal (old_uid, uid);

    /* The list is sorted.  Find the first match and total count. */
    if (!*count && match)
      *position = i, *count = 1;
    else if (match) /* Subsequent matches */
      ++*count;
    else if (*position) /* We moved past the last match */
      break;
  }
}


static void
chatty_eds_load_contact (ChattyEds     *self,
                         EContact      *contact,
                         EContactField  field_id)
{
  g_autoptr(GList) attributes = NULL;
  ChattyProtocol protocol = 0;

  g_assert (CHATTY_IS_EDS (self));
  g_assert (E_IS_CONTACT (contact));

  if (field_id == E_CONTACT_TEL) {
    if (self->protocols & CHATTY_PROTOCOL_CALL)
      protocol = CHATTY_PROTOCOL_CALL;
    else
      protocol = CHATTY_PROTOCOL_MMS_SMS;
  } else if (field_id == E_CONTACT_IM_JABBER) {
    protocol = CHATTY_PROTOCOL_XMPP;
  } else {
    g_warn_if_reached ();
  }

  /* Only container should be freed, attribute is freed in ChattyContact */
  attributes = e_contact_get_attributes (contact, field_id);

  for (GSList *l = (GSList *)attributes; l != NULL; l = l->next) {
    g_autofree char *value = NULL;

    value = e_vcard_attribute_get_value (l->data);

    if (value && *value)
      g_ptr_array_add (self->contacts_array,
                       chatty_contact_new (contact, l->data, protocol));

    }
}

static void
chatty_eds_remove_contact (ChattyEds  *self,
                           const char *uid)
{
  guint position, count;

  g_assert (CHATTY_IS_EDS (self));

  eds_find_contact_index (self, uid, &position, &count);

  if (count)
    g_list_store_splice (self->contacts_list, position, count, NULL, 0);
}

static void
chatty_eds_objects_added_cb (ChattyEds       *self,
                             const GSList    *objects,
                             EBookClientView *view)
{
  g_assert (CHATTY_IS_EDS (self));
  g_assert (E_IS_BOOK_CLIENT_VIEW (view));

  if (!self->contacts_array)
    self->contacts_array = g_ptr_array_new_full (100, g_object_unref);

  for (GSList *l = (GSList *)objects; l != NULL; l = l->next)
    {
      if (self->protocols & CHATTY_PROTOCOL_MMS_SMS ||
          self->protocols & CHATTY_PROTOCOL_CALL)
        chatty_eds_load_contact (self, l->data, E_CONTACT_TEL);

      if (self->protocols & CHATTY_PROTOCOL_XMPP)
        chatty_eds_load_contact (self, l->data, E_CONTACT_IM_JABBER);
    }
}

static void
chatty_eds_objects_modified_cb (ChattyEds       *self,
                                const GSList    *objects,
                                EBookClientView *view)
{
  g_assert (CHATTY_IS_EDS (self));
  g_assert (E_IS_BOOK_CLIENT_VIEW (view));

  for (GSList *l = (GSList *)objects; l != NULL; l = l->next)
    chatty_eds_remove_contact (self, e_contact_get_const (l->data, E_CONTACT_UID));

  chatty_eds_objects_added_cb (self, objects, view);
}

static void
chatty_eds_objects_removed_cb (ChattyEds       *self,
                               const GSList    *objects,
                               EBookClientView *view)
{
  g_assert (CHATTY_IS_EDS (self));
  g_assert (E_IS_BOOK_CLIENT_VIEW (view));

  for (GSList *node = (GSList *)objects; node; node = node->next)
    chatty_eds_remove_contact (self, node->data);
}

static void
chatty_eds_load_complete_cb (ChattyEds *self)
{
  g_autoptr(GPtrArray) array = NULL;

  if (!self->contacts_array || !self->contacts_array->len)
    return;

  array = g_steal_pointer (&self->contacts_array);
  g_list_store_splice (self->contacts_list, 0, 0, array->pdata, array->len);

  self->is_ready = TRUE;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_READY]);
}

static void
chatty_eds_get_view_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  g_autoptr(ChattyEds) self = user_data;
  EBookClient *client = E_BOOK_CLIENT (object);
  EBookClientView *client_view;
  g_autoptr(GError) error = NULL;

  g_assert (CHATTY_IS_EDS (self));
  g_assert (E_IS_BOOK_CLIENT (client));

  e_book_client_get_view_finish (E_BOOK_CLIENT (client), result, &client_view, &error);

  if (error)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Error: %s", error->message);

      return;
    }

  g_list_store_append (self->eds_view_list, client_view);
  g_object_unref (client_view);

  g_signal_connect_object (client_view, "objects-added",
                           G_CALLBACK (chatty_eds_objects_added_cb), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (client_view, "objects-modified",
                           G_CALLBACK (chatty_eds_objects_modified_cb), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (client_view, "objects-removed",
                           G_CALLBACK (chatty_eds_objects_removed_cb), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (client_view, "complete",
                           G_CALLBACK (chatty_eds_load_complete_cb), self,
                           G_CONNECT_SWAPPED);

  e_book_client_view_start (client_view, &error);

  if (error)
    g_warning ("Error: %s", error->message);
}

static void
chatty_eds_client_connected_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  g_autoptr(ChattyEds) self = user_data;
  EClient *client;
  g_autoptr(GError) error = NULL;
  const char *sexp;

  g_assert (CHATTY_IS_EDS (self));

  client = e_book_client_connect_finish (result, &error);

  if (!error)
    {
      ESourceOffline *extension;
      ESource *source;

      source = e_client_get_source (client);
      extension = e_source_get_extension (source, E_SOURCE_EXTENSION_OFFLINE);
      e_source_offline_set_stay_synchronized (extension, TRUE);
      e_source_registry_commit_source_sync (self->source_registry, source,
                                            self->cancellable, &error);
    }

  if (error)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Error: %s", error->message);
      g_clear_object (&client);

      return;
    }

  if (self->protocols & CHATTY_PROTOCOL_CALL)
    sexp = PHONE_SEXP;
  else
    sexp = CHATTY_SEXP;

  e_book_client_get_view (E_BOOK_CLIENT (client),
                          sexp,
                          NULL,
                          chatty_eds_get_view_cb,
                          g_object_ref (self));
  g_clear_object (&client);
}


static void
chatty_eds_load_contacts (ChattyEds *self)
{
  GList *sources;

  g_assert (CHATTY_IS_EDS (self));
  g_assert (E_IS_SOURCE_REGISTRY (self->source_registry));

  sources = e_source_registry_list_sources (self->source_registry,
                                            E_SOURCE_EXTENSION_ADDRESS_BOOK);

  for (GList *l = sources; l != NULL; l = l->next)
    {
      self->providers_to_load++;
      e_book_client_connect (l->data,
                             -1,    /* timeout seconds */
                             NULL,
                             chatty_eds_client_connected_cb,
                             g_object_ref (self));
    }

  g_list_free_full (sources, g_object_unref);
}


static void
chatty_eds_registry_new_finish_cb (GObject      *object,
                                   GAsyncResult *result,
                                   gpointer      user_data)
{
  g_autoptr(ChattyEds) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (CHATTY_IS_EDS (self));

  self->source_registry = e_source_registry_new_finish (result, &error);

  if (!error)
    chatty_eds_load_contacts (self);
  else if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    g_warning ("Error: %s", error->message);
}


static void
chatty_eds_load (ChattyEds *self)
{
  g_assert (CHATTY_IS_EDS (self));
  g_assert (G_IS_CANCELLABLE (self->cancellable));

  e_source_registry_new (self->cancellable,
                         chatty_eds_registry_new_finish_cb,
                         g_object_ref (self));
}


static void
chatty_eds_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  ChattyEds *self = (ChattyEds *)object;

  switch (prop_id)
    {
    case PROP_IS_READY:
      g_value_set_boolean (value, self->is_ready);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}


static void
chatty_eds_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  ChattyEds *self = (ChattyEds *)object;

  switch (prop_id)
    {
    case PROP_PROTOCOLS:
      self->protocols = g_value_get_int (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}


static void
chatty_eds_constructed (GObject *object)
{
  ChattyEds *self = (ChattyEds *)object;

  G_OBJECT_CLASS (chatty_eds_parent_class)->constructed (object);

  chatty_eds_load (self);
}


static void
chatty_eds_finalize (GObject *object)
{
  ChattyEds *self = (ChattyEds *)object;

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->eds_view_list);
  g_clear_object (&self->contacts_list);
  if (self->contacts_array)
    g_ptr_array_free (self->contacts_array, TRUE);

  G_OBJECT_CLASS (chatty_eds_parent_class)->finalize (object);
}


static void
chatty_eds_class_init (ChattyEdsClass *klass)
{
  GObjectClass *object_class  = G_OBJECT_CLASS (klass);

  object_class->get_property = chatty_eds_get_property;
  object_class->set_property = chatty_eds_set_property;
  object_class->constructed  = chatty_eds_constructed;
  object_class->finalize = chatty_eds_finalize;

  /**
   * ChattyEds:is-ready:
   *
   * Emitted when the contact provider is ready and all contacts
   * from at least one provider has loaded.
   * The property change may be notified multiple times as new
   * providers are loaded.
   */
  properties[PROP_IS_READY] =
    g_param_spec_boolean ("is-ready",
                          "Is Ready",
                          "The contact provider is ready and loaded",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  /**
   * ChattyEds:protocols:
   *
   * Matching protocols for the contacts to be loaded.
   */
  properties[PROP_PROTOCOLS] =
    g_param_spec_int ("protocols",
                      "Protocols",
                      "Protocols to be loaded from contact",
                      CHATTY_PROTOCOL_NONE,
                      CHATTY_PROTOCOL_XMPP,
                      CHATTY_PROTOCOL_NONE,
                      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}


static void
chatty_eds_init (ChattyEds *self)
{
  self->eds_view_list = g_list_store_new (E_TYPE_BOOK_CLIENT_VIEW);
  self->contacts_list = g_list_store_new (CHATTY_TYPE_CONTACT);
  self->cancellable = g_cancellable_new ();
}


/**
 * chatty_eds_new:
 * @protocols: #ChattyProtocol flag
 *
 * Create a new contact provider.  Call this function only
 * once and reuse the #ChattyEds for further use.
 *
 * @protocols should contain all the protocols to be loaded
 * from the contacts found.  Load all protocols at once even
 * if you don't require now, but may be required later.
 *
 * Say for example: You may not require XMPP contacts
 * if the network is down, but you may require them when
 * network is up.  So, regardless of the network state,
 * always load XMPP contacts and filter it out when not
 * needed.
 *
 * Returns: (transfer full): A #ChattyEds
 */
ChattyEds *
chatty_eds_new (ChattyProtocol protocols)
{
  return g_object_new (CHATTY_TYPE_EDS,
                       "protocols", protocols,
                       NULL);
}


/**
 * chatty_eds_is_ready:
 * @self: A #ChattyEds
 *
 * Get if the contact provider is ready and loading
 * of at least one provider is complete.
 *
 * Returns: %TRUE if @self is ready for use.
 * %FALSE otherwise
 */
gboolean
chatty_eds_is_ready (ChattyEds  *self)
{
  g_return_val_if_fail (CHATTY_IS_EDS (self), FALSE);

  return self->is_ready;
}


/**
 * chatty_eds_get_model:
 * @self: A #ChattyEds
 *
 * Get A #GListModel that contains all the
 * #ChattyContact loaded.
 *
 * Returns: (transfer none): A #GListModel.
 */
GListModel *
chatty_eds_get_model (ChattyEds *self)
{
  g_return_val_if_fail (CHATTY_IS_EDS (self), NULL);

  return G_LIST_MODEL (self->contacts_list);
}


/**
 * chatty_eds_find_by_number:
 * @self: A #ChattyEds
 * @phone_number: A Valid Phone number to match
 *
 * Find the first #ChattyContact matching @phone_number.
 * A match can be either exact or one excluding the
 * country prefix (Eg: +1987654321 and 987654321 matches)
 *
 * Returns: (transfer none) (nullable): A #ChattyContact.
 */
ChattyContact *
chatty_eds_find_by_number (ChattyEds  *self,
                           const char *phone_number)
{
  g_return_val_if_fail (CHATTY_IS_EDS (self), NULL);

  return chatty_contact_provider_get_match (self, phone_number, CHATTY_PROTOCOL_ANY);
}


/**
 * chatty_eds_launch_contacts:
 * @self: A #ChattyEds
 *
 * Open GNOME Contacts.
 */
void
chatty_eds_open_contacts_app (ChattyEds           *self,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (CHATTY_IS_EDS (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);

  g_bus_get (G_BUS_TYPE_SESSION,
             cancellable,
             chatty_eds_bus_got,
             g_steal_pointer (&task));
}

gboolean
chatty_eds_open_contacts_app_finish (ChattyEds    *self,
                                     GAsyncResult *result,
                                     GError       **error)
{
  g_return_val_if_fail (CHATTY_IS_EDS (self), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
chatty_save_contact_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  GDBusConnection *connection = (GDBusConnection *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GVariant) variant = NULL;
  GError *error = NULL;

  g_assert (G_IS_DBUS_CONNECTION (connection));
  g_assert (G_IS_TASK (task));

  variant = g_dbus_connection_call_finish (connection, result, &error);

  if (error) {
    g_dbus_error_strip_remote_error (error);
    g_warning ("Error saving to contacts: %s", error->message);
    g_task_return_error (task, error);
  } else {
    g_task_return_boolean (task, TRUE);
  }
}

static void
chatty_check_save_contact_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  GDBusConnection *connection = (GDBusConnection *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GVariant) variant = NULL;
  GError *error = NULL;

  g_assert (G_IS_DBUS_CONNECTION (connection));
  g_assert (G_IS_TASK (task));

  variant = g_dbus_connection_call_finish (connection, result, &error);

  if (error) {
    g_dbus_error_strip_remote_error (error);
    g_warning ("Error saving to contacts: %s", error->message);
    g_task_return_error (task, error);
  } else {
    GCancellable *cancellable;
    GVariantBuilder options;
    GVariant *contact;

    cancellable = g_task_get_cancellable (task);
    contact = g_task_get_task_data (task);
    g_assert (contact);

    g_variant_builder_init (&options, G_VARIANT_TYPE ("av"));
    g_variant_builder_add (&options, "v", contact);

    g_dbus_connection_call (connection,
                            "org.gnome.Contacts",
                            "/org/gnome/Contacts",
                            "org.gtk.Actions",
                            "Activate",
                            g_variant_new ("(sava{sv})",
                                           "new-contact-data", &options, NULL),
                            NULL,
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            cancellable,
                            chatty_save_contact_cb,
                            g_steal_pointer (&task));
  }
}

static void
eds_write_contact_bus_got (GObject      *object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(GDBusConnection) connection = NULL;
  GCancellable *cancellable;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  connection  = g_bus_get_finish (result, &error);
  cancellable = g_task_get_cancellable (task);

  /* XXX: A direct call to 'new-contact-data' action doesn't seem to fail
     if the action is missing, So let's manually check if it exists */
  if (error) {
    g_warning ("Error getting dbus: %s", error->message);
    g_task_return_error (task, error);
  } else {
    g_dbus_connection_call (connection,
                            "org.gnome.Contacts",
                            "/org/gnome/Contacts",
                            "org.gtk.Actions",
                            "Describe",
                            g_variant_new ("(s)", "new-contact-data"),
                            NULL,
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            cancellable,
                            chatty_check_save_contact_cb,
                            g_steal_pointer (&task));
  }
}

void
chatty_eds_write_contact_async (const char          *name,
                                const char          *phone_number,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  GVariant *contact;

  g_return_if_fail (name);
  g_return_if_fail (phone_number);

  contact =  g_variant_new_parsed ("[('full-name', %s), ('phone-numbers', %s)]",
                                   name, phone_number);

  task = g_task_new (NULL, NULL, callback, user_data);
  g_task_set_task_data (task, contact, NULL);

  g_bus_get (G_BUS_TYPE_SESSION,
             NULL,
             eds_write_contact_bus_got,
             g_steal_pointer (&task));
}

gboolean
chatty_eds_write_contact_finish (GAsyncResult  *result,
                                 GError       **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

