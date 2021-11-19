/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#ifndef __XEPS_H_INCLUDE__
#define __XEPS_H_INCLUDE__

#include "xmlnode.h"

#include <libpurple/plugin.h>
#include <glib.h>

#define CHATTY_XEPS_NS_STREAM "http://etherx.jabber.org/streams"
#define CHATTY_XEPS_NS_CSI    "urn:xmpp:csi:0"
#define CHATTY_XEPS_JABBER_PROTOCOL_ID "prpl-jabber"
G_DEFINE_AUTOPTR_CLEANUP_FUNC (xmlnode, xmlnode_free)

void * chatty_xeps_get_handle (void);
PurplePlugin * chatty_xeps_get_jabber (void);

void chatty_xeps_init (void);
void chatty_xeps_close (void);

#endif
