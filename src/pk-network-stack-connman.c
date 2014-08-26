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
#include <gio/gio.h>
#include <dbus/dbus-glib.h>

#include "pk-cleanup.h"
#include "pk-network-stack-connman.h"

struct PkNetworkStackConnmanPrivate
{
	guint			 watch_id;
	gboolean		 is_enabled;
	GDBusConnection		*bus;
	GDBusProxy		*proxy;
};

G_DEFINE_TYPE (PkNetworkStackConnman, pk_network_stack_connman, PK_TYPE_NETWORK_STACK)
#define PK_NETWORK_STACK_CONNMAN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_NETWORK_STACK_CONNMAN, PkNetworkStackConnmanPrivate))

#define CONNMAN_DBUS_NAME			"net.connman"
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
	GDBusProxy *proxy, *proxy_service;
	GError *error = NULL;
	GHashTable *hash_manager = NULL;
	GHashTable *hash_service = NULL;
	GValue *value;
	GSList *list;
	GSList *services_list = NULL;
	gchar *state;
	PkNetworkEnum type;
	PkNetworkStackConnman *nstack_connman = PK_NETWORK_STACK_CONNMAN (nstack);
	GDBusConnection *connection = nstack_connman->priv->bus;

	proxy = nstack_connman->priv->proxy;
	proxy_service = NULL;

	/* get services */
	g_dbus_proxy_call_sync (proxy, "GetProperties", &error,
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

		g_debug ("service path is %s", path);

		proxy_service = g_dbus_proxy_new_sync (connection,
						       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
						       NULL,
						       CONNMAN_DBUS_NAME,
						       path,
						       CONNMAN_SERVICE_DBUS_INTERFACE,
						       NULL,
						       &error);

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
	g_dbus_proxy_call_sync (proxy_service, "GetProperties", &error,
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

	g_debug ("network type is %s", pk_network_enum_to_string (type));
	g_object_unref (proxy_service);
	return type;
}

/**
 * pk_network_stack_connman_state_changed
 **/
static void
pk_network_stack_connman_state_changed (PkNetworkStackConnman *nstack_connman,
					GVariant *parameters)
{
	PkNetworkEnum network_state;
	gchar *property = NULL;
	GVariant *value = NULL;

	g_return_if_fail (PK_IS_NETWORK_STACK_CONNMAN (nstack_connman));

	g_variant_get (parameters, "(&sv)", &property, &value);
	if (property && value && (g_str_equal (property, "State") == TRUE)) {
		gchar *state = NULL;

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
			g_debug ("emitting network-state-changed: %s", pk_network_enum_to_string (network_state));
			g_signal_emit_by_name (PK_NETWORK_STACK (nstack_connman), "state-changed", network_state);
		}
	}
}


/**
 * pk_network_stack_connman_dbus_signal_cb:
 **/
static void
pk_network_stack_connman_dbus_signal_cb (GDBusProxy *proxy,
					 gchar *sender_name,
					 gchar *signal_name,
					 GVariant *parameters,
					 PkNetworkStackConnman *nstack_connman)
{
	g_return_if_fail (PK_IS_NETWORK_STACK_CONNMAN (nstack_connman));

	/* do not use */
	if (!nstack_connman->priv->is_enabled) {
		g_debug ("not enabled, so ignoring %s", signal_name);
		return;
	}

	/* don't use parameters, just refresh state */
	if (g_strcmp0 (signal_name, "PropertyChanged") == 0) {
		pk_network_stack_connman_state_changed (nstack_connman,
							parameters);
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
 * pk_network_stack_connman_appeared_cb:
 **/
static void
pk_network_stack_connman_appeared_cb (GDBusConnection *connection,
				      const gchar *name,
				      const gchar *name_owner,
				      gpointer user_data)
{
	PkNetworkStackConnman *nstack_connman = PK_NETWORK_STACK_CONNMAN (user_data);
	nstack_connman->priv->is_enabled = TRUE;
}

/**
 * pk_network_stack_connman_vanished_cb:
 **/
static void
pk_network_stack_connman_vanished_cb (GDBusConnection *connection,
				      const gchar *name,
				      gpointer user_data)
{
	PkNetworkStackConnman *nstack_connman = PK_NETWORK_STACK_CONNMAN (user_data);
	nstack_connman->priv->is_enabled = FALSE;
}

static void
pk_marshal_VOID__STRING_BOXED (GClosure *closure,
			       GValue *return_value G_GNUC_UNUSED,
			       guint n_param_values,
			       const GValue *param_values,
			       gpointer invocation_hint G_GNUC_UNUSED,
			       gpointer marshal_data)
{
	typedef void (*GMarshalFunc_VOID__STRING_BOXED) (gpointer data1,
		gpointer arg_1,
		gpointer arg_2,
		gpointer data2);
		register GMarshalFunc_VOID__STRING_BOXED callback;
		register GCClosure *cc = (GCClosure*) closure;
		register gpointer data1, data2;

	g_return_if_fail (n_param_values == 3);

	if (G_CCLOSURE_SWAP_DATA (closure)) {
		data1 = closure->data;
		data2 = g_value_get_pointer (param_values + 0);
	} else {
		data1 = g_value_get_pointer (param_values + 0);
		data2 = closure->data;
	}
	callback = (GMarshalFunc_VOID__STRING_BOXED) (marshal_data ? marshal_data : cc->callback);
	callback (data1,
		 g_value_get_string (param_values + 1),
		 g_value_get_boxed (param_values + 2),
		 data2);
}

/**
 * pk_network_stack_connman_init:
 **/
static void
pk_network_stack_connman_init (PkNetworkStackConnman *nstack_connman)
{
	GDBusProxy *proxy;
	_cleanup_error_free_ GError *error = NULL;

	nstack_connman->priv = PK_NETWORK_STACK_CONNMAN_GET_PRIVATE (nstack_connman);

	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_BOXED,
					   G_TYPE_NONE, G_TYPE_STRING,
					   G_TYPE_VALUE, G_TYPE_INVALID);

	/* get system connection */
	nstack_connman->priv->bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (nstack_connman->priv->bus == NULL) {
		g_warning ("Couldn't connect to system bus: %s", error->message);
		return;
	}

	/* check if ConnMan is on the bus */
	nstack_connman->priv->watch_id =
		g_bus_watch_name (G_BUS_TYPE_SYSTEM,
				  CONNMAN_DBUS_NAME,
				  G_BUS_NAME_WATCHER_FLAGS_NONE,
				  pk_network_stack_connman_appeared_cb,
				  pk_network_stack_connman_vanished_cb,
				  nstack_connman,
				  NULL);

	proxy = g_dbus_proxy_new_sync (nstack_connman->priv->bus,
				       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
				       NULL,
				       CONNMAN_DBUS_NAME,
				       CONNMAN_MANAGER_DBUS_PATH,
				       CONNMAN_MANAGER_DBUS_INTERFACE,
				       NULL,
				       &error);
	if (proxy == NULL) {
		g_warning ("Cannot connect to connman: %s", error->message);
		return;
	}
	nstack_connman->priv->proxy = proxy;
	g_signal_connect (nstack_connman->priv->proxy,
			  "g-signal",
			  G_CALLBACK (pk_network_stack_connman_dbus_signal_cb),
			  nstack_connman);
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

	g_bus_unwatch_name (nstack_connman->priv->watch_id);
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

