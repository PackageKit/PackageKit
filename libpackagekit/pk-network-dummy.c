/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:pk-network-dummy
 * @short_description: Dummy network detection code
 *
 * This file contains a dummy network implimentation.
 * It is designed for people that don't have NetworkManager installed.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>

#include "pk-debug.h"
#include "pk-network.h"
#include "pk-marshal.h"

static void     pk_network_class_init	(PkNetworkClass *klass);
static void     pk_network_init		(PkNetwork      *network);
static void     pk_network_finalize	(GObject        *object);

#define PK_NETWORK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_NETWORK, PkNetworkPrivate))

/**
 * _PkNetworkPrivate:
 *
 * Private #PkNetwork data
 **/
struct _PkNetworkPrivate
{
	gpointer		 data;
};

enum {
	PK_NETWORK_ONLINE,
	PK_NETWORK_LAST_SIGNAL
};

static guint signals [PK_NETWORK_LAST_SIGNAL] = { 0 };
static gpointer pk_network_object = NULL;

G_DEFINE_TYPE (PkNetwork, pk_network, G_TYPE_OBJECT)

/**
 * pk_network_is_online:
 * @network: a valid #PkNetwork instance
 *
 * Return value: %TRUE if the network is online
 * Note: This is a dummy file and no checks are done
 **/
gboolean
pk_network_is_online (PkNetwork *network)
{
	/* don't do any checks */
	return TRUE;
}

/**
 * pk_network_class_init:
 * @klass: The PkNetworkClass
 **/
static void
pk_network_class_init (PkNetworkClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_network_finalize;
	signals [PK_NETWORK_ONLINE] =
		g_signal_new ("online",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	g_type_class_add_private (klass, sizeof (PkNetworkPrivate));
}

/**
 * pk_network_init:
 * @network: This class instance
 **/
static void
pk_network_init (PkNetwork *network)
{
	network->priv = PK_NETWORK_GET_PRIVATE (network);
}

/**
 * pk_network_finalize:
 * @object: The object to finalize
 **/
static void
pk_network_finalize (GObject *object)
{
	PkNetwork *network;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_NETWORK (object));
	network = PK_NETWORK (object);

	g_return_if_fail (network->priv != NULL);
	G_OBJECT_CLASS (pk_network_parent_class)->finalize (object);
}

/**
 * pk_network_new:
 *
 * Return value: a new PkNetwork object.
 **/
PkNetwork *
pk_network_new (void)
{
	if (pk_network_object != NULL) {
		g_object_ref (pk_network_object);
	} else {
		pk_network_object = g_object_new (PK_TYPE_NETWORK, NULL);
		g_object_add_weak_pointer (pk_network_object, &pk_network_object);
	}
	return PK_NETWORK (pk_network_object);
}

