/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <pk-backend-python.h>

/**
 * pk_backend_python_cancel:
 */
void
pk_backend_python_cancel (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	/* this feels bad... */
	pk_backend_spawn_kill (backend);
}

/**
 * pk_backend_python_get_depends:
 */
void
pk_backend_python_get_depends (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "get-depends.py", package_id, NULL);
}

/**
 * pk_backend_python_get_description:
 */
void
pk_backend_python_get_description (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "get-description.py", package_id, NULL);
}

/**
 * pk_backend_python_get_files:
 */
void
pk_backend_python_get_files (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "get-files.py", package_id, NULL);
}

/**
 * pk_backend_python_get_requires:
 */
void
pk_backend_python_get_requires (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "get-requires.py", package_id, NULL);
}

/**
 * pk_backend_python_get_updates:
 */
void
pk_backend_python_get_updates (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "get-updates.py", NULL);
}

/**
 * pk_backend_python_get_update_detail:
 */
void
pk_backend_python_get_update_detail (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "get-update-detail.py", package_id, NULL);
}

/**
 * pk_backend_python_install_package:
 */
void
pk_backend_python_install_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	/* check network state */
	if (pk_backend_network_is_online (backend) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot install when offline");
		pk_backend_finished (backend);
		return;
	}
	pk_backend_spawn_helper (backend, "install.py", package_id, NULL);
}

/**
 * pk_backend_python_install_file:
 */
void
pk_backend_python_install_file (PkBackend *backend, const gchar *full_path)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "install-file.py", full_path, NULL);
}

/**
 * pk_backend_python_refresh_cache:
 */
void
pk_backend_python_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (backend != NULL);
	/* check network state */
	if (pk_backend_network_is_online (backend) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_finished (backend);
		return;
	}
	pk_backend_spawn_helper (backend, "refresh-cache.py", NULL);
}

/**
 * pk_backend_python_remove_package:
 */
void
pk_backend_python_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps)
{
	g_return_if_fail (backend != NULL);
	const gchar *deps;
	if (allow_deps == TRUE) {
		deps = "yes";
	} else {
		deps = "no";
	}
	pk_backend_spawn_helper (backend, "remove.py", deps, package_id, NULL);
}

/**
 * pk_backend_python_search_details:
 */
void
pk_backend_python_search_details (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "search-details.py", filter, search, NULL);
}

/**
 * pk_backend_python_search_file:
 */
void
pk_backend_python_search_file (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "search-file.py", filter, search, NULL);
}

/**
 * pk_backend_python_search_group:
 */
void
pk_backend_python_search_group (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "search-group.py", filter, search, NULL);
}

/**
 * pk_backend_python_search_name:
 */
void
pk_backend_python_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "search-name.py", filter, search, NULL);
}

/**
 * pk_backend_python_update_package:
 */
void
pk_backend_python_update_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	/* check network state */
	if (pk_backend_network_is_online (backend) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot update when offline");
		pk_backend_finished (backend);
		return;
	}
	pk_backend_spawn_helper (backend, "update.py", package_id, NULL);
}

/**
 * pk_backend_python_update_system:
 */
void
pk_backend_python_update_system (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "update-system.py", NULL);
}

/**
 * pk_backend_python_resolve:
 */
void
pk_backend_python_resolve (PkBackend *backend, const gchar *filter, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "resolve.py", filter, package_id, NULL);
}

/**
 * pk_backend_python_get_repo_list:
 */
void
pk_backend_python_get_repo_list (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "get-repo-list.py", NULL);
}

/**
 * pk_backend_python_repo_enable:
 */
void
pk_backend_python_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
	g_return_if_fail (backend != NULL);
	if (enabled == TRUE) {
		pk_backend_spawn_helper (backend, "repo-enable.py", rid, "true", NULL);
	} else {
		pk_backend_spawn_helper (backend, "repo-enable.py", rid, "false", NULL);
	}
}

/**
 * pk_backend_python_repo_set_data:
 */
void
pk_backend_python_repo_set_data (PkBackend *backend, const gchar *rid, 
        const gchar *parameter, const gchar *value)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "repo-set-data.py", rid, parameter, value, NULL);
}
