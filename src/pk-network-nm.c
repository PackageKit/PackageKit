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

#include <sys/wait.h>
#include <fcntl.h>

#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <NetworkManager.h>
#include <libnm_glib.h>

#include "egg-debug.h"
#include "pk-network-nm.h"
#include "pk-marshal.h"

static void     pk_network_nm_class_init	(PkNetworkNmClass *klass);
static void     pk_network_nm_init		(PkNetworkNm      *network_nm);
static void     pk_network_nm_finalize		(GObject          *object);

#define PK_NETWORK_NM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_NETWORK_NM, PkNetworkNmPrivate))

/* experimental code */
#define PK_NETWORK_NM_GET_CONNECTION_TYPE	1

/**
 * PkNetworkNmPrivate:
 *
 * Private #PkNetworkNm data
 **/
struct _PkNetworkNmPrivate
{
	libnm_glib_ctx		*ctx;
	guint			 callback_id;
	DBusGConnection		*bus;
};

enum {
	PK_NETWORK_NM_STATE_CHANGED,
	PK_NETWORK_NM_LAST_SIGNAL
};

static guint signals [PK_NETWORK_NM_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkNetworkNm, pk_network_nm, G_TYPE_OBJECT)

/**
 * pk_network_nm_prioritise_connection_type:
 *
 * GSM is more important than ethernet, so if we are using an
 * important connection even bridged we should prioritise it
 **/
static NMDeviceType
pk_network_nm_prioritise_connection_type (NMDeviceType type_old, NMDeviceType type_new)
{
	NMDeviceType type = type_old;
	/* by sheer fluke we can use the enum ordering */
	if (type_new > type_old)
		type = type_new;
	return type;
}

/**
 * pk_network_nm_get_active_connection_type_for_device:
 **/
static NMDeviceType
pk_network_nm_get_active_connection_type_for_device (PkNetworkNm *network_nm, const gchar *device)
{
	gboolean ret;
	GError *error = NULL;
	DBusGProxy *proxy;
	GValue value = { 0 };
	NMDeviceType type = NM_DEVICE_TYPE_UNKNOWN;

	/* get if the device is default */
	proxy = dbus_g_proxy_new_for_name (network_nm->priv->bus, "org.freedesktop.NetworkManager",
					   device, "org.freedesktop.DBus.Properties");
	ret = dbus_g_proxy_call (proxy, "Get", &error,
				 G_TYPE_STRING, "org.freedesktop.NetworkManager.Device",
				 G_TYPE_STRING, "DeviceType",
				 G_TYPE_INVALID,
				 G_TYPE_VALUE, &value,
				 G_TYPE_INVALID);
	if (!ret) {
		g_warning ("Error getting DeviceType: %s", error->message);
		g_error_free (error);
		goto out;
	}
	type = g_value_get_uint (&value);
	egg_debug ("type: %i", type);
out:
	g_object_unref (proxy);
	return type;
}

/**
 * pk_network_nm_get_active_connection_type_for_connection:
 **/
static NMDeviceType
pk_network_nm_get_active_connection_type_for_connection (PkNetworkNm *network_nm, const gchar *active_connection)
{
	guint i;
	gboolean ret;
	GError *error = NULL;
	DBusGProxy *proxy;
	const gchar *device;
	GValue value_default = { 0 };
	GValue value_devices = { 0 };
	gboolean is_default;
	GPtrArray *devices;
	NMDeviceType type_tmp;
	NMDeviceType type = NM_DEVICE_TYPE_UNKNOWN;


	/* get if the device is default */
	proxy = dbus_g_proxy_new_for_name (network_nm->priv->bus, "org.freedesktop.NetworkManager",
					   active_connection, "org.freedesktop.DBus.Properties");
	ret = dbus_g_proxy_call (proxy, "Get", &error,
				 G_TYPE_STRING, "org.freedesktop.NetworkManager.Connection.Active",
				 G_TYPE_STRING, "Default",
				 G_TYPE_INVALID,
				 G_TYPE_VALUE, &value_default,
				 G_TYPE_INVALID);
	if (!ret) {
		g_warning ("Error getting Default: %s", error->message);
		g_error_free (error);
		goto out;
	}
	is_default = g_value_get_boolean (&value_default);
	egg_debug ("is_default: %i", is_default);
	if (!is_default) {
		egg_debug ("not default, skipping");
		goto out;
	}

	/* get the physical devices for the connection */
	ret = dbus_g_proxy_call (proxy, "Get", &error,
				 G_TYPE_STRING, "org.freedesktop.NetworkManager.Connection.Active",
				 G_TYPE_STRING, "Devices",
				 G_TYPE_INVALID,
				 G_TYPE_VALUE, &value_devices,
				 G_TYPE_INVALID);
	if (!ret) {
		g_warning ("Error getting Devices: %s", error->message);
		g_error_free (error);
		goto out;
	}

	devices = g_value_get_boxed (&value_devices);
	egg_debug ("number of devices: %i", devices->len);
	if (devices->len == 0)
		goto out;

	/* find the types of the active connection */
	for (i=0; i<devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		type_tmp = pk_network_nm_get_active_connection_type_for_device (network_nm, device);
		type = pk_network_nm_prioritise_connection_type (type, type_tmp);
	}

out:
	g_object_unref (proxy);
	return type;
}

/**
 * pk_network_nm_get_active_connection_type:
 **/
static NMDeviceType
pk_network_nm_get_active_connection_type (PkNetworkNm *network_nm)
{
	guint i;
	gboolean ret;
	DBusGProxy *proxy;
	GError *error = NULL;
	GPtrArray *active_connections = NULL;
	const gchar *active_connection;
	GValue value = { 0 };
	NMDeviceType type_tmp;
	NMDeviceType type = NM_DEVICE_TYPE_UNKNOWN;

	/* get proxy */
	proxy = dbus_g_proxy_new_for_name (network_nm->priv->bus, "org.freedesktop.NetworkManager",
					   "/org/freedesktop/NetworkManager",
					   "org.freedesktop.DBus.Properties");
	ret = dbus_g_proxy_call (proxy, "Get", &error,
				 G_TYPE_STRING, "org.freedesktop.NetworkManager",
				 G_TYPE_STRING, "ActiveConnections",
				 G_TYPE_INVALID,
				 G_TYPE_VALUE, &value,
				 G_TYPE_INVALID);
	if (!ret) {
		g_warning ("Error getting ActiveConnections: %s", error->message);
		g_error_free (error);
		goto out;
	}

	active_connections = g_value_get_boxed (&value);
	egg_debug ("active connections: %i", active_connections->len);
	if (active_connections->len == 0)
		goto out;

	/* find the active connection */
	for (i=0; i<active_connections->len; i++) {
		active_connection = g_ptr_array_index (active_connections, i);
		type_tmp = pk_network_nm_get_active_connection_type_for_connection (network_nm, active_connection);
		type = pk_network_nm_prioritise_connection_type (type, type_tmp);
	}

out:
	g_object_unref (proxy);
	g_ptr_array_foreach (active_connections, (GFunc) g_free, NULL);
	g_ptr_array_free (active_connections, TRUE);
	return type;
}

/**
 * pk_network_nm_get_network_state:
 * @network_nm: a valid #PkNetworkNm instance
 *
 * Return value: %TRUE if the network_nm is online
 **/
PkNetworkEnum
pk_network_nm_get_network_state (PkNetworkNm *network_nm)
{
	PkNetworkEnum ret;
#ifdef PK_NETWORK_NM_GET_CONNECTION_TYPE
	NMDeviceType type;
#else
	libnm_glib_state state;
#endif

	g_return_val_if_fail (PK_IS_NETWORK_NM (network_nm), PK_NETWORK_ENUM_UNKNOWN);

#ifdef PK_NETWORK_NM_GET_CONNECTION_TYPE
	/* get connection type */
	type = pk_network_nm_get_active_connection_type (network_nm);
	switch (type) {
	case NM_DEVICE_TYPE_UNKNOWN:
		ret = PK_NETWORK_ENUM_OFFLINE;
		break;
	case NM_DEVICE_TYPE_ETHERNET:
		ret = PK_NETWORK_ENUM_FAST;
		break;
	case NM_DEVICE_TYPE_WIFI:
		ret = PK_NETWORK_ENUM_ONLINE;
		break;
	case NM_DEVICE_TYPE_GSM:
	case NM_DEVICE_TYPE_CDMA:
		ret = PK_NETWORK_ENUM_SLOW;
		break;
	default:
		ret = PK_NETWORK_ENUM_ONLINE;
	}
#else
	state = libnm_glib_get_network_state (network_nm->priv->ctx);
	switch (state) {
	case LIBNM_NO_NETWORK_CONNECTION:
		ret = PK_NETWORK_ENUM_OFFLINE;
		break;
	default:
		ret = PK_NETWORK_ENUM_ONLINE;
	}
#endif
	egg_debug ("network state is %s", pk_network_enum_to_text (ret));
	return ret;
}

/**
 * pk_network_nm_nm_changed_cb:
 **/
static void
pk_network_nm_nm_changed_cb (libnm_glib_ctx *libnm_ctx, gpointer data)
{
	gboolean ret;
	PkNetworkNm *network_nm = (PkNetworkNm *) data;

	g_return_if_fail (PK_IS_NETWORK_NM (network_nm));

	ret = pk_network_nm_get_network_state (network_nm);
	g_signal_emit (network_nm, signals [PK_NETWORK_NM_STATE_CHANGED], 0, ret);
}

/**
 * pk_network_nm_class_init:
 * @klass: The PkNetworkNmClass
 **/
static void
pk_network_nm_class_init (PkNetworkNmClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_network_nm_finalize;
	signals [PK_NETWORK_NM_STATE_CHANGED] =
		g_signal_new ("state-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	g_type_class_add_private (klass, sizeof (PkNetworkNmPrivate));
}

/**
 * pk_network_nm_init:
 * @network_nm: This class instance
 **/
static void
pk_network_nm_init (PkNetworkNm *network_nm)
{
	GError *error = NULL;
	GMainContext *context;

	network_nm->priv = PK_NETWORK_NM_GET_PRIVATE (network_nm);
	context = g_main_context_default ();
	network_nm->priv->ctx = libnm_glib_init ();
	network_nm->priv->callback_id =
		libnm_glib_register_callback (network_nm->priv->ctx,
					      pk_network_nm_nm_changed_cb,
					      network_nm, context);

	/* get system connection */
	network_nm->priv->bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (network_nm->priv->bus == NULL) {
		egg_warning ("Couldn't connect to system bus: %s", error->message);
		g_error_free (error);
	}
}

/**
 * pk_network_nm_finalize:
 * @object: The object to finalize
 **/
static void
pk_network_nm_finalize (GObject *object)
{
	PkNetworkNm *network_nm;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_NETWORK_NM (object));
	network_nm = PK_NETWORK_NM (object);

	g_return_if_fail (network_nm->priv != NULL);

	libnm_glib_unregister_callback (network_nm->priv->ctx, network_nm->priv->callback_id);
	libnm_glib_shutdown (network_nm->priv->ctx);

	/* be paranoid */
	network_nm->priv->ctx = NULL;
	network_nm->priv->callback_id = 0;

	G_OBJECT_CLASS (pk_network_nm_parent_class)->finalize (object);
}

/**
 * pk_network_nm_new:
 *
 * Return value: a new PkNetworkNm object.
 **/
PkNetworkNm *
pk_network_nm_new (void)
{
	PkNetworkNm *network_nm;
	network_nm = g_object_new (PK_TYPE_NETWORK_NM, NULL);
	return PK_NETWORK_NM (network_nm);
}

