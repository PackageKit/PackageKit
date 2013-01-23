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
#include <gio/gio.h>
#include <packagekit-glib2/packagekit.h>
#include <packagekit-glib2/packagekit-private.h>
#include <stdlib.h>
#include <unistd.h>

#define PK_OFFLINE_UPDATE_RESULTS_GROUP		"PackageKit Offline Update Results"
#define PK_OFFLINE_UPDATE_TRIGGER_FILENAME	"/system-update"
#define PK_OFFLINE_UPDATE_RESULTS_FILENAME	"/var/lib/PackageKit/offline-update-competed"
#define PK_OFFLINE_PREPARED_UPDATE_FILENAME	"/var/lib/PackageKit/prepared-update"

/**
 * pk_offline_update_set_plymouth_msg:
 **/
static void
pk_offline_update_set_plymouth_msg (const gchar *msg)
{
	gboolean ret;
	gchar *cmd;
	GError *error = NULL;

	/* allow testing without sending commands to plymouth */
	if (g_getenv ("PK_OFFLINE_UPDATE_TEST") != NULL)
		return;
	cmd = g_strdup_printf ("plymouth display-message --text=\"%s\"", msg);
	ret = g_spawn_command_line_async (cmd, &error);
	if (!ret) {
		g_warning ("failed to display message on splash: %s",
			   error->message);
		g_error_free (error);
		error = NULL;
	}
	g_free (cmd);
}

/**
 * pk_offline_update_set_plymouth_mode:
 **/
static void
pk_offline_update_set_plymouth_mode (const gchar *mode)
{
	gboolean ret;
	GError *error = NULL;
	gchar *cmdline;

	/* allow testing without sending commands to plymouth */
	if (g_getenv ("PK_OFFLINE_UPDATE_TEST") != NULL)
		return;
	cmdline = g_strdup_printf ("plymouth change-mode --%s", mode);
	ret = g_spawn_command_line_async (cmdline, &error);
	if (!ret) {
		g_warning ("failed to change mode for splash: %s",
			   error->message);
		g_error_free (error);
	}
	g_free (cmdline);
}

/**
 * pk_offline_update_set_plymouth_percentage:
 **/
static void
pk_offline_update_set_plymouth_percentage (guint percentage)
{
	gboolean ret;
	GError *error = NULL;
	gchar *cmdline;

	/* allow testing without sending commands to plymouth */
	if (g_getenv ("PK_OFFLINE_UPDATE_TEST") != NULL)
		return;
	cmdline = g_strdup_printf ("plymouth system-update --progress=%i",
				   percentage);
	ret = g_spawn_command_line_async (cmdline, &error);
	if (!ret) {
		g_warning ("failed to set percentage for splash: %s",
			   error->message);
		g_error_free (error);
	}
	g_free (cmdline);
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
	PkInfoEnum info;
	PkPackage *pkg = NULL;
	PkProgressBar *progressbar = PK_PROGRESS_BAR (user_data);

	switch (type) {
	case PK_PROGRESS_TYPE_ROLE:
		pk_progress_bar_start (progressbar, "Updating system");
		break;
	case PK_PROGRESS_TYPE_PACKAGE:
		g_object_get (progress, "package", &pkg, NULL);
		info = pk_package_get_info (pkg);
		if (info == PK_INFO_ENUM_UPDATING) {
			msg = g_strdup_printf ("Updating %s",
					       pk_package_get_name (pkg));
			pk_progress_bar_start (progressbar, msg);
		} else if (info == PK_INFO_ENUM_INSTALLING) {
			msg = g_strdup_printf ("Installing %s",
					       pk_package_get_name (pkg));
			pk_progress_bar_start (progressbar, msg);
		} else if (info == PK_INFO_ENUM_REMOVING) {
			msg = g_strdup_printf ("Removing %s",
					       pk_package_get_name (pkg));
			pk_progress_bar_start (progressbar, msg);
		}
		break;
	case PK_PROGRESS_TYPE_PERCENTAGE:
		g_object_get (progress, "percentage", &percentage, NULL);
		if (percentage < 0)
			goto out;

		/* print on terminal */
		pk_progress_bar_set_percentage (progressbar, percentage);

		/* update plymouth */
		pk_offline_update_set_plymouth_percentage (percentage);
		break;
	default:
		break;
	}
out:
	if (pkg != NULL)
		g_object_unref (pkg);
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

	/* allow testing without rebooting */
	if (g_getenv ("PK_OFFLINE_UPDATE_TEST") != NULL) {
		g_print ("TESTING, so not rebooting\n");
		return;
	}

	/* reboot using systemd */
	pk_offline_update_set_plymouth_mode ("shutdown");
	pk_offline_update_set_plymouth_msg ("Rebooting after installing updates...");
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
		for (i = 0; i < packages->len; i++) {
			package = g_ptr_array_index (packages, i);
			switch (pk_package_get_info (package)) {
			case PK_INFO_ENUM_UPDATING:
			case PK_INFO_ENUM_INSTALLING:
				g_string_append_printf (string, "%s,",
							pk_package_get_id (package));
				break;
			default:
				break;
			}
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
 * pk_offline_update_write_dummy_results:
 *
 * If the transaction crashes, the kernel oopses or we loose power
 * during the transaction then we never get a chance to write the actual
 * transaction success / failure file.
 *
 * Write a dummy file so at least the user gets notified that something
 * bad happened.
 **/
static void
pk_offline_update_write_dummy_results (gchar **package_ids)
{
	gboolean ret;
	gchar *data = NULL;
	GError *error = NULL;
	GKeyFile *key_file;
	GString *string;
	guint i;

	key_file = g_key_file_new ();
	g_key_file_set_boolean (key_file,
				PK_OFFLINE_UPDATE_RESULTS_GROUP,
				"Success",
				FALSE);
	g_key_file_set_string (key_file,
			       PK_OFFLINE_UPDATE_RESULTS_GROUP,
			       "ErrorCode",
			       pk_error_enum_to_string (PK_ERROR_ENUM_FAILED_INITIALIZATION));
	g_key_file_set_string (key_file,
			       PK_OFFLINE_UPDATE_RESULTS_GROUP,
			       "ErrorDetails",
			       "The transaction did not complete");

	/* save packages if any set */
	string = g_string_new ("");
	for (i = 0; package_ids[i] != NULL; i++)
		g_string_append_printf (string, "%s,", package_ids[i]);
	if (string->len > 0)
		g_string_set_size (string, string->len - 1); {
		g_key_file_set_string (key_file,
				       PK_OFFLINE_UPDATE_RESULTS_GROUP,
				       "Packages",
				       string->str);
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

	/* ensure this is written to disk */
	sync ();
out:
	g_string_free (string, TRUE);
	g_key_file_free (key_file);
	g_free (data);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean ret;
	gchar **package_ids = NULL;
	gchar *packages_data = NULL;
	GError *error = NULL;
	GFile *file = NULL;
	gint retval;
	PkResults *results;
	PkTask *task = NULL;
	PkProgressBar *progressbar = NULL;

#if (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 35)
	g_type_init ();
#endif

	/* ensure root user */
	if (getuid () != 0 || geteuid () != 0) {
		retval = EXIT_FAILURE;
		g_warning ("This program can only be used using root");
		goto out;
	}

	/* always do this first to avoid a loop if this tool segfaults */
	g_unlink (PK_OFFLINE_UPDATE_TRIGGER_FILENAME);

	/* get the list of packages to update */
	ret = g_file_get_contents (PK_OFFLINE_PREPARED_UPDATE_FILENAME,
				   &packages_data,
				   NULL,
				   &error);
	if (!ret) {
		retval = EXIT_FAILURE;
		g_warning ("failed to read: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* use a progress bar when the user presses <esc> in plymouth */
	progressbar = pk_progress_bar_new ();
	pk_progress_bar_set_size (progressbar, 25);
	pk_progress_bar_set_padding (progressbar, 30);

	/* just update the system */
	task = pk_task_new ();
	pk_task_set_interactive (task, FALSE);
	pk_offline_update_set_plymouth_mode ("updates");
	package_ids = g_strsplit (packages_data, "\n", -1);
	pk_offline_update_write_dummy_results (package_ids);
	results = pk_client_update_packages (PK_CLIENT (task),
					     0,
					     package_ids,
					     NULL, /* GCancellable */
					     pk_offline_update_progress_cb,
					     progressbar, /* user_data */
					     &error);
	if (results == NULL) {
		retval = EXIT_FAILURE;
		pk_offline_update_write_error (error);
		g_warning ("failed to update system: %s", error->message);
		g_error_free (error);
		goto out;
	}
	pk_progress_bar_end (progressbar);
	pk_offline_update_write_results (results);

	/* delete prepared-update file */
	file = g_file_new_for_path (PK_OFFLINE_PREPARED_UPDATE_FILENAME);
	ret = g_file_delete (file, NULL, &error);
	if (!ret) {
		retval = EXIT_FAILURE;
		g_warning ("failed to delete %s: %s",
			   PK_OFFLINE_PREPARED_UPDATE_FILENAME,
			   error->message);
		g_error_free (error);
		goto out;
	}

	retval = EXIT_SUCCESS;
out:
	pk_offline_update_reboot ();
	g_free (packages_data);
	g_strfreev (package_ids);
	if (progressbar != NULL)
		g_object_unref (progressbar);
	if (file != NULL)
		g_object_unref (file);
	if (task != NULL)
		g_object_unref (task);
	return retval;
}
