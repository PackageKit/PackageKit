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

static void
pk_offline_update_progress_cb (PkProgress *progress,
			       PkProgressType type,
			       gpointer user_data)
{
	gboolean ret;
	gchar *cmd = NULL;
	GError *error = NULL;
	guint percentage;

	if (type != PK_PROGRESS_TYPE_PERCENTAGE)
		return;
	g_object_get (progress, "percentage", &percentage, NULL);
	g_print ("Update process %i%% complete\n", percentage);

	/* update plymouth */
	if (percentage > 0) {
		cmd = g_strdup_printf ("plymouth display-message --text=\"Update process %i%% complete\"",
				       percentage);
		ret = g_spawn_command_line_async (cmd, &error);
		if (!ret) {
			g_warning ("failed to spawn plymouth: %s",
				   error->message);
			g_error_free (error);
		}
	}
	g_free (cmd);
}

static void
pk_offline_update_reboot (void)
{
	GDBusConnection *connection;
	GError *error = NULL;
	GVariant *val = NULL;

	/* reboot using systemd */
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
