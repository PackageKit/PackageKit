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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <errno.h>
#include <string.h>

#include "pk-offline.h"
#include "pk-offline-private.h"

/**
 * pk_offline_error_quark:
 *
 * Return value: The error quark.
 *
 * Since: 0.9.6
 **/
G_DEFINE_QUARK (pk-offline-error-quark, pk_offline_error)

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

/**
 * pk_offline_cancel:
 * @cancellable: A #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Cancels the offline operation that has been scheduled. If there is no
 * scheduled offline operation then this method returns with success.
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 0.9.6
 **/
gboolean
pk_offline_cancel (GCancellable *cancellable, GError **error)
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
					   G_DBUS_CALL_FLAGS_NONE,
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
 * Crears the last offline operation report, which may be success or failure.
 * If the report does not exist then this method returns success.
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 0.9.6
 **/
gboolean
pk_offline_clear_results (GCancellable *cancellable, GError **error)
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
					   G_DBUS_CALL_FLAGS_NONE,
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
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 0.9.6
 **/
gboolean
pk_offline_trigger (PkOfflineAction action, GCancellable *cancellable, GError **error)
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
					   G_DBUS_CALL_FLAGS_NONE,
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
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 1.0.12
 **/
gboolean
pk_offline_trigger_upgrade (PkOfflineAction action, GCancellable *cancellable, GError **error)
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
					   G_DBUS_CALL_FLAGS_NONE,
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
	if (!g_file_test (PK_OFFLINE_TRIGGER_FILENAME, G_FILE_TEST_EXISTS) ||
	    !g_file_test (PK_OFFLINE_ACTION_FILENAME, G_FILE_TEST_EXISTS))
		return PK_OFFLINE_ACTION_UNSET;

	/* read data file */
	if (!g_file_get_contents (PK_OFFLINE_ACTION_FILENAME,
				  &action_data, NULL, &error_local)) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "Failed to open %s: %s",
			     PK_OFFLINE_ACTION_FILENAME,
			     error_local->message);
		return PK_OFFLINE_ACTION_UNKNOWN;
	}
	action = pk_offline_action_from_string (action_data);
	if (action == PK_OFFLINE_ACTION_UNKNOWN) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_INVALID_VALUE,
			     "Failed to parse '%s'", action_data);
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
		if (!pk_package_sack_add_package_by_id (sack,
							package_ids[i],
							error))
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
	g_autofree gchar *prepared_ids = NULL;
	g_autoptr(GKeyFile) keyfile = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* does exist? */
	if (!g_file_test (PK_OFFLINE_PREPARED_FILENAME, G_FILE_TEST_EXISTS)) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_NO_DATA,
			     "No offline updates have been prepared");
		return NULL;
	}

	/* read data file */
	if (!g_file_get_contents (PK_OFFLINE_PREPARED_FILENAME,
				  &data, NULL, &error_local)) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "Failed to read %s: %s",
			     PK_OFFLINE_PREPARED_FILENAME,
			     error_local->message);
		return NULL;
	}

	keyfile = g_key_file_new ();
	if (!g_key_file_load_from_data (keyfile, data, -1, G_KEY_FILE_NONE, &error_local)) {
		/* fall back to previous plain text file format for backwards compatibility */
		return g_strsplit (data, "\n", -1);
	}

	prepared_ids = g_key_file_get_string (keyfile, "update", "prepared_ids", error);
	if (prepared_ids == NULL)
		return NULL;

	/* return raw package ids */
	return g_strsplit (prepared_ids, ",", -1);
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
	file = g_file_new_for_path (PK_OFFLINE_PREPARED_FILENAME);
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
	file = g_file_new_for_path (PK_OFFLINE_PREPARED_UPGRADE_FILENAME);
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
	file = g_file_new_for_path (PK_OFFLINE_ACTION_FILENAME);
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

	file = g_file_new_for_path (PK_OFFLINE_RESULTS_FILENAME);
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
				     PK_OFFLINE_RESULTS_FILENAME);
			return 0;
		}
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "Failed to read %s: %s",
			     PK_OFFLINE_RESULTS_FILENAME,
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
	guint i;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *data = NULL;
	g_autoptr(GKeyFile) file = NULL;
	g_autoptr(PkError) pk_error = NULL;
	g_autoptr(PkResults) results = NULL;
	g_auto(GStrv) package_ids = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* does not exist */
	if (!g_file_test (PK_OFFLINE_RESULTS_FILENAME, G_FILE_TEST_EXISTS)) {
		g_set_error_literal (error,
				     PK_OFFLINE_ERROR,
				     PK_OFFLINE_ERROR_NO_DATA,
				     "no update results available");
		return NULL;
	}

	/* load data */
	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file,
					 PK_OFFLINE_RESULTS_FILENAME,
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
	success = g_key_file_get_boolean (file, PK_OFFLINE_RESULTS_GROUP,
					  "Success", NULL);
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
			      "code", pk_error_enum_from_string (enum_str),
			      "details", details,
			      NULL);
		pk_results_set_error_code (results, pk_error);
		pk_results_set_exit_code (results, PK_EXIT_ENUM_FAILED);
	} else {
		pk_results_set_exit_code (results, PK_EXIT_ENUM_SUCCESS);
	}

	/* add packages */
	data = g_key_file_get_string (file, PK_OFFLINE_RESULTS_GROUP,
				      "Packages", NULL);
	if (data != NULL) {
		package_ids = g_strsplit (data, ",", -1);
		for (i = 0; package_ids[i] != NULL; i++) {
			g_autoptr(PkPackage) pkg = NULL;
			pkg = pk_package_new ();
			pk_package_set_info (pkg, PK_INFO_ENUM_UPDATING);
			if (!pk_package_set_id (pkg, package_ids[i], error))
				return NULL;
			pk_results_add_package (results, pkg);
		}
	}
	return g_object_ref (results);
}
