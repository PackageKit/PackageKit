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
#include <glib-unix.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <packagekit-glib2/packagekit.h>
#include <packagekit-glib2/packagekit-private.h>
#include <stdlib.h>
#include <unistd.h>
#include <systemd/sd-journal.h>

#include "src/pk-cleanup.h"

#define PK_OFFLINE_UPDATE_RESULTS_GROUP		"PackageKit Offline Update Results"
#define PK_OFFLINE_UPDATE_TRIGGER_FILENAME	"/system-update"
#define PK_OFFLINE_UPDATE_RESULTS_FILENAME	"/var/lib/PackageKit/offline-update-competed"
#define PK_OFFLINE_UPDATE_ACTION_FILENAME	"/var/lib/PackageKit/offline-update-action"
#define PK_OFFLINE_PREPARED_UPDATE_FILENAME	"/var/lib/PackageKit/prepared-update"

/**
 * pk_offline_update_set_plymouth_msg:
 **/
static void
pk_offline_update_set_plymouth_msg (const gchar *msg)
{
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *cmdline = NULL;

	/* allow testing without sending commands to plymouth */
	if (g_getenv ("PK_OFFLINE_UPDATE_TEST") != NULL)
		return;
	cmdline = g_strdup_printf ("plymouth display-message --text=\"%s\"", msg);
	if (!g_spawn_command_line_async (cmdline, &error)) {
		sd_journal_print (LOG_WARNING,
				  "failed to display message on splash: %s",
				  error->message);
	} else {
		sd_journal_print (LOG_INFO, "sent msg to plymouth '%s'", msg);
	}
}

/**
 * pk_offline_update_set_plymouth_mode:
 **/
static void
pk_offline_update_set_plymouth_mode (const gchar *mode)
{
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *cmdline = NULL;

	/* allow testing without sending commands to plymouth */
	if (g_getenv ("PK_OFFLINE_UPDATE_TEST") != NULL)
		return;
	cmdline = g_strdup_printf ("plymouth change-mode --%s", mode);
	if (!g_spawn_command_line_async (cmdline, &error)) {
		sd_journal_print (LOG_WARNING,
				  "failed to change mode for splash: %s",
				  error->message);
	} else {
		sd_journal_print (LOG_INFO, "sent mode to plymouth '%s'", mode);
	}
}

/**
 * pk_offline_update_set_plymouth_percentage:
 **/
static void
pk_offline_update_set_plymouth_percentage (guint percentage)
{
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *cmdline = NULL;

	/* allow testing without sending commands to plymouth */
	if (g_getenv ("PK_OFFLINE_UPDATE_TEST") != NULL)
		return;
	cmdline = g_strdup_printf ("plymouth system-update --progress=%i",
				   percentage);
	if (!g_spawn_command_line_async (cmdline, &error)) {
		sd_journal_print (LOG_WARNING,
				  "failed to set percentage for splash: %s",
				  error->message);
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
	PkInfoEnum info;
	PkProgressBar *progressbar = PK_PROGRESS_BAR (user_data);
	PkStatusEnum status;
	gint percentage;
	_cleanup_free_ gchar *msg = NULL;
	_cleanup_object_unref_ PkPackage *pkg = NULL;

	switch (type) {
	case PK_PROGRESS_TYPE_ROLE:
		sd_journal_print (LOG_INFO, "assigned role");
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
		sd_journal_print (LOG_INFO,
				  "package %s\t%s-%s.%s (%s)",
				  pk_info_enum_to_string (info),
				  pk_package_get_name (pkg),
				  pk_package_get_version (pkg),
				  pk_package_get_arch (pkg),
				  pk_package_get_data (pkg));
		break;
	case PK_PROGRESS_TYPE_PERCENTAGE:
		g_object_get (progress, "percentage", &percentage, NULL);
		if (percentage < 0)
			return;
		sd_journal_print (LOG_INFO, "percentage %i%%", percentage);

		/* TRANSLATORS: this is the message we send plymouth to
		 * advise of the new percentage completion */
		msg = g_strdup_printf ("%s - %i%%", _("Installing Updates"), percentage);
		if (percentage > 10)
			pk_offline_update_set_plymouth_msg (msg);

		/* print on terminal */
		pk_progress_bar_set_percentage (progressbar, percentage);

		/* update plymouth */
		pk_offline_update_set_plymouth_percentage (percentage);
		break;
	case PK_PROGRESS_TYPE_STATUS:
		g_object_get (progress, "status", &status, NULL);
		sd_journal_print (LOG_INFO,
				  "status %s",
				  pk_status_enum_to_string (status));
	default:
		break;
	}
}

/**
 * pk_offline_update_reboot:
 **/
static void
pk_offline_update_reboot (void)
{
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ GDBusConnection *connection;
	_cleanup_variant_unref_ GVariant *val = NULL;

	/* reboot using systemd */
	sd_journal_print (LOG_INFO, "rebooting");
	pk_offline_update_set_plymouth_mode ("shutdown");
	/* TRANSLATORS: we've finished doing offline updates */
	pk_offline_update_set_plymouth_msg (_("Rebooting after installing updates…"));
	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (connection == NULL) {
		sd_journal_print (LOG_WARNING,
				  "Failed to get system bus connection: %s",
				  error->message);
		return;
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
		sd_journal_print (LOG_WARNING,
				  "Failed to reboot: %s",
				  error->message);
		return;
	}
}

/**
 * pk_offline_update_power_off:
 **/
static void
pk_offline_update_power_off (void)
{
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ GDBusConnection *connection = NULL;
	_cleanup_variant_unref_ GVariant *val = NULL;

	/* reboot using systemd */
	sd_journal_print (LOG_INFO, "shutting down");
	pk_offline_update_set_plymouth_mode ("shutdown");
	/* TRANSLATORS: we've finished doing offline updates */
	pk_offline_update_set_plymouth_msg (_("Shutting down after installing updates…"));
	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (connection == NULL) {
		sd_journal_print (LOG_WARNING,
				  "Failed to get system bus connection: %s",
				  error->message);
		return;
	}
	val = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.systemd1",
					   "/org/freedesktop/systemd1",
					   "org.freedesktop.systemd1.Manager",
					   "PowerOff",
					   NULL,
					   NULL,
					   G_DBUS_CALL_FLAGS_NONE,
					   -1,
					   NULL,
					   &error);
	if (val == NULL) {
		sd_journal_print (LOG_WARNING,
				  "Failed to power off: %s",
				  error->message);
		return;
	}
}

/**
 * pk_offline_update_write_error:
 **/
static void
pk_offline_update_write_error (const GError *error)
{
	PkErrorEnum error_enum = PK_ERROR_ENUM_UNKNOWN;
	gboolean ret;
	_cleanup_error_free_ GError *error_local = NULL;
	_cleanup_free_ gchar *data = NULL;
	_cleanup_keyfile_unref_ GKeyFile *key_file = NULL;

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
		sd_journal_print (LOG_WARNING,
				  "failed to get keyfile data: %s",
				  error_local->message);
		return;
	}
	ret = g_file_set_contents (PK_OFFLINE_UPDATE_RESULTS_FILENAME,
				   data, -1, &error_local);
	if (!ret) {
		sd_journal_print (LOG_WARNING,
				  "failed to write file: %s",
				  error_local->message);
		return;
	}
}

/**
 * pk_offline_update_write_results:
 **/
static void
pk_offline_update_write_results (PkResults *results)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *packages;
	guint i;
	PkError *pk_error;
	PkPackage *package;
	_cleanup_free_ gchar *data = NULL;
	_cleanup_keyfile_unref_ GKeyFile *key_file = NULL;

	sd_journal_print (LOG_INFO, "writing actual results");
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
		_cleanup_string_free_ GString *string = NULL;
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
	}

	/* write file */
	data = g_key_file_to_data (key_file, NULL, &error);
	if (data == NULL) {
		sd_journal_print (LOG_WARNING,
				  "failed to get keyfile data: %s",
				  error->message);
		return;
	}
	ret = g_file_set_contents (PK_OFFLINE_UPDATE_RESULTS_FILENAME,
				   data, -1, &error);
	if (!ret) {
		sd_journal_print (LOG_WARNING,
				  "failed to write file: %s",
				  error->message);
		return;
	}
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
	guint i;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *data = NULL;
	_cleanup_keyfile_unref_ GKeyFile *key_file = NULL;
	_cleanup_string_free_ GString *string = NULL;

	sd_journal_print (LOG_INFO, "writing dummy results");
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
		sd_journal_print (LOG_WARNING,
				  "failed to get keyfile data: %s",
				  error->message);
		return;
	}
	ret = g_file_set_contents (PK_OFFLINE_UPDATE_RESULTS_FILENAME,
				   data, -1, &error);
	if (!ret) {
		sd_journal_print (LOG_WARNING,
				  "failed to write dummy %s: %s",
				  PK_OFFLINE_UPDATE_RESULTS_FILENAME,
				  error->message);
		return;
	}

	/* ensure this is written to disk */
	sync ();
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
 * pk_offline_update_sigint_cb:
 **/
static gboolean
pk_offline_update_sigint_cb (gpointer user_data)
{
	sd_journal_print (LOG_WARNING, "Handling SIGINT");
	return FALSE;
}

typedef enum {
	PK_OFFLINE_UPDATE_ACTION_NOTHING,
	PK_OFFLINE_UPDATE_ACTION_REBOOT,
	PK_OFFLINE_UPDATE_ACTION_POWER_OFF
} PkOfflineUpdateAction;

static PkOfflineUpdateAction
pk_offline_update_get_action (void)
{
	gboolean ret;
	PkOfflineUpdateAction action;
	_cleanup_free_ gchar *action_data = NULL;

	/* allow testing without rebooting */
	if (g_getenv ("PK_OFFLINE_UPDATE_TEST") != NULL) {
		g_print ("TESTING, so not doing action\n");
		return PK_OFFLINE_UPDATE_ACTION_NOTHING;
	}

	ret = g_file_get_contents (PK_OFFLINE_UPDATE_ACTION_FILENAME,
				   &action_data, NULL, NULL);
	if (!ret) {
		g_warning ("Failed to get post-update action, using reboot");
		return PK_OFFLINE_UPDATE_ACTION_REBOOT;
	}
	if (g_strcmp0 (action_data, "reboot") == 0)
		return PK_OFFLINE_UPDATE_ACTION_REBOOT;
	if (g_strcmp0 (action_data, "power-off") == 0)
		return PK_OFFLINE_UPDATE_ACTION_POWER_OFF;
	g_warning ("failed to parse action '%s', using reboot", action_data);
	return PK_OFFLINE_UPDATE_ACTION_REBOOT;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	PkOfflineUpdateAction action;
	gboolean ret;
	gint retval;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *packages_data = NULL;
	_cleanup_main_loop_unref_ GMainLoop *loop = NULL;
	_cleanup_object_unref_ GFile *file = NULL;
	_cleanup_object_unref_ PkProgressBar *progressbar = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;
	_cleanup_object_unref_ PkTask *task = NULL;
	_cleanup_strv_free_ gchar **package_ids = NULL;

#if (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 35)
	g_type_init ();
#endif

	/* ensure root user */
	if (getuid () != 0 || geteuid () != 0) {
		retval = EXIT_FAILURE;
		g_print ("This program can only be used using root\n");
		sd_journal_print (LOG_WARNING, "not called with the root user");
		goto out;
	}

	/* always do this first to avoid a loop if this tool segfaults */
	g_unlink (PK_OFFLINE_UPDATE_TRIGGER_FILENAME);

	/* do stuff on ctrl-c */
	g_unix_signal_add_full (G_PRIORITY_DEFAULT,
				SIGINT,
				pk_offline_update_sigint_cb,
				NULL,
				NULL);

	/* get the list of packages to update */
	ret = g_file_get_contents (PK_OFFLINE_PREPARED_UPDATE_FILENAME,
				   &packages_data,
				   NULL,
				   &error);
	if (!ret) {
		retval = EXIT_FAILURE;
		sd_journal_print (LOG_WARNING,
				  "failed to read %s: %s",
				  PK_OFFLINE_PREPARED_UPDATE_FILENAME,
				  error->message);
		goto out;
	}

	/* use a progress bar when the user presses <esc> in plymouth */
	progressbar = pk_progress_bar_new ();
	pk_progress_bar_set_size (progressbar, 25);
	pk_progress_bar_set_padding (progressbar, 30);

	/* just update the system */
	task = pk_task_new ();
	pk_client_set_interactive (PK_CLIENT (task), FALSE);
	pk_offline_update_set_plymouth_mode ("updates");
	/* TRANSLATORS: we've started doing offline updates */
	pk_offline_update_set_plymouth_msg (_("Installing updates, this could take a while..."));
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
		sd_journal_print (LOG_WARNING,
				  "failed to update system: %s",
				  error->message);
		goto out;
	}
	pk_progress_bar_end (progressbar);
	pk_offline_update_write_results (results);

	/* delete prepared-update file if it's not already been done by the
	 * pk-plugin-systemd-update daemon plugin */
	file = g_file_new_for_path (PK_OFFLINE_PREPARED_UPDATE_FILENAME);
	ret = g_file_delete (file, NULL, &error);
	if (!ret) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			retval = EXIT_FAILURE;
			sd_journal_print (LOG_WARNING,
					  "failed to delete %s: %s",
					  PK_OFFLINE_PREPARED_UPDATE_FILENAME,
					  error->message);
			goto out;
		}
	}

	retval = EXIT_SUCCESS;
out:
	/* if we failed, we pause to show any error on the screen */
	if (retval != EXIT_SUCCESS) {
		loop = g_main_loop_new (NULL, FALSE);
		g_timeout_add_seconds (10, pk_offline_update_loop_quit_cb, loop);
		g_main_loop_run (loop);
	}
	/* we have to manually either restart or shutdown */
	action = pk_offline_update_get_action ();
	if (action == PK_OFFLINE_UPDATE_ACTION_REBOOT)
		pk_offline_update_reboot ();
	else if (action == PK_OFFLINE_UPDATE_ACTION_POWER_OFF)
		pk_offline_update_power_off ();
	return retval;
}
