/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009  Intel Corporation. All rights reserved.
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#include "egg-debug.h"
#include "egg-dbus-monitor.h"

#include "pk-network-stack-connman.h"
#include "pk-conf.h"
#include "pk-marshal.h"

struct PkNetworkStackConnmanPrivate
{
	EggDbusMonitor		*dbus_monitor;
	PkConf			*conf;
	gboolean		 is_enabled;
	DBusGConnection		*bus;
	DBusGProxy		*proxy;
};

G_DEFINE_TYPE (PkNetworkStackConnman, pk_network_stack_connman, PK_TYPE_NETWORK_STACK)
#define PK_NETWORK_STACK_CONNMAN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_NETWORK_STACK_CONNMAN, PkNetworkStackConnmanPrivate))

#define CONNMAN_DBUS_NAME			"org.moblin.connman"
#define CONNMAN_MANAGER_DBUS_INTERFACE		CONNMAN_DBUS_NAME ".Manager"
#define CONNMAN_SERVICE_DBUS_INTERFACE		CONNMAN_DBUS_NAME ".Service"
#define CONNMAN_MANAGER_DBUS_PATH		"/"

/**
 * pk_network_stack_connman_iterate_list:
 **/
static void
pk_network_stack_connman_iterate_list (const GValue *value, gpointer user_data)
{
	GSList **list = user_data;
	gchar *path = g_value_dup_boxed (value);

	if (path == NULL)
		return;

	*list = g_slist_append (*list, path);
}

/**
 * pk_network_stack_connman_get_connection_type:
 **/
static PkNetworkEnum
pk_network_stack_connman_get_connection_type (const GValue *value)
{
	const char *type = value ? g_value_get_string(value) : NULL;

	if (type == NULL)
		return PK_NETWORK_ENUM_UNKNOWN;
	else if (g_str_equal (type, "ethernet"))
		return PK_NETWORK_ENUM_WIRED;
	else if (g_str_equal (type, "wifi"))
		return PK_NETWORK_ENUM_WIFI;
	else if (g_str_equal (type, "wimax"))
		return PK_NETWORK_ENUM_MOBILE;

	return PK_NETWORK_ENUM_UNKNOWN;
}

/**
 * pk_network_stack_connman_get_state:
 **/
static PkNetworkEnum
pk_network_stack_connman_get_state (PkNetworkStack *nstack)
{
	DBusGProxy *proxy, *proxy_service;
	GError *error = NULL;
	GHashTable *hash_manager = NULL;
	GHashTable *hash_service = NULL;
	GValue *value;
	GSList *list;
	GSList *services_list = NULL;
	gchar *state;
	PkNetworkEnum type;
	PkNetworkStackConnman *nstack_connman = PK_NETWORK_STACK_CONNMAN (nstack);
	DBusGConnection *connection = nstack_connman->priv->bus;

	proxy = nstack_connman->priv->proxy;
	proxy_service = NULL;

	/* get services */
	dbus_g_proxy_call (proxy, "GetProperties", &error,
			   G_TYPE_INVALID,
			   dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), &hash_manager,
			   G_TYPE_INVALID);
	if (error != NULL || hash_manager == NULL) {
		if (error)
			g_clear_error (&error);
		return PK_NETWORK_ENUM_UNKNOWN;
	}

	value = g_hash_table_lookup (hash_manager, "State");
	state = value ? g_value_dup_string (value) : NULL;

	if (g_str_equal (state, "online") == FALSE)
		return PK_NETWORK_ENUM_OFFLINE;

	value = g_hash_table_lookup (hash_manager, "Services");
	if (value == NULL)
		return PK_NETWORK_ENUM_UNKNOWN;

	dbus_g_type_collection_value_iterate (value, pk_network_stack_connman_iterate_list, &services_list);

	for (list = services_list; list; list = list->next) {
		gchar *path = list->data;

		egg_debug ("service path is %s", path);

		proxy_service = dbus_g_proxy_new_for_name (connection,
							   CONNMAN_DBUS_NAME,
							   path,
							   CONNMAN_SERVICE_DBUS_INTERFACE);
		if (proxy_service != NULL)
			break;
	}

	/* free service list */
	for (list = services_list; list; list = list->next) {
		gchar *path = list->data;
		g_free (path);
	}
	g_slist_free (services_list);

	if (proxy_service == NULL)
		return PK_NETWORK_ENUM_UNKNOWN;

	/* now proxy_service point to first available service */
	/* get connection type for i t*/
	dbus_g_proxy_call (proxy_service, "GetProperties", &error,
			   G_TYPE_INVALID,
			   dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), &hash_service,
			   G_TYPE_INVALID);
	if (error != NULL || hash_service == NULL) {
		if (error)
			g_clear_error (&error);
		return PK_NETWORK_ENUM_OFFLINE;
	}

	value = g_hash_table_lookup (hash_service, "Type");
	type = pk_network_stack_connman_get_connection_type (value);

	egg_debug ("network type is %s", pk_network_enum_to_string (type));
	g_object_unref (proxy_service);
	return type;
}

/**
 * pk_network_stack_connman_state_changed
 **/
static void
pk_network_stack_connman_state_changed (DBusGProxy *proxy, const char *property,
					GValue *value, gpointer user_data)
{
	PkNetworkEnum network_state;
	PkNetworkStackConnman *nstack_connman = (PkNetworkStackConnman *) user_data;

	g_return_if_fail (PK_IS_NETWORK_STACK_CONNMAN (nstack_connman));

	if (g_str_equal (property, "State") == TRUE) {
		gchar *state;

		state = g_value_dup_string (value);
		if (g_str_equal (state, "online") == TRUE)
			network_state = PK_NETWORK_ENUM_ONLINE;
		else
			network_state = PK_NETWORK_ENUM_OFFLINE;
		egg_debug ("emitting network-state-changed: %s", pk_network_enum_to_string (network_state));
		g_signal_emit_by_name (PK_NETWORK_STACK (nstack_connman), "state-changed", network_state);
	}

}

/**
 * pk_network_stack_connman_is_enabled:
 *
 * Return %TRUE on success, %FALSE if we failed to is_enabled or no data
 **/
static gboolean
pk_network_stack_connman_is_enabled (PkNetworkStack *nstack)
{
	PkNetworkStackConnman *nstack_connman = PK_NETWORK_STACK_CONNMAN (nstack);
	return nstack_connman->priv->is_enabled;
}

/**
 * pk_network_stack_connman_init:
 **/
static void
pk_network_stack_connman_init (PkNetworkStackConnman *nstack_connman)
{
	GError *error = NULL;
	DBusGProxy *proxy;
	gboolean service_alive;

	nstack_connman->priv = PK_NETWORK_STACK_CONNMAN_GET_PRIVATE (nstack_connman);
	nstack_connman->priv->conf = pk_conf_new ();

	/* do we use this code? */
	nstack_connman->priv->is_enabled = pk_conf_get_bool (nstack_connman->priv->conf, "UseNetworkConnman");
	nstack_connman->priv->proxy = NULL;

	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_BOXED,
					   G_TYPE_NONE, G_TYPE_STRING,
					   G_TYPE_VALUE, G_TYPE_INVALID);

	/* get system connection */
	nstack_connman->priv->bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (nstack_connman->priv->bus == NULL) {
		egg_warning ("Couldn't connect to system bus: %s", error->message);
		g_error_free (error);
		return;
	}

	/* check if ConnMan is on the bus */
	nstack_connman->priv->dbus_monitor = egg_dbus_monitor_new ();
	egg_dbus_monitor_assign (nstack_connman->priv->dbus_monitor, EGG_DBUS_MONITOR_SYSTEM, CONNMAN_DBUS_NAME);
	service_alive = egg_dbus_monitor_is_connected (nstack_connman->priv->dbus_monitor);

	/* ConnMan isn't up, so we can't use it */
	if (nstack_connman->priv->is_enabled && !service_alive) {
		egg_warning ("UseNetworkConnman true, but %s not up", CONNMAN_DBUS_NAME);
		nstack_connman->priv->is_enabled = FALSE;
	}

	proxy = dbus_g_proxy_new_for_name_owner (nstack_connman->priv->bus,
						 CONNMAN_DBUS_NAME,
						 CONNMAN_MANAGER_DBUS_PATH,
						 CONNMAN_MANAGER_DBUS_INTERFACE, &error);
	nstack_connman->priv->proxy = proxy;

	if (error != NULL) {
		egg_warning ("Cannot connect to connman: %s", error->message);
		g_error_free (error);
		return;
	}
	dbus_g_proxy_add_signal (proxy, "PropertyChanged",
				 G_TYPE_STRING, G_TYPE_VALUE, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "PropertyChanged",
				     G_CALLBACK(pk_network_stack_connman_state_changed), nstack_connman, NULL);
}

/**
 * pk_network_stack_connman_finalize:
 **/
static void
pk_network_stack_connman_finalize (GObject *object)
{
	PkNetworkStackConnman *nstack_connman;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_NETWORK_STACK_CONNMAN (object));

	nstack_connman = PK_NETWORK_STACK_CONNMAN (object);
	g_return_if_fail (nstack_connman->priv != NULL);

	g_object_unref (nstack_connman->priv->dbus_monitor);
	g_object_unref (nstack_connman->priv->conf);
	if (nstack_connman->priv->proxy != NULL)
		g_object_unref (nstack_connman->priv->proxy);

	G_OBJECT_CLASS (pk_network_stack_connman_parent_class)->finalize (object);
}

/**
 * pk_network_stack_connman_class_init:
 **/
static void
pk_network_stack_connman_class_init (PkNetworkStackConnmanClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	PkNetworkStackClass *nstack_class = PK_NETWORK_STACK_CLASS (klass);

	object_class->finalize = pk_network_stack_connman_finalize;
	nstack_class->get_state = pk_network_stack_connman_get_state;
	nstack_class->is_enabled = pk_network_stack_connman_is_enabled;

	g_type_class_add_private (klass, sizeof (PkNetworkStackConnmanPrivate));
}

/**
 * pk_network_stack_connman_new:
 **/
PkNetworkStackConnman *
pk_network_stack_connman_new (void)
{
	return g_object_new (PK_TYPE_NETWORK_STACK_CONNMAN, NULL);
}

