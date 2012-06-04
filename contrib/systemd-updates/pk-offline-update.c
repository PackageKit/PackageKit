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

#define PK_OFFLINE_UPDATE_RESULTS_GROUP		"PackageKit Offline Update Results"
#define PK_OFFLINE_UPDATE_RESULTS_FILENAME	"/var/lib/PackageKit/offline-update-competed"

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
 * pk_offline_update_write_error:
 **/
static void
pk_offline_update_write_error (const GError *error)
{
	gboolean ret;
	gchar *data = NULL;
	GError *error_local = NULL;
	GKeyFile *key_file;
	PkErrorEnum error_enum = PK_ERROR_ENUM_UNKNOWN;

	/* just write what we've got */
	key_file = g_key_file_new ();
	g_key_file_set_boolean (key_file,
				PK_OFFLINE_UPDATE_RESULTS_GROUP,
				"Success",
				FALSE);
	g_key_file_set_string (key_file,
			       PK_OFFLINE_UPDATE_RESULTS_GROUP,
			       "ErrorDetails",
			       error->message);
	if (error->code >= 0xff)
		error_enum = error->code - 0xff;
	if (error_enum != PK_ERROR_ENUM_UNKNOWN) {
		g_key_file_set_string (key_file,
				       PK_OFFLINE_UPDATE_RESULTS_GROUP,
				       "ErrorCode",
				       pk_error_enum_to_string (error_enum));
	}

	/* write file */
	data = g_key_file_to_data (key_file, NULL, &error_local);
	if (data == NULL) {
		g_warning ("failed to get keyfile data: %s",
			   error_local->message);
		g_error_free (error_local);
		goto out;
	}
	ret = g_file_set_contents (PK_OFFLINE_UPDATE_RESULTS_FILENAME,
				   data,
				   -1,
				   &error_local);
	if (!ret) {
		g_warning ("failed to write file: %s",
			   error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	g_key_file_free (key_file);
	g_free (data);
}

/**
 * pk_offline_update_write_results:
 **/
static void
pk_offline_update_write_results (PkResults *results)
{
	gboolean ret;
	gchar *data = NULL;
	GError *error = NULL;
	GKeyFile *key_file;
	GPtrArray *packages;
	GString *string;
	guint i;
	PkError *pk_error;
	PkPackage *package;

	key_file = g_key_file_new ();
	pk_error = pk_results_get_error_code (results);
	if (pk_error != NULL) {
		g_key_file_set_boolean (key_file,
					PK_OFFLINE_UPDATE_RESULTS_GROUP,
					"Success",
					FALSE);
		g_key_file_set_string (key_file,
				       PK_OFFLINE_UPDATE_RESULTS_GROUP,
				       "ErrorCode",
				       pk_error_enum_to_string (pk_error_get_code (pk_error)));
		g_key_file_set_string (key_file,
				       PK_OFFLINE_UPDATE_RESULTS_GROUP,
				       "ErrorDetails",
				       pk_error_get_details (pk_error));
	} else {
		g_key_file_set_boolean (key_file,
					PK_OFFLINE_UPDATE_RESULTS_GROUP,
					"Success",
					TRUE);
	}

	/* save packages if any set */
	packages = pk_results_get_package_array (results);
	if (packages != NULL) {
		string = g_string_new ("");
		for (i = 0; packages->len; i++) {
			package = g_ptr_array_index (packages, i);
			g_string_append_printf (string, "%s,",
						pk_package_get_id (package));
		}
		if (string->len > 0)
			g_string_set_size (string, string->len - 1);
		g_key_file_set_string (key_file,
				       PK_OFFLINE_UPDATE_RESULTS_GROUP,
				       "Packages",
				       string->str);
		g_string_free (string, TRUE);
	}

	/* write file */
	data = g_key_file_to_data (key_file, NULL, &error);
	if (data == NULL) {
		g_warning ("failed to get keyfile data: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}
	ret = g_file_set_contents (PK_OFFLINE_UPDATE_RESULTS_FILENAME,
				   data,
				   -1,
				   &error);
	if (!ret) {
		g_warning ("failed to write file: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_key_file_free (key_file);
	g_free (data);
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
		pk_offline_update_write_error (error);
		g_warning ("failed to update system: %s", error->message);
		g_error_free (error);
		goto out;
	}
	pk_offline_update_write_results (results);
	g_unlink ("/var/lib/PackageKit/prepared-update");
	retval = EXIT_SUCCESS;
out:
	g_unlink ("/system-update");
	pk_offline_update_reboot ();
	if (task != NULL)
		g_object_unref (task);
	return retval;
}
