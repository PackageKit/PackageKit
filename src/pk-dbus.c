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

#include <config.h>

#include <stdlib.h>
#include <glib.h>
#include <gio/gio.h>

#ifdef HAVE_SYSTEMD_SD_LOGIN_H
 #include <systemd/sd-login.h>
#endif

#include "pk-dbus.h"

#define PK_DBUS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_DBUS, PkDbusPrivate))

struct PkDbusPrivate
{
	GDBusConnection		*connection;
	GDBusProxy		*proxy_pid;
	GDBusProxy		*proxy_uid;
	GDBusProxy		*proxy_session;
};

static gpointer pk_dbus_object = NULL;

G_DEFINE_TYPE (PkDbus, pk_dbus, G_TYPE_OBJECT)

/**
 * pk_dbus_get_uid:
 * @dbus: the #PkDbus instance
 * @sender: the sender
 *
 * Gets the process UID.
 *
 * Return value: the UID, or %G_MAXUINT if it could not be obtained
 **/
guint
pk_dbus_get_uid (PkDbus *dbus, const gchar *sender)
{
	guint uid = G_MAXUINT;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) value = NULL;

	g_return_val_if_fail (PK_IS_DBUS (dbus), G_MAXUINT);
	g_return_val_if_fail (sender != NULL, G_MAXUINT);

	/* set in the test suite */
	if (g_strcmp0 (sender, ":org.freedesktop.PackageKit") == 0) {
		g_debug ("using self-check shortcut");
		return 500;
	}
	value = g_dbus_proxy_call_sync (dbus->priv->proxy_uid,
					"GetConnectionUnixUser",
					g_variant_new ("(s)",
						       sender),
					G_DBUS_CALL_FLAGS_NONE,
					2000,
					NULL,
					&error);
	if (value == NULL) {
		g_warning ("Failed to get uid for %s: %s",
			   sender, error->message);
		return G_MAXUINT;
	}
	g_variant_get (value, "(u)", &uid);
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
static guint
pk_dbus_get_pid (PkDbus *dbus, const gchar *sender)
{
	guint pid = G_MAXUINT;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) value = NULL;

	g_return_val_if_fail (PK_IS_DBUS (dbus), G_MAXUINT);
	g_return_val_if_fail (sender != NULL, G_MAXUINT);

	/* set in the test suite */
	if (g_strcmp0 (sender, ":org.freedesktop.PackageKit") == 0) {
		g_debug ("using self-check shortcut");
		return G_MAXUINT - 1;
	}

	/* no connection to DBus */
	if (dbus->priv->proxy_pid == NULL)
		return G_MAXUINT;

	/* get pid from DBus */
	value = g_dbus_proxy_call_sync (dbus->priv->proxy_pid,
					"GetConnectionUnixProcessID",
					g_variant_new ("(s)",
						       sender),
					G_DBUS_CALL_FLAGS_NONE,
					2000,
					NULL,
					&error);
	if (value == NULL) {
		g_warning ("Failed to get pid for %s: %s",
			   sender, error->message);
		return G_MAXUINT;
	}
	g_variant_get (value, "(u)", &pid);
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
	gchar *cmdline = NULL;
	guint pid;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *filename = NULL;

	g_return_val_if_fail (PK_IS_DBUS (dbus), NULL);
	g_return_val_if_fail (sender != NULL, NULL);

	/* set in the test suite */
	if (g_strcmp0 (sender, ":org.freedesktop.PackageKit") == 0) {
		g_debug ("using self-check shortcut");
		return g_strdup ("/usr/sbin/packagekit");
	}

	/* get pid */
	pid = pk_dbus_get_pid (dbus, sender);
	if (pid == G_MAXUINT) {
		g_warning ("failed to get PID");
		return NULL;
	}

	/* get command line from proc */
	filename = g_strdup_printf ("/proc/%i/cmdline", pid);
	ret = g_file_get_contents (filename, &cmdline, NULL, &error);
	if (!ret)
		g_warning ("failed to get cmdline: %s", error->message);
	return cmdline;
}

#ifdef HAVE_SYSTEMD_SD_LOGIN_H
static gchar *
pk_dbus_make_logind_session_id (const gchar *session)
{
	g_assert (session != NULL);
	return g_strdup_printf ("/org/freedesktop/logind/session-%s", session);
}

static gchar *
pk_dbus_get_session_systemd (guint pid)
{
	g_autofree gchar *session_id = NULL;
	uid_t uid;

	/* do process -> pid -> same session */
	if (sd_pid_get_session (pid, &session_id) >= 0)
		return pk_dbus_make_logind_session_id (session_id);

	/* do process -> uid -> graphical session */
	if (sd_pid_get_owner_uid (pid, &uid) < 0)
		return NULL;
	if (sd_uid_get_display (uid, &session_id) >= 0)
		return pk_dbus_make_logind_session_id (session_id);

	return NULL;
}
#endif

/**
 * pk_dbus_get_session:
 * @dbus: the #PkDbus instance
 * @sender: the sender, usually got from dbus_g_method_get_dbus()
 *
 * Gets the logind or ConsoleKit session for the ID.
 *
 * Return value: the session identifier, or %NULL if it could not be obtained
 **/
gchar *
pk_dbus_get_session (PkDbus *dbus, const gchar *sender)
{
	gchar *session = NULL;
#ifndef HAVE_SYSTEMD_SD_LOGIN_H
	g_autoptr(GError) error = NULL;
#endif
	guint pid;
	g_autoptr(GVariant) value = NULL;

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

	/* get session from systemd or ConsoleKit */
#ifdef HAVE_SYSTEMD_SD_LOGIN_H
	session = pk_dbus_get_session_systemd (pid);
	if (session == NULL)
		g_warning ("failed to get session for pid %u", pid);
#else
	/* get session from ConsoleKit */
	value = g_dbus_proxy_call_sync (dbus->priv->proxy_session,
					"GetSessionForUnixProcess",
					g_variant_new ("(u)",
						       pid),
					G_DBUS_CALL_FLAGS_NONE,
					2000,
					NULL,
					&error);
	if (value == NULL) {
		g_warning ("Failed to get session for %s: %s",
			   sender, error->message);
		goto out;
	}
	g_variant_get (value, "(o)", &session);
#endif
out:
	return session;
}

static void
pk_dbus_finalize (GObject *object)
{
	PkDbus *dbus;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_DBUS (object));
	dbus = PK_DBUS (object);

	if (dbus->priv->proxy_pid != NULL)
		g_object_unref (dbus->priv->proxy_pid);
	if (dbus->priv->proxy_uid != NULL)
		g_object_unref (dbus->priv->proxy_uid);
	if (dbus->priv->proxy_session != NULL)
		g_object_unref (dbus->priv->proxy_session);

	G_OBJECT_CLASS (pk_dbus_parent_class)->finalize (object);
}

static void
pk_dbus_class_init (PkDbusClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_dbus_finalize;

	g_type_class_add_private (klass, sizeof (PkDbusPrivate));
}

gboolean
pk_dbus_connect (PkDbus *dbus, GError **error)
{
	if (dbus->priv->connection != NULL)
		return TRUE;

	/* use the bus to get the uid */
	dbus->priv->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM,
						 NULL, error);
	if (dbus->priv->connection == NULL) {
		g_prefix_error (error, "cannot connect to the system bus: ");
		return FALSE;
	}

	/* connect to DBus so we can get the pid */
	dbus->priv->proxy_pid =
		g_dbus_proxy_new_sync (dbus->priv->connection,
				       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
				       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
				       NULL,
				       "org.freedesktop.DBus",
				       "/org/freedesktop/DBus/Bus",
				       "org.freedesktop.DBus",
				       NULL,
				       error);
	if (dbus->priv->proxy_pid == NULL) {
		g_prefix_error (error, "cannot connect to DBus: ");
		return FALSE;
	}

	/* connect to DBus so we can get the uid */
	dbus->priv->proxy_uid =
		g_dbus_proxy_new_sync (dbus->priv->connection,
				       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
				       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
				       NULL,
				       "org.freedesktop.DBus",
				       "/org/freedesktop/DBus",
				       "org.freedesktop.DBus",
				       NULL,
				       error);
	if (dbus->priv->proxy_uid == NULL) {
		g_prefix_error (error, "cannot connect to DBus: ");
		return FALSE;
	}

	/* use ConsoleKit to get the session */
	dbus->priv->proxy_session =
		g_dbus_proxy_new_sync (dbus->priv->connection,
				       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
				       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
				       NULL,
				       "org.freedesktop.ConsoleKit",
				       "/org/freedesktop/ConsoleKit/Manager",
				       "org.freedesktop.ConsoleKit.Manager",
				       NULL,
				       error);
	if (dbus->priv->proxy_session == NULL) {
		g_prefix_error (error, "cannot connect to DBus: ");
		return FALSE;
	}

	/* success */
	return TRUE;
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
	dbus->priv = PK_DBUS_GET_PRIVATE (dbus);
}

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

