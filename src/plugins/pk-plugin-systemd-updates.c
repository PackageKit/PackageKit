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
static PkPackageSack *
pk_plugin_get_existing_prepared_updates (const gchar *filename)
{
	gboolean ret;
	gchar **package_ids = NULL;
	gchar *packages_data = NULL;
	GError *error = NULL;
	PkPackageSack *sack;
	guint i;

	/* always return a valid sack, even for failure */
	sack = pk_package_sack_new ();

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
		pk_package_sack_add_package_by_id (sack, package_ids[i], NULL);
out:
	g_free (packages_data);
	g_strfreev (package_ids);
	return sack;
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
	GError *error = NULL;
	PkPackage *pkg;
	PkPackageSack *sack;
	gboolean ret;
	gchar **package_ids;
	gchar *packages_str = NULL;
	guint i;

	/* get the existing prepared updates */
	sack = pk_plugin_get_existing_prepared_updates (PK_OFFLINE_PREPARED_UPDATE_FILENAME);

	/* add any new ones */
	package_ids = pk_transaction_get_package_ids (transaction);
	for (i = 0; package_ids[i] != NULL; i++) {
		/* does this package match exactly */
		pkg = pk_package_sack_find_by_id (sack, package_ids[i]);
		if (pkg != NULL) {
			g_object_unref (pkg);
		} else {
			/* does this package exist in another version */
			pkg = pk_package_sack_find_by_id_name_arch (sack, package_ids[i]);
			if (pkg != NULL) {
				g_debug ("removing old update %s from prepared updates",
					 pk_package_get_id (pkg));
				pk_package_sack_remove_package (sack, pkg);
				g_object_unref (pkg);
			}
			g_debug ("adding new update %s to prepared updates",
				 package_ids[i]);
			pk_package_sack_add_package_by_id (sack, package_ids[i], NULL);
		}
	}

	/* write filename */
	package_ids = pk_package_sack_get_ids (sack);
	packages_str = g_strjoinv ("\n", package_ids);
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
	g_object_unref (sack);
	g_strfreev (package_ids);
}

/**
 * pk_plugin_transaction_action_method:
 */
static void
pk_plugin_transaction_action_method (PkPlugin *plugin,
				     PkTransaction *transaction,
				     PkResults *results)
{
	GPtrArray *invalidated = NULL;
	PkPackage *pkg;
	PkPackageSack *sack;
	const gchar *package_id;
	gchar **package_ids;
	guint i;

	/* get the existing prepared updates */
	sack = pk_plugin_get_existing_prepared_updates (PK_OFFLINE_PREPARED_UPDATE_FILENAME);
	if (pk_package_sack_get_size (sack) == 0)
		goto out;

	/* are there any requested packages that match in prepared-updates */
	package_ids = pk_transaction_get_package_ids (transaction);
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = pk_package_sack_find_by_id_name_arch (sack, package_ids[i]);
		if (pkg != NULL) {
			g_debug ("%s modified %s, invalidating prepared-updates",
				 package_ids[i], pk_package_get_id (pkg));
			pk_plugin_state_changed (plugin);
			g_object_unref (pkg);
			goto out;
		}
	}

	/* are there any changed deps that match a package in prepared-updates */
	invalidated = pk_results_get_package_array (results);
	for (i = 0; i < invalidated->len; i++) {
		package_id = pk_package_get_id (g_ptr_array_index (invalidated, i));
		pkg = pk_package_sack_find_by_id_name_arch (sack, package_id);
		if (pkg != NULL) {
			g_debug ("%s modified %s, invalidating prepared-updates",
				 package_id, pk_package_get_id (pkg));
			pk_plugin_state_changed (plugin);
			g_object_unref (pkg);
			goto out;
		}
	}
out:
	if (invalidated != NULL)
		g_ptr_array_unref (invalidated);
	g_object_unref (sack);
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

/* only do this if we have systemd */
#ifndef PK_BUILD_SYSTEMD
	g_debug ("No systemd, so no PreparedUpdates");
	return;
#endif

	/* skip simulate actions */
	if (pk_bitfield_contain (pk_transaction_get_transaction_flags (transaction),
				 PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		goto out;
	}

	/* don't do anything if the method failed */
	results = pk_transaction_get_results (transaction);
	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS)
		goto out;

	/* if we're doing UpdatePackages[only-download] then update the
	 * prepared-updates file */
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
	if (role == PK_ROLE_ENUM_REFRESH_CACHE ||
	    role == PK_ROLE_ENUM_REPO_SET_DATA ||
	    role == PK_ROLE_ENUM_REPO_ENABLE) {
		pk_plugin_state_changed (plugin);
		goto out;
	}

	/* delete the file if the action affected any package already listed in
	 * the prepared updates file */
	if (role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		pk_plugin_transaction_action_method (plugin, transaction, results);
	}
out:
	return;
}
