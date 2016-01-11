/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2014 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

#ifdef HAVE_NETWORKMANAGER
#include <NetworkManager.h>
#endif

#include <glib/gi18n.h>

#include "pk-cleanup.h"
#include "pk-network.h"

static void	 pk_network_finalize	(GObject	*object);

#define PK_NETWORK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_NETWORK, PkNetworkPrivate))

#define PK_NETWORK_UNIX_PROC_ROUTE	"/proc/net/route"

/**
 * _PkNetworkPrivate:
 *
 * Private #PkNetwork data
 **/
struct _PkNetworkPrivate
{
	GDBusConnection		*bus;
	GDBusProxy		*proxy_cm;
	GDBusProxy		*proxy_nm;
	GFileMonitor		*unix_monitor;
	PkNetworkEnum		 state_old;
	gboolean		 enabled_cm;
	gboolean		 enabled_nm;
	gboolean		 enabled_unix;
	guint			 watch_cm_id;
	guint			 watch_nm_id;
};

enum {
	PK_NETWORK_STATE_CHANGED,
	PK_NETWORK_LAST_SIGNAL
};

static guint signals [PK_NETWORK_LAST_SIGNAL] = { 0 };
static gpointer pk_network_object = NULL;

G_DEFINE_TYPE (PkNetwork, pk_network, G_TYPE_OBJECT)

/**
 * pk_network_unix_is_valid:
 **/
static gboolean
pk_network_unix_is_valid (const gchar *line)
{
	guint number_sections;
	_cleanup_strv_free_ gchar **sections = NULL;

	/* empty line */
	if (line == NULL || line[0] == '\0')
		return FALSE;

	/* tab delimited */
	sections = g_strsplit (line, "\t", 0);
	if (sections == NULL) {
		g_warning ("unable to split %s", PK_NETWORK_UNIX_PROC_ROUTE);
		return FALSE;
	}

	/* is header? */
	if (g_strcmp0 (sections[0], "Iface") == 0)
		return FALSE;

	/* is loopback? */
	if (g_strcmp0 (sections[0], "lo") == 0)
		return FALSE;

	/* is correct parameters? */
	number_sections = g_strv_length (sections);
	if (number_sections != 11) {
		g_warning ("invalid line '%s' (%i)", line, number_sections);
		return FALSE;
	}

	/* is destination zero (default route)? */
	if (g_strcmp0 (sections[1], "00000000") == 0) {
		g_debug ("destination %s is valid", sections[0]);
		return TRUE;
	}

	/* is gateway nonzero? */
	if (g_strcmp0 (sections[2], "00000000") != 0) {
		g_debug ("interface %s is valid", sections[0]);
		return TRUE;
	}
	return FALSE;
}

/**
 * pk_network_unix_get_state:
 **/
static PkNetworkEnum
pk_network_unix_get_state (PkNetwork *network)
{
	gboolean ret;
	guint i;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *contents = NULL;
	_cleanup_strv_free_ gchar **lines = NULL;

	/* no warning if the file is missing, like if no /proc */
	if (!g_file_test (PK_NETWORK_UNIX_PROC_ROUTE, G_FILE_TEST_EXISTS))
		return PK_NETWORK_ENUM_ONLINE;

	/* hack, because netlink is teh suck */
	ret = g_file_get_contents (PK_NETWORK_UNIX_PROC_ROUTE, &contents, NULL, &error);
	if (!ret) {
		g_warning ("could not open %s: %s", PK_NETWORK_UNIX_PROC_ROUTE, error->message);
		return PK_NETWORK_ENUM_ONLINE;
	}

	/* something insane */
	if (contents == NULL) {
		g_warning ("insane contents of %s", PK_NETWORK_UNIX_PROC_ROUTE);
		return PK_NETWORK_ENUM_ONLINE;
	}

	/* one line per interface */
	lines = g_strsplit (contents, "\n", 0);
	if (lines == NULL) {
		g_warning ("unable to split %s", PK_NETWORK_UNIX_PROC_ROUTE);
		return PK_NETWORK_ENUM_ONLINE;
	}

	/* is valid interface */
	for (i = 0; lines[i] != NULL; i++) {
		if (pk_network_unix_is_valid (lines[i]))
			return PK_NETWORK_ENUM_ONLINE;
	}
	return PK_NETWORK_ENUM_OFFLINE;
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
 * pk_network_unix_file_monitor_changed_cb:
 **/
static void
pk_network_unix_file_monitor_changed_cb (GFileMonitor *unix_monitor,
					 GFile *file,
					 GFile *other_file,
					 GFileMonitorEvent event_type,
					 PkNetwork *network)
{
	PkNetworkEnum state;

	g_return_if_fail (PK_IS_NETWORK (network));

	/* do not use */
	if (!network->priv->enabled_unix) {
		g_debug ("not enabled, so ignoring");
		return;
	}

	/* same state? */
	state = pk_network_unix_get_state (PK_NETWORK (network));
	if (state == network->priv->state_old) {
		g_debug ("same state");
		return;
	}

	/* new state */
	network->priv->state_old = state;
	g_debug ("emitting network-state-changed: %s", pk_network_enum_to_string (state));
	g_signal_emit_by_name (PK_NETWORK (network), "state-changed", state);
}

/**
 * pk_network_cm_get_state:
 **/
static PkNetworkEnum
pk_network_cm_get_state (PkNetwork *network)
{
	PkNetworkEnum type = PK_NETWORK_ENUM_ONLINE;
	const gchar *state;
	_cleanup_variant_unref_ GVariant *res = NULL;
	_cleanup_variant_unref_ GVariant *child = NULL;
	_cleanup_error_free_ GError *error = NULL;

	/* get services */
	res = g_dbus_proxy_call_sync (network->priv->proxy_cm,
				      "GetProperties",
				      NULL,
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      NULL,
				      &error);
	if (res == NULL) {
		g_warning ("Failed to get properties: %s", error->message);
		return PK_NETWORK_ENUM_UNKNOWN;
	}
	child = g_variant_get_child_value (res, 0);
	if (!g_variant_lookup (child, "State", "&s", &state)) {
		g_warning ("Failed to get State property");
		return PK_NETWORK_ENUM_UNKNOWN;
	}
	if (g_strcmp0 (state, "online") != 0)
		type = PK_NETWORK_ENUM_OFFLINE;
	return type;
}

/**
 * pk_network_cm_state_changed
 **/
static void
pk_network_cm_state_changed (PkNetwork *network, GVariant *parameters)
{
	PkNetworkEnum network_state;
	gchar *property = NULL;
	GVariant *value = NULL;

	g_return_if_fail (PK_IS_NETWORK (network));

	g_variant_get (parameters, "(&sv)", &property, &value);
	if (value != NULL && (g_strcmp0 (property, "State") == 0)) {
		const gchar *state = NULL;

		g_variant_get (value, "&s", &state);
		if (state == NULL)
			network_state = PK_NETWORK_ENUM_UNKNOWN;
		else if (g_str_equal (state, "online") == TRUE)
			network_state = PK_NETWORK_ENUM_ONLINE;
		else if (g_str_equal (state, "idle") == TRUE)
			network_state = PK_NETWORK_ENUM_OFFLINE;
		else if (g_str_equal (state, "offline") == TRUE)
			network_state = PK_NETWORK_ENUM_OFFLINE;
		else
			network_state = PK_NETWORK_ENUM_UNKNOWN;
		if (network_state != PK_NETWORK_ENUM_UNKNOWN) {
			g_debug ("emitting network-state-changed: %s",
				 pk_network_enum_to_string (network_state));
			g_signal_emit_by_name (PK_NETWORK (network),
					       "state-changed", network_state);
		}
	}
}

/**
 * pk_network_cm_dbus_signal_cb:
 **/
static void
pk_network_cm_dbus_signal_cb (GDBusProxy *proxy_cm,
			      gchar *sender_name,
			      gchar *signal_name,
			      GVariant *parameters,
			      PkNetwork *network)
{
	g_return_if_fail (PK_IS_NETWORK (network));

	/* do not use */
	if (!network->priv->enabled_cm) {
		g_debug ("not enabled, so ignoring %s", signal_name);
		return;
	}

	/* don't use parameters, just refresh state */
	if (g_strcmp0 (signal_name, "PropertyChanged") == 0)
		pk_network_cm_state_changed (network, parameters);
}

/**
 * pk_network_cm_appeared_cb:
 **/
static void
pk_network_cm_appeared_cb (GDBusConnection *connection,
			   const gchar *name,
			   const gchar *name_owner,
			   gpointer user_data)
{
	PkNetworkEnum network_state;
	PkNetwork *network = PK_NETWORK (user_data);
	network->priv->enabled_cm = TRUE;
	network_state = pk_network_cm_get_state (PK_NETWORK (user_data));
	g_signal_emit_by_name (PK_NETWORK (network),
			       "state-changed", network_state);
}

/**
 * pk_network_cm_vanished_cb:
 **/
static void
pk_network_cm_vanished_cb (GDBusConnection *connection,
			   const gchar *name,
			   gpointer user_data)
{
	PkNetwork *network = PK_NETWORK (user_data);
	network->priv->enabled_cm = FALSE;
}

#ifdef HAVE_NETWORKMANAGER
/**
 * pk_network_nm_prioritise_connection_type:
 *
 * GSM is more important than ethernet, so if we are using an
 * important connection even bridged we should prioritise it
 **/
static NMDeviceType
pk_network_nm_prioritise_connection_type (NMDeviceType type_old,
					  NMDeviceType type_new)
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
pk_network_nm_get_active_connection_type_for_device (PkNetwork *network,
						     const gchar *device)
{
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ GDBusProxy *proxy_nm = NULL;
	_cleanup_variant_unref_ GVariant *value = NULL;

	/* get if the device is default */
	proxy_nm = g_dbus_proxy_new_sync (network->priv->bus,
				       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
				       NULL,
				       "org.freedesktop.NetworkManager",
				       device,
				       "org.freedesktop.NetworkManager.Device",
				       NULL,
				       &error);
	if (proxy_nm == NULL) {
		g_warning ("Error getting DeviceType: %s", error->message);
		return NM_DEVICE_TYPE_UNKNOWN;
	}
	value = g_dbus_proxy_get_cached_property (proxy_nm, "DeviceType");
	if (value == NULL)
		return NM_DEVICE_TYPE_UNKNOWN;
	return g_variant_get_uint32 (value);
}

/**
 * pk_network_nm_get_active_connection_type_for_connection:
 **/
static NMDeviceType
pk_network_nm_get_active_connection_type_for_connection (PkNetwork *network,
							 const gchar *active_connection)
{
	const gchar *device;
	gboolean is_default;
	NMDeviceType type = NM_DEVICE_TYPE_UNKNOWN;
	NMDeviceType type_tmp;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ GDBusProxy *proxy_nm = NULL;
	_cleanup_variant_iter_free_ GVariantIter *iter = NULL;
	_cleanup_variant_unref_ GVariant *value_default = NULL;
	_cleanup_variant_unref_ GVariant *value_devices = NULL;

	/* get if the device is default */
	proxy_nm = g_dbus_proxy_new_sync (network->priv->bus,
				       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
				       NULL,
				       "org.freedesktop.NetworkManager",
				       active_connection,
				       "org.freedesktop.NetworkManager.Connection.Active",
				       NULL,
				       &error);
	if (proxy_nm == NULL) {
		g_warning ("Error getting Default: %s", error->message);
		return NM_DEVICE_TYPE_UNKNOWN;
	}
	value_default = g_dbus_proxy_get_cached_property (proxy_nm, "Default");
	if (value_default == NULL)
		return NM_DEVICE_TYPE_UNKNOWN;
	is_default = g_variant_get_boolean (value_default);
	g_debug ("is_default: %i", is_default);
	if (!is_default) {
		g_debug ("not default, skipping");
		return NM_DEVICE_TYPE_UNKNOWN;
	}

	/* get the physical devices for the connection */
	value_devices = g_dbus_proxy_get_cached_property (proxy_nm, "Devices");
	if (value_devices == NULL)
		return NM_DEVICE_TYPE_UNKNOWN;
	g_variant_get (value_devices, "ao", &iter);

	/* find the types of the active connection */
	while (g_variant_iter_next (iter, "&o", &device)) {
		type_tmp = pk_network_nm_get_active_connection_type_for_device (network,
										device);
		type = pk_network_nm_prioritise_connection_type (type, type_tmp);
	}
	return type;
}

/**
 * pk_network_nm_get_active_connection_type:
 **/
static NMDeviceType
pk_network_nm_get_active_connection_type (PkNetwork *network)
{
	const gchar *active_connection;
	NMDeviceType type = NM_DEVICE_TYPE_UNKNOWN;
	NMDeviceType type_tmp;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ GDBusProxy *proxy_nm = NULL;
	_cleanup_variant_iter_free_ GVariantIter *iter = NULL;
	_cleanup_variant_unref_ GVariant *value = NULL;

	/* get proxy_nm */
	proxy_nm = g_dbus_proxy_new_sync (network->priv->bus,
				       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
				       NULL,
				       "org.freedesktop.NetworkManager",
				       "/org/freedesktop/NetworkManager",
				       "org.freedesktop.NetworkManager",
				       NULL,
				       &error);
	if (proxy_nm == NULL) {
		g_warning ("Error getting ActiveConnections: %s", error->message);
		return NM_DEVICE_TYPE_UNKNOWN;
	}
	value = g_dbus_proxy_get_cached_property (proxy_nm, "ActiveConnections");
	if (value == NULL)
		return NM_DEVICE_TYPE_UNKNOWN;
	g_variant_get (value, "ao", &iter);

	/* find the active connection */
	while (g_variant_iter_next (iter, "&o", &active_connection)) {
		type_tmp = pk_network_nm_get_active_connection_type_for_connection (network,
										    active_connection);
		type = pk_network_nm_prioritise_connection_type (type, type_tmp);
	}
	return type;
}

/**
 * pk_network_nm_get_state:
 **/
static PkNetworkEnum
pk_network_nm_get_state (PkNetwork *network)
{
	PkNetworkEnum ret;
	NMDeviceType type;

	/* get connection type */
	type = pk_network_nm_get_active_connection_type (network);
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
 * pk_network_nm_appeared_cb:
 **/
static void
pk_network_nm_appeared_cb (GDBusConnection *connection,
				 const gchar *name,
				 const gchar *name_owner,
				 gpointer user_data)
{
	PkNetworkEnum network_state;
	PkNetwork *network = PK_NETWORK (user_data);
	network->priv->enabled_nm = TRUE;
	network_state = pk_network_nm_get_state (PK_NETWORK (user_data));
	g_signal_emit_by_name (PK_NETWORK (network),
			       "state-changed", network_state);
}

/**
 * pk_network_nm_vanished_cb:
 **/
static void
pk_network_nm_vanished_cb (GDBusConnection *connection,
			   const gchar *name,
			   gpointer user_data)
{
	PkNetwork *network = PK_NETWORK (user_data);
	network->priv->enabled_nm = FALSE;
}

/**
 * pk_network_nm_dbus_signal_cb:
 **/
static void
pk_network_nm_dbus_signal_cb (GDBusProxy *proxy_nm,
			      gchar *sender_name,
			      gchar *signal_name,
			      GVariant *parameters,
			      PkNetwork *network)
{
	PkNetworkEnum state;

	g_return_if_fail (PK_IS_NETWORK (network));

	/* do not use */
	if (!network->priv->enabled_nm) {
		g_debug ("not enabled, so ignoring %s", signal_name);
		return;
	}

	/* don't use parameters, just refresh state */
	if (g_strcmp0 (signal_name, "StateChanged") == 0) {
		state = pk_network_nm_get_state (PK_NETWORK (network));
		g_debug ("emitting network-state-changed: %s",
			 pk_network_enum_to_string (state));
		g_signal_emit_by_name (PK_NETWORK (network), "state-changed", state);
	}
}
#endif

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

	/* try each networking stack in order of preference */
#ifdef HAVE_NETWORKMANAGER
	if (network->priv->enabled_nm)
		return pk_network_nm_get_state (network);
#endif
	if (network->priv->enabled_cm)
		return pk_network_cm_get_state (network);
	if (network->priv->enabled_unix)
		return pk_network_unix_get_state (network);

	/* no valid data providers */
	return PK_NETWORK_ENUM_ONLINE;
}

/**
 * pk_network_init:
 * @network: This class instance
 **/
static void
pk_network_init (PkNetwork *network)
{
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ GFile *file = NULL;

	network->priv = PK_NETWORK_GET_PRIVATE (network);


	network->priv = PK_NETWORK_GET_PRIVATE (network);
	network->priv->state_old = PK_NETWORK_ENUM_UNKNOWN;

	/* get system connection */
	network->priv->bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (network->priv->bus == NULL) {
		g_warning ("Couldn't connect to system bus: %s", error->message);
		return;
	}

	/* check if NM is on the bus */
#ifdef HAVE_NETWORKMANAGER
	network->priv->watch_nm_id =
		g_bus_watch_name (G_BUS_TYPE_SYSTEM,
				  "org.freedesktop.NetworkManager",
				  G_BUS_NAME_WATCHER_FLAGS_NONE,
				  pk_network_nm_appeared_cb,
				  pk_network_nm_vanished_cb,
				  network,
				  NULL);

	/* connect to changed as libnm-glib is teh suc */
	network->priv->proxy_nm =
		g_dbus_proxy_new_sync (network->priv->bus,
				       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
				       G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
				       NULL,
				       "org.freedesktop.NetworkManager",
				       "/org/freedesktop/NetworkManager",
				       "org.freedesktop.NetworkManager",
				       NULL,
				       &error);
	if (network->priv->proxy_nm != NULL) {
		g_signal_connect (network->priv->proxy_nm,
				  "g-signal",
				  G_CALLBACK (pk_network_nm_dbus_signal_cb),
				  network);
	} else {
		g_warning ("Failed to connect to NetworkManager: %s", error->message);
		g_clear_error (&error);
	}
#endif

	/* check if ConnMan is on the bus */
	network->priv->watch_cm_id =
		g_bus_watch_name (G_BUS_TYPE_SYSTEM,
				  "net.connman",
				  G_BUS_NAME_WATCHER_FLAGS_NONE,
				  pk_network_cm_appeared_cb,
				  pk_network_cm_vanished_cb,
				  network,
				  NULL);
	network->priv->proxy_cm =
		g_dbus_proxy_new_sync (network->priv->bus,
				       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
				       NULL,
				       "net.connman",
				       "/",
				       "net.connman.Manager",
				       NULL,
				       &error);
	if (network->priv->proxy_cm != NULL) {
		g_signal_connect (network->priv->proxy_cm,
				  "g-signal",
				  G_CALLBACK (pk_network_cm_dbus_signal_cb),
				  network);
	} else {
		g_warning ("Cannot connect to connman: %s", error->message);
		g_clear_error (&error);
	}

	/* use a UNIX fallback, and monitor the route file for changes */
	network->priv->enabled_unix = TRUE;
	file = g_file_new_for_path (PK_NETWORK_UNIX_PROC_ROUTE);
	network->priv->unix_monitor = g_file_monitor_file (file,
							  G_FILE_MONITOR_NONE,
							  NULL,
							  &error);
	if (network->priv->unix_monitor == NULL) {
		g_warning ("Failed to set watch on %s: %s",
			   PK_NETWORK_UNIX_PROC_ROUTE, error->message);
	} else {
		g_signal_connect (network->priv->unix_monitor, "changed",
				  G_CALLBACK (pk_network_unix_file_monitor_changed_cb),
				  network);
	}
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

	if (network->priv->watch_cm_id != 0)
		g_bus_unwatch_name (network->priv->watch_cm_id);
	if (network->priv->watch_nm_id != 0)
		g_bus_unwatch_name (network->priv->watch_nm_id);
	if (network->priv->proxy_cm != NULL)
		g_object_unref (network->priv->proxy_cm);
	if (network->priv->proxy_nm != NULL)
		g_object_unref (network->priv->proxy_nm);
	if (network->priv->bus != NULL)
		g_object_unref (network->priv->bus);
	g_object_unref (network->priv->unix_monitor);

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
