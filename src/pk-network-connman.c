/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009  Intel Corporation. All rights reserved.
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
#include <glib.h>
#include <dbus/dbus-glib.h>

#include "egg-debug.h"
#include "pk-network-connman.h"
#include "pk-marshal.h"

static void	pk_network_connman_finalize		(GObject	*object);

#define PK_NETWORK_CONNMAN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_NETWORK_CONNMAN, PkNetworkConnmanPrivate))

/**
 * PkNetworkConnmanPrivate:
 *
 * Private #PkNetworkConnman data
 **/
struct _PkNetworkConnmanPrivate
{
	DBusGConnection		*bus;
	DBusGProxy		*proxy_connman;
};

enum {
	PK_NETWORK_CONNMAN_STATE_CHANGED,
	PK_NETWORK_CONNMAN_LAST_SIGNAL
};

static guint signals [PK_NETWORK_CONNMAN_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkNetworkConnman, pk_network_connman, G_TYPE_OBJECT)

#define CONNMAN_DBUS_NAME "org.moblin.connman"

#define CONNMAN_MANAGER_DBUS_INTERFACE CONNMAN_DBUS_NAME ".Manager"
#define CONNMAN_SERVICE_DBUS_INTERFACE CONNMAN_DBUS_NAME ".Service"
#define CONNMAN_MANAGER_DBUS_PATH "/"

static void
iterate_list(const GValue *value, gpointer user_data)
{
	GSList **list = user_data;
	gchar *path = g_value_dup_boxed(value);

	if (path == NULL)
		return;

	*list = g_slist_append(*list, path);
}

/**
 * pk_network_nm_get_active_connection_type_for_device:
 **/
static PkNetworkEnum
pk_network_connman_get_connection_type (const GValue *value)
{
	const char *type = value ? g_value_get_string(value) : NULL;

	if (type == NULL)
		return PK_NETWORK_ENUM_UNKNOWN;
	else if (g_str_equal (type, "ethernet") == TRUE)
		return PK_NETWORK_ENUM_WIRED;
	else if (g_str_equal (type, "wifi") == TRUE)
		return PK_NETWORK_ENUM_WIFI;
	else if (g_str_equal (type, "wimax") == TRUE)
		return PK_NETWORK_ENUM_MOBILE;

	return PK_NETWORK_ENUM_UNKNOWN;
}

/**
 * pk_network_connman_get_network_state:
 * @network_connman: a valid #PkNetworkConnman instance
 *
 * Return value: %TRUE if the network_connman is online
 **/
PkNetworkEnum
pk_network_connman_get_network_state (PkNetworkConnman *network_connman)
{
	DBusGProxy *proxy, *proxy_service;
	GError *error = NULL;
	GHashTable *hash_manager = NULL, *hash_service = NULL;
	GValue *value;
	GSList *list, *services_list = NULL;
	DBusGConnection *connection = network_connman->priv->bus;
	gchar *state;
	PkNetworkEnum type;

	g_return_val_if_fail (PK_IS_NETWORK_CONNMAN (network_connman), PK_NETWORK_ENUM_UNKNOWN);
	proxy = network_connman->priv->proxy_connman;
	proxy_service = NULL;

	/* get services */
	dbus_g_proxy_call (proxy, "GetProperties", &error, G_TYPE_INVALID,
		dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), &hash_manager, G_TYPE_INVALID);

	if (error != NULL || hash_manager == NULL) {
		if (error)
			g_clear_error (&error);
		return PK_NETWORK_ENUM_UNKNOWN;
	}

	value = g_hash_table_lookup (hash_manager, "State");
	state = value ? g_value_dup_string(value) : NULL;

	if (g_str_equal (state, "online") == FALSE)
		return PK_NETWORK_ENUM_OFFLINE;

	value = g_hash_table_lookup (hash_manager, "Services");
	if (value == NULL)
		return PK_NETWORK_ENUM_UNKNOWN;

	dbus_g_type_collection_value_iterate (value, iterate_list, &services_list);

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

	for (list = services_list; list; list = list->next) {
		gchar *path = list->data;
		g_free (path);
	}
	g_slist_free (services_list);

	if (proxy_service == NULL)
		return PK_NETWORK_ENUM_UNKNOWN;

	/* now proxy_service point to first available service*/
	/* get connection type for it*/
	dbus_g_proxy_call (proxy_service, "GetProperties", &error, G_TYPE_INVALID,
		dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), &hash_service, G_TYPE_INVALID);

	if (error != NULL || hash_service == NULL) {
		if (error)
			g_clear_error (&error);
		return PK_NETWORK_ENUM_OFFLINE;
	}

	value = g_hash_table_lookup (hash_service, "Type");
	type = pk_network_connman_get_connection_type (value);

	egg_debug ("network type is %s", pk_network_enum_to_text (type));
	g_object_unref (proxy_service);
	return type;
}

/**
 * pk_network_connman_state_changed
 **/
static void
pk_network_connman_state_changed (DBusGProxy *proxy, const char *property,
					GValue *value, gpointer user_data)
{
	gboolean ret;
	PkNetworkConnman *network_connman = (PkNetworkConnman *) user_data;

	g_return_if_fail (PK_IS_NETWORK_CONNMAN (network_connman));

	if (g_str_equal (property, "State") == TRUE) {
		gchar *state;

		state = g_value_dup_string (value);
		if (g_str_equal (state, "online") == TRUE)
			ret = TRUE;
		else
			ret = FALSE;
		g_signal_emit (network_connman, signals [PK_NETWORK_CONNMAN_STATE_CHANGED], 0, ret);
	}

}

/**
 * pk_network_connman_class_init:
 * @klass: The PkNetworkConnmanClass
 **/
static void
pk_network_connman_class_init (PkNetworkConnmanClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_network_connman_finalize;
	signals [PK_NETWORK_CONNMAN_STATE_CHANGED] =
		g_signal_new ("state-changed",
			     G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			     0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			     G_TYPE_NONE, 1, G_TYPE_UINT);
	g_type_class_add_private (klass, sizeof (PkNetworkConnmanPrivate));
}

/**
 * pk_network_connman_init:
 * @network_connman: This class instance
 **/
static void
pk_network_connman_init (PkNetworkConnman *network_connman)
{
	GError *error = NULL;
	DBusGProxy *proxy;

	network_connman->priv = PK_NETWORK_CONNMAN_GET_PRIVATE (network_connman);

	network_connman->priv->proxy_connman = NULL;

	dbus_g_object_register_marshaller(pk_marshal_VOID__STRING_BOXED,
					  G_TYPE_NONE, G_TYPE_STRING,
					  G_TYPE_VALUE, G_TYPE_INVALID);

	/* get system connection */
	network_connman->priv->bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (network_connman->priv->bus == NULL) {
		egg_warning ("Couldn't connect to system bus: %s", error->message);
		g_error_free (error);
		return;
	}

	proxy = dbus_g_proxy_new_for_name_owner (network_connman->priv->bus,
			CONNMAN_DBUS_NAME, CONNMAN_MANAGER_DBUS_PATH, CONNMAN_MANAGER_DBUS_INTERFACE, &error);
	network_connman->priv->proxy_connman = proxy;

	if (error != NULL) {
		egg_warning ("Cannot connect to connman: %s", error->message);
		g_error_free (error);
		return;
	}
	dbus_g_proxy_add_signal (proxy, "PropertyChanged",
				 G_TYPE_STRING, G_TYPE_VALUE, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "PropertyChanged",
				     G_CALLBACK(pk_network_connman_state_changed), network_connman, NULL);

}

/**
 * pk_network_connman_finalize:
 * @object: The object to finalize
 **/
static void
pk_network_connman_finalize (GObject *object)
{
	PkNetworkConnman *network_connman;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_NETWORK_CONNMAN (object));
	network_connman = PK_NETWORK_CONNMAN (object);

	g_return_if_fail (network_connman->priv != NULL);

	if (network_connman->priv->proxy_connman != NULL)
		g_object_unref (network_connman->priv->proxy_connman);

	G_OBJECT_CLASS (pk_network_connman_parent_class)->finalize (object);
}

/**
 * pk_network_connman_new:
 *
 * Return value: a new PkNetworkConnman object.
 **/
PkNetworkConnman *
pk_network_connman_new (void)
{
	PkNetworkConnman *network_connman;
	network_connman = g_object_new (PK_TYPE_NETWORK_CONNMAN, NULL);
	return PK_NETWORK_CONNMAN (network_connman);
}

