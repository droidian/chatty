/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-phone-utils.cpp
 *
 * Copyright (C) 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "chatty-phone-utils"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <phonenumbers/phonenumberutil.h>

#include "chatty-phone-utils.h"

using i18n::phonenumbers::PhoneNumber;
using i18n::phonenumbers::PhoneNumberUtil;


gboolean
chatty_phone_utils_is_valid (const char *number,
                             const char *country_code)
{
  PhoneNumberUtil *util = PhoneNumberUtil::GetInstance ();
  PhoneNumber phone_number;

  if (!number || !*number ||
      !country_code || strlen (country_code) != 2)
    return FALSE;

  if (util->Parse (number, country_code, &phone_number) == PhoneNumberUtil::NO_PARSING_ERROR)
    return util->IsValidNumber (phone_number);

  return FALSE;
}

gboolean
chatty_phone_utils_is_possible (const char *number,
                                const char *country_code)
{
  PhoneNumberUtil *util = PhoneNumberUtil::GetInstance ();

  if (!number || !*number ||
      !country_code || strlen (country_code) != 2)
    return FALSE;

  return util->IsPossibleNumberForString (number, country_code);
}
