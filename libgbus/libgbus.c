/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2007 Richard Hughes <richard@hughsie.com>
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

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus.h>

#include <libgbus.h>

static void     libgbus_class_init (LibGBusClass *klass);
static void     libgbus_init       (LibGBus      *watch);
static void     libgbus_finalize   (GObject      *object);

#define LIBGBUS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), LIBGBUS_TYPE, LibGBusPrivate))

struct LibGBusPrivate
{
	LibGBusType		 bus_type;
	gchar			*service;
	DBusGProxy		*proxy;
	DBusGConnection		*connection;
	gboolean		 connected;
};

enum {
	CONNECTION_CHANGED,
	LAST_SIGNAL
};

static guint	     signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (LibGBus, libgbus, G_TYPE_OBJECT)

/**
 * name_owner_changed_cb:
 **/
static void
name_owner_changed_cb (DBusGProxy     *proxy,
		       const gchar    *name,
		       const gchar    *prev,
		       const gchar    *new,
		       LibGBus	      *libgbus)
{
	g_return_if_fail (IS_LIBGBUS (libgbus));
	if (libgbus->priv->proxy == NULL) {
		return;
	}

	if (strcmp (name, libgbus->priv->service) == 0) {
		/* ITS4: ignore, not used for allocation */
		if (strlen (prev) != 0 && strlen (new) == 0 && libgbus->priv->connected == TRUE) {
			g_signal_emit (libgbus, signals [CONNECTION_CHANGED], 0, FALSE);
			libgbus->priv->connected = FALSE;
		}
		/* ITS4: ignore, not used for allocation */
		if (strlen (prev) == 0 && strlen (new) != 0 && libgbus->priv->connected == FALSE) {
			g_signal_emit (libgbus, signals [CONNECTION_CHANGED], 0, TRUE);
			libgbus->priv->connected = TRUE;
		}
	}
}

/**
 * libgbus_assign:
 * @libgbus: This class instance
 * @bus_type: The bus type, either LIBGBUS_SESSION or LIBGBUS_SYSTEM
 * @service: The LIBGBUS service name
 * Return value: success
 *
 * Emits connection-changed(TRUE) if connection is alive - this means you
 * have to connect up the callback before this function is called.
 **/
gboolean
libgbus_assign (LibGBus      *libgbus,
		LibGBusType   bus_type,
		const gchar  *service)
{
	GError *error = NULL;

	g_return_val_if_fail (IS_LIBGBUS (libgbus), FALSE);
	g_return_val_if_fail (service != NULL, FALSE);

	if (libgbus->priv->proxy != NULL) {
		g_warning ("already assigned!");
		return FALSE;
	}

	libgbus->priv->service = g_strdup (service);
	libgbus->priv->bus_type = bus_type;

	/* connect to correct bus */
	if (bus_type == LIBGBUS_SESSION) {
		libgbus->priv->connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	} else {
		libgbus->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	}
	if (error != NULL) {
		g_warning ("Cannot connect to bus: %s", error->message);
		g_error_free (error);
		return FALSE;
	}
	libgbus->priv->proxy = dbus_g_proxy_new_for_name_owner (libgbus->priv->connection,
								DBUS_SERVICE_DBUS,
								DBUS_PATH_DBUS,
						 		DBUS_INTERFACE_DBUS,
								&error);
	if (error != NULL) {
		g_warning ("Cannot connect to DBUS: %s", error->message);
		g_error_free (error);
		return FALSE;
	}
	dbus_g_proxy_add_signal (libgbus->priv->proxy, "NameOwnerChanged",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (libgbus->priv->proxy, "NameOwnerChanged",
				     G_CALLBACK (name_owner_changed_cb),
				     libgbus, NULL);

	/* coldplug */
	libgbus->priv->connected = libgbus_is_connected (libgbus);
	if (libgbus->priv->connected == TRUE) {
		g_signal_emit (libgbus, signals [CONNECTION_CHANGED], 0, TRUE);
	}
	return TRUE;
}

/**
 * libgbus_is_connected:
 * @libgbus: This class instance
 * Return value: if we are connected to a valid watch
 **/
gboolean
libgbus_is_connected (LibGBus *libgbus)
{
	DBusError error;
	DBusConnection *conn;
	gboolean ret;
	g_return_val_if_fail (IS_LIBGBUS (libgbus), FALSE);

	/* get raw connection */
	conn = dbus_g_connection_get_connection (libgbus->priv->connection);
	dbus_error_init (&error);
	ret = dbus_bus_name_has_owner (conn, libgbus->priv->service, &error);
	if (dbus_error_is_set (&error)) {
		g_debug ("error: %s", error.message);
		dbus_error_free (&error);
	}

	return ret;
}

/**
 * libgbus_class_init:
 * @libgbus: This class instance
 **/
static void
libgbus_class_init (LibGBusClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = libgbus_finalize;
	g_type_class_add_private (klass, sizeof (LibGBusPrivate));

	signals [CONNECTION_CHANGED] =
		g_signal_new ("connection-changed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (LibGBusClass, connection_changed),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

/**
 * libgbus_init:
 * @libgbus: This class instance
 **/
static void
libgbus_init (LibGBus *libgbus)
{
	libgbus->priv = LIBGBUS_GET_PRIVATE (libgbus);

	libgbus->priv->service = NULL;
	libgbus->priv->bus_type = LIBGBUS_SESSION;
	libgbus->priv->proxy = NULL;
}

/**
 * libgbus_finalize:
 * @object: This class instance
 **/
static void
libgbus_finalize (GObject *object)
{
	LibGBus *libgbus;
	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_LIBGBUS (object));

	libgbus = LIBGBUS_OBJECT (object);
	libgbus->priv = LIBGBUS_GET_PRIVATE (libgbus);

	if (libgbus->priv->proxy != NULL) {
		g_object_unref (libgbus->priv->proxy);
	}
	G_OBJECT_CLASS (libgbus_parent_class)->finalize (object);
}

/**
 * libgbus_new:
 * Return value: new class instance.
 **/
LibGBus *
libgbus_new (void)
{
	LibGBus *libgbus;
	libgbus = g_object_new (LIBGBUS_TYPE, NULL);
	return LIBGBUS_OBJECT (libgbus);
}

