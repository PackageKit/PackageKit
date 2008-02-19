/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

#include <pk-network.h>
#include <pk-backend.h>
#include <pk-backend-spawn.h>

static PkBackendSpawn *spawn;
static PkNetwork *network;

/**
 * backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_initialize (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_debug ("FILTER: initialize");
	network = pk_network_new ();
	spawn = pk_backend_spawn_new ();
	pk_backend_spawn_set_name (spawn, "yum");
}

/**
 * backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_destroy (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_debug ("FILTER: destroy");
	g_object_unref (network);
	g_object_unref (spawn);
}

/**
 * backend_get_groups:
 */
static void
backend_get_groups (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);
	pk_enum_list_append_multiple (elist,
				      PK_GROUP_ENUM_ADMIN_TOOLS,
				      PK_GROUP_ENUM_DESKTOP_GNOME,
				      PK_GROUP_ENUM_DESKTOP_KDE,
				      PK_GROUP_ENUM_DESKTOP_XFCE,
				      PK_GROUP_ENUM_DESKTOP_OTHER,
				      PK_GROUP_ENUM_EDUCATION,
				      PK_GROUP_ENUM_FONTS,
				      PK_GROUP_ENUM_GAMES,
				      PK_GROUP_ENUM_GRAPHICS,
				      PK_GROUP_ENUM_INTERNET,
				      PK_GROUP_ENUM_LEGACY,
				      PK_GROUP_ENUM_LOCALIZATION,
				      PK_GROUP_ENUM_MULTIMEDIA,
				      PK_GROUP_ENUM_OFFICE,
				      PK_GROUP_ENUM_OTHER,
				      PK_GROUP_ENUM_PROGRAMMING,
				      PK_GROUP_ENUM_PUBLISHING,
				      PK_GROUP_ENUM_SERVERS,
				      PK_GROUP_ENUM_SYSTEM,
				      PK_GROUP_ENUM_VIRTUALIZATION,
				      -1);
}

/**
 * backend_get_filters:
 */
static void
backend_get_filters (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);
	pk_enum_list_append_multiple (elist,
				      PK_FILTER_ENUM_GUI,
				      PK_FILTER_ENUM_INSTALLED,
				      PK_FILTER_ENUM_DEVELOPMENT,
				      PK_FILTER_ENUM_FREE,
				      -1);
}

/**
 * pk_backend_bool_to_text:
 */
static const gchar *
pk_backend_bool_to_text (gboolean value)
{
	if (value == TRUE) {
		return "yes";
	}
	return "no";
}

/**
 * pk_backend_cancel:
 */
static void
backend_cancel (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	/* this feels bad... */
	pk_backend_spawn_kill (spawn);
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, const gchar *package_id, gboolean recursive)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_backend_spawn_helper (spawn, "get-depends.py", package_id, pk_backend_bool_to_text (recursive), NULL);
}

/**
 * backend_get_description:
 */
static void
backend_get_description (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_backend_spawn_helper (spawn, "get-description.py", package_id, NULL);
}

/**
 * backend_get_files:
 */
static void
backend_get_files (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_backend_spawn_helper (spawn, "get-files.py", package_id, NULL);
}

/**
 * backend_get_requires:
 */
static void
backend_get_requires (PkBackend *backend, const gchar *package_id, gboolean recursive)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_backend_spawn_helper (spawn, "get-requires.py", package_id, pk_backend_bool_to_text (recursive), NULL);
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_backend_spawn_helper (spawn, "get-updates.py", NULL);
}

/**
 * backend_get_update_detail:
 */
static void
backend_get_update_detail (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_backend_spawn_helper (spawn, "get-update-detail.py", package_id, NULL);
}

/**
 * backend_install_package:
 */
static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);

	/* check network state */
	if (pk_network_is_online (network) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot install when offline");
		pk_backend_finished (backend);
		return;
	}

	pk_backend_spawn_helper (spawn, "install.py", package_id, NULL);
}

/**
 * backend_install_file:
 */
static void
backend_install_file (PkBackend *backend, const gchar *full_path)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_backend_spawn_helper (spawn, "install-file.py", full_path, NULL);
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);

	/* check network state */
	if (pk_network_is_online (network) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_finished (backend);
		return;
	}

	pk_backend_spawn_helper (spawn, "refresh-cache.py", NULL);
}

/**
 * pk_backend_remove_package:
 */
static void
backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_backend_spawn_helper (spawn, "remove.py", pk_backend_bool_to_text (allow_deps), package_id, NULL);
}

/**
 * pk_backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_backend_spawn_helper (spawn, "search-details.py", filter, search, NULL);
}

/**
 * pk_backend_search_file:
 */
static void
backend_search_file (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_backend_spawn_helper (spawn, "search-file.py", filter, search, NULL);
}

/**
 * pk_backend_search_group:
 */
static void
backend_search_group (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_backend_spawn_helper (spawn, "search-group.py", filter, search, NULL);
}

/**
 * pk_backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_backend_spawn_helper (spawn, "search-name.py", filter, search, NULL);
}

/**
 * pk_backend_update_package:
 */
static void
backend_update_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);

	/* check network state */
	if (pk_network_is_online (network) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot install when offline");
		pk_backend_finished (backend);
		return;
	}

	pk_backend_spawn_helper (spawn, "update.py", package_id, NULL);
}

/**
 * pk_backend_update_system:
 */
static void
backend_update_system (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_backend_spawn_helper (spawn, "update-system.py", NULL);
}

/**
 * pk_backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, const gchar *filter, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_backend_spawn_helper (spawn, "resolve.py", filter, package_id, NULL);
}

/**
 * pk_backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_backend_spawn_helper (spawn, "get-repo-list.py", NULL);
}

/**
 * pk_backend_repo_enable:
 */
static void
backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	if (enabled == TRUE) {
		pk_backend_spawn_helper (spawn, "repo-enable.py", rid, "true", NULL);
	} else {
		pk_backend_spawn_helper (spawn, "repo-enable.py", rid, "false", NULL);
	}
}

/**
 * pk_backend_repo_set_data:
 */
static void
backend_repo_set_data (PkBackend *backend, const gchar *rid, const gchar *parameter, const gchar *value)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (spawn != NULL);
	pk_backend_spawn_helper (spawn, "repo-set-data.py", rid, parameter, value, NULL);
}

PK_BACKEND_OPTIONS (
	"YUM",					/* description */
	"Tim Lauridsen <timlau@fedoraproject.org>",	/* author */
	backend_initialize,			/* initalize */
	backend_destroy,			/* destroy */
	backend_get_groups,			/* get_groups */
	backend_get_filters,			/* get_filters */
	backend_cancel,				/* cancel */
	backend_get_depends,			/* get_depends */
	backend_get_description,		/* get_description */
	backend_get_files,			/* get_files */
	backend_get_requires,			/* get_requires */
	backend_get_update_detail,		/* get_update_detail */
	backend_get_updates,			/* get_updates */
	backend_install_package,		/* install_package */
	backend_install_file,			/* install_file */
	backend_refresh_cache,			/* refresh_cache */
	backend_remove_package,			/* remove_package */
	backend_resolve,			/* resolve */
	NULL,					/* rollback */
	backend_search_details,			/* search_details */
	backend_search_file,			/* search_file */
	backend_search_group,			/* search_group */
	backend_search_name,			/* search_name */
	backend_update_package,			/* update_package */
	backend_update_system,			/* update_system */
	backend_get_repo_list,			/* get_repo_list */
	backend_repo_enable,			/* repo_enable */
	backend_repo_set_data,			/* repo_set_data */
	NULL					/* service_pack */
);

