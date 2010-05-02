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
#include "backend-groups.h"
#include "backend-pacman.h"
#include "backend-repos.h"
#include "backend-packages.h"

gchar *
pacman_package_make_id (PacmanPackage *package)
{
	const gchar *name, *version, *arch, *repo;
	PacmanDatabase *database;

	g_return_val_if_fail (local_database != NULL, NULL);
	g_return_val_if_fail (package != NULL, NULL);

	name = pacman_package_get_name (package);
	version = pacman_package_get_version (package);

	arch = pacman_package_get_arch (package);
	if (arch == NULL) {
		arch = "any";
	}

	/* PackageKit requires "local" for package files and "installed" for installed packages */
	database = pacman_package_get_database (package);
	if (database == NULL) {
		repo = "local";
	} else if (database == local_database) {
		repo = "installed";
	} else {
		repo = pacman_database_get_name (database);
	}

	return pk_package_id_build (name, version, arch, repo);
}

void
backend_package (PkBackend *backend, PacmanPackage *package, PkInfoEnum info)
{
	gchar *package_id;

	g_return_if_fail (backend != NULL);
	g_return_if_fail (package != NULL);

	/* build and emit package id */
	package_id = pacman_package_make_id (package);
	pk_backend_package (backend, info, package_id, pacman_package_get_description (package));
	g_free (package_id);
}

PacmanPackage *
backend_get_package (PkBackend *backend, const gchar *package_id)
{
	gchar **package_id_data;
	const gchar *repo;
	PacmanDatabase *database;
	PacmanPackage *package;

	g_return_val_if_fail (pacman != NULL, NULL);
	g_return_val_if_fail (local_database != NULL, NULL);
	g_return_val_if_fail (backend != NULL, NULL);
	g_return_val_if_fail (package_id != NULL, NULL);

	package_id_data = pk_package_id_split (package_id);
	repo = package_id_data[PK_PACKAGE_ID_DATA];

	/* find the database to search in */
	if (g_strcmp0 (repo, "installed") == 0) {
		database = local_database;
	} else {
		database = pacman_manager_find_sync_database (pacman, repo);
	}

	if (database == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "Could not find repo [%s]", repo);
		g_strfreev (package_id_data);
		return NULL;
	}

	/* find the package in the database */
	package = pacman_database_find_package (database, package_id_data[PK_PACKAGE_ID_NAME]);
	if (package == NULL || g_strcmp0 (pacman_package_get_version (package), package_id_data[PK_PACKAGE_ID_VERSION]) != 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "Could not find package with ID %s", package_id);
		g_strfreev (package_id_data);
		return NULL;
	}

	g_strfreev (package_id_data);
	return package;
}

static gboolean
backend_resolve_thread (PkBackend *backend)
{
	guint iterator;

	gchar **package_ids;
	PkBitfield filters;

	gboolean search_installed;
	gboolean search_not_installed;

	g_return_val_if_fail (pacman != NULL, FALSE);
	g_return_val_if_fail (local_database != NULL, FALSE);
	g_return_val_if_fail (backend != NULL, FALSE);

	package_ids = pk_backend_get_strv (backend, "package_ids");
	filters = pk_backend_get_uint (backend, "filters");

	g_return_val_if_fail (package_ids != NULL, FALSE);

	search_installed = pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED);
	search_not_installed = pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED);

	for (iterator = 0; package_ids[iterator] != NULL; ++iterator) {
		if (backend_cancelled (backend)) {
			break;
		}

		/* find a package with the given id or name */
		if (pk_package_id_check (package_ids[iterator])) {
			PacmanPackage *package = backend_get_package (backend, package_ids[iterator]);
			if (package == NULL) {
				backend_finished (backend);
				return FALSE;
			}

			/* don't emit when not needed */
			if (pacman_package_get_database (package) == local_database) {
				if (!search_not_installed) {
					backend_package (backend, package, PK_INFO_ENUM_INSTALLED);
				}
			} else {
				if (!search_installed) {
					backend_package (backend, package, PK_INFO_ENUM_AVAILABLE);
				}
			}
		} else {
			/* find installed packages first */
			if (!search_not_installed) {
				PacmanPackage *package = pacman_database_find_package (local_database, package_ids[iterator]);

				if (package != NULL) {
					backend_package (backend, package, PK_INFO_ENUM_INSTALLED);
					continue;
				}
			}

			if (!search_installed) {
				const PacmanList *databases;

				for (databases = pacman_manager_get_sync_databases (pacman); databases != NULL; databases = pacman_list_next (databases)) {
					PacmanDatabase *database = (PacmanDatabase *) pacman_list_get (databases);
					PacmanPackage *package = pacman_database_find_package (database, package_ids[iterator]);

					if (package != NULL) {
						backend_package (backend, package, PK_INFO_ENUM_AVAILABLE);
						break;
					}
				}
			}
		}
	}

	backend_finished (backend);
	return TRUE;
}

/**
 * backend_resolve:
 **/
void
backend_resolve (PkBackend *backend, PkBitfield filters, gchar **package_ids)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (package_ids != NULL);

	backend_run (backend, PK_STATUS_ENUM_QUERY, backend_resolve_thread);
}

static gboolean
backend_get_details_thread (PkBackend *backend)
{
	guint iterator;

	gchar **package_ids;

	g_return_val_if_fail (local_database != NULL, FALSE);
	g_return_val_if_fail (backend != NULL, FALSE);

	package_ids = pk_backend_get_strv (backend, "package_ids");

	g_return_val_if_fail (package_ids != NULL, FALSE);

	/* collect details about packages */
	for (iterator = 0; package_ids[iterator] != NULL; ++iterator) {
		PacmanPackage *package;
		const PacmanList *list;
		GString *string;

		gchar *licenses;
		PkGroupEnum group;
		const gchar *description, *url;
		gulong size;

		if (backend_cancelled (backend)) {
			break;
		}

		package = backend_get_package (backend, package_ids[iterator]);
		if (package == NULL) {
			backend_finished (backend);
			return FALSE;
		}

		list = pacman_package_get_licenses (package);
		if (list == NULL) {
			string = g_string_new ("unknown");
		} else {
			string = g_string_new ((const gchar *) pacman_list_get (list));
			for (list = pacman_list_next (list); list != NULL; list = pacman_list_next (list)) {
				/* assume OR although it may not be correct */
				g_string_append_printf (string, " or %s", (const gchar *) pacman_list_get (list));
			}
		}

		group = pk_group_enum_from_string (pacman_package_get_group (package));
		description = pacman_package_get_description (package);
		url = pacman_package_get_url (package);

		if (pacman_package_get_database (package) == local_database) {
			size = pacman_package_get_installed_size (package);
		} else {
			/* FS#18769: change to get_download_size */
			size = pacman_package_get_size (package);
		}

		licenses = g_string_free (string, FALSE);
		pk_backend_details (backend, package_ids[iterator], licenses, group, description, url, size);
		g_free (licenses);
	}

	backend_finished (backend);
	return TRUE;
}

/**
 * backend_get_details:
 **/
void
backend_get_details (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (package_ids != NULL);

	backend_run (backend, PK_STATUS_ENUM_QUERY, backend_get_details_thread);
}

static gboolean
backend_get_files_thread (PkBackend *backend)
{
	guint iterator;

	gchar **package_ids;

	g_return_val_if_fail (pacman != NULL, FALSE);
	g_return_val_if_fail (backend != NULL, FALSE);

	package_ids = pk_backend_get_strv (backend, "package_ids");

	g_return_val_if_fail (package_ids != NULL, FALSE);

	/* enumerate files provided by package */
	for (iterator = 0; package_ids[iterator] != NULL; ++iterator) {
		PacmanPackage *package;
		const PacmanList *list;

		GString *string;
		gchar *files;

		if (backend_cancelled (backend)) {
			break;
		}

		package = backend_get_package (backend, package_ids[iterator]);
		if (package == NULL) {
			backend_finished (backend);
			return FALSE;
		}

		list = pacman_package_get_files (package);
		if (list == NULL) {
			string = g_string_new ("");
		} else {
			const gchar *root_path = pacman_manager_get_root_path (pacman);
			string = g_string_new (root_path);
			g_string_append (string, (const gchar *) pacman_list_get (list));

			for (list = pacman_list_next (list); list != NULL; list = pacman_list_next (list)) {
				g_string_append_printf (string, ";%s%s", root_path, (const gchar *) pacman_list_get (list));
			}
		}

		files = g_string_free (string, FALSE);
		pk_backend_files (backend, package_ids[iterator], files);
		g_free (files);
	}

	backend_finished (backend);
	return TRUE;
}

/**
 * backend_get_files:
 **/
void
backend_get_files (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (pacman != NULL);
	g_return_if_fail (backend != NULL);

	backend_run (backend, PK_STATUS_ENUM_QUERY, backend_get_files_thread);
}
