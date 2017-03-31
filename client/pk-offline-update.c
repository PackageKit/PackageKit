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
#include <packagekit-glib2/pk-offline-private.h>
#include <stdlib.h>
#include <unistd.h>
#include <systemd/sd-journal.h>

/**
 * pk_offline_update_set_plymouth_msg:
 **/
static void
pk_offline_update_set_plymouth_msg (const gchar *msg)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *cmdargv = NULL;
	g_autofree gchar *cmdline = NULL;

	/* allow testing without sending commands to plymouth */
	if (g_getenv ("PK_OFFLINE_UPDATE_TEST") != NULL)
		return;
	cmdargv = g_find_program_in_path ("plymouth");
	if (cmdargv == NULL)
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
	g_autoptr(GError) error = NULL;
	g_autofree gchar *cmdargv = NULL;
	g_autofree gchar *cmdline = NULL;

	/* allow testing without sending commands to plymouth */
	if (g_getenv ("PK_OFFLINE_UPDATE_TEST") != NULL)
		return;
	cmdargv = g_find_program_in_path ("plymouth");
	if (cmdargv == NULL)
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
	g_autoptr(GError) error = NULL;
	g_autofree gchar *cmdargv = NULL;
	g_autofree gchar *cmdline = NULL;

	/* allow testing without sending commands to plymouth */
	if (g_getenv ("PK_OFFLINE_UPDATE_TEST") != NULL)
		return;
	cmdargv = g_find_program_in_path ("plymouth");
	if (cmdargv == NULL)
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
	g_autofree gchar *msg = NULL;
	g_autoptr(PkPackage) pkg = NULL;

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
static int
pk_offline_update_reboot (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) val = NULL;

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
		return EXIT_FAILURE;
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
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

/**
 * pk_offline_update_power_off:
 **/
static int
pk_offline_update_power_off (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) val = NULL;

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
		return EXIT_FAILURE;
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
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

/**
 * pk_offline_update_write_error:
 **/
static void
pk_offline_update_write_error (const GError *error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(PkError) pk_error = NULL;
	g_autoptr(PkResults) results = NULL;

	sd_journal_print (LOG_INFO, "writing failed results");
	results = pk_results_new ();
	pk_results_set_exit_code (results, PK_EXIT_ENUM_FAILED);
	pk_error = pk_error_new ();
	g_object_set (pk_error,
		      "code", PK_ERROR_ENUM_FAILED_INITIALIZATION,
		      "details", error->message,
		      NULL);
	pk_results_set_error_code (results, pk_error);
	if (!pk_offline_auth_set_results (results, &error_local))
		sd_journal_print (LOG_WARNING, "%s", error_local->message);
}

/**
 * pk_offline_update_write_results:
 **/
static void
pk_offline_update_write_results (PkResults *results)
{
	g_autoptr(GError) error = NULL;
	sd_journal_print (LOG_INFO, "writing actual results");
	if (!pk_offline_auth_set_results (results, &error))
		sd_journal_print (LOG_WARNING, "%s", error->message);
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
pk_offline_update_write_dummy_results (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(PkError) pk_error = NULL;
	g_autoptr(PkResults) results = NULL;

	sd_journal_print (LOG_INFO, "writing dummy results");
	results = pk_results_new ();
	pk_results_set_exit_code (results, PK_EXIT_ENUM_FAILED);
	pk_error = pk_error_new ();
	g_object_set (pk_error,
		      "code", PK_ERROR_ENUM_FAILED_INITIALIZATION,
		      "details", "The transaction did not complete",
		      NULL);
	pk_results_set_error_code (results, pk_error);
	if (!pk_offline_auth_set_results (results, &error))
		sd_journal_print (LOG_WARNING, "%s", error->message);

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

static PkOfflineAction
pk_offline_update_get_action (void)
{
	PkOfflineAction action;

	/* allow testing without rebooting */
	if (g_getenv ("PK_OFFLINE_UPDATE_TEST") != NULL) {
		g_print ("TESTING, so not doing action\n");
		return PK_OFFLINE_ACTION_UNSET;
	}
	action = pk_offline_get_action (NULL);
	if (action == PK_OFFLINE_ACTION_UNKNOWN) {
		g_warning ("failed to parse action, using reboot");
		return PK_OFFLINE_ACTION_REBOOT;
	}
	return action;
}

static gboolean
pk_offline_update_do_update (PkTask *task, PkProgressBar *progressbar, GError **error)
{
	g_autoptr(PkResults) results = NULL;
	g_auto(GStrv) package_ids = NULL;

	/* get the list of packages to update */
	package_ids = pk_offline_get_prepared_ids (error);
	if (package_ids == NULL) {
		g_prefix_error (error, "failed to read %s: ", PK_OFFLINE_PREPARED_FILENAME);
		return FALSE;
	}

	/* TRANSLATORS: we've started doing offline updates */
	pk_offline_update_set_plymouth_msg (_("Installing updates; this could take a while..."));
	pk_offline_update_write_dummy_results ();
	results = pk_client_update_packages (PK_CLIENT (task),
	                                     0,
	                                     package_ids,
	                                     NULL, /* GCancellable */
	                                     pk_offline_update_progress_cb,
	                                     progressbar, /* user_data */
	                                     error);
	if (results == NULL) {
		return FALSE;
	}

	pk_offline_update_write_results (results);

	return TRUE;
}

static gboolean
pk_offline_update_do_upgrade (PkTask *task, PkProgressBar *progressbar, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autoptr(PkResults) results = NULL;

	/* get the version to upgrade to */
	version = pk_offline_get_prepared_upgrade_version (error);
	if (version == NULL) {
	        g_prefix_error (error, "failed to get prepared system upgrade version: ");
	        return FALSE;
	}

	/* TRANSLATORS: we've started doing offline system upgrade */
	pk_offline_update_set_plymouth_msg (_("Installing system upgrade; this could take a while..."));
	pk_offline_update_write_dummy_results ();
	results = pk_client_upgrade_system (PK_CLIENT (task),
	                                    0,
	                                    version,
	                                    PK_UPGRADE_KIND_ENUM_DEFAULT,
	                                    NULL, /* GCancellable */
	                                    pk_offline_update_progress_cb,
	                                    progressbar, /* user_data */
	                                    error);
	if (results == NULL) {
		return FALSE;
	}

	pk_offline_update_write_results (results);

	return TRUE;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	PkOfflineAction action = PK_OFFLINE_ACTION_UNKNOWN;
	gint retval;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *link = NULL;
	g_autoptr(GMainLoop) loop = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(PkProgressBar) progressbar = NULL;
	g_autoptr(PkTask) task = NULL;

	/* ensure root user */
	if (getuid () != 0 || geteuid () != 0) {
		retval = EXIT_FAILURE;
		g_print ("This program can only be used using root\n");
		sd_journal_print (LOG_WARNING, "not called with the root user");
		goto out;
	}

	/* verify this is pointing to our cache */
	link = g_file_read_link (PK_OFFLINE_TRIGGER_FILENAME, NULL);
	if (link == NULL) {
		sd_journal_print (LOG_INFO, "no trigger, exiting");
		retval = EXIT_SUCCESS;
		goto out;
	}
	if (g_strcmp0 (link, PK_OFFLINE_PREPARED_FILENAME) != 0 &&
	    g_strcmp0 (link, PK_OFFLINE_PREPARED_UPGRADE_FILENAME) != 0 &&
	    g_strcmp0 (link, "/var/cache/PackageKit") != 0 &&
	    g_strcmp0 (link, "/var/cache") != 0) {
		sd_journal_print (LOG_INFO, "another framework set up the trigger");
		retval = EXIT_SUCCESS;
		goto out;
	}

	/* get the action, and then delete the file */
	action = pk_offline_update_get_action ();
	g_unlink (PK_OFFLINE_ACTION_FILENAME);

	/* always do this first to avoid a loop if this tool segfaults */
	g_unlink (PK_OFFLINE_TRIGGER_FILENAME);

	/* do stuff on ctrl-c */
	g_unix_signal_add_full (G_PRIORITY_DEFAULT,
				SIGINT,
				pk_offline_update_sigint_cb,
				NULL,
				NULL);

	/* use a progress bar when the user presses <esc> in plymouth */
	progressbar = pk_progress_bar_new ();
	pk_progress_bar_set_size (progressbar, 25);
	pk_progress_bar_set_padding (progressbar, 30);

	task = pk_task_new ();
	pk_client_set_interactive (PK_CLIENT (task), FALSE);
	pk_offline_update_set_plymouth_mode ("updates");

	if (g_strcmp0 (link, PK_OFFLINE_PREPARED_UPGRADE_FILENAME) == 0 &&
	    g_file_test (PK_OFFLINE_PREPARED_UPGRADE_FILENAME, G_FILE_TEST_EXISTS)) {
		/* do system upgrade */
		if (!pk_offline_update_do_upgrade (task, progressbar, &error)) {
			retval = EXIT_FAILURE;
			pk_offline_update_write_error (error);
			sd_journal_print (LOG_WARNING,
					  "failed to upgrade system: %s",
					  error->message);
			goto out;
		}
	} else {
		/* just update the system */
		if (!pk_offline_update_do_update (task, progressbar, &error)) {
			retval = EXIT_FAILURE;
			pk_offline_update_write_error (error);
			sd_journal_print (LOG_WARNING,
					  "failed to update system: %s",
					  error->message);
			goto out;
		}
	}

	pk_progress_bar_end (progressbar);

	/* delete prepared-update and prepared-upgrade files as they are
	 * both now out of date */
	if (!pk_offline_auth_invalidate (&error)) {
		retval = EXIT_FAILURE;
		sd_journal_print (LOG_WARNING,
				  "failed to delete %s: %s",
				  PK_OFFLINE_PREPARED_FILENAME,
				  error->message);
		goto out;
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
	if (action == PK_OFFLINE_ACTION_REBOOT)
		retval = pk_offline_update_reboot ();
	else if (action == PK_OFFLINE_ACTION_POWER_OFF)
		retval = pk_offline_update_power_off ();

	/* We must return success if we queued the shutdown or reboot
	 * request, so the failure action specified by the unit is not
	 * triggered. If we failed to enqueue, return failure which
	 * will cause systemd to trigger the failure action. */
	return retval;
}
