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
#define CONNMAN_MANAGER_DBUS_PATH		"/"

/**
 * pk_network_stack_connman_get_state:
 **/
static PkNetworkEnum
pk_network_stack_connman_get_state (PkNetworkStack *nstack)
{
	PkNetworkEnum type = PK_NETWORK_ENUM_ONLINE;
	PkNetworkStackConnman *nstack_connman = PK_NETWORK_STACK_CONNMAN (nstack);
	const gchar *state;
	_cleanup_variant_unref_ GVariant *res = NULL;
	_cleanup_variant_unref_ GVariant *child = NULL;
	_cleanup_error_free_ GError *error = NULL;

	/* get services */
	res = g_dbus_proxy_call_sync (nstack_connman->priv->proxy,
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
	PkNetworkEnum network_state;
	PkNetworkStackConnman *nstack_connman = PK_NETWORK_STACK_CONNMAN (user_data);
	nstack_connman->priv->is_enabled = TRUE;
	network_state = pk_network_stack_connman_get_state (PK_NETWORK_STACK (user_data));
	g_signal_emit_by_name (PK_NETWORK_STACK (nstack_connman),
			       "state-changed", network_state);
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

/**
 * pk_network_stack_connman_init:
 **/
static void
pk_network_stack_connman_init (PkNetworkStackConnman *nstack_connman)
{
	GDBusProxy *proxy;
	_cleanup_error_free_ GError *error = NULL;

	nstack_connman->priv = PK_NETWORK_STACK_CONNMAN_GET_PRIVATE (nstack_connman);

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

