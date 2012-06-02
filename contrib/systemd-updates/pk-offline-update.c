/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
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

#include <glib.h>
#include <glib/gstdio.h>
#include <packagekit-glib2/packagekit.h>
#include <stdlib.h>

/**
 * pk_offline_update_set_boot_msg:
 **/
static void
pk_offline_update_set_boot_msg (const gchar *msg)
{
	gboolean ret;
	gchar *cmd;
	GError *error = NULL;

	cmd = g_strdup_printf ("plymouth display-message --text=\"%s\"", msg);
	ret = g_spawn_command_line_sync (cmd, NULL, NULL, NULL, &error);
	if (!ret) {
		g_warning ("failed to display message on splash: %s",
			   error->message);
		g_error_free (error);
		error = NULL;
	}
	g_free (cmd);
}

/**
 * pk_offline_update_pause_progress:
 **/
static void
pk_offline_update_pause_progress (void)
{
	gboolean ret;
	GError *error = NULL;

	ret = g_spawn_command_line_async ("plymouth pause-progress", &error);
	if (!ret) {
		g_warning ("failed to pause progress for splash: %s",
			   error->message);
		g_error_free (error);
	}
}

/**
 * pk_offline_update_progress_cb:
 **/
static void
pk_offline_update_progress_cb (PkProgress *progress,
			       PkProgressType type,
			       gpointer user_data)
{
	gchar *msg = NULL;
	gint percentage;

	if (type != PK_PROGRESS_TYPE_PERCENTAGE)
		goto out;
	g_object_get (progress, "percentage", &percentage, NULL);
	if (percentage < 0)
		goto out;

	/* print on terminal */
	g_print ("Offline update process %i%% complete\n", percentage);

	/* update plymouth */
	msg = g_strdup_printf ("Update process %i%% complete", percentage);
	pk_offline_update_set_boot_msg (msg);
out:
	g_free (msg);
}

/**
 * pk_offline_update_reboot:
 **/
static void
pk_offline_update_reboot (void)
{
	GDBusConnection *connection;
	GError *error = NULL;
	GVariant *val = NULL;

	/* reboot using systemd */
	pk_offline_update_set_boot_msg ("Rebooting after installing updates...");
	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (connection == NULL) {
		g_warning ("Failed to get system bus connection: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}
	val = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.systemd1",
					   "/org/freedesktop/systemd1",
					   "org.freedesktop.systemd1.Manager",
					   "Reboot",
					   NULL,
					   NULL,
					   G_DBUS_CALL_FLAGS_NONE,
					   -1,
					   NULL,
					   &error);
	if (val == NULL) {
		g_warning ("Failed to reboot: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (connection != NULL)
		g_object_unref (connection);
	if (val != NULL)
		g_variant_unref (val);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GError *error = NULL;
	gint retval;
	PkTask *task = NULL;
	PkResults *results;

	/* setup */
	g_type_init ();

	/* ensure root user */
	if (getuid () != 0 || geteuid () != 0) {
		retval = EXIT_FAILURE;
		g_warning ("This program can only be used using root");
		goto out;
	}

	/* just update the system */
	task = pk_task_new ();
	pk_task_set_interactive (task, FALSE);
	pk_offline_update_pause_progress ();
	results = pk_client_update_system (PK_CLIENT (task),
					   0,
					   NULL, /* GCancellable */
					   pk_offline_update_progress_cb,
					   NULL, /* user_data */
					   &error);
	if (results == NULL) {
		retval = EXIT_FAILURE;
		g_warning ("failed to update system: %s", error->message);
		g_error_free (error);
		goto out;
	}
	retval = EXIT_SUCCESS;
out:
	g_unlink ("/system-update");
	pk_offline_update_reboot ();
	if (task != NULL)
		g_object_unref (task);
	return retval;
}
