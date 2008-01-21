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
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <glib/gprintf.h>

#include <gmodule.h>
#include <libgbus.h>
#include <dbus/dbus-glib.h>

#include <pk-common.h>
#include <pk-package-id.h>
#include <pk-enum.h>
#include <pk-network.h>

#include "pk-debug.h"
#include "pk-backend-internal.h"
#include "pk-backend-dbus.h"
#include "pk-marshal.h"
#include "pk-enum.h"
#include "pk-time.h"
#include "pk-inhibit.h"
#include "pk-thread-list.h"

#define PK_BACKEND_DBUS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_BACKEND_DBUS, PkBackendDbusPrivate))

struct PkBackendDbusPrivate
{
	DBusGConnection		*connection;
	DBusGProxy		*proxy;
	PkBackend		*backend;
	gchar			*service;
	gchar			*interface;
	gchar			*path;
	gulong			 signal_finished;
};

G_DEFINE_TYPE (PkBackendDbus, pk_backend_dbus, G_TYPE_OBJECT)

/**
 * pk_backend_dbus_finished_cb:
 **/
static void
pk_backend_dbus_finished_cb (DBusGProxy *proxy, PkExitEnum exit, PkBackendDbus *backend_dbus)
{
	pk_debug ("deleting dbus %p, exit %s", backend_dbus, pk_exit_enum_to_text (exit));
	pk_backend_finished (backend_dbus->priv->backend);
}

/**
 * pk_backend_dbus_kill:
 **/
gboolean
pk_backend_dbus_set_name (PkBackendDbus *backend_dbus, const gchar *service,
			  const gchar *interface, const gchar *path)
{
	DBusGProxy *proxy;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	if (backend_dbus->priv->proxy != NULL) {
		pk_warning ("need to unref old one -- is this logically allowed?");
		g_object_unref (backend_dbus->priv->proxy);
	}

	/* grab this */
	proxy = dbus_g_proxy_new_for_name (backend_dbus->priv->connection,
					   service, path, interface);
	dbus_g_proxy_add_signal (proxy, "Finished",
				 G_TYPE_INT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Finished",
				     G_CALLBACK (pk_backend_dbus_finished_cb), backend_dbus, NULL);
	backend_dbus->priv->proxy = proxy;

	/* save for later */
	g_free (backend_dbus->priv->service);
	g_free (backend_dbus->priv->interface);
	g_free (backend_dbus->priv->path);
	backend_dbus->priv->service = g_strdup (service);
	backend_dbus->priv->interface = g_strdup (interface);
	backend_dbus->priv->path = g_strdup (path);
	return TRUE;
}

/**
 * pk_backend_dbus_kill:
 **/
gboolean
pk_backend_dbus_kill (PkBackendDbus *backend_dbus)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "Exit", &error,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_search_name:
 **/
gboolean
pk_backend_dbus_search_name (PkBackendDbus *backend_dbus, const gchar *filter, const gchar *search)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "SearchName", &error,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_finalize:
 **/
static void
pk_backend_dbus_finalize (GObject *object)
{
	PkBackendDbus *backend_dbus;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_BACKEND_DBUS (object));

	backend_dbus = PK_BACKEND_DBUS (object);
	g_free (backend_dbus->priv->service);
	g_free (backend_dbus->priv->interface);
	g_free (backend_dbus->priv->path);
	g_object_unref (backend_dbus->priv->proxy);
	g_object_unref (backend_dbus->priv->backend);

	G_OBJECT_CLASS (pk_backend_dbus_parent_class)->finalize (object);
}

/**
 * pk_backend_dbus_class_init:
 **/
static void
pk_backend_dbus_class_init (PkBackendDbusClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_backend_dbus_finalize;
	g_type_class_add_private (klass, sizeof (PkBackendDbusPrivate));
}

/**
 * pk_backend_dbus_init:
 **/
static void
pk_backend_dbus_init (PkBackendDbus *backend_dbus)
{
	GError *error = NULL;

	backend_dbus->priv = PK_BACKEND_DBUS_GET_PRIVATE (backend_dbus);
	backend_dbus->priv->proxy = NULL;
	backend_dbus->priv->service = NULL;
	backend_dbus->priv->interface = NULL;
	backend_dbus->priv->path = NULL;
	backend_dbus->priv->backend = pk_backend_new ();

	/* get connection */
	backend_dbus->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
}

/**
 * pk_backend_dbus_new:
 **/
PkBackendDbus *
pk_backend_dbus_new (void)
{
	PkBackendDbus *backend_dbus;
	backend_dbus = g_object_new (PK_TYPE_BACKEND_DBUS, NULL);
	return PK_BACKEND_DBUS (backend_dbus);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_backend_dbus (LibSelfTest *test)
{
	PkBackendDbus *backend_dbus;

	if (libst_start (test, "PkBackendDbus", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "get an backend_dbus");
	backend_dbus = pk_backend_dbus_new ();
	if (backend_dbus != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	g_object_unref (backend_dbus);

	libst_end (test);
}
#endif

