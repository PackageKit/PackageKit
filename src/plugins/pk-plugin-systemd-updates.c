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

#include <config.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <pk-plugin.h>

#define PK_OFFLINE_PREPARED_UPDATE_FILENAME	"/var/lib/PackageKit/prepared-update"

/**
 * pk_plugin_get_description:
 */
const gchar *
pk_plugin_get_description (void)
{
	return "A plugin to write the prepared-updates file";
}

/**
 * pk_plugin_get_existing_prepared_updates:
 **/
static GPtrArray *
pk_plugin_get_existing_prepared_updates (const gchar *filename)
{
	gboolean ret;
	gchar **package_ids = NULL;
	gchar *packages_data = NULL;
	GError *error = NULL;
	GPtrArray *packages;
	guint i;

	/* always return a valid array, even for failure */
	packages = g_ptr_array_new_with_free_func (g_free);

	/* does the file exist ? */
	if (!g_file_test (filename, G_FILE_TEST_EXISTS))
		goto out;

	/* get the list of packages to update */
	ret = g_file_get_contents (filename,
				   &packages_data,
				   NULL,
				   &error);
	if (!ret) {
		g_warning ("failed to read: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* add them to the new array */
	package_ids = g_strsplit (packages_data, "\n", -1);
	for (i = 0; package_ids[i] != NULL; i++)
		g_ptr_array_add (packages, g_strdup (package_ids[i]));
out:
	g_free (packages_data);
	g_strfreev (package_ids);
	return packages;
}

/**
 * pk_plugin_array_str_exists:
 **/
static gboolean
pk_plugin_array_str_exists (GPtrArray *array, const gchar *str)
{
	guint i;
	const gchar *tmp;
	for (i = 0; i < array->len; i++) {
		tmp = g_ptr_array_index (array, i);
		if (g_strcmp0 (tmp, str) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * pk_plugin_state_changed:
 *
 * Delete the prepared-update if the daemon state has changed, for
 * instance, if the computer has just been resumed;
 */
void
pk_plugin_state_changed (PkPlugin *plugin)
{
	/* if the state changed because of a yum command that could
	 * have changed the updates list then nuke the prepared-updates
	 * file */
	if (g_file_test (PK_OFFLINE_PREPARED_UPDATE_FILENAME,
			 G_FILE_TEST_EXISTS)) {
		g_debug ("Removing %s as state has changed",
			 PK_OFFLINE_PREPARED_UPDATE_FILENAME);
		g_unlink (PK_OFFLINE_PREPARED_UPDATE_FILENAME);
	} else {
		g_debug ("No %s needed to be deleted",
			 PK_OFFLINE_PREPARED_UPDATE_FILENAME);
	}
}

/**
 * pk_plugin_transaction_update_packages:
 */
static void
pk_plugin_transaction_update_packages (PkTransaction *transaction)
{
	gboolean ret;
	gchar **package_ids;
	gchar *packages_str = NULL;
	GError *error = NULL;
	GPtrArray *packages;
	guint i;

/* only do this if we have systemd */
#ifndef PK_BUILD_SYSTEMD
	g_debug ("No systemd, so no PreparedUpdates");
	return;
#endif

	/* get the existing prepared updates */
	packages = pk_plugin_get_existing_prepared_updates (PK_OFFLINE_PREPARED_UPDATE_FILENAME);

	/* add any new ones */
	package_ids = pk_transaction_get_package_ids (transaction);
	for (i = 0; package_ids[i] != NULL; i++) {
		if (!pk_plugin_array_str_exists (packages, package_ids[i])) {
			g_ptr_array_add (packages,
					 g_strdup (package_ids[i]));
		}
	}
	g_ptr_array_add (packages, NULL);

	/* write filename */
	packages_str = g_strjoinv ("\n", (gchar **) packages->pdata);
	ret = g_file_set_contents (PK_OFFLINE_PREPARED_UPDATE_FILENAME,
				   packages_str,
				   -1,
				   &error);
	if (!ret) {
		g_warning ("failed to write %s: %s",
			   PK_OFFLINE_PREPARED_UPDATE_FILENAME,
			   error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (packages_str);
	return;
}

/**
 * pk_plugin_transaction_get_updates:
 */
static void
pk_plugin_transaction_get_updates (PkTransaction *transaction)
{
	GPtrArray *array;
	PkResults *results;

	results = pk_transaction_get_results (transaction);
	array = pk_results_get_package_array (results);
	if (array->len != 0) {
		g_debug ("got %i updates, so ignoring %s",
			 array->len, PK_OFFLINE_PREPARED_UPDATE_FILENAME);
		goto out;
	}
	if (g_file_test (PK_OFFLINE_PREPARED_UPDATE_FILENAME,
			 G_FILE_TEST_EXISTS)) {
		g_debug ("Removing %s as no updates",
			 PK_OFFLINE_PREPARED_UPDATE_FILENAME);
		g_unlink (PK_OFFLINE_PREPARED_UPDATE_FILENAME);
	} else {
		g_debug ("No %s present, so no need to delete",
			 PK_OFFLINE_PREPARED_UPDATE_FILENAME);
	}
out:
	g_ptr_array_unref (array);
}

/**
 * pk_plugin_transaction_finished_end:
 */
void
pk_plugin_transaction_finished_end (PkPlugin *plugin,
				    PkTransaction *transaction)
{
	PkBitfield transaction_flags;
	PkExitEnum exit_enum;
	PkResults *results;
	PkRoleEnum role;

	/* skip simulate actions */
	if (pk_bitfield_contain (pk_transaction_get_transaction_flags (transaction),
				 PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		goto out;
	}

	/* check for success */
	results = pk_transaction_get_results (transaction);
	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS) {
		g_debug ("not writing %s as transaction failed",
			 PK_OFFLINE_PREPARED_UPDATE_FILENAME);
		goto out;
	}

	/* if we're doing only-download then update prepared-updates */
	role = pk_transaction_get_role (transaction);
	transaction_flags = pk_transaction_get_transaction_flags (transaction);
	if (role == PK_ROLE_ENUM_UPDATE_PACKAGES &&
	    pk_bitfield_contain (transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD)) {
		pk_plugin_transaction_update_packages (transaction);
		goto out;
	}

	/* if we do get-updates and there's no updates then remove
	 * prepared-updates so the UI doesn't display update & reboot */
	if (role == PK_ROLE_ENUM_GET_UPDATES) {
		pk_plugin_transaction_get_updates (transaction);
		goto out;
	}

	/* delete the prepared updates file as it's no longer valid */
	if (role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    role == PK_ROLE_ENUM_REMOVE_PACKAGES ||
	    role == PK_ROLE_ENUM_REFRESH_CACHE) {
		pk_plugin_state_changed (plugin);
	}
out:
	return;
}
