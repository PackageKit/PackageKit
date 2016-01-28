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
 * pk_offline_auth_set_action:
 * @action: a #PkOfflineAction, e.g. %PK_OFFLINE_ACTION_REBOOT
 * @error: A #GError or %NULL
 *
 * Sets the action to be done after the offline action has been performed.
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 0.9.6
 **/
gboolean
pk_offline_auth_set_action (PkOfflineAction action, GError **error)
{
	const gchar *action_str;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (action == PK_OFFLINE_ACTION_UNKNOWN) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_INVALID_VALUE,
			     "Failed to set unknown %i", action);
		return FALSE;
	}
	if (action == PK_OFFLINE_ACTION_UNSET)
		return pk_offline_auth_cancel (error);

	action_str = pk_offline_action_to_string (action);
	if (action_str == NULL) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "Failed to convert %i", action);
		return FALSE;
	}
	if (!g_file_set_contents (PK_OFFLINE_ACTION_FILENAME,
				  action_str, -1, &error_local)) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "failed to write file: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_offline_auth_cancel:
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
pk_offline_auth_cancel (GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file1 = NULL;
	g_autoptr(GFile) file2 = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	file1 = g_file_new_for_path (PK_OFFLINE_TRIGGER_FILENAME);
	if (!g_file_query_exists (file1, NULL))
		return TRUE;
	if (!g_file_delete (file1, NULL, &error_local)) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "Cannot delete %s: %s",
			     PK_OFFLINE_TRIGGER_FILENAME,
			     error_local->message);
		return FALSE;
	}
	file2 = g_file_new_for_path (PK_OFFLINE_ACTION_FILENAME);
	if (g_file_query_exists (file2, NULL) &&
	    !g_file_delete (file2, NULL, &error_local)) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "Cannot delete %s: %s",
			     PK_OFFLINE_ACTION_FILENAME,
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_offline_auth_clear_results:
 * @error: A #GError or %NULL
 *
 * Creates the last offline operation report, which may be success or failure.
 * If the report does not exist then this method returns success.
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 0.9.6
 **/
gboolean
pk_offline_auth_clear_results (GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not existing is success */
	if (!g_file_test (PK_OFFLINE_RESULTS_FILENAME, G_FILE_TEST_EXISTS))
		return TRUE;

	file = g_file_new_for_path (PK_OFFLINE_RESULTS_FILENAME);
	if (!g_file_delete (file, NULL, &error_local)) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "Cannot delete %s: %s",
			     PK_OFFLINE_RESULTS_FILENAME,
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_offline_auth_invalidate:
 * @error: A #GError or %NULL
 *
 * Invalidates the offline operation. This is normally done when the package
 * cache has been refreshed, or a package listed in the prepared transaction
 * is manually installed or removed.
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 0.9.6
 **/
gboolean
pk_offline_auth_invalidate (GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file1 = NULL;
	g_autoptr(GFile) file2 = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* cancel the pending update */
	if (!pk_offline_auth_cancel (error))
		return FALSE;

	/* delete the prepared file */
	file1 = g_file_new_for_path (PK_OFFLINE_PREPARED_FILENAME);
	if (g_file_query_exists (file1, NULL) &&
	    !g_file_delete (file1, NULL, &error_local)) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "Cannot delete %s: %s",
			     PK_OFFLINE_PREPARED_FILENAME,
			     error_local->message);
		return FALSE;
	}

	/* delete the prepared system upgrade file */
	file2 = g_file_new_for_path (PK_OFFLINE_PREPARED_UPGRADE_FILENAME);
	if (g_file_query_exists (file2, NULL) &&
	    !g_file_delete (file2, NULL, &error_local)) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "Cannot delete %s: %s",
			     PK_OFFLINE_PREPARED_UPGRADE_FILENAME,
			     error_local->message);
		return FALSE;
	}

	return TRUE;
}

static gboolean
pk_offline_auth_trigger_prepared_file (PkOfflineAction action, const gchar *prepared_file, GError **error)
{
	gint rc;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check the prepared update exists */
	if (!g_file_test (prepared_file, G_FILE_TEST_EXISTS)) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_NO_DATA,
			     "Prepared update not found: %s",
			     prepared_file);
		return FALSE;
	}

	/* triggering a new update clears the status from any previous one */
	if (!pk_offline_auth_clear_results (error))
		return FALSE;

	/* set the action type */
	if (!pk_offline_auth_set_action (action, error))
		return FALSE;

	/* create symlink for the systemd-system-update-generator */
	rc = symlink ("/var/cache/PackageKit", PK_OFFLINE_TRIGGER_FILENAME);
	if (rc < 0) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "Failed to create symlink: %s",
			     strerror (errno));
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_offline_auth_trigger:
 * @action: a #PkOfflineAction, e.g. %PK_OFFLINE_ACTION_REBOOT
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
pk_offline_auth_trigger (PkOfflineAction action, GError **error)
{
	return pk_offline_auth_trigger_prepared_file (action, PK_OFFLINE_PREPARED_FILENAME, error);
}

/**
 * pk_offline_auth_trigger_upgrade:
 * @action: a #PkOfflineAction, e.g. %PK_OFFLINE_ACTION_REBOOT
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
pk_offline_auth_trigger_upgrade (PkOfflineAction action, GError **error)
{
	return pk_offline_auth_trigger_prepared_file (action, PK_OFFLINE_PREPARED_UPGRADE_FILENAME, error);
}

/**
 * pk_offline_auth_set_prepared_ids:
 * @package_ids: Array of package-ids
 * @error: A #GError or %NULL
 *
 * Saves the package-ids to a prepared transaction file.
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 0.9.6
 **/
gboolean
pk_offline_auth_set_prepared_ids (gchar **package_ids, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(GKeyFile) keyfile = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	data = g_strjoinv (",", package_ids);
	keyfile = g_key_file_new ();
	g_key_file_set_string (keyfile, "update", "prepared_ids", data);
	return g_key_file_save_to_file (keyfile, PK_OFFLINE_PREPARED_FILENAME, error);
}

/**
 * pk_offline_auth_set_prepared_upgrade_version:
 * @release_ver: Distro version to upgrade to
 * @error: A #GError or %NULL
 *
 * Saves the distro version to upgrade to a prepared transaction file.
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 1.0.12
 **/
gboolean
pk_offline_auth_set_prepared_upgrade_version (const gchar *release_ver, GError **error)
{
	g_autoptr(GKeyFile) keyfile = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	keyfile = g_key_file_new ();
	g_key_file_set_string (keyfile, "update", "releasever", release_ver);
	return g_key_file_save_to_file (keyfile, PK_OFFLINE_PREPARED_UPGRADE_FILENAME, error);
}

/**
 * pk_offline_auth_set_results:
 * @results: A #PkResults
 * @error: A #GError or %NULL
 *
 * Saves the transaction results to a file.
 *
 * Return value: %TRUE for success, else %FALSE and @error set
 *
 * Since: 0.9.6
 **/
gboolean
pk_offline_auth_set_results (PkResults *results, GError **error)
{
	guint i;
	PkPackage *package;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *data = NULL;
	g_autoptr(GKeyFile) key_file = NULL;
	g_autoptr(PkError) pk_error = NULL;
	g_autoptr(GPtrArray) packages = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	key_file = g_key_file_new ();
	pk_error = pk_results_get_error_code (results);
	if (pk_error != NULL) {
		g_key_file_set_boolean (key_file,
					PK_OFFLINE_RESULTS_GROUP,
					"Success",
					FALSE);
		g_key_file_set_string (key_file,
				       PK_OFFLINE_RESULTS_GROUP,
				       "ErrorCode",
				       pk_error_enum_to_string (pk_error_get_code (pk_error)));
		g_key_file_set_string (key_file,
				       PK_OFFLINE_RESULTS_GROUP,
				       "ErrorDetails",
				       pk_error_get_details (pk_error));
	} else {
		g_key_file_set_boolean (key_file,
					PK_OFFLINE_RESULTS_GROUP,
					"Success",
					TRUE);
	}

	/* save packages if any set */
	packages = pk_results_get_package_array (results);
	if (packages->len > 0) {
		g_autoptr(GString) string = NULL;
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
				       PK_OFFLINE_RESULTS_GROUP,
				       "Packages",
				       string->str);
	}

	/* write file */
	data = g_key_file_to_data (key_file, NULL, &error_local);
	if (data == NULL) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "failed to get keyfile data: %s",
			     error_local->message);
		return FALSE;
	}
	if (!g_file_set_contents (PK_OFFLINE_RESULTS_FILENAME,
				  data, -1, &error_local)) {
		g_set_error (error,
			     PK_OFFLINE_ERROR,
			     PK_OFFLINE_ERROR_FAILED,
			     "failed to write file: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}
