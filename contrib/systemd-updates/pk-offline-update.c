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
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <packagekit-glib2/packagekit.h>
#include <packagekit-glib2/packagekit-private.h>
#include <stdlib.h>
#include <unistd.h>

#define PK_OFFLINE_UPDATE_RESULTS_GROUP		"PackageKit Offline Update Results"
#define PK_OFFLINE_UPDATE_TRIGGER_FILENAME	"/system-update"
#define PK_OFFLINE_UPDATE_RESULTS_FILENAME	"/var/lib/PackageKit/offline-update-competed"
#define PK_OFFLINE_UPDATE_LOG_DEBUG		"/var/log/PackageKit-offline-update"
#define PK_OFFLINE_PREPARED_UPDATE_FILENAME	"/var/lib/PackageKit/prepared-update"

typedef struct {
	PkProgressBar	*progressbar;
	gint64		 time_started;
	GString		*log;
} PkOfflineUpdateHelper;

/**
 * pk_log_write:
 **/
static void
pk_log_write (PkOfflineUpdateHelper *helper)
{
	gboolean ret;
	GError *error = NULL;

	ret = g_file_set_contents (PK_OFFLINE_UPDATE_LOG_DEBUG,
				   helper->log->str,
				   helper->log->len,
				   &error);
	if (!ret) {
		g_warning ("failed to save log: %s", error->message);
		g_error_free (error);
	}
}

/**
 * pk_log_raw:
 **/
static void
pk_log_raw (PkOfflineUpdateHelper *helper, const gchar *buffer)
{
	gint delta_ms = (g_get_real_time () - helper->time_started) / 1000;
	g_string_append_printf (helper->log,
				"%07" G_GINT64_FORMAT "ms\t%s\n",
				delta_ms, buffer);
}

/**
 * pk_warning:
 **/
G_GNUC_PRINTF(2,3)
static void
pk_log_warning (PkOfflineUpdateHelper *helper, const gchar *format, ...)
{
	gchar *buffer = NULL;
	gchar *tmp;
	va_list args;

	va_start (args, format);
	g_vasprintf (&buffer, format, args);
	va_end (args);

	tmp = g_strdup_printf ("WARNING: %s", buffer);
	pk_log_raw (helper, tmp);

	g_free (tmp);
	g_free (buffer);
}

/**
 * pk_info:
 **/
G_GNUC_PRINTF(2,3)
static void
pk_log_info (PkOfflineUpdateHelper *helper, const gchar *format, ...)
{
	gchar *buffer = NULL;
	gchar *tmp;
	va_list args;

	va_start (args, format);
	g_vasprintf (&buffer, format, args);
	va_end (args);

	tmp = g_strdup_printf ("INFO: %s", buffer);
	pk_log_raw (helper, tmp);

	g_free (tmp);
	g_free (buffer);
}

/**
 * pk_offline_update_set_plymouth_msg:
 **/
static void
pk_offline_update_set_plymouth_msg (PkOfflineUpdateHelper *helper, const gchar *msg)
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
		pk_log_warning (helper, "failed to display message '%s' on splash: %s",
				msg, error->message);
		g_error_free (error);
	} else {
		pk_log_info (helper, "sent text to plymouth '%s'", msg);
	}
	g_free (cmd);
}

/**
 * pk_offline_update_set_plymouth_mode:
 **/
static void
pk_offline_update_set_plymouth_mode (PkOfflineUpdateHelper *helper, const gchar *mode)
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
		pk_log_warning (helper, "failed to change mode for splash: %s",
				error->message);
		g_error_free (error);
	} else {
		pk_log_info (helper, "sent text to plymouth '%s'", mode);
	}
	g_free (cmdline);
}

/**
 * pk_offline_update_set_plymouth_percentage:
 **/
static void
pk_offline_update_set_plymouth_percentage (PkOfflineUpdateHelper *helper,
					   guint percentage)
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
		pk_log_warning (helper, "failed to set percentage for splash: %s",
				error->message);
		g_error_free (error);
	} else {
		pk_log_info (helper, "sent percentage to plymouth %i%%", percentage);
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
	PkInfoEnum info;
	PkOfflineUpdateHelper *helper = (PkOfflineUpdateHelper *) user_data;
	PkPackage *pkg = NULL;
	PkStatusEnum status;
	gchar *msg = NULL;
	gint percentage;

	switch (type) {
	case PK_PROGRESS_TYPE_ROLE:
		pk_progress_bar_start (helper->progressbar, "Updating system");
		pk_log_info (helper, "assigned role");
		break;
	case PK_PROGRESS_TYPE_PACKAGE:
		g_object_get (progress, "package", &pkg, NULL);
		info = pk_package_get_info (pkg);
		if (info == PK_INFO_ENUM_UPDATING) {
			msg = g_strdup_printf ("Updating %s",
					       pk_package_get_name (pkg));
			pk_progress_bar_start (helper->progressbar, msg);
		} else if (info == PK_INFO_ENUM_INSTALLING) {
			msg = g_strdup_printf ("Installing %s",
					       pk_package_get_name (pkg));
			pk_progress_bar_start (helper->progressbar, msg);
		} else if (info == PK_INFO_ENUM_REMOVING) {
			msg = g_strdup_printf ("Removing %s",
					       pk_package_get_name (pkg));
			pk_progress_bar_start (helper->progressbar, msg);
		}
		pk_log_info (helper,
			     "package %s\t%s",
			     pk_info_enum_to_string (info),
			     pk_package_get_name (pkg));
		break;

	case PK_PROGRESS_TYPE_PERCENTAGE:
		g_object_get (progress, "percentage", &percentage, NULL);
		if (percentage < 0)
			goto out;
		pk_log_info (helper, "percentage %i%%", percentage);

		/* TRANSLATORS: this is the message we send plymouth to
		 * advise of the new percentage completion */
		msg = g_strdup_printf ("%s - %i%%", _("Installing Updates"), percentage);
		if (percentage > 10)
			pk_offline_update_set_plymouth_msg (helper, msg);

		/* print on terminal */
		pk_progress_bar_set_percentage (helper->progressbar, percentage);

		/* update plymouth */
		pk_offline_update_set_plymouth_percentage (helper, percentage);
		break;

	case PK_PROGRESS_TYPE_STATUS:
		g_object_get (progress, "status", &status, NULL);
		pk_log_info (helper, "status %s",
			     pk_status_enum_to_string (status));
	}

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
pk_offline_update_reboot (PkOfflineUpdateHelper *helper)
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
	pk_offline_update_set_plymouth_mode (helper, "shutdown");
	/* TRANSLATORS: we've finished doing offline updates */
	pk_offline_update_set_plymouth_msg (helper, _("Rebooting after installing updates…"));
	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (connection == NULL) {
		pk_log_warning (helper, "Failed to get system bus connection: %s",
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
		pk_log_warning (helper, "Failed to reboot: %s", error->message);
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
pk_offline_update_write_error (PkOfflineUpdateHelper *helper, const GError *error)
{
	gboolean ret;
	gchar *data = NULL;
	GError *error_local = NULL;
	GKeyFile *key_file;
	PkErrorEnum error_enum = PK_ERROR_ENUM_UNKNOWN;

	/* just write what we've got */
	pk_log_info (helper, "writing error");
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
		pk_log_warning (helper, "failed to get keyfile data: %s",
				error_local->message);
		g_error_free (error_local);
		goto out;
	}
	ret = g_file_set_contents (PK_OFFLINE_UPDATE_RESULTS_FILENAME,
				   data,
				   -1,
				   &error_local);
	if (!ret) {
		pk_log_warning (helper, "failed to write file: %s",
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
pk_offline_update_write_results (PkOfflineUpdateHelper *helper, PkResults *results)
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

	pk_log_info (helper, "writing actual results");
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
		pk_log_warning (helper, "failed to get keyfile data: %s",
				error->message);
		g_error_free (error);
		goto out;
	}
	ret = g_file_set_contents (PK_OFFLINE_UPDATE_RESULTS_FILENAME,
				   data,
				   -1,
				   &error);
	if (!ret) {
		pk_log_warning (helper, "failed to write file: %s", error->message);
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
pk_offline_update_write_dummy_results (PkOfflineUpdateHelper *helper, gchar **package_ids)
{
	gboolean ret;
	gchar *data = NULL;
	GError *error = NULL;
	GKeyFile *key_file;
	GString *string;
	guint i;

	pk_log_info (helper, "writing dummy results");
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
		pk_log_warning (helper, "failed to get keyfile data: %s",
				error->message);
		g_error_free (error);
		goto out;
	}
	ret = g_file_set_contents (PK_OFFLINE_UPDATE_RESULTS_FILENAME,
				   data,
				   -1,
				   &error);
	if (!ret) {
		pk_log_warning (helper, "failed to write dummy %s: %s",
				PK_OFFLINE_UPDATE_RESULTS_FILENAME,
				error->message);
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
 * pk_offline_update_loop_quit_cb:
 **/
static gboolean
pk_offline_update_loop_quit_cb (gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	g_main_loop_quit (loop);
	return FALSE;
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
	GMainLoop *loop = NULL;
	PkResults *results = NULL;
	PkTask *task = NULL;
	PkOfflineUpdateHelper *helper = NULL;

#if (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 35)
	g_type_init ();
#endif

	/* ensure root user */
	if (getuid () != 0 || geteuid () != 0) {
		retval = EXIT_FAILURE;
		g_print ("This program can only be used using root\n");
		goto out;
	}

	/* always do this first to avoid a loop if this tool segfaults */
	g_unlink (PK_OFFLINE_UPDATE_TRIGGER_FILENAME);

	/* create a private helper */
	helper = g_new0 (PkOfflineUpdateHelper, 1);
	helper->log = g_string_new ("started\n");
	helper->time_started = g_get_real_time ();

	/* get the list of packages to update */
	ret = g_file_get_contents (PK_OFFLINE_PREPARED_UPDATE_FILENAME,
				   &packages_data,
				   NULL,
				   &error);
	if (!ret) {
		retval = EXIT_FAILURE;
		pk_log_warning (helper, "failed to read %s: %s",
				PK_OFFLINE_PREPARED_UPDATE_FILENAME,
				error->message);
		g_error_free (error);
		goto out;
	}

	/* use a progress bar when the user presses <esc> in plymouth */
	helper->progressbar = pk_progress_bar_new ();
	pk_progress_bar_set_size (helper->progressbar, 25);
	pk_progress_bar_set_padding (helper->progressbar, 30);

	/* just update the system */
	task = pk_task_new ();
	pk_task_set_interactive (task, FALSE);
	pk_offline_update_set_plymouth_mode (helper, "updates");
	/* TRANSLATORS: we've started doing offline updates */
	pk_offline_update_set_plymouth_msg (helper, _("Installing updates, this could take a while…"));
	package_ids = g_strsplit (packages_data, "\n", -1);
	pk_offline_update_write_dummy_results (helper, package_ids);
	results = pk_client_update_packages (PK_CLIENT (task),
					     0,
					     package_ids,
					     NULL, /* GCancellable */
					     pk_offline_update_progress_cb,
					     helper, /* user_data */
					     &error);
	if (results == NULL) {
		retval = EXIT_FAILURE;
		pk_offline_update_write_error (helper, error);
		pk_log_warning (helper, "failed to update system: %s", error->message);
		g_error_free (error);
		goto out;
	}
	pk_progress_bar_end (helper->progressbar);
	pk_offline_update_write_results (helper, results);

	/* delete prepared-update file if it's not already been done by the
	 * pk-plugin-systemd-update daemon plugin */
	file = g_file_new_for_path (PK_OFFLINE_PREPARED_UPDATE_FILENAME);
	ret = g_file_delete (file, NULL, &error);
	if (!ret) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			retval = EXIT_FAILURE;
			pk_log_warning (helper, "failed to delete %s: %s",
					PK_OFFLINE_PREPARED_UPDATE_FILENAME,
					error->message);
			g_error_free (error);
			goto out;
		}
		g_clear_error (&error);
	}

	retval = EXIT_SUCCESS;
out:
	/* if we failed, we pause to show any error on the screen */
	if (retval != EXIT_SUCCESS) {
		loop = g_main_loop_new (NULL, FALSE);
		g_timeout_add_seconds (10, pk_offline_update_loop_quit_cb, loop);
		g_main_loop_run (loop);
	}
	pk_log_info (helper, "rebooting");
	pk_log_write (helper);
	pk_offline_update_reboot (helper);
	g_free (packages_data);
	g_strfreev (package_ids);
	if (helper != NULL) {
		g_string_free (helper->log, TRUE);
		if (helper->progressbar != NULL)
			g_object_unref (helper->progressbar);
		g_free (helper);
	}
	if (file != NULL)
		g_object_unref (file);
	if (results != NULL)
		g_object_unref (results);
	if (task != NULL)
		g_object_unref (task);
	if (loop != NULL)
		g_main_loop_unref (loop);
	return retval;
}
