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
#include "backend-packages.h"
#include "backend-pacman.h"
#include "backend-repos.h"
#include "backend-depends.h"

static PacmanPackage *
pacman_list_find_provider (const PacmanList *packages, PacmanDependency *depend)
{
	const PacmanList *list;

	g_return_val_if_fail (depend != NULL, NULL);

	/* find a package that provides depend */
	for (list = packages; list != NULL; list = pacman_list_next (list)) {
		PacmanPackage *provider = (PacmanPackage *) pacman_list_get (list);

		if (pacman_dependency_satisfied_by (depend, provider)) {
			return provider;
		}
	}

	return NULL;
}

static PacmanPackage *
pacman_sync_databases_find_provider (PacmanDependency *depend)
{
	const PacmanList *databases;

	g_return_val_if_fail (pacman != NULL, NULL);
	g_return_val_if_fail (depend != NULL, NULL);

	/* find the default package that provides depend */
	for (databases = pacman_manager_get_sync_databases (pacman); databases != NULL; databases = pacman_list_next (databases)) {
		PacmanDatabase *database = (PacmanDatabase *) pacman_list_get (databases);
		PacmanPackage *provider = pacman_database_find_package (database, pacman_dependency_get_name (depend));

		if (provider != NULL && pacman_dependency_satisfied_by (depend, provider)) {
			return provider;
		}
	}

	/* find any package that provides depend */
	for (databases = pacman_manager_get_sync_databases (pacman); databases != NULL; databases = pacman_list_next (databases)) {
		PacmanDatabase *database = (PacmanDatabase *) pacman_list_get (databases);
		PacmanPackage *provider = pacman_list_find_provider (pacman_database_get_packages (database), depend);

		if (provider != NULL) {
			return provider;
		}
	}

	return NULL;
}

static gboolean
backend_get_depends_thread (PkBackend *backend)
{
	guint iterator;
	PacmanList *list, *packages = NULL;

	PkBitfield filters;
	gchar **package_ids;
	gboolean recursive;

	gboolean search_installed;
	gboolean search_not_installed;

	g_return_val_if_fail (local_database != NULL, FALSE);
	g_return_val_if_fail (backend != NULL, FALSE);

	package_ids = pk_backend_get_strv (backend, "package_ids");
	recursive = pk_backend_get_bool (backend, "recursive");

	g_return_val_if_fail (package_ids != NULL, FALSE);

	filters = pk_backend_get_uint (backend, "filters");
	search_installed = pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED);
	search_not_installed = pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED);

	/* construct an initial package list */
	for (iterator = 0; package_ids[iterator] != NULL; ++iterator) {
		PacmanPackage *package = backend_get_package (backend, package_ids[iterator]);

		if (backend_cancelled (backend)) {
			break;
		} else if (package == NULL) {
			pacman_list_free (packages);
			backend_finished (backend);
			return FALSE;
		}

		packages = pacman_list_add (packages, package);
	}

	/* package list might be modified along the way but that is ok */
	for (list = packages; list != NULL; list = pacman_list_next (list)) {
		PacmanPackage *package = (PacmanPackage *) pacman_list_get (list);
		const PacmanList *depends;

		if (backend_cancelled (backend)) {
			break;
		}

		for (depends = pacman_package_get_dependencies (package); depends != NULL; depends = pacman_list_next (depends)) {
			PacmanDependency *depend = (PacmanDependency *) pacman_list_get (depends);
			PacmanPackage *provider = pacman_list_find_provider (packages, depend);

			if (backend_cancelled (backend)) {
				break;
			} else if (provider != NULL) {
				continue;
			}

			/* look for installed dependencies */
			provider = pacman_list_find_provider (pacman_database_get_packages (local_database), depend);
			if (provider != NULL) {
				/* don't emit when not needed... */
				if (!search_not_installed) {
					backend_package (backend, provider, PK_INFO_ENUM_INSTALLED);
					/* ... and assume installed packages also have installed dependencies */
					if (recursive) {
						packages = pacman_list_add (packages, provider);
					}
				}
				continue;
			}

			/* look for non-installed dependencies */
			provider = pacman_sync_databases_find_provider (depend);
			if (provider != NULL) {
				/* don't emit when not needed... */
				if (!search_installed) {
					backend_package (backend, provider, PK_INFO_ENUM_AVAILABLE);
				}
				/* ... but keep looking for installed dependencies */
				if (recursive) {
					packages = pacman_list_add (packages, provider);
				}
			} else {
				gchar *depend_id = pacman_dependency_to_string (depend);
				pk_backend_error_code (backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "Could not resolve dependency %s", depend_id);
				g_free (depend_id);

				pacman_list_free (packages);
				backend_finished (backend);
				return FALSE;
			}
		}
	}

	pacman_list_free (packages);
	backend_finished (backend);
	return TRUE;
}

/**
 * backend_get_depends:
 **/
void
backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (package_ids != NULL);

	backend_run (backend, PK_STATUS_ENUM_QUERY, backend_get_depends_thread);
}

static PacmanPackage *
pacman_list_find_package (const PacmanList *packages, const gchar *name)
{
	const PacmanList *list;

	g_return_val_if_fail (name != NULL, NULL);

	/* find a package called name */
	for (list = packages; list != NULL; list = pacman_list_next (list)) {
		PacmanPackage *package = (PacmanPackage *) pacman_list_get (list);

		if (g_strcmp0 (name, pacman_package_get_name (package)) == 0) {
			return package;
		}
	}

	return NULL;
}

static gboolean
backend_get_requires_thread (PkBackend *backend)
{
	guint iterator;
	PacmanList *list, *packages = NULL;

	gchar **package_ids;
	gboolean recursive;

	g_return_val_if_fail (local_database != NULL, FALSE);
	g_return_val_if_fail (backend != NULL, FALSE);

	package_ids = pk_backend_get_strv (backend, "package_ids");
	recursive = pk_backend_get_bool (backend, "recursive");

	g_return_val_if_fail (package_ids != NULL, FALSE);

	/* construct an initial package list */
	for (iterator = 0; package_ids[iterator] != NULL; ++iterator) {
		PacmanPackage *package = backend_get_package (backend, package_ids[iterator]);

		if (backend_cancelled (backend)) {
			break;
		} else if (package == NULL) {
			pacman_list_free (packages);
			backend_finished (backend);
			return FALSE;
		}

		packages = pacman_list_add (packages, package);
	}

	/* package list might be modified along the way but that is ok */
	for (list = packages; list != NULL; list = pacman_list_next (list)) {
		PacmanPackage *package = (PacmanPackage *) pacman_list_get (list);
		PacmanList *requires, *required_by = pacman_package_find_required_by (package);

		if (backend_cancelled (backend)) {
			break;
		}

		for (requires = required_by; requires != NULL; requires = pacman_list_next (requires)) {
			const gchar *name = (const gchar *) pacman_list_get (requires);
			PacmanPackage *requirer = pacman_list_find_package (packages, name);

			if (backend_cancelled (backend)) {
				break;
			} else if (requirer != NULL) {
				continue;
			}

			/* look for installed requirers */
			requirer = pacman_database_find_package (local_database, name);
			if (requirer == NULL) {
				pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "Could not find package %s", name);

				pacman_list_free_full (required_by, g_free);
				pacman_list_free (packages);
				backend_finished (backend);
				return FALSE;
			}

			backend_package (backend, requirer, PK_INFO_ENUM_INSTALLED);
			if (recursive) {
				packages = pacman_list_add (packages, requirer);
			}
		}

		pacman_list_free_full (required_by, g_free);
	}

	pacman_list_free (packages);
	backend_finished (backend);
	return TRUE;
}

/**
 * backend_get_requires:
 **/
void
backend_get_requires (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (package_ids != NULL);

	backend_run (backend, PK_STATUS_ENUM_QUERY, backend_get_requires_thread);
}
