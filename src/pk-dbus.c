/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "pk-dbus.h"

#define PK_DBUS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_DBUS, PkDbusPrivate))

struct PkDbusPrivate
{
	DBusGConnection		*connection;
	DBusGProxy		*proxy_pid;
	DBusGProxy		*proxy_session;
};

static gpointer pk_dbus_object = NULL;

G_DEFINE_TYPE (PkDbus, pk_dbus, G_TYPE_OBJECT)

/**
 * pk_dbus_get_uid:
 * @dbus: the #PkDbus instance
 * @sender: the sender, usually got from dbus_g_method_get_dbus()
 *
 * Gets the process UID.
 *
 * Return value: the UID, or %G_MAXUINT if it could not be obtained
 **/
guint
pk_dbus_get_uid (PkDbus *dbus, const gchar *sender)
{
	guint uid;
	DBusError error;
	DBusConnection *con;

	g_return_val_if_fail (PK_IS_DBUS (dbus), G_MAXUINT);
	g_return_val_if_fail (sender != NULL, G_MAXUINT);

	/* set in the test suite */
	if (g_strcmp0 (sender, ":org.freedesktop.PackageKit") == 0) {
		g_debug ("using self-check shortcut");
		uid = 500;
		goto out;
	}

	dbus_error_init (&error);
	con = dbus_g_connection_get_connection (dbus->priv->connection);
	uid = dbus_bus_get_unix_user (con, sender, &error);
	if (dbus_error_is_set (&error)) {
		g_warning ("Could not get uid for connection: %s %s", error.name, error.message);
		uid = G_MAXUINT;
		goto out;
	}
out:
	return uid;
}

/**
 * pk_dbus_get_pid:
 * @dbus: the #PkDbus instance
 * @sender: the sender, usually got from dbus_g_method_get_dbus()
 *
 * Gets the process ID.
 *
 * Return value: the PID, or %G_MAXUINT if it could not be obtained
 **/
guint
pk_dbus_get_pid (PkDbus *dbus, const gchar *sender)
{
	guint pid = G_MAXUINT;
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_DBUS (dbus), G_MAXUINT);
	g_return_val_if_fail (sender != NULL, G_MAXUINT);

	/* set in the test suite */
	if (g_strcmp0 (sender, ":org.freedesktop.PackageKit") == 0) {
		g_debug ("using self-check shortcut");
		pid = G_MAXUINT - 1;
		goto out;
	}

	/* no connection to DBus */
	if (dbus->priv->proxy_pid == NULL)
		goto out;

	/* get pid from DBus (quite slow) - TODO: cache this */
	ret = dbus_g_proxy_call (dbus->priv->proxy_pid,
				 "GetConnectionUnixProcessID", &error,
				 G_TYPE_STRING, sender,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &pid,
				 G_TYPE_INVALID);
	if (!ret) {
		g_warning ("failed to get pid: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return pid;
}

/**
 * pk_dbus_get_cmdline:
 * @dbus: the #PkDbus instance
 * @sender: the sender, usually got from dbus_g_method_get_dbus()
 *
 * Gets the command line for the ID.
 *
 * Return value: the cmdline, or %NULL if it could not be obtained
 **/
gchar *
pk_dbus_get_cmdline (PkDbus *dbus, const gchar *sender)
{
	gboolean ret;
	gchar *filename = NULL;
	gchar *cmdline = NULL;
	GError *error = NULL;
	guint pid;

	g_return_val_if_fail (PK_IS_DBUS (dbus), NULL);
	g_return_val_if_fail (sender != NULL, NULL);

	/* set in the test suite */
	if (g_strcmp0 (sender, ":org.freedesktop.PackageKit") == 0) {
		g_debug ("using self-check shortcut");
		cmdline = g_strdup ("/usr/sbin/packagekit");
		goto out;
	}

	/* get pid */
	pid = pk_dbus_get_pid (dbus, sender);
	if (pid == G_MAXUINT) {
		g_warning ("failed to get PID");
		goto out;
	}

	/* get command line from proc */
	filename = g_strdup_printf ("/proc/%i/cmdline", pid);
	ret = g_file_get_contents (filename, &cmdline, NULL, &error);
	if (!ret) {
		g_warning ("failed to get cmdline: %s", error->message);
		g_error_free (error);
	}
out:
	g_free (filename);
	return cmdline;
}

/**
 * pk_dbus_get_session:
 * @dbus: the #PkDbus instance
 * @sender: the sender, usually got from dbus_g_method_get_dbus()
 *
 * Gets the ConsoleKit session for the ID.
 *
 * Return value: the session identifier, or %NULL if it could not be obtained
 **/
gchar *
pk_dbus_get_session (PkDbus *dbus, const gchar *sender)
{
	gboolean ret;
	gchar *session = NULL;
	GError *error = NULL;
	guint pid;

	g_return_val_if_fail (PK_IS_DBUS (dbus), NULL);
	g_return_val_if_fail (sender != NULL, NULL);

	/* set in the test suite */
	if (g_strcmp0 (sender, ":org.freedesktop.PackageKit") == 0) {
		g_debug ("using self-check shortcut");
		session = g_strdup ("xxx");
		goto out;
	}

	/* no ConsoleKit? */
	if (dbus->priv->proxy_session == NULL) {
		g_warning ("no ConsoleKit, so cannot get session");
		goto out;
	}

	/* get pid */
	pid = pk_dbus_get_pid (dbus, sender);
	if (pid == G_MAXUINT) {
		g_warning ("failed to get PID");
		goto out;
	}

	/* get session from ConsoleKit (quite slow) */
	ret = dbus_g_proxy_call (dbus->priv->proxy_session,
				 "GetSessionForUnixProcess", &error,
				 G_TYPE_UINT, pid,
				 G_TYPE_INVALID,
				 DBUS_TYPE_G_OBJECT_PATH, &session,
				 G_TYPE_INVALID);
	if (!ret) {
		g_warning ("failed to get session for %i: %s", pid, error->message);
		g_error_free (error);
		goto out;
	}

out:
	return session;
}

/**
 * pk_dbus_finalize:
 **/
static void
pk_dbus_finalize (GObject *object)
{
	PkDbus *dbus;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_DBUS (object));
	dbus = PK_DBUS (object);

	g_object_unref (dbus->priv->proxy_pid);
	if (dbus->priv->proxy_session != NULL)
		g_object_unref (dbus->priv->proxy_session);

	G_OBJECT_CLASS (pk_dbus_parent_class)->finalize (object);
}

/**
 * pk_dbus_class_init:
 **/
static void
pk_dbus_class_init (PkDbusClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_dbus_finalize;

	g_type_class_add_private (klass, sizeof (PkDbusPrivate));
}

/**
 * pk_dbus_init:
 *
 * initializes the dbus class. NOTE: We expect dbus objects
 * to *NOT* be removed or added during the session.
 * We only control the first dbus object if there are more than one.
 **/
static void
pk_dbus_init (PkDbus *dbus)
{
	GError *error = NULL;
	dbus->priv = PK_DBUS_GET_PRIVATE (dbus);

	/* use the bus to get the uid */
	dbus->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, NULL);

	/* connect to DBus so we can get the pid */
	dbus->priv->proxy_pid =
		dbus_g_proxy_new_for_name_owner (dbus->priv->connection,
						 "org.freedesktop.DBus",
						 "/org/freedesktop/DBus/Bus",
						 "org.freedesktop.DBus", &error);
	if (dbus->priv->proxy_pid == NULL) {
		g_warning ("cannot connect to DBus: %s", error->message);
		g_error_free (error);
	}

	/* use ConsoleKit to get the session */
	dbus->priv->proxy_session =
		dbus_g_proxy_new_for_name_owner (dbus->priv->connection,
						 "org.freedesktop.ConsoleKit",
						 "/org/freedesktop/ConsoleKit/Manager",
						 "org.freedesktop.ConsoleKit.Manager", &error);
	if (dbus->priv->proxy_session == NULL) {
		g_warning ("cannot connect to DBus: %s", error->message);
		g_error_free (error);
	}
}

/**
 * pk_dbus_new:
 * Return value: A new dbus class instance.
 **/
PkDbus *
pk_dbus_new (void)
{
	if (pk_dbus_object != NULL) {
		g_object_ref (pk_dbus_object);
	} else {
		pk_dbus_object = g_object_new (PK_TYPE_DBUS, NULL);
		g_object_add_weak_pointer (pk_dbus_object, &pk_dbus_object);
	}
	return PK_DBUS (pk_dbus_object);
}

