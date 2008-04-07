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
 * SECTION:pk-polkit-client
 * @short_description: Provides a nice GObject to get a PolKit action auth
 *
 * This file contains functions that can be used for authorising a PolKit action.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include "pk-common.h"
#include "pk-debug.h"
#include "pk-polkit-client.h"

static void     pk_polkit_client_class_init	(PkPolkitClientClass *klass);
static void     pk_polkit_client_init		(PkPolkitClient      *polkit_client);
static void     pk_polkit_client_finalize		(GObject           *object);

#define PK_POLKIT_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_POLKIT_CLIENT, PkPolkitClientPrivate))

#define POLKIT_DBUS_SERVICE		"org.gnome.PolicyKit"
#define POLKIT_DBUS_PATH		"/org/gnome/PolicyKit/Manager"
#define POLKIT_DBUS_INTERFACE		"org.gnome.PolicyKit.Manager"

/**
 * PkPolkitClientPrivate:
 *
 * Private #PkPolkitClient data
 **/
struct _PkPolkitClientPrivate
{
	DBusGConnection		*connection;
	DBusGProxy		*proxy;
};

G_DEFINE_TYPE (PkPolkitClient, pk_polkit_client, G_TYPE_OBJECT)

/**
 * pk_polkit_client_gain_privilege:
 * @pclient: a valid #PkPolkitClient instance
 * @pk_action: a PolicyKit action description, e.g. "org.freedesktop.packagekit.installfile"
 *
 * This function is indented to be used by client tools to gain extra privileges
 * needed to do extra functionality.
 *
 * Return value: if we gained the privilege we asked for
 **/
gboolean
pk_polkit_client_gain_privilege (PkPolkitClient *pclient, const gchar *pk_action)
{
	GError *error = NULL;
	gboolean gained_privilege;

	g_return_val_if_fail (PK_IS_POLKIT_CLIENT (pclient), FALSE);
	g_return_val_if_fail (pk_action != NULL, FALSE);

	/* Use PolicyKit-gnome to bring up an auth dialog (we
	 * don't have any windows so set the XID to "null") */
	if (!dbus_g_proxy_call_with_timeout (pclient->priv->proxy,
					     "ShowDialog",
					     INT_MAX,
					     &error,
					     /* parameters: */
					     G_TYPE_STRING, pk_action,      /* action_id */
					     G_TYPE_UINT, 0,		/* X11 window ID */
					     G_TYPE_INVALID,
					     /* return values: */
					     G_TYPE_BOOLEAN, &gained_privilege,
					     G_TYPE_INVALID)) {
		pk_warning ("Caught exception '%s'", error->message);
		g_error_free (error);
		return FALSE;
	}
	pk_debug ("gained %s privilege = %d", pk_action, gained_privilege);

	return gained_privilege;
}

/**
 * pk_polkit_client_gain_privilege_str:
 * @pclient: a valid #PkPolkitClient instance
 * @error_str: the raw output error, e.g. "org.freedesktop.packagekit.installfile no"
 *
 * This function is indented to be passed failure messages from dbus methods
 * so that extra auth can be requested.
 *
 * Return value: if we gained the privilege we asked for
 **/
gboolean
pk_polkit_client_gain_privilege_str (PkPolkitClient *pclient, const gchar *error_str)
{
	gboolean ret;
	gchar **tokens;

	g_return_val_if_fail (PK_IS_POLKIT_CLIENT (pclient), FALSE);
	g_return_val_if_fail (error_str != NULL, FALSE);

	tokens = g_strsplit (error_str, " ", 0);
	if (tokens == NULL) {
		pk_warning ("invalid string (null)");
		return FALSE;
	}
	if (g_strv_length (tokens) < 2) {
		pk_warning ("invalid string '%s'", error_str);
		g_strfreev (tokens);
		return FALSE;
	}

	/* we've now just got the pk_action */
	pk_debug ("pk_action='%s' pk_result='%s'", tokens[0], tokens[1]);
	ret = pk_polkit_client_gain_privilege (pclient, tokens[0]);

	g_strfreev (tokens);
	return ret;
}

/**
 * pk_polkit_client_error_denied_by_policy:
 * @error: a valid #GError
 *
 * Return value: %TRUE if the error is the PolicyKit "RefusedByPolicy"
 **/
gboolean
pk_polkit_client_error_denied_by_policy (GError *error)
{
	const gchar *error_name;

	/* if not set */
	if (error == NULL) {
		pk_debug ("not an error, is this sane?");
		return FALSE;
	}

	/* not a dbus error */
	if (error->code != DBUS_GERROR_REMOTE_EXCEPTION) {
		pk_warning ("not a remote exception, is this sane?");
		return FALSE;
	}

	/* check for specific error */
	error_name = dbus_g_error_get_name (error);
	pk_debug ("ERROR: %s: %s", error_name, error->message);
	if (pk_strequal (error_name, "org.freedesktop.PackageKit.RefusedByPolicy")) {
		return TRUE;
	}
	return FALSE;
}

/**
 * pk_polkit_client_class_init:
 **/
static void
pk_polkit_client_class_init (PkPolkitClientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_polkit_client_finalize;
	g_type_class_add_private (klass, sizeof (PkPolkitClientPrivate));
}

/**
 * pk_polkit_client_init:
 **/
static void
pk_polkit_client_init (PkPolkitClient *pclient)
{
	GError *error = NULL;

	pclient->priv = PK_POLKIT_CLIENT_GET_PRIVATE (pclient);

	/* check dbus connections, exit if not valid */
	pclient->priv->connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (error) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error ("Could not connect to system DBUS.");
	}

	/* get a connection */
	pclient->priv->proxy = dbus_g_proxy_new_for_name (pclient->priv->connection,
							  POLKIT_DBUS_SERVICE,
							  POLKIT_DBUS_PATH,
							  POLKIT_DBUS_INTERFACE);
	if (pclient->priv->proxy == NULL) {
		g_error ("Cannot connect to PackageKit.");
	}
}

/**
 * pk_polkit_client_finalize:
 **/
static void
pk_polkit_client_finalize (GObject *object)
{
	PkPolkitClient *pclient;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_POLKIT_CLIENT (object));
	pclient = PK_POLKIT_CLIENT (object);
	g_return_if_fail (pclient->priv != NULL);

	/* free the proxy */
	g_object_unref (G_OBJECT (pclient->priv->proxy));

	G_OBJECT_CLASS (pk_polkit_client_parent_class)->finalize (object);
}

/**
 * pk_polkit_client_new:
 **/
PkPolkitClient *
pk_polkit_client_new (void)
{
	PkPolkitClient *pclient;
	pclient = g_object_new (PK_TYPE_POLKIT_CLIENT, NULL);
	return PK_POLKIT_CLIENT (pclient);
}

