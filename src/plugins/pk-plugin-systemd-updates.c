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

#include <packagekit-glib2/pk-offline.h>
#include <packagekit-glib2/pk-offline-private.h>

/**
 * pk_plugin_get_description:
 */
const gchar *
pk_plugin_get_description (void)
{
	return "A plugin to write the prepared-updates file";
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
	_cleanup_error_free_ GError *error = NULL;
	/* if the state changed because of a dnf command that could
	 * have changed the updates list then nuke the prepared-updates
	 * file */
	g_debug ("Invalidating any offline update as state changed");
	if (!pk_offline_auth_invalidate (&error))
		g_warning ("failed to invalidate: %s", error->message);
}

/**
 * pk_plugin_transaction_update_packages:
 */
static void
pk_plugin_transaction_update_packages (PkTransaction *transaction)
{
	gchar **package_ids;
	_cleanup_error_free_ GError *error = NULL;

	/* write the package ids to the prepared update file */
	package_ids = pk_transaction_get_package_ids (transaction);
	if (!pk_offline_auth_set_prepared_ids (package_ids, &error))
		g_warning ("failed to write offline update: %s", error->message);
}

/**
 * pk_plugin_transaction_action_method:
 */
static void
pk_plugin_transaction_action_method (PkPlugin *plugin,
				     PkTransaction *transaction,
				     PkResults *results)
{
	PkPackage *pkg;
	const gchar *package_id;
	gchar **package_ids;
	guint i;
	_cleanup_object_unref_ PkPackageSack *sack;
	_cleanup_ptrarray_unref_ GPtrArray *invalidated = NULL;

	/* get the existing prepared updates */
	sack = pk_offline_get_prepared_sack (NULL);
	if (sack == NULL)
		return;

	/* are there any requested packages that match in prepared-updates */
	package_ids = pk_transaction_get_package_ids (transaction);
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = pk_package_sack_find_by_id_name_arch (sack, package_ids[i]);
		if (pkg != NULL) {
			g_debug ("%s modified %s, invalidating prepared-updates",
				 package_ids[i], pk_package_get_id (pkg));
			pk_plugin_state_changed (plugin);
			g_object_unref (pkg);
			return;
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
			return;
		}
	}
}

/**
 * pk_plugin_transaction_get_updates:
 */
static void
pk_plugin_transaction_get_updates (PkTransaction *transaction)
{
	PkResults *results;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_ptrarray_unref_ GPtrArray *array = NULL;

	results = pk_transaction_get_results (transaction);
	array = pk_results_get_package_array (results);
	if (array->len != 0) {
		g_debug ("got %i updates, so ignoring offline update", array->len);
		return;
	}
	if (!pk_offline_auth_invalidate (&error))
		g_warning ("failed to invalidate: %s", error->message);
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
		return;
	}

	/* don't do anything if the method failed */
	results = pk_transaction_get_results (transaction);
	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS)
		return;

	/* if we're doing UpdatePackages[only-download] then update the
	 * prepared-updates file */
	role = pk_transaction_get_role (transaction);
	transaction_flags = pk_transaction_get_transaction_flags (transaction);
	if (role == PK_ROLE_ENUM_UPDATE_PACKAGES &&
	    pk_bitfield_contain (transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD)) {
		pk_plugin_transaction_update_packages (transaction);
		return;
	}

	/* if we do get-updates and there's no updates then remove
	 * prepared-updates so the UI doesn't display update & reboot */
	if (role == PK_ROLE_ENUM_GET_UPDATES) {
		pk_plugin_transaction_get_updates (transaction);
		return;
	}

	/* delete the prepared updates file as it's no longer valid */
	if (role == PK_ROLE_ENUM_REFRESH_CACHE ||
	    role == PK_ROLE_ENUM_REPO_SET_DATA ||
	    role == PK_ROLE_ENUM_REPO_ENABLE) {
		pk_plugin_state_changed (plugin);
		return;
	}

	/* delete the file if the action affected any package already listed in
	 * the prepared updates file */
	if (role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		pk_plugin_transaction_action_method (plugin, transaction, results);
	}
}
