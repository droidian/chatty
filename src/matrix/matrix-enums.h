/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* matrix-enums.h
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
 * MatrixError:
 *
 * The Error returned by the Matrix Server
 * See https://matrix.org/docs/spec/client_server/r0.6.1#api-standards
 * for details.
 */
typedef enum {
  M_FORBIDDEN = 1,
  M_UNKNOWN_TOKEN,
  M_MISSING_TOKEN,
  M_BAD_JSON,
  M_NOT_JSON,
  M_NOT_FOUND,
  M_LIMIT_EXCEEDED,
  M_UNKNOWN,
  M_UNRECOGNIZED,
  M_UNAUTHORIZED,
  M_USER_DEACTIVATED,
  M_USER_IN_USE,
  M_INVALID_USERNAME,
  M_ROOM_IN_USE,
  M_INVALID_ROOM_STATE,
  M_THREEPID_IN_USE,
  M_THREEPID_NOT_FOUND,
  M_THREEPID_AUTH_FAILED,
  M_THREEPID_DENIED,
  M_SERVER_NOT_TRUSTED,
  M_UNSUPPORTED_ROOM_VERSION,
  M_INCOMPATIBLE_ROOM_VERSION,
  M_BAD_STATE,
  M_GUEST_ACCESS_FORBIDDEN,
  M_CAPTCHA_NEEDED,
  M_CAPTCHA_INVALID,
  M_MISSING_PARAM,
  M_INVALID_PARAM,
  M_TOO_LARGE,
  M_EXCLUSIVE,
  M_RESOURCE_LIMIT_EXCEEDED,
  M_CANNOT_LEAVE_SERVER_NOTICE_ROOM,

  /* Local options */
  M_BAD_PASSWORD,
  M_NO_HOME_SERVER,
  M_BAD_HOME_SERVER,
} MatrixError;


/*
 * MATRIX_BLUE_PILL and MATRIX_RED_PILL
 * are objects than actions
 */
typedef enum {
  /* When nothing real is happening */
  MATRIX_BLUE_PILL,  /* For no/unknown command */
  MATRIX_GET_HOMESERVER,
  MATRIX_VERIFY_HOMESERVER,
  MATRIX_PASSWORD_LOGIN,
  MATRIX_ACCESS_TOKEN_LOGIN,
  MATRIX_UPLOAD_KEY,
  MATRIX_GET_JOINED_ROOMS,
  MATRIX_SET_TYPING,
  MATRIX_SEND_MESSAGE,
  MATRIX_SEND_IMAGE,
  MATRIX_SEND_VIDEO,
  MATRIX_SEND_FILE,
  /* sync: plugged into the Matrix from real world */
  MATRIX_RED_PILL,
} MatrixAction;
