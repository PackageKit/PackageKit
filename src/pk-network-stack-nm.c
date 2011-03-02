/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <glib.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <NetworkManager.h>

#include "egg-dbus-monitor.h"

#include "pk-network-stack-nm.h"
#include "pk-conf.h"
#include "pk-marshal.h"

#ifndef NM_CHECK_VERSION
#define NM_CHECK_VERSION(x,y,z) 0
#endif

struct PkNetworkStackNmPrivate
{
	EggDbusMonitor		*dbus_monitor;
	PkConf			*conf;
	DBusGConnection		*bus;
	gboolean		 is_enabled;
	DBusGProxy		*proxy_changed;
};

G_DEFINE_TYPE (PkNetworkStackNm, pk_network_stack_nm, PK_TYPE_NETWORK_STACK)
#define PK_NETWORK_STACK_NM_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_NETWORK_STACK_NM, PkNetworkStackNmPrivate))

/**
 * pk_network_stack_nm_prioritise_connection_type:
 *
 * GSM is more important than ethernet, so if we are using an
 * important connection even bridged we should prioritise it
 **/
static NMDeviceType
pk_network_stack_nm_prioritise_connection_type (NMDeviceType type_old, NMDeviceType type_new)
{
	NMDeviceType type = type_old;
	/* by sheer fluke we can use the enum ordering */
	if (type_new > type_old)
		type = type_new;
	return type;
}

/**
 * pk_network_stack_nm_get_active_connection_type_for_device:
 **/
static NMDeviceType
pk_network_stack_nm_get_active_connection_type_for_device (PkNetworkStackNm *nstack_nm, const gchar *device)
{
	gboolean ret;
	GError *error = NULL;
	DBusGProxy *proxy;
	GValue value = { 0 };
	NMDeviceType type = NM_DEVICE_TYPE_UNKNOWN;

	/* get if the device is default */
	proxy = dbus_g_proxy_new_for_name (nstack_nm->priv->bus, "org.freedesktop.NetworkManager",
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
	g_debug ("type: %i", type);
out:
	g_object_unref (proxy);
	return type;
}

/**
 * pk_network_stack_nm_get_active_connection_type_for_connection:
 **/
static NMDeviceType
pk_network_stack_nm_get_active_connection_type_for_connection (PkNetworkStackNm *nstack_nm, const gchar *active_connection)
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
	proxy = dbus_g_proxy_new_for_name (nstack_nm->priv->bus, "org.freedesktop.NetworkManager",
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
	g_debug ("is_default: %i", is_default);
	if (!is_default) {
		g_debug ("not default, skipping");
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
	g_debug ("number of devices: %i", devices->len);
	if (devices->len == 0)
		goto out;

	/* find the types of the active connection */
	for (i=0; i<devices->len; i++) {
		device = g_ptr_array_index (devices, i);
		type_tmp = pk_network_stack_nm_get_active_connection_type_for_device (nstack_nm, device);
		type = pk_network_stack_nm_prioritise_connection_type (type, type_tmp);
	}

out:
	g_object_unref (proxy);
	return type;
}

/**
 * pk_network_stack_nm_get_active_connection_type:
 **/
static NMDeviceType
pk_network_stack_nm_get_active_connection_type (PkNetworkStackNm *nstack_nm)
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
	proxy = dbus_g_proxy_new_for_name (nstack_nm->priv->bus, "org.freedesktop.NetworkManager",
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
	g_debug ("active connections: %i", active_connections->len);
	if (active_connections->len == 0)
		goto out;

	/* find the active connection */
	for (i=0; i<active_connections->len; i++) {
		active_connection = g_ptr_array_index (active_connections, i);
		type_tmp = pk_network_stack_nm_get_active_connection_type_for_connection (nstack_nm, active_connection);
		type = pk_network_stack_nm_prioritise_connection_type (type, type_tmp);
	}

out:
	g_object_unref (proxy);
	g_ptr_array_foreach (active_connections, (GFunc) g_free, NULL);
	g_ptr_array_free (active_connections, TRUE);
	return type;
}

/**
 * pk_network_stack_nm_get_state:
 **/
static PkNetworkEnum
pk_network_stack_nm_get_state (PkNetworkStack *nstack)
{
	PkNetworkEnum ret;
	NMDeviceType type;

	PkNetworkStackNm *nstack_nm = PK_NETWORK_STACK_NM (nstack);

	/* get connection type */
	type = pk_network_stack_nm_get_active_connection_type (nstack_nm);
	switch (type) {
	case NM_DEVICE_TYPE_UNKNOWN:
		ret = PK_NETWORK_ENUM_OFFLINE;
		break;
	case NM_DEVICE_TYPE_ETHERNET:
		ret = PK_NETWORK_ENUM_WIRED;
		break;
	case NM_DEVICE_TYPE_WIFI:
		ret = PK_NETWORK_ENUM_WIFI;
		break;
#if NM_CHECK_VERSION(0,8,992)
	case NM_DEVICE_TYPE_WIMAX:
	case NM_DEVICE_TYPE_MODEM:
#else
	case NM_DEVICE_TYPE_GSM:
	case NM_DEVICE_TYPE_CDMA:
#endif
	case NM_DEVICE_TYPE_BT:
		ret = PK_NETWORK_ENUM_MOBILE;
		break;
	default:
		ret = PK_NETWORK_ENUM_ONLINE;
	}

	g_debug ("network state is %s", pk_network_enum_to_string (ret));
	return ret;
}

/**
 * pk_network_stack_nm_status_changed_cb:
 */
static void
pk_network_stack_nm_status_changed_cb (DBusGProxy *proxy, guint status, PkNetworkStackNm *nstack_nm)
{
	PkNetworkEnum state;

	g_return_if_fail (PK_IS_NETWORK_STACK_NM (nstack_nm));

	/* do not use */
	if (!nstack_nm->priv->is_enabled) {
		g_debug ("not enabled, so ignoring");
		return;
	}

	state = pk_network_stack_nm_get_state (PK_NETWORK_STACK (nstack_nm));
	g_debug ("emitting network-state-changed: %s", pk_network_enum_to_string (state));
	g_signal_emit_by_name (PK_NETWORK_STACK (nstack_nm), "state-changed", state);
}

/**
 * pk_network_stack_nm_is_enabled:
 *
 * Return %TRUE on success, %FALSE if we failed to is_enabled or no data
 **/
static gboolean
pk_network_stack_nm_is_enabled (PkNetworkStack *nstack)
{
	PkNetworkStackNm *nstack_nm = PK_NETWORK_STACK_NM (nstack);
	return nstack_nm->priv->is_enabled;
}

/**
 * pk_network_stack_nm_init:
 **/
static void
pk_network_stack_nm_init (PkNetworkStackNm *nstack_nm)
{
	GError *error = NULL;
	gboolean service_alive;

	nstack_nm->priv = PK_NETWORK_STACK_NM_GET_PRIVATE (nstack_nm);
	nstack_nm->priv->conf = pk_conf_new ();

	/* do we use this code? */
	nstack_nm->priv->is_enabled = pk_conf_get_bool (nstack_nm->priv->conf, "UseNetworkManager");

	/* get system connection */
	nstack_nm->priv->bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (nstack_nm->priv->bus == NULL) {
		g_warning ("Couldn't connect to system bus: %s", error->message);
		g_error_free (error);
	}

	/* check if NM is on the bus */
	nstack_nm->priv->dbus_monitor = egg_dbus_monitor_new ();
	egg_dbus_monitor_assign (nstack_nm->priv->dbus_monitor, EGG_DBUS_MONITOR_SYSTEM, "org.freedesktop.NetworkManager");
	service_alive = egg_dbus_monitor_is_connected (nstack_nm->priv->dbus_monitor);

	/* connect to changed as libnm-glib is teh suck and causes multithreading issues with dbus-glib */
	nstack_nm->priv->proxy_changed = dbus_g_proxy_new_for_name (nstack_nm->priv->bus,
								    "org.freedesktop.NetworkManager",
								    "/org/freedesktop/NetworkManager",
								    "org.freedesktop.NetworkManager");
	dbus_g_proxy_add_signal (nstack_nm->priv->proxy_changed, "StateChanged", G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (nstack_nm->priv->proxy_changed, "StateChanged",
				     G_CALLBACK (pk_network_stack_nm_status_changed_cb), nstack_nm, NULL);

	/* NetworkManager isn't up, so we can't use it */
	if (nstack_nm->priv->is_enabled && !service_alive) {
		g_warning ("UseNetworkManager true, but org.freedesktop.NetworkManager not up");
		nstack_nm->priv->is_enabled = FALSE;
	}
}

/**
 * pk_network_stack_nm_finalize:
 **/
static void
pk_network_stack_nm_finalize (GObject *object)
{
	PkNetworkStackNm *nstack_nm;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_NETWORK_STACK_NM (object));

	nstack_nm = PK_NETWORK_STACK_NM (object);
	g_return_if_fail (nstack_nm->priv != NULL);

	dbus_g_proxy_disconnect_signal (nstack_nm->priv->proxy_changed, "StateChanged",
					G_CALLBACK (pk_network_stack_nm_status_changed_cb), nstack_nm);
	g_object_unref (nstack_nm->priv->proxy_changed);
	g_object_unref (nstack_nm->priv->conf);
	g_object_unref (nstack_nm->priv->dbus_monitor);

	G_OBJECT_CLASS (pk_network_stack_nm_parent_class)->finalize (object);
}

/**
 * pk_network_stack_nm_class_init:
 **/
static void
pk_network_stack_nm_class_init (PkNetworkStackNmClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	PkNetworkStackClass *nstack_class = PK_NETWORK_STACK_CLASS (klass);

	object_class->finalize = pk_network_stack_nm_finalize;
	nstack_class->get_state = pk_network_stack_nm_get_state;
	nstack_class->is_enabled = pk_network_stack_nm_is_enabled;

	g_type_class_add_private (klass, sizeof (PkNetworkStackNmPrivate));
}

/**
 * pk_network_stack_nm_new:
 **/
PkNetworkStackNm *
pk_network_stack_nm_new (void)
{
	return g_object_new (PK_TYPE_NETWORK_STACK_NM, NULL);
}

