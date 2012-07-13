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
 */
void
pk_plugin_state_changed (PkPlugin *plugin)
{
	gchar *file;

	/* if the state changed because of a yum command that could
	 * have changed the updates list then nuke the prepared-updates
	 * file */
	file = g_build_filename (LOCALSTATEDIR,
				 "lib",
				 "PackageKit",
				 "prepared-update",
				 NULL);
	if (g_file_test (file, G_FILE_TEST_EXISTS)) {
		g_debug ("Removing %s as state has changed", file);
		g_unlink (file);
	} else {
		g_debug ("No %s needed to be deleted", file);
	}
	g_free (file);
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
	gchar *path = NULL;
	GError *error = NULL;
	GPtrArray *packages;
	guint i;
	PkBitfield transaction_flags;

	/* only write the file for only-download */
	transaction_flags = pk_transaction_get_transaction_flags (transaction);
	if (!pk_bitfield_contain (transaction_flags,
				  PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD))
		return;

	/* get the existing prepared updates */
	path = g_build_filename (LOCALSTATEDIR,
				 "lib",
				 "PackageKit",
				 "prepared-update",
				 NULL);
	packages = pk_plugin_get_existing_prepared_updates (path);

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
	ret = g_file_set_contents (path,
				   packages_str,
				   -1,
				   &error);
	if (!ret) {
		g_warning ("failed to write %s: %s",
			   path, error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (packages_str);
	g_free (path);
	return;
}

/**
 * pk_plugin_transaction_get_updates:
 */
static void
pk_plugin_transaction_get_updates (PkTransaction *transaction)
{
	gchar *path;
	GPtrArray *array;
	PkResults *results;

	results = pk_transaction_get_results (transaction);
	path = g_build_filename (LOCALSTATEDIR,
				 "lib",
				 "PackageKit",
				 "prepared-update",
				 NULL);
	array = pk_results_get_package_array (results);
	if (array->len != 0) {
		g_debug ("got %i updates, so ignoring %s",
			 array->len, path);
		goto out;
	}
	if (g_file_test (path, G_FILE_TEST_EXISTS)) {
		g_debug ("Removing %s as no updates", path);
		g_unlink (path);
	} else {
		g_debug ("No %s present, so no need to delete", path);
	}
out:
	g_free (path);
	g_ptr_array_unref (array);
}

/**
 * pk_plugin_transaction_finished_end:
 */
void
pk_plugin_transaction_finished_end (PkPlugin *plugin,
				    PkTransaction *transaction)
{
	PkExitEnum exit_enum;
	PkResults *results;
	PkRoleEnum role;

	/* skip simulate actions */
	if (pk_bitfield_contain (pk_transaction_get_transaction_flags (transaction),
				PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		return;
	}

	/* check for success */
	results = pk_transaction_get_results (transaction);
	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS)
		goto out;

	/* if we're doing only-download then update prepared-updates */
	role = pk_transaction_get_role (transaction);
	if (role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		pk_plugin_transaction_update_packages (transaction);
		goto out;
	}

	/* if we do get-updates and there's no updates then remove
	 * prepared-updates so the UI doesn't display update & reboot */
	if (role == PK_ROLE_ENUM_GET_UPDATES) {
		pk_plugin_transaction_get_updates (transaction);
		goto out;
	}
out:
	return;
}
