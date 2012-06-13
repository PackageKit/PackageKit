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
#include <gio/gio.h>
#include <NetworkManager.h>

#include "pk-network-stack-nm.h"
#include "pk-conf.h"
#include "pk-marshal.h"

#ifndef NM_CHECK_VERSION
#define NM_CHECK_VERSION(x,y,z) 0
#endif

struct PkNetworkStackNmPrivate
{
	guint			 watch_id;
	PkConf			*conf;
	GDBusConnection		*bus;
	gboolean		 is_enabled;
	GDBusProxy		*proxy_changed;
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
pk_network_stack_nm_prioritise_connection_type (NMDeviceType type_old,
						NMDeviceType type_new)
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
pk_network_stack_nm_get_active_connection_type_for_device (PkNetworkStackNm *nstack_nm,
							   const gchar *device)
{
	GDBusProxy *proxy;
	GError *error = NULL;
	GVariant *value = NULL;
	NMDeviceType type = NM_DEVICE_TYPE_UNKNOWN;

	/* get if the device is default */
	proxy = g_dbus_proxy_new_sync (nstack_nm->priv->bus,
				       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
				       NULL,
				       "org.freedesktop.NetworkManager",
				       device,
				       "org.freedesktop.NetworkManager.Device",
				       NULL,
				       &error);
	if (proxy == NULL) {
		g_warning ("Error getting DeviceType: %s", error->message);
		g_error_free (error);
		goto out;
	}
	value = g_dbus_proxy_get_cached_property (proxy, "DeviceType");
	if (value == NULL)
		goto out;
	type = g_variant_get_uint32 (value);
	g_debug ("type: %i", type);
out:
	if (proxy != NULL)
		g_object_unref (proxy);
	if (value != NULL)
		g_variant_unref (value);
	return type;
}

/**
 * pk_network_stack_nm_get_active_connection_type_for_connection:
 **/
static NMDeviceType
pk_network_stack_nm_get_active_connection_type_for_connection (PkNetworkStackNm *nstack_nm,
							       const gchar *active_connection)
{
	const gchar *device;
	gboolean is_default;
	GDBusProxy *proxy;
	GError *error = NULL;
	GVariantIter *iter = NULL;
	GVariant *value_default = NULL;
	GVariant *value_devices = NULL;
	NMDeviceType type = NM_DEVICE_TYPE_UNKNOWN;
	NMDeviceType type_tmp;

	/* get if the device is default */
	proxy = g_dbus_proxy_new_sync (nstack_nm->priv->bus,
				       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
				       NULL,
				       "org.freedesktop.NetworkManager",
				       active_connection,
				       "org.freedesktop.NetworkManager.Connection.Active",
				       NULL,
				       &error);
	if (proxy == NULL) {
		g_warning ("Error getting Default: %s", error->message);
		g_error_free (error);
		goto out;
	}
	value_default = g_dbus_proxy_get_cached_property (proxy, "Default");
	if (value_default == NULL)
		goto out;
	is_default = g_variant_get_boolean (value_default);
	g_debug ("is_default: %i", is_default);
	if (!is_default) {
		g_debug ("not default, skipping");
		goto out;
	}

	/* get the physical devices for the connection */
	value_devices = g_dbus_proxy_get_cached_property (proxy, "Devices");
	if (value_devices == NULL)
		goto out;
	g_variant_get (value_devices, "ao", &iter);

	/* find the types of the active connection */
	while (g_variant_iter_next (iter, "&o", &device)) {
		type_tmp = pk_network_stack_nm_get_active_connection_type_for_device (nstack_nm,
										      device);
		type = pk_network_stack_nm_prioritise_connection_type (type,
								       type_tmp);
	}
out:
	if (iter != NULL)
		g_variant_iter_free (iter);
	if (proxy != NULL)
		g_object_unref (proxy);
	if (value_devices != NULL)
		g_variant_unref (value_devices);
	if (value_default != NULL)
		g_variant_unref (value_default);
	return type;
}

/**
 * pk_network_stack_nm_get_active_connection_type:
 **/
static NMDeviceType
pk_network_stack_nm_get_active_connection_type (PkNetworkStackNm *nstack_nm)
{
	const gchar *active_connection;
	GDBusProxy *proxy;
	GError *error = NULL;
	GVariantIter *iter = NULL;
	GVariant *value = NULL;
	NMDeviceType type = NM_DEVICE_TYPE_UNKNOWN;
	NMDeviceType type_tmp;

	/* get proxy */
	proxy = g_dbus_proxy_new_sync (nstack_nm->priv->bus,
				       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
				       NULL,
				       "org.freedesktop.NetworkManager",
				       "/org/freedesktop/NetworkManager",
				       "org.freedesktop.NetworkManager",
				       NULL,
				       &error);
	if (proxy == NULL) {
		g_warning ("Error getting ActiveConnections: %s", error->message);
		g_error_free (error);
		goto out;
	}
	value = g_dbus_proxy_get_cached_property (proxy, "ActiveConnections");
	if (value == NULL)
		goto out;
	g_variant_get (value, "ao", &iter);

	/* find the active connection */
	while (g_variant_iter_next (iter, "&o", &active_connection)) {
		type_tmp = pk_network_stack_nm_get_active_connection_type_for_connection (nstack_nm,
											  active_connection);
		type = pk_network_stack_nm_prioritise_connection_type (type, type_tmp);
	}

out:
	if (iter != NULL)
		g_variant_iter_free (iter);
	if (proxy != NULL)
		g_object_unref (proxy);
	if (value != NULL)
		g_variant_unref (value);
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
 * pk_network_stack_nm_appeared_cb:
 **/
static void
pk_network_stack_nm_appeared_cb (GDBusConnection *connection,
				 const gchar *name,
				 const gchar *name_owner,
				 gpointer user_data)
{
	gboolean ret;
	PkNetworkStackNm *nstack_nm = PK_NETWORK_STACK_NM (user_data);
	ret = pk_conf_get_bool (nstack_nm->priv->conf,
				"UseNetworkManager");
	nstack_nm->priv->is_enabled = ret;
}

/**
 * pk_network_stack_nm_vanished_cb:
 **/
static void
pk_network_stack_nm_vanished_cb (GDBusConnection *connection,
				 const gchar *name,
				 gpointer user_data)
{
	PkNetworkStackNm *nstack_nm = PK_NETWORK_STACK_NM (user_data);
	nstack_nm->priv->is_enabled = FALSE;
}

/**
 * pk_network_stack_nm_dbus_signal_cb:
 **/
static void
pk_network_stack_nm_dbus_signal_cb (GDBusProxy *proxy,
				    gchar *sender_name,
				    gchar *signal_name,
				    GVariant *parameters,
				    PkNetworkStackNm *nstack_nm)
{
	PkNetworkEnum state;

	g_return_if_fail (PK_IS_NETWORK_STACK_NM (nstack_nm));

	/* do not use */
	if (!nstack_nm->priv->is_enabled) {
		g_debug ("not enabled, so ignoring %s", signal_name);
		return;
	}

	/* don't use parameters, just refresh state */
	if (g_strcmp0 (signal_name, "StateChanged") == 0) {
		state = pk_network_stack_nm_get_state (PK_NETWORK_STACK (nstack_nm));
		g_debug ("emitting network-state-changed: %s", pk_network_enum_to_string (state));
		g_signal_emit_by_name (PK_NETWORK_STACK (nstack_nm), "state-changed", state);
	}
}

/**
 * pk_network_stack_nm_init:
 **/
static void
pk_network_stack_nm_init (PkNetworkStackNm *nstack_nm)
{
	GError *error = NULL;

	nstack_nm->priv = PK_NETWORK_STACK_NM_GET_PRIVATE (nstack_nm);
	nstack_nm->priv->conf = pk_conf_new ();

	/* get system connection */
	nstack_nm->priv->bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (nstack_nm->priv->bus == NULL) {
		g_warning ("Couldn't connect to system bus: %s", error->message);
		g_error_free (error);
	}

	/* check if NM is on the bus */
	nstack_nm->priv->watch_id =
		g_bus_watch_name (G_BUS_TYPE_SYSTEM,
				  "org.freedesktop.NetworkManager",
				  G_BUS_NAME_WATCHER_FLAGS_NONE,
				  pk_network_stack_nm_appeared_cb,
				  pk_network_stack_nm_vanished_cb,
				  nstack_nm,
				  NULL);

	/* connect to changed as libnm-glib is teh suc */
	nstack_nm->priv->proxy_changed =
		g_dbus_proxy_new_sync (nstack_nm->priv->bus,
				       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
				       NULL,
				       "org.freedesktop.NetworkManager",
				       "/org/freedesktop/NetworkManager",
				       "org.freedesktop.NetworkManager",
				       NULL,
				       &error);
	g_signal_connect (nstack_nm->priv->proxy_changed,
			  "g-signal",
			  G_CALLBACK (pk_network_stack_nm_dbus_signal_cb),
			  nstack_nm);
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

	g_object_unref (nstack_nm->priv->proxy_changed);
	g_object_unref (nstack_nm->priv->conf);
	g_bus_unwatch_name (nstack_nm->priv->watch_id);

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

