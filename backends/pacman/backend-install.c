/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
 * Copyright (C) 2008, 2009 Valeriy Lyasotskiy <onestep@ukr.net>
 * Copyright (C) 2010 Jonathan Conder <j@skurvy.no-ip.org>
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

#include <pacman.h>
#include "backend-error.h"
#include "backend-pacman.h"
#include "backend-packages.h"
#include "backend-repos.h"
#include "backend-transaction.h"
#include "backend-install.h"

static PacmanList *
backend_transaction_list_targets (PkBackend *backend)
{
	gchar **package_ids;
	guint iterator;
	PacmanList *list = NULL;

	g_return_val_if_fail (backend != NULL, NULL);

	package_ids = pk_backend_get_strv (backend, "package_ids");

	g_return_val_if_fail (package_ids != NULL, NULL);

	for (iterator = 0; package_ids[iterator] != NULL; ++iterator) {
		gchar **package_id_data = pk_package_id_split (package_ids[iterator]);
		list = pacman_list_add (list, g_strdup_printf ("%s/%s", package_id_data[PK_PACKAGE_ID_DATA], package_id_data[PK_PACKAGE_ID_NAME]));
		g_strfreev (package_id_data);
	}

	return list;
}

static void
backend_transaction_packages (PkBackend *backend, PacmanTransaction *transaction)
{
	const PacmanList *removes, *packages;

	g_return_if_fail (backend != NULL);
	g_return_if_fail (transaction != NULL);

	/* emit packages that would have been installed */
	for (packages = pacman_transaction_get_packages (transaction); packages != NULL; packages = pacman_list_next (packages)) {
		PacmanPackage *package = (PacmanPackage *) pacman_list_get (packages);

		for (removes = pacman_package_get_removes (package); removes != NULL; removes = pacman_list_next (removes)) {
			PacmanPackage *remove = (PacmanPackage *) pacman_list_get (removes);

			if (backend_cancelled (backend)) {
				break;
			} else {
				PkRoleEnum role = pk_backend_get_role (backend);
				if (role == PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES) {
					backend_package (backend, remove, PK_INFO_ENUM_OBSOLETING);
				} else {
					backend_package (backend, remove, PK_INFO_ENUM_REMOVING);
				}
			}
		}

		if (backend_cancelled (backend)) {
			break;
		} else {
			const gchar *name = pacman_package_get_name (package);
			if (pacman_database_find_package (local_database, name) != NULL) {
				backend_package (backend, package, PK_INFO_ENUM_UPDATING);
			} else {
				backend_package (backend, package, PK_INFO_ENUM_INSTALLING);
			}
		}
	}
}

static gboolean
backend_download_packages_thread (PkBackend *backend)
{
	PacmanList *list;
	PacmanList *cache_paths;
	const gchar *directory;

	PacmanTransaction *transaction = NULL;
	PacmanTransactionFlags flags = PACMAN_TRANSACTION_FLAGS_IGNORE_DEPENDENCIES |
				       PACMAN_TRANSACTION_FLAGS_IGNORE_DEPENDENCY_CONFLICTS |
				       PACMAN_TRANSACTION_FLAGS_SYNC_DOWNLOAD_ONLY;

	g_return_val_if_fail (pacman != NULL, FALSE);
	g_return_val_if_fail (backend != NULL, FALSE);

	directory = pk_backend_get_string (backend, "directory");

	g_return_val_if_fail (directory != NULL, FALSE);

	/* download files to a PackageKit directory */
	cache_paths = pacman_list_strdup (pacman_manager_get_cache_paths (pacman));
	pacman_manager_set_cache_paths (pacman, NULL);
	pacman_manager_add_cache_path (pacman, directory);

	/* run the transaction */
	list = backend_transaction_list_targets (backend);
	if (list != NULL) {
		transaction = backend_transaction_run (backend, PACMAN_TRANSACTION_SYNC, flags, list);
		pacman_list_free_full (list, g_free);
	}

	pacman_manager_set_cache_paths (pacman, cache_paths);
	pacman_list_free_full (cache_paths, g_free);
	return backend_transaction_finished (backend, transaction);
}

/**
 * backend_download_packages:
 **/
void
backend_download_packages (PkBackend *backend, gchar **package_ids, const gchar *directory)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (package_ids != NULL);
	g_return_if_fail (directory != NULL);

	backend_run (backend, PK_STATUS_ENUM_SETUP, backend_download_packages_thread);
}

static gboolean
backend_install_files_thread (PkBackend *backend)
{
	guint iterator;
	PacmanList *list = NULL;

	/* FS#5331: use only_trusted */
	gchar **full_paths;

	PacmanTransaction *transaction = NULL;
	PacmanTransactionFlags flags = PACMAN_TRANSACTION_FLAGS_NONE;

	g_return_val_if_fail (backend != NULL, FALSE);

	full_paths = pk_backend_get_strv (backend, "full_paths");

	g_return_val_if_fail (full_paths != NULL, FALSE);

	/* run the transaction */
	for (iterator = 0; full_paths[iterator] != NULL; ++iterator) {
		list = pacman_list_add (list, full_paths[iterator]);
	}
	if (list != NULL) {
		transaction = backend_transaction_run (backend, PACMAN_TRANSACTION_INSTALL, flags, list);
		pacman_list_free (list);
	}

	return backend_transaction_finished (backend, transaction);
}

/**
 * backend_install_files:
 **/
void
backend_install_files (PkBackend *backend, gboolean only_trusted, gchar	**full_paths)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (full_paths != NULL);

	backend_run (backend, PK_STATUS_ENUM_SETUP, backend_install_files_thread);
}

static gboolean
backend_install_packages_thread (PkBackend *backend)
{
	PacmanList *list;
	/* FS#5331: use only_trusted */
	PacmanTransaction *transaction = NULL;
	PacmanTransactionFlags flags = PACMAN_TRANSACTION_FLAGS_NONE;

	g_return_val_if_fail (backend != NULL, FALSE);

	/* run the transaction */
	list = backend_transaction_list_targets (backend);
	if (list != NULL) {
		transaction = backend_transaction_run (backend, PACMAN_TRANSACTION_SYNC, flags, list);
		pacman_list_free_full (list, g_free);
	}

	return backend_transaction_finished (backend, transaction);
}

/**
 * backend_install_packages:
 **/
void
backend_install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (package_ids != NULL);

	backend_run (backend, PK_STATUS_ENUM_SETUP, backend_install_packages_thread);
}

static gboolean
backend_simulate_install_packages_thread (PkBackend *backend)
{
	PacmanList *list;

	PacmanTransaction *transaction = NULL;
	PacmanTransactionFlags flags = PACMAN_TRANSACTION_FLAGS_NONE;

	g_return_val_if_fail (backend != NULL, FALSE);

	/* prepare the transaction */
	list = backend_transaction_list_targets (backend);
	if (list != NULL) {
		transaction = backend_transaction_simulate (backend, PACMAN_TRANSACTION_SYNC, flags, list);
		pacman_list_free_full (list, g_free);

		if (transaction != NULL) {
			/* emit packages that would have been installed or removed */
			backend_transaction_packages (backend, transaction);
		}
	}

	return backend_transaction_finished (backend, transaction);
}

/**
 * backend_simulate_install_packages:
 **/
void
backend_simulate_install_packages (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (package_ids != NULL);

	backend_run (backend, PK_STATUS_ENUM_SETUP, backend_simulate_install_packages_thread);
}

static gboolean
backend_update_packages_thread (PkBackend *backend)
{
	PacmanList *list, *asdeps = NULL;
	/* FS#5331: use only_trusted */
	PacmanTransaction *transaction = NULL;
	PacmanTransactionFlags flags = PACMAN_TRANSACTION_FLAGS_NONE, dflags = PACMAN_TRANSACTION_FLAGS_INSTALL_IMPLICIT;

	g_return_val_if_fail (backend != NULL, FALSE);

	/* prepare the transaction */
	list = backend_transaction_list_targets (backend);
	if (list != NULL) {
		transaction = backend_transaction_simulate (backend, PACMAN_TRANSACTION_SYNC, flags, list);
		pacman_list_free_full (list, g_free);

		if (transaction != NULL) {
			const PacmanList *packages;

			/* check for packages that should be installed as deps */
			for (packages = pacman_transaction_get_packages (transaction); packages != NULL; packages = pacman_list_next (packages)) {
				PacmanPackage *package = (PacmanPackage *) pacman_list_get (packages);
				const PacmanList *removes = pacman_package_get_removes (package);

				if (backend_cancelled (backend)) {
					break;
				} else if (removes != NULL) {
					for (; removes != NULL; removes = pacman_list_next (removes)) {
						PacmanPackage *remove = (PacmanPackage *) pacman_list_get (removes);

						if (backend_cancelled (backend)) {
							break;
						} else if (pacman_package_was_explicitly_installed (remove)) {
							break;
						}
					}

					/* if all removes were installed as dependencies, do the same for this package */
					if (removes == NULL) {
						const gchar *name, *repo;
						PacmanDatabase *database = pacman_package_get_database (package);

						name = pacman_package_get_name (package);
						repo = pacman_database_get_name (database);

						asdeps = pacman_list_add (asdeps, g_strdup_printf ("%s/%s", repo, name));
					}
				}
			}

			transaction = backend_transaction_commit (backend, transaction);
		}
	}

	/* mark replacements as deps if required */
	if (transaction != NULL && asdeps != NULL) {
		g_object_unref (transaction);
		transaction = backend_transaction_run (backend, PACMAN_TRANSACTION_SYNC, dflags, asdeps);
		pacman_list_free_full (asdeps, g_free);
	} else if (asdeps != NULL) {
		pacman_list_free_full (asdeps, g_free);
	}

	return backend_transaction_finished (backend, transaction);
}

/**
 * backend_update_packages:
 **/
void
backend_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (package_ids != NULL);

	backend_run (backend, PK_STATUS_ENUM_SETUP, backend_update_packages_thread);
}

/**
 * backend_simulate_update_packages:
 **/
void
backend_simulate_update_packages (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (package_ids != NULL);

	backend_run (backend, PK_STATUS_ENUM_SETUP, backend_simulate_install_packages_thread);
}
