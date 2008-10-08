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
 * SECTION:pk-connection
 * @short_description: Functionality to see when packagekid starts and stops
 *
 * This file contains functions that can be used to see when packagekitd starts
 * and stops.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus.h>

#include "egg-debug.h"
#include "pk-common.h"
#include "pk-connection.h"

#define PK_CONNECTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CONNECTION, PkConnectionPrivate))

/**
 * PkConnectionPrivate:
 *
 * Private #PkConnection data
 **/
struct _PkConnectionPrivate
{
	DBusGProxy		*proxy;
	DBusGConnection		*connection;
};

enum {
	CONNECTION_CHANGED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };
static gpointer pk_connection_object = NULL;

G_DEFINE_TYPE (PkConnection, pk_connection, G_TYPE_OBJECT)

/**
 * pk_connection_valid:
 * @connection: a valid #PkConnection instance
 *
 * Return value: %TRUE if packagekitd is running
 **/
gboolean
pk_connection_valid (PkConnection *connection)
{
	DBusError error;
	DBusConnection *conn;
	gboolean ret;
	g_return_val_if_fail (PK_IS_CONNECTION (connection), FALSE);

	/* get raw connection */
	conn = dbus_g_connection_get_connection (connection->priv->connection);
	dbus_error_init (&error);
	ret = dbus_bus_name_has_owner (conn, PK_DBUS_SERVICE, &error);
	if (dbus_error_is_set (&error)) {
		egg_debug ("error: %s", error.message);
		dbus_error_free (&error);
	}

	return ret;
}

/**
 * egg_dbus_connection_name_owner_changed_cb:
 **/
static void
egg_dbus_connection_name_owner_changed_cb (DBusGProxy *proxy, const gchar *name,
				       const gchar *prev, const gchar *new,
				       PkConnection *connection)
{
	guint new_len;
	guint prev_len;

	g_return_if_fail (PK_IS_CONNECTION (connection));
	if (connection->priv->proxy == NULL)
		return;

	/* not us */
	if (strcmp (name, PK_DBUS_SERVICE) != 0)
		return;

	/* ITS4: ignore, not used for allocation */
	new_len = strlen (new);
	/* ITS4: ignore, not used for allocation */
	prev_len = strlen (prev);

	/* something --> nothing */
	if (prev_len != 0 && new_len == 0) {
		g_signal_emit (connection , signals [CONNECTION_CHANGED], 0, FALSE);
		return;
	}

	/* nothing --> something */
	if (prev_len == 0 && new_len != 0) {
		g_signal_emit (connection , signals [CONNECTION_CHANGED], 0, TRUE);
		return;
	}
}

/**
 * pk_connection_finalize:
 **/
static void
pk_connection_finalize (GObject *object)
{
	PkConnection *connection;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_CONNECTION (object));
	connection = PK_CONNECTION (object);
	g_return_if_fail (connection->priv != NULL);

	g_object_unref (connection->priv->proxy);

	G_OBJECT_CLASS (pk_connection_parent_class)->finalize (object);
}

/**
 * pk_connection_class_init:
 **/
static void
pk_connection_class_init (PkConnectionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize	   = pk_connection_finalize;

	signals [CONNECTION_CHANGED] =
		g_signal_new ("connection-changed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkConnectionClass, connection_changed),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	g_type_class_add_private (klass, sizeof (PkConnectionPrivate));
}

/**
 * pk_connection_init:
 **/
static void
pk_connection_init (PkConnection *connection)
{
	GError *error = NULL;
	connection->priv = PK_CONNECTION_GET_PRIVATE (connection);

	/* connect to correct bus */
	connection->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		egg_warning ("Cannot connect to bus: %s", error->message);
		g_error_free (error);
		return;
	}
	connection->priv->proxy = dbus_g_proxy_new_for_name_owner (connection->priv->connection,
								   DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
								   DBUS_INTERFACE_DBUS, &error);
	if (error != NULL) {
		egg_warning ("Cannot connect to proxy: %s", error->message);
		g_error_free (error);
		return;
	}
	dbus_g_proxy_add_signal (connection->priv->proxy, "NameOwnerChanged",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (connection->priv->proxy, "NameOwnerChanged",
				     G_CALLBACK (egg_dbus_connection_name_owner_changed_cb),
				     connection, NULL);
}

/**
 * pk_connection_new:
 *
 * Return value: A new #PkConnection instance
 **/
PkConnection *
pk_connection_new (void)
{
	if (pk_connection_object != NULL) {
		g_object_ref (pk_connection_object);
	} else {
		pk_connection_object = g_object_new (PK_TYPE_CONNECTION, NULL);
		g_object_add_weak_pointer (pk_connection_object, &pk_connection_object);
	}
	return PK_CONNECTION (pk_connection_object);
}

