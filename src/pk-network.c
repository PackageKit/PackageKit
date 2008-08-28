/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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
 * SECTION:pk-network
 * @short_description: network detection code
 *
 * This file contains a network checker.
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

#include "egg-debug.h"
#include "egg-dbus-monitor.h"
#include "pk-network.h"
#include "pk-network-nm.h"
#include "pk-network-unix.h"
#include "pk-marshal.h"
#include "pk-conf.h"

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
	gboolean		 use_nm;
	gboolean		 use_unix;
	PkNetworkNm		*net_nm;
	PkNetworkUnix		*net_unix;
	PkConf			*conf;
	EggDbusMonitor		*nm_bus;
};

enum {
	PK_NETWORK_STATE_CHANGED,
	PK_NETWORK_LAST_SIGNAL
};

static guint signals [PK_NETWORK_LAST_SIGNAL] = { 0 };
static gpointer pk_network_object = NULL;

G_DEFINE_TYPE (PkNetwork, pk_network, G_TYPE_OBJECT)

/**
 * pk_network_get_network_state:
 * @network: a valid #PkNetwork instance
 *
 * Return value: %TRUE if the network is online
 * Note: This is a dummy file and no checks are done
 **/
PkNetworkEnum
pk_network_get_network_state (PkNetwork *network)
{
	g_return_val_if_fail (PK_IS_NETWORK (network), PK_NETWORK_ENUM_UNKNOWN);
	/* use the correct backend */
	if (network->priv->use_nm) {
		return pk_network_nm_get_network_state (network->priv->net_nm);
	}
	if (network->priv->use_unix) {
		return pk_network_unix_get_network_state (network->priv->net_unix);
	}
	return PK_NETWORK_ENUM_ONLINE;
}

/**
 * pk_network_nm_network_changed_cb:
 **/
static void
pk_network_nm_network_changed_cb (PkNetworkNm *net_nm, gboolean online, PkNetwork *network)
{
	PkNetworkEnum ret;
	g_return_if_fail (PK_IS_NETWORK (network));
	if (network->priv->use_nm) {
		if (online) {
			ret = PK_NETWORK_ENUM_ONLINE;
		} else {
			ret = PK_NETWORK_ENUM_OFFLINE;
		}
		g_signal_emit (network, signals [PK_NETWORK_STATE_CHANGED], 0, ret);
	}
}

/**
 * pk_network_unix_network_changed_cb:
 **/
static void
pk_network_unix_network_changed_cb (PkNetworkUnix *net_unix, gboolean online, PkNetwork *network)
{
	g_return_if_fail (PK_IS_NETWORK (network));
	if (network->priv->use_unix) {
		g_signal_emit (network, signals [PK_NETWORK_STATE_CHANGED], 0, online);
	}
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
	signals [PK_NETWORK_STATE_CHANGED] =
		g_signal_new ("state-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	g_type_class_add_private (klass, sizeof (PkNetworkPrivate));
}

/**
 * pk_network_init:
 * @network: This class instance
 **/
static void
pk_network_init (PkNetwork *network)
{
	gboolean nm_alive;
	network->priv = PK_NETWORK_GET_PRIVATE (network);
	network->priv->conf = pk_conf_new ();
	network->priv->net_nm = pk_network_nm_new ();
	g_signal_connect (network->priv->net_nm, "state-changed",
			  G_CALLBACK (pk_network_nm_network_changed_cb), network);
	network->priv->net_unix = pk_network_unix_new ();
	g_signal_connect (network->priv->net_unix, "state-changed",
			  G_CALLBACK (pk_network_unix_network_changed_cb), network);

	/* get the defaults from the config file */
	network->priv->use_nm = pk_conf_get_bool (network->priv->conf, "UseNetworkManager");
	network->priv->use_unix = pk_conf_get_bool (network->priv->conf, "UseNetworkHeuristic");

	/* check if NM is on the bus */
	network->priv->nm_bus = egg_dbus_monitor_new ();
	egg_dbus_monitor_assign (network->priv->nm_bus, EGG_DBUS_MONITOR_SYSTEM, "org.freedesktop.NetworkManager");
	nm_alive = egg_dbus_monitor_is_connected (network->priv->nm_bus);

	/* NetworkManager isn't up, so we can't use it */
	if (network->priv->use_nm && !nm_alive) {
		egg_warning ("UseNetworkManager true, but org.freedesktop.NetworkManager not up");
		network->priv->use_nm = FALSE;
	}

#if !PK_BUILD_NETWORKMANAGER
	/* check we can actually use the default */
	if (network->priv->use_nm) {
		egg_warning ("UseNetworkManager true, but not built with NM support");
		network->priv->use_nm = FALSE;
	}
#endif
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
	g_object_unref (network->priv->conf);
	g_object_unref (network->priv->nm_bus);
	g_object_unref (network->priv->net_nm);
	g_object_unref (network->priv->net_unix);
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

