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
#include <libgbus.h>

#include "pk-debug.h"
#include "pk-common.h"
#include "pk-connection.h"

#define PK_CONNECTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CONNECTION, PkConnectionPrivate))

/**
 * PkConnectionPrivate:
 *
 * Private #PkConnection data
 **/
struct PkConnectionPrivate
{
	LibGBus			*libgbus;
};

enum {
	CONNECTION_CHANGED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };
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
	return libgbus_is_connected (connection->priv->libgbus);
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

	g_object_unref (connection->priv->libgbus);

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
 * pk_connection_connection_changed_cb:
 **/
static void
pk_connection_connection_changed_cb (LibGBus *libgbus, gboolean connected, PkConnection *connection)
{
	pk_debug ("emit connection-changed: %i", connected);
	g_signal_emit (connection , signals [CONNECTION_CHANGED], 0, connected);
}

/**
 * pk_connection_init:
 **/
static void
pk_connection_init (PkConnection *connection)
{
	connection->priv = PK_CONNECTION_GET_PRIVATE (connection);
	connection->priv->libgbus = libgbus_new ();
	g_signal_connect (connection->priv->libgbus, "connection-changed",
			  G_CALLBACK (pk_connection_connection_changed_cb), connection);

	/* hardcode to PackageKit */
	libgbus_assign (connection->priv->libgbus, LIBGBUS_SYSTEM, PK_DBUS_SERVICE);
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

