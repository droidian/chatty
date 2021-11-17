/* chatty-settings.c
 *
 * Copyright 2019 Purism SPC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-settings"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "chatty-settings.h"

/**
 * SECTION: chatty-settings
 * @title: ChattySettings
 * @short_description: The Application settings
 * @include: "chatty-settings.h"
 *
 * A class that handles application specific settings, and
 * to store them to disk.
 */

struct _ChattySettings
{
  GObject     parent_instance;

  GSettings  *settings;
  char       *country_code;
};

G_DEFINE_TYPE (ChattySettings, chatty_settings, G_TYPE_OBJECT)


enum {
  PROP_0,
  PROP_FIRST_START,
  PROP_SEND_RECEIPTS,
  PROP_MESSAGE_CARBONS,
  PROP_SEND_TYPING,
  PROP_BLUR_IDLE_BUDDIES,
  PROP_INDICATE_UNKNOWN_CONTACTS,
  PROP_CONVERT_EMOTICONS,
  PROP_RETURN_SENDS_MESSAGE,
  PROP_REQUEST_SMS_DELIVERY_REPORTS,
  PROP_REQUEST_MMSD_TNG_SMIL,
  PROP_MMSD_CARRIER_MMSC,
  PROP_MMSD_CARRIER_APN,
  PROP_MMSD_CARRIER_PROXY,
  PROP_MAM_ENABLED,
  PROP_PURPLE_ENABLED,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
chatty_settings_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  ChattySettings *self = (ChattySettings *)object;

  switch (prop_id)
    {
    case PROP_FIRST_START:
      g_value_set_boolean (value, chatty_settings_get_first_start (self));
      break;

    case PROP_SEND_RECEIPTS:
      g_value_set_boolean (value, chatty_settings_get_send_receipts (self));
      break;

    case PROP_MESSAGE_CARBONS:
      g_value_set_boolean (value, chatty_settings_get_message_carbons (self));
      break;

    case PROP_MAM_ENABLED:
      g_value_set_boolean (value, chatty_settings_get_mam_enabled (self));
      break;

    case PROP_SEND_TYPING:
      g_value_set_boolean (value, chatty_settings_get_send_typing (self));
      break;

    case PROP_BLUR_IDLE_BUDDIES:
      g_value_set_boolean (value, chatty_settings_get_blur_idle_buddies (self));
      break;

    case PROP_INDICATE_UNKNOWN_CONTACTS:
      g_value_set_boolean (value, chatty_settings_get_indicate_unknown_contacts (self));
      break;

    case PROP_CONVERT_EMOTICONS:
      g_value_set_boolean (value, chatty_settings_get_convert_emoticons (self));
      break;

    case PROP_RETURN_SENDS_MESSAGE:
      g_value_set_boolean (value, chatty_settings_get_return_sends_message (self));
      break;

    case PROP_REQUEST_SMS_DELIVERY_REPORTS:
      g_value_set_boolean (value, chatty_settings_request_sms_delivery_reports (self));
      break;

    case PROP_REQUEST_MMSD_TNG_SMIL:
      g_value_set_boolean (value, chatty_settings_request_mmsd_tng_smil (self));
      break;

    case PROP_MMSD_CARRIER_MMSC:
      g_value_set_string (value, chatty_settings_get_mms_carrier_mmsc (self));
      break;

    case PROP_MMSD_CARRIER_APN:
      g_value_set_string (value, chatty_settings_get_mms_carrier_apn (self));
      break;

    case PROP_MMSD_CARRIER_PROXY:
      g_value_set_string (value, chatty_settings_get_mms_carrier_proxy (self));
      break;

    case PROP_PURPLE_ENABLED:
      g_value_set_boolean (value, chatty_settings_get_purple_enabled (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
chatty_settings_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  ChattySettings *self = (ChattySettings *)object;


  switch (prop_id)
    {
    case PROP_FIRST_START:
      g_settings_set_boolean (self->settings, "first-start",
                              g_value_get_boolean (value));
      break;

    case PROP_SEND_RECEIPTS:
      g_settings_set_boolean (self->settings, "send-receipts",
                              g_value_get_boolean (value));
      break;

    case PROP_MESSAGE_CARBONS:
      g_settings_set_boolean (self->settings, "message-carbons",
                              g_value_get_boolean (value));
      break;

    case PROP_MAM_ENABLED:
      g_settings_set_boolean (self->settings, "mam-enabled",
                              g_value_get_boolean (value));
      break;

    case PROP_SEND_TYPING:
      g_settings_set_boolean (self->settings, "send-typing",
                              g_value_get_boolean (value));
      break;

    case PROP_BLUR_IDLE_BUDDIES:
      g_settings_set_boolean (self->settings, "blur-idle-buddies",
                              g_value_get_boolean (value));
      break;

    case PROP_INDICATE_UNKNOWN_CONTACTS:
      g_settings_set_boolean (self->settings, "indicate-unknown-contacts",
                              g_value_get_boolean (value));
      break;

    case PROP_CONVERT_EMOTICONS:
      g_settings_set_boolean (self->settings, "convert-emoticons",
                              g_value_get_boolean (value));
      break;

    case PROP_RETURN_SENDS_MESSAGE:
      g_settings_set_boolean (self->settings, "return-sends-message",
                              g_value_get_boolean (value));
      break;

    case PROP_REQUEST_SMS_DELIVERY_REPORTS:
      g_settings_set_boolean (self->settings, "request-sms-delivery-reports",
                              g_value_get_boolean (value));
      break;

    case PROP_REQUEST_MMSD_TNG_SMIL:
      g_settings_set_boolean (self->settings, "request-mmsd-smil",
                              g_value_get_boolean (value));
      break;

    case PROP_MMSD_CARRIER_MMSC:
      g_settings_set_string (G_SETTINGS (self->settings), "mmsd-carrier-mmsc",
                             g_value_get_string (value));
      break;

    case PROP_MMSD_CARRIER_APN:
      g_settings_set_string (G_SETTINGS (self->settings), "mmsd-carrier-mms-apn",
                             g_value_get_string (value));
      break;

    case PROP_MMSD_CARRIER_PROXY:
      g_settings_set_string (G_SETTINGS (self->settings), "mmsd-carrier-mms-proxy",
                             g_value_get_string (value));
      break;

    case PROP_PURPLE_ENABLED:
      g_settings_set_boolean (self->settings, "purple-enabled",
                              g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
chatty_settings_constructed (GObject *object)
{
  ChattySettings *self = (ChattySettings *)object;

  G_OBJECT_CLASS (chatty_settings_parent_class)->constructed (object);

  g_settings_bind (self->settings, "send-receipts",
                   self, "send-receipts", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "message-carbons",
                   self, "message-carbons", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "mam-enabled",
                   self, "mam-enabled", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "purple-enabled",
                   self, "purple-enabled", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "send-typing",
                   self, "send-typing", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "blur-idle-buddies",
                   self, "blur-idle-buddies", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "indicate-unknown-contacts",
                   self, "indicate-unknown-contacts", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "return-sends-message",
                   self, "return-sends-message", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "request-sms-delivery-reports",
                   self, "request-sms-delivery-reports", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "request-mmsd-smil",
                   self, "request-mmsd-smil", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "mmsd-carrier-mmsc",
                   self, "mmsd-carrier-mmsc", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "mmsd-carrier-mms-apn",
                   self, "mmsd-carrier-mms-apn", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "mmsd-carrier-mms-proxy",
                   self, "mmsd-carrier-mms-proxy", G_SETTINGS_BIND_DEFAULT);
  self->country_code = g_settings_get_string (self->settings, "country-code");
}

static void
chatty_settings_finalize (GObject *object)
{
  ChattySettings *self = (ChattySettings *)object;

  g_settings_set_boolean (self->settings, "first-start", FALSE);
  g_object_unref (self->settings);
  g_free (self->country_code);

  G_OBJECT_CLASS (chatty_settings_parent_class)->finalize (object);
}

static void
chatty_settings_class_init (ChattySettingsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = chatty_settings_get_property;
  object_class->set_property = chatty_settings_set_property;
  object_class->constructed = chatty_settings_constructed;
  object_class->finalize = chatty_settings_finalize;

  properties[PROP_FIRST_START] =
    g_param_spec_boolean ("first-start",
                          "First Start",
                          "Whether the application is launched the first time",
                          TRUE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_SEND_RECEIPTS] =
    g_param_spec_boolean ("send-receipts",
                          "Send Receipts",
                          "Send message read receipts",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_MESSAGE_CARBONS] =
    g_param_spec_boolean ("message-carbons",
                          "Message Carbons",
                          "Share chat history among devices",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_MAM_ENABLED] =
    g_param_spec_boolean ("mam-enabled",
                          "MAM is Enabled",
                          "Synchronize MAM Message Archive",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_PURPLE_ENABLED] =
    g_param_spec_boolean ("purple-enabled",
                          "Enable purple",
                          "Enable purple accounts",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_SEND_TYPING] =
    g_param_spec_boolean ("send-typing",
                          "Send Typing",
                          "Send typing notifications",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_BLUR_IDLE_BUDDIES] =
    g_param_spec_boolean ("blur-idle-buddies",
                          "Blur Idle Buddies",
                          "Blur Idle Buddies",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_INDICATE_UNKNOWN_CONTACTS] =
    g_param_spec_boolean ("indicate-unknown-contacts",
                          "Indicate Unknown Contacts",
                          "Indicate Unknown Contacts",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_CONVERT_EMOTICONS] =
    g_param_spec_boolean ("convert-emoticons",
                          "Convert Emoticons",
                          "Convert Emoticons",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_RETURN_SENDS_MESSAGE] =
    g_param_spec_boolean ("return-sends-message",
                          "Return Sends Message",
                          "Whether Return key sends message",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_REQUEST_SMS_DELIVERY_REPORTS] =
    g_param_spec_boolean ("request-sms-delivery-reports",
                          "Request SMS delivery reports",
                          "Whether to request delivery reports for outgoing SMS",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_REQUEST_MMSD_TNG_SMIL] =
    g_param_spec_boolean ("request-mmsd-smil",
                          "Request mmsd to handle smil",
                          "Whether to request mmsd to create smil",
                          TRUE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_MMSD_CARRIER_MMSC] =
    g_param_spec_string ("mmsd-carrier-mmsc",
                         "Carrier mmsc for mmsd",
                         "The carrier mmsc to set for mmsd",
                         "",
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_MMSD_CARRIER_APN] =
    g_param_spec_string ("mmsd-carrier-mms-apn",
                         "Carrier mms apn for mmsd",
                         "The mms APN to set for mmsd",
                         "",
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_MMSD_CARRIER_PROXY] =
    g_param_spec_string ("mmsd-carrier-mms-proxy",
                         "Carrier mms proxy for mmsd",
                         "The mms proxy to set for mmsd",
                         "",
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
chatty_settings_init (ChattySettings *self)
{
  self->settings = g_settings_new ("sm.puri.Chatty");
}

/**
 * chatty_settings_get_default:
 *
 * Get the default settings
 *
 * Returns: (transfer none): A #ChattySettings.
 */
ChattySettings *
chatty_settings_get_default (void)
{
  static ChattySettings *self;

  if (!self)
    {
      self = g_object_new (CHATTY_TYPE_SETTINGS, NULL);
      g_object_add_weak_pointer (G_OBJECT (self), (gpointer *)&self);
    }

  return self;
}


/**
 * chatty_settings_get_first_start:
 * @self: A #ChattySettings
 *
 * Get if the application is launching for the first time.
 *
 * Returns: %TRUE if the application is launching for the
 * first time. %FALSE otherwise.
 */
gboolean
chatty_settings_get_first_start (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, "first-start");
}

/**
 * chatty_settings_get_send_receipts:
 * @self: A #ChattySettings
 *
 * Get if the application should send others the information
 * whether a received message is read or not.
 *
 * Returns: %TRUE if the send receipts should be sent.
 * %FALSE otherwise.
 */
gboolean
chatty_settings_get_send_receipts (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, "send-receipts");
}

/**
 * chatty_settings_get_message_carbons:
 * @self: A #ChattySettings
 *
 * Get wether the chat history is shared among devices.
 *
 * Returns: %TRUE if message carbons should be sent.
 * %FALSE otherwise.
 */
gboolean
chatty_settings_get_message_carbons (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, "message-carbons");
}

/**
 * chatty_settings_get_mam_enabled:
 * @self: A #ChattySettings
 *
 * Get if the application should use Message Archive Management
 * when its support is discovered.
 *
 * Returns: %TRUE if the MAM query should be sent.
 * %FALSE otherwise.
 */
gboolean
chatty_settings_get_mam_enabled (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, "mam-enabled");
}

gboolean
chatty_settings_get_purple_enabled (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, "purple-enabled");
}

/**
 * chatty_settings_get_send_typing:
 * @self: A #ChattySettings
 *
 * Get if the typing notification should be sent to
 * other user.
 *
 * Returns: %TRUE if typing notification should be sent.
 * %FALSE otherwise
 */
gboolean
chatty_settings_get_send_typing (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, "send-typing");
}

gboolean
chatty_settings_get_blur_idle_buddies (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, "blur-idle-buddies");
}

gboolean
chatty_settings_get_indicate_unknown_contacts (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, "indicate-unknown-contacts");
}

gboolean
chatty_settings_get_convert_emoticons (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, "convert-emoticons");
}

gboolean
chatty_settings_get_return_sends_message (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, "return-sends-message");
}

/**
 * chatty_settings_get_window_maximized:
 * @self: A #ChattySettings
 *
 * Get the window maximized state as saved in @self.
 *
 * Returns: %TRUE if maximized.  %FALSE otherwise.
 */
gboolean
chatty_settings_get_window_maximized (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (G_SETTINGS (self->settings), "window-maximized");
}

/**
 * chatty_settings_set_window_maximized:
 * @self: A #ChattySettings
 * @maximized: The window state to save
 *
 * Set the window maximized state in @self.
 */
void
chatty_settings_set_window_maximized (ChattySettings *self,
                                      gboolean        maximized)
{
  g_return_if_fail (CHATTY_IS_SETTINGS (self));

  g_settings_set_boolean (G_SETTINGS (self->settings), "window-maximized", !!maximized);
}

/**
 * chatty_settings_get_window_geometry:
 * @self: A #ChattySettings
 * @geometry: (out): A #GdkRectangle
 *
 * Get the window geometry as saved in @self.
 */
void
chatty_settings_get_window_geometry (ChattySettings *self,
                                     GdkRectangle   *geometry)
{
  g_return_if_fail (CHATTY_IS_SETTINGS (self));
  g_return_if_fail (geometry != NULL);

  g_settings_get (G_SETTINGS (self->settings), "window-size", "(ii)", &geometry->width, &geometry->height);

  geometry->x = geometry->y = -1;
}

/**
 * chatty_settings_set_window_geometry:
 * @self: A #ChattySettings
 * @geometry: A #GdkRectangle
 *
 * Set the window geometry in @self.
 */
void
chatty_settings_set_window_geometry (ChattySettings *self,
                                     GdkRectangle   *geometry)
{
  g_return_if_fail (CHATTY_IS_SETTINGS (self));
  g_return_if_fail (geometry != NULL);

  g_settings_set (G_SETTINGS (self->settings), "window-size", "(ii)", geometry->width, geometry->height);
}

const char *
chatty_settings_get_country_iso_code (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), NULL);

  if (self->country_code && *self->country_code)
    return self->country_code;

  return NULL;
}

void
chatty_settings_set_country_iso_code (ChattySettings *self,
                                      const char     *country_code)
{
  g_return_if_fail (CHATTY_IS_SETTINGS (self));

  g_free (self->country_code);
  self->country_code = g_strdup (country_code);
  g_settings_set (G_SETTINGS (self->settings), "country-code", "s", country_code);
}

char *
chatty_settings_get_mms_carrier_mmsc (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), NULL);

  return g_settings_get_string (G_SETTINGS (self->settings),
                                 "mmsd-carrier-mmsc");
}

void
chatty_settings_set_mms_carrier_mmsc (ChattySettings *self,
                                      const char     *mms_carrier_mmsc)
{
  g_return_if_fail (CHATTY_IS_SETTINGS (self));

  if (mms_carrier_mmsc)
    g_object_set (self, "mmsd-carrier-mmsc", mms_carrier_mmsc, NULL);
  else
    g_object_set (self, "mmsd-carrier-mmsc", "", NULL);
}

char *
chatty_settings_get_mms_carrier_apn (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), NULL);

  return g_settings_get_string (G_SETTINGS (self->settings),
                                 "mmsd-carrier-mms-apn");
}

void
chatty_settings_set_mms_carrier_apn (ChattySettings *self,
                                     const char     *mms_carrier_apn)
{
  g_return_if_fail (CHATTY_IS_SETTINGS (self));

  if (mms_carrier_apn)
    g_object_set (self, "mmsd-carrier-mms-apn", mms_carrier_apn, NULL);
  else
    g_object_set (self, "mmsd-carrier-mms-apn", "", NULL);
}

char *
chatty_settings_get_mms_carrier_proxy (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), NULL);

  return g_settings_get_string (G_SETTINGS (self->settings),
                                 "mmsd-carrier-mms-proxy");
}

void
chatty_settings_set_mms_carrier_proxy (ChattySettings *self,
                                       const char     *mms_carrier_proxy)
{
  g_return_if_fail (CHATTY_IS_SETTINGS (self));

  if (mms_carrier_proxy)
    g_object_set (self, "mmsd-carrier-mms-proxy", mms_carrier_proxy, NULL);
  else
    g_object_set (self, "mmsd-carrier-mms-proxy", "", NULL);
}

gboolean
chatty_settings_request_sms_delivery_reports (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (G_SETTINGS (self->settings),
                                 "request-sms-delivery-reports");
}

gboolean
chatty_settings_request_mmsd_tng_smil (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (G_SETTINGS (self->settings),
                                 "request-mmsd-smil");
}

gboolean
chatty_settings_get_experimental_features (ChattySettings *self)
{
  g_return_val_if_fail (CHATTY_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (G_SETTINGS (self->settings), "experimental-features");
}

void
chatty_settings_enable_experimental_features (ChattySettings *self,
                                              gboolean        enable)
{
  g_return_if_fail (CHATTY_IS_SETTINGS (self));

  g_settings_set_boolean (G_SETTINGS (self->settings), "experimental-features", !!enable);
}
