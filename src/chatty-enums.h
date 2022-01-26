/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* chatty-enums.h
 *
 * Copyright 2020 Purism SPC
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once


/**
 * ChattyStatus:
 *
 * Account connection status for #ChattyPpAccount
 */
typedef enum
{
  CHATTY_UNKNOWN,
  CHATTY_DISCONNECTED,
  CHATTY_CONNECTING,
  CHATTY_CONNECTED,
} ChattyStatus;

/**
 * ChattyIdType:
 *
 * The type of ID used.  This can be different
 * from protocol.  Say, a user may use a phone
 * number to log in to a matrix account.
 */
typedef enum
{
  CHATTY_ID_UNKNOWN,
  CHATTY_ID_PHONE,
  CHATTY_ID_EMAIL,
  CHATTY_ID_MATRIX,
  CHATTY_ID_XMPP
} ChattyIdType;

/**
 * ChattyProtocol:
 *
 * Protocols supported/implemented by #ChattyUser
 */
typedef enum
{
  CHATTY_PROTOCOL_NONE     = 0,
  CHATTY_PROTOCOL_MMS_SMS  = 1 << 0,
  CHATTY_PROTOCOL_MMS      = 1 << 1,
  CHATTY_PROTOCOL_CALL     = 1 << 2,
  CHATTY_PROTOCOL_XMPP     = 1 << 3,
  CHATTY_PROTOCOL_MATRIX   = 1 << 4,
  CHATTY_PROTOCOL_TELEGRAM = 1 << 5,
  CHATTY_PROTOCOL_EMAIL    = 1 << 6,
  CHATTY_PROTOCOL_DELTA    = 1 << 7, /* prpl-delta */
  CHATTY_PROTOCOL_THREEPL  = 1 << 8, /* prpl-threepl */
  CHATTY_PROTOCOL_LAST     = 1 << 8,
  CHATTY_PROTOCOL_ANY      = ~0
} ChattyProtocol;


/**
 * ChattyEncryption:
 *
 * Encryption status of a #ChattyChat
 */
typedef enum
{
  CHATTY_ENCRYPTION_UNKNOWN,
  CHATTY_ENCRYPTION_ENABLED,
  CHATTY_ENCRYPTION_DISABLED,
  CHATTY_ENCRYPTION_UNSUPPORTED
} ChattyEncryption;

/**
 * ChattyUserFlag:
 *
 * Different flags set for user
 */
typedef enum
{
  CHATTY_USER_FLAG_NONE,
  CHATTY_USER_FLAG_MEMBER    = 1 << 0,
  CHATTY_USER_FLAG_MODERATOR = 1 << 1,
  CHATTY_USER_FLAG_OWNER     = 1 << 2,
} ChattyUserFlag;

/**
 * ChattyMsgDirection:
 *
 * The Direction of a #ChattyMessage
 */
typedef enum
{
  CHATTY_DIRECTION_UNKNOWN,
  CHATTY_DIRECTION_IN,
  CHATTY_DIRECTION_OUT,
  CHATTY_DIRECTION_SYSTEM
} ChattyMsgDirection;

/**
 * ChattyMsgStatus:
 *
 * The Status of a #ChattyMessage.
 */
typedef enum
{
  CHATTY_STATUS_UNKNOWN,
  CHATTY_STATUS_RECIEVED,
  CHATTY_STATUS_SENDING,
  CHATTY_STATUS_SENT,
  CHATTY_STATUS_DELIVERED,
  CHATTY_STATUS_READ,
  CHATTY_STATUS_SENDING_FAILED,
  CHATTY_STATUS_DELIVERY_FAILED
} ChattyMsgStatus;

typedef enum
{
  CHATTY_MESSAGE_UNKNOWN,
  CHATTY_MESSAGE_TEXT,
  CHATTY_MESSAGE_HTML,
  CHATTY_MESSAGE_HTML_ESCAPED,
  CHATTY_MESSAGE_MATRIX_HTML,
  CHATTY_MESSAGE_LOCATION,
  CHATTY_MESSAGE_FILE,
  CHATTY_MESSAGE_IMAGE,
  CHATTY_MESSAGE_VIDEO,
  CHATTY_MESSAGE_AUDIO,
  /* An MMS can have a several files in different formats */
  CHATTY_MESSAGE_MMS,
} ChattyMsgType;

typedef enum
{
  CHATTY_FILE_UNKNOWN,
  CHATTY_FILE_DOWNLOADED = 1,
  CHATTY_FILE_MISSING,
  CHATTY_FILE_DECRYPT_FAILED,
  CHATTY_FILE_DOWNLOADING,
  CHATTY_FILE_ERROR,
} ChattyFileStatus;

typedef enum
{
  CHATTY_ITEM_VISIBLE,
  CHATTY_ITEM_HIDDEN,
  CHATTY_ITEM_ARCHIVED,
  CHATTY_ITEM_BLOCKED,
} ChattyItemState;
