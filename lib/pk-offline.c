/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
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

#include <glib.h>
#include <errno.h>
#include <string.h>

#include "pk-offline.h"
#include "pk-offline-private.h"

/**
 * SECTION:pk-offline
 * @title: Offline Updates
 * @short_description: Function to perform offline updates
 *
 * Functions for perform offline updates.
 */

/**
 * pk_offline_error_quark:
 *
 * An error quark for #PkOfflineError.
 *
 * Return value: an error quark.
 *
 * Since: 0.9.6
 **/
G_DEFINE_QUARK (pk-offline-error-quark, pk_offline_error)

typedef enum {
	PK_OFFLINE_FILE_PREPARED,
	PK_OFFLINE_FILE_PREPARED_UPGRADE,
	PK_OFFLINE_FILE_TRIGGER,
	PK_OFFLINE_FILE_RESULTS,
	PK_OFFLINE_FILE_ACTION,
	PK_OFFLINE_FILE_LAST
} PkOfflineFile;

/* the state files, relative to the root directory; the trigger has to be at
 * the top level as that is where systemd's system-update-generator looks */
/* clang-format off */
static const gchar *pk_offline_file_paths[PK_OFFLINE_FILE_LAST] = {
	[PK_OFFLINE_FILE_PREPARED]	   = "/var/lib/PackageKit/prepared-update",
	[PK_OFFLINE_FILE_PREPARED_UPGRADE] = "/var/lib/PackageKit/prepared-upgrade",
	[PK_OFFLINE_FILE_TRIGGER]	   = "/system-update",
	[PK_OFFLINE_FILE_RESULTS]	   = "/var/lib/PackageKit/offline-update-competed",
	[PK_OFFLINE_FILE_ACTION]	   = "/var/lib/PackageKit/offline-update-action",
};
/* clang-format on */

G_LOCK_DEFINE_STATIC (pk_offline_root);
static gchar *pk_offline_root_dir = NULL;
static gchar *pk_offline_filenames[PK_OFFLINE_FILE_LAST] = { NULL };

/* must be called with the lock held */
static void
pk_offline_build_filenames_locked (const gchar *root_dir)
{
	g_free (pk_offline_root_dir);
	pk_offline_root_dir = g_strdup (root_dir);
	for (guint i = 0; i < PK_OFFLINE_FILE_LAST; i++) {
		g_free (pk_offline_filenames[i]);
		pk_offline_filenames[i] = g_build_filename (root_dir,
							    pk_offline_file_paths[i],
							    NULL);
	}
}

/*
 * pk_offline_set_root_dir:
 * @root_dir: the directory to treat as "/", or %NULL for the real root
 *
 * Sets the directory the offline update state files are looked up in. The
 * daemon uses this to honor its RootDir configuration and the self tests use
 * it to keep the system state untouched.
 *
 * Call this before any other pk_offline function: the strings returned by the
 * filename getters stay valid only until the root directory is changed again.
 **/
void
pk_offline_set_root_dir (const gchar *root_dir)
{
	G_LOCK (pk_offline_root);
	pk_offline_build_filenames_locked (root_dir != NULL && root_dir[0] != '\0' ? root_dir
										   : "/");
	G_UNLOCK (pk_offline_root);
}

static const gchar *
pk_offline_get_filename (PkOfflineFile file)
{
	const gchar *filename;

	G_LOCK (pk_offline_root);
	if (pk_offline_root_dir == NULL)
		pk_offline_build_filenames_locked ("/");
	filename = pk_offline_filenames[file];
	G_UNLOCK (pk_offline_root);
	return filename;
}

/*
 * pk_offline_get_prepared_filename:
 *
 * Return value: the path of the file describing the prepared offline update
 **/
const gchar *
pk_offline_get_prepared_filename (void)
{
	return pk_offline_get_filename (PK_OFFLINE_FILE_PREPARED);
}

/*
 * pk_offline_get_prepared_upgrade_filename:
 *
 * Return value: the path of the file describing the prepared system upgrade
 **/
const gchar *
pk_offline_get_prepared_upgrade_filename (void)
{
	return pk_offline_get_filename (PK_OFFLINE_FILE_PREPARED_UPGRADE);
}

/*
 * pk_offline_get_trigger_filename:
 *
 * Return value: the path of the symlink that makes systemd boot into the
 * offline update target
 **/
const gchar *
pk_offline_get_trigger_filename (void)
{
	return pk_offline_get_filename (PK_OFFLINE_FILE_TRIGGER);
}

/*
 * pk_offline_get_results_filename:
 *
 * Return value: the path of the keyfile holding the results of the last
 * offline update
 **/
const gchar *
pk_offline_get_results_filename (void)
{
	return pk_offline_get_filename (PK_OFFLINE_FILE_RESULTS);
}

/*
 * pk_offline_get_action_filename:
 *
 * Return value: the path of the file holding the action to take after the
 * offline update
 **/
const gchar *
pk_offline_get_action_filename (void)
{
	return pk_offline_get_filename (PK_OFFLINE_FILE_ACTION);
}

/**
 * pk_offline_action_to_string:
 * @action: a #PkOfflineAction, e.g. %PK_OFFLINE_ACTION_REBOOT
 *
 * Converts the enumerated value to a string.
 *
 * Return value: string value, or %NULL for invalid
 *
 * Since: 0.9.6
 **/
const gchar *
pk_offline_action_to_string (PkOfflineAction action)
{
	if (action == PK_OFFLINE_ACTION_UNKNOWN)
		return "unknown";
	if (action == PK_OFFLINE_ACTION_REBOOT)
		return "reboot";
	if (action == PK_OFFLINE_ACTION_POWER_OFF)
		return "power-off";
	if (action == PK_OFFLINE_ACTION_UNSET)
		return "unset";
	return NULL;
}

/**
 * pk_offline_action_from_string:
 * @action: a string representation of a #PkOfflineAction, e.g. "reboot"
 *
 * Converts the string to the enumerated value.
 *
 * Return value: A #PkOfflineAction, or %PK_OFFLINE_ACTION_UNKNOWN for invalid
 *
 * Since: 0.9.6
 **/
PkOfflineAction
pk_offline_action_from_string (const gchar *action)
{
	if (g_strcmp0 (action, "unknown") == 0)
		return PK_OFFLINE_ACTION_UNKNOWN;
	if (g_strcmp0 (action, "reboot") == 0)
		return PK_OFFLINE_ACTION_REBOOT;
	if (g_strcmp0 (action, "power-off") == 0)
		return PK_OFFLINE_ACTION_POWER_OFF;
	if (g_strcmp0 (action, "unset") == 0)
		return PK_OFFLINE_ACTION_UNSET;
	return PK_OFFLINE_ACTION_UNKNOWN;
}

static GDBusCallFlags
pk_offline_flags_to_gdbus_call_flags (PkOfflineFlags flags)
{
	if ((flags & PK_OFFLINE_FLAGS_INTERACTIVE) != 0)
		return G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION;
	return G_DBUS_CALL_FLAGS_NONE;
}

/**
 * pk_offline_cancel:
 * @cancellable: A #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Cancels the offline operation that has been scheduled. If there is no
 * scheduled offline operation then this method returns with success.
 * The function always allows user interaction. To change the behavior,
 * use pk_offline_cancel_with_flags().
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 0.9.6
 **/
gboolean
pk_offline_cancel (GCancellable *cancellable, GError **error)
{
	return pk_offline_cancel_with_flags (PK_OFFLINE_FLAGS_INTERACTIVE, cancellable, error);
}

/**
 * pk_offline_cancel_with_flags:
 * @flags: bit-or of #PkOfflineFlags
 * @cancellable: A #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Cancels the offline operation that has been scheduled. If there is no
 * scheduled offline operation then this method returns with success.
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 1.2.5
 **/
gboolean
pk_offline_cancel_with_flags (PkOfflineFlags flags, GCancellable *cancellable, GError **error)
{
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) res = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, cancellable, error);
	if (connection == NULL)
		return FALSE;
	res = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.PackageKit.Offline",
					   "Cancel",
					   NULL,
					   NULL,
					   pk_offline_flags_to_gdbus_call_flags (flags),
					   -1,
					   cancellable,
					   error);
	if (res == NULL)
		return FALSE;
	return TRUE;
}

/**
 * pk_offline_clear_results:
 * @cancellable: A #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Clears the last offline operation report, which may be success or failure.
 * If the report does not exist then this method returns success.
 * The function always allows user interaction. To change the behavior,
 * use pk_offline_clear_results_with_flags().
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 0.9.6
 **/
gboolean
pk_offline_clear_results (GCancellable *cancellable, GError **error)
{
	return pk_offline_clear_results_with_flags (PK_OFFLINE_FLAGS_INTERACTIVE,
						    cancellable,
						    error);
}

/**
 * pk_offline_clear_results_with_flags:
 * @flags: bit-or of #PkOfflineFlags
 * @cancellable: A #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Clears the last offline operation report, which may be success or failure.
 * If the report does not exist then this method returns success.
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 1.2.5
 **/
gboolean
pk_offline_clear_results_with_flags (PkOfflineFlags flags,
				     GCancellable *cancellable,
				     GError **error)
{
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) res = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, cancellable, error);
	if (connection == NULL)
		return FALSE;
	res = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.PackageKit.Offline",
					   "ClearResults",
					   NULL,
					   NULL,
					   pk_offline_flags_to_gdbus_call_flags (flags),
					   -1,
					   cancellable,
					   error);
	if (res == NULL)
		return FALSE;
	return TRUE;
}

/**
 * pk_offline_trigger:
 * @action: a #PkOfflineAction, e.g. %PK_OFFLINE_ACTION_REBOOT
 * @cancellable: A #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Triggers the offline update so that the next reboot will perform the
 * pending transaction.
 * The function always allows user interaction. To change the behavior,
 * use pk_offline_trigger_with_flags().
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 0.9.6
 **/
gboolean
pk_offline_trigger (PkOfflineAction action, GCancellable *cancellable, GError **error)
{
	return pk_offline_trigger_with_flags (action,
					      PK_OFFLINE_FLAGS_INTERACTIVE,
					      cancellable,
					      error);
}

/**
 * pk_offline_trigger_with_flags:
 * @action: a #PkOfflineAction, e.g. %PK_OFFLINE_ACTION_REBOOT
 * @flags: bit-or of #PkOfflineFlags
 * @cancellable: A #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Triggers the offline update so that the next reboot will perform the
 * pending transaction.
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 1.2.5
 **/
gboolean
pk_offline_trigger_with_flags (PkOfflineAction action,
			       PkOfflineFlags flags,
			       GCancellable *cancellable,
			       GError **error)
{
	const gchar *tmp;
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) res = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, cancellable, error);
	if (connection == NULL)
		return FALSE;
	tmp = pk_offline_action_to_string (action);
	res = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.PackageKit.Offline",
					   "Trigger",
					   g_variant_new ("(s)", tmp),
					   NULL,
					   pk_offline_flags_to_gdbus_call_flags (flags),
					   -1,
					   cancellable,
					   error);
	if (res == NULL)
		return FALSE;
	return TRUE;
}

/**
 * pk_offline_trigger_upgrade:
 * @action: a #PkOfflineAction, e.g. %PK_OFFLINE_ACTION_REBOOT
 * @cancellable: A #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Triggers the offline system upgrade so that the next reboot will perform the
 * pending transaction.
 * The function always allows user interaction. To change the behavior,
 * use pk_offline_trigger_upgrade_with_flags().
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 1.0.12
 **/
gboolean
pk_offline_trigger_upgrade (PkOfflineAction action, GCancellable *cancellable, GError **error)
{
	return pk_offline_trigger_upgrade_with_flags (action,
						      PK_OFFLINE_FLAGS_INTERACTIVE,
						      cancellable,
						      error);
}

/**
 * pk_offline_trigger_upgrade_with_flags:
 * @action: a #PkOfflineAction, e.g. %PK_OFFLINE_ACTION_REBOOT
 * @flags: bit-or of #PkOfflineFlags
 * @cancellable: A #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Triggers the offline system upgrade so that the next reboot will perform the
 * pending transaction.
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 1.2.5
 **/
gboolean
pk_offline_trigger_upgrade_with_flags (PkOfflineAction action,
				       PkOfflineFlags flags,
				       GCancellable *cancellable,
				       GError **error)
{
	const gchar *tmp;
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) res = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, cancellable, error);
	if (connection == NULL)
		return FALSE;
	tmp = pk_offline_action_to_string (action);
	res = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.PackageKit.Offline",
					   "TriggerUpgrade",
					   g_variant_new ("(s)", tmp),
					   NULL,
					   pk_offline_flags_to_gdbus_call_flags (flags),
					   -1,
					   cancellable,
					   error);
	if (res == NULL)
		return FALSE;
	return TRUE;
}

/**
 * pk_offline_get_action:
 * @error: A #GError or %NULL
 *
 * Gets the action that will be taken after the offline action has completed.
 *
 * An error is set if the the value %PK_OFFLINE_ACTION_UNKNOWN is returned.
 *
 * Return value: a #PkOfflineAction, e.g. %PK_OFFLINE_ACTION_REBOOT
 *
 * Since: 0.9.6
 **/
PkOfflineAction
pk_offline_get_action (GError **error)
{
	PkOfflineAction action;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *action_data = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, PK_OFFLINE_ACTION_UNKNOWN);

	/* is the trigger set? */
	if (!g_file_test (pk_offline_get_trigger_filename (), G_FILE_TEST_EXISTS) ||
	    !g_file_test (pk_offline_get_action_filename (), G_FILE_TEST_EXISTS))
		return PK_OFFLINE_ACTION_UNSET;

	/* read data file */
	if (!g_file_get_contents (pk_offline_get_action_filename (),
				  &action_data,
				  NULL,
				  &error_local)) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "Failed to open %s: %s",
			     pk_offline_get_action_filename (),
			     error_local->message);
		return PK_OFFLINE_ACTION_UNKNOWN;
	}
	action = pk_offline_action_from_string (action_data);
	if (action == PK_OFFLINE_ACTION_UNKNOWN) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_INVALID_VALUE,
			     "Failed to parse '%s'",
			     action_data);
	}
	return action;
}

/**
 * pk_offline_get_prepared_sack:
 * @error: A #GError or %NULL
 *
 * Gets a package sack of the packages in the prepared transaction.
 *
 * Return value: (transfer full): A new #PkPackageSack, or %NULL
 *
 * Since: 0.9.6
 **/
PkPackageSack *
pk_offline_get_prepared_sack (GError **error)
{
	guint i;
	g_autoptr(PkPackageSack) sack = NULL;
	g_auto(GStrv) package_ids = NULL;

	/* get the list of packages */
	package_ids = pk_offline_get_prepared_ids (error);
	if (package_ids == NULL)
		return NULL;

	/* add them to the new array */
	sack = pk_package_sack_new ();
	for (i = 0; package_ids[i] != NULL; i++) {
		if (!pk_package_sack_add_package_by_id (sack, package_ids[i], error))
			return NULL;
	}
	return g_object_ref (sack);
}

/**
 * pk_offline_get_prepared_ids:
 * @error: A #GError or %NULL
 *
 * Gets the package-ids in the prepared transaction.
 *
 * Return value: (transfer full): array of package-ids, or %NULL
 *
 * Since: 0.9.6
 **/
gchar **
pk_offline_get_prepared_ids (GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *data = NULL;
	g_auto(GStrv) prepared_ids = NULL;
	gsize prepared_ids_size;
	g_autoptr(GKeyFile) keyfile = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* does exist? */
	if (!g_file_test (pk_offline_get_prepared_filename (), G_FILE_TEST_EXISTS)) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_NO_DATA,
			     "No offline updates have been prepared");
		return NULL;
	}

	/* read data file */
	if (!g_file_get_contents (pk_offline_get_prepared_filename (), &data, NULL, &error_local)) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "Failed to read %s: %s",
			     pk_offline_get_prepared_filename (),
			     error_local->message);
		return NULL;
	}

	keyfile = g_key_file_new ();
	g_key_file_set_list_separator (keyfile, ',');
	if (!g_key_file_load_from_data (keyfile, data, -1, G_KEY_FILE_NONE, &error_local)) {
		/* fall back to previous plain text file format for backwards compatibility */
		return g_strsplit (data, "\n", -1);
	}

	prepared_ids = g_key_file_get_string_list (keyfile,
						   "update",
						   "prepared_ids",
						   &prepared_ids_size,
						   error);

	if (prepared_ids == NULL || prepared_ids_size == 0)
		return NULL;

	return g_steal_pointer (&prepared_ids);
}

/**
 * pk_offline_get_prepared_upgrade_name:
 * @error: A #GError or %NULL
 *
 * Gets the name of the prepared system upgrade in the prepared transaction.
 *
 * Return value: the name, or %NULL if unset, free with g_free()
 *
 * Since: 1.1.2
 **/
gchar *
pk_offline_get_prepared_upgrade_name (GError **error)
{
	gchar *name = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!pk_offline_get_prepared_upgrade (&name, NULL, error))
		return NULL;

	return name;
}

/**
 * pk_offline_get_prepared_upgrade_version:
 * @error: A #GError or %NULL
 *
 * Gets the version of the prepared system upgrade in the prepared transaction.
 *
 * Return value: the version, or %NULL if unset, free with g_free()
 *
 * Since: 1.0.12
 **/
gchar *
pk_offline_get_prepared_upgrade_version (GError **error)
{
	gchar *version = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!pk_offline_get_prepared_upgrade (NULL, &version, error))
		return NULL;

	return version;
}

/**
 * pk_offline_get_prepared_monitor:
 * @cancellable: A #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Gets a file monitor for the prepared transaction.
 *
 * Return value: (transfer full): A #GFileMonitor, or %NULL
 *
 * Since: 0.9.6
 **/
GFileMonitor *
pk_offline_get_prepared_monitor (GCancellable *cancellable, GError **error)
{
	g_autoptr(GFile) file = NULL;
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	file = g_file_new_for_path (pk_offline_get_prepared_filename ());
	return g_file_monitor_file (file, G_FILE_MONITOR_NONE, NULL, error);
}

/**
 * pk_offline_get_prepared_upgrade_monitor:
 * @cancellable: A #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Gets a file monitor for the prepared system upgrade transaction.
 *
 * Return value: (transfer full): A #GFileMonitor, or %NULL
 *
 * Since: 1.0.12
 **/
GFileMonitor *
pk_offline_get_prepared_upgrade_monitor (GCancellable *cancellable, GError **error)
{
	g_autoptr(GFile) file = NULL;
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	file = g_file_new_for_path (pk_offline_get_prepared_upgrade_filename ());
	return g_file_monitor_file (file, G_FILE_MONITOR_NONE, NULL, error);
}

/**
 * pk_offline_get_action_monitor:
 * @cancellable: A #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Gets a file monitor for the trigger.
 *
 * Return value: (transfer full): A #GFileMonitor, or %NULL
 *
 * Since: 0.9.6
 **/
GFileMonitor *
pk_offline_get_action_monitor (GCancellable *cancellable, GError **error)
{
	g_autoptr(GFile) file = NULL;
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	file = g_file_new_for_path (pk_offline_get_action_filename ());
	return g_file_monitor_file (file, G_FILE_MONITOR_NONE, NULL, error);
}

/**
 * pk_offline_get_results_mtime:
 * @error: A #GError or %NULL
 *
 * Gets the modification time of the prepared transaction.
 *
 * Return value: a unix time, or 0 for error.
 *
 * Since: 0.9.6
 **/
guint64
pk_offline_get_results_mtime (GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, 0);

	file = g_file_new_for_path (pk_offline_get_results_filename ());
	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_TIME_MODIFIED,
				  G_FILE_QUERY_INFO_NONE,
				  NULL,
				  &error_local);
	if (info == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			g_set_error (error,
				     PK_OFFLINE_ERROR,
				     PK_OFFLINE_ERROR_NO_DATA,
				     "%s does not exist",
				     pk_offline_get_results_filename ());
			return 0;
		}
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "Failed to read %s: %s",
			     pk_offline_get_results_filename (),
			     error_local->message);
		return 0;
	}
	return g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
}

/**
 * pk_offline_get_results:
 * @error: A #GError or %NULL
 *
 * Gets the last result of the offline transaction.
 *
 * Return value: (transfer full): A #PkResults, or %NULL
 *
 * Since: 0.9.6
 **/
PkResults *
pk_offline_get_results (GError **error)
{
	gboolean ret;
	gboolean success;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *role_str = NULL;
	g_autoptr(GKeyFile) file = NULL;
	g_autoptr(PkError) pk_error = NULL;
	g_autoptr(PkResults) results = NULL;
	g_auto(GStrv) package_ids = NULL;
	gsize package_ids_size;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* does not exist */
	if (!g_file_test (pk_offline_get_results_filename (), G_FILE_TEST_EXISTS)) {
		g_set_error_literal (error,
				     PK_OFFLINE_ERROR,
				     PK_OFFLINE_ERROR_NO_DATA,
				     "no update results available");
		return NULL;
	}

	/* load data */
	file = g_key_file_new ();
	g_key_file_set_list_separator (file, ',');
	ret = g_key_file_load_from_file (file,
					 pk_offline_get_results_filename (),
					 G_KEY_FILE_NONE,
					 &error_local);
	if (!ret) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "results file invalid: %s",
			     error_local->message);
		return NULL;
	}

	/* add error */
	results = pk_results_new ();
	success = g_key_file_get_boolean (file, PK_OFFLINE_RESULTS_GROUP, "Success", NULL);
	if (!success) {
		g_autofree gchar *details = NULL;
		g_autofree gchar *enum_str = NULL;
		pk_error = pk_error_new ();
		enum_str = g_key_file_get_string (file,
						  PK_OFFLINE_RESULTS_GROUP,
						  "ErrorCode",
						  NULL);
		details = g_key_file_get_string (file,
						 PK_OFFLINE_RESULTS_GROUP,
						 "ErrorDetails",
						 NULL);
		g_object_set (pk_error,
			      "code",
			      pk_error_enum_from_string (enum_str),
			      "details",
			      details,
			      NULL);
		pk_results_set_error_code (results, pk_error);
		pk_results_set_exit_code (results, PK_EXIT_ENUM_FAILED);
	} else {
		pk_results_set_exit_code (results, PK_EXIT_ENUM_SUCCESS);
	}

	/* set role */
	role_str = g_key_file_get_string (file, PK_OFFLINE_RESULTS_GROUP, "Role", NULL);
	if (role_str != NULL)
		pk_results_set_role (results, pk_role_enum_from_string (role_str));

	/* add packages */
	package_ids = g_key_file_get_string_list (file,
						  PK_OFFLINE_RESULTS_GROUP,
						  "Packages",
						  &package_ids_size,
						  NULL);
	if (package_ids == NULL || package_ids_size == 0)
		goto out;

	for (guint i = 0; package_ids[i] != NULL; i++) {
		g_autoptr(PkPackage) pkg = NULL;
		pkg = pk_package_new ();
		pk_package_set_info (pkg, PK_INFO_ENUM_UPDATING);
		if (!pk_package_set_id (pkg, package_ids[i], error))
			return NULL;
		pk_results_add_package (results, pkg);
	}
out:
	return g_object_ref (results);
}
