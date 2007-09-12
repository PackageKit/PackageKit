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

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <pk-backend.h>

/**
 * backend_get_groups:
 */
static void
backend_get_groups (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);
	pk_enum_list_append_multiple (elist,
				      PK_GROUP_ENUM_ACCESSIBILITY,
				      PK_GROUP_ENUM_GAMES,
				      PK_GROUP_ENUM_SYSTEM,
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
				      -1);
}

/**
 * backend_cancel_job_try:
 */
static void
backend_cancel_job_try (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	/* this feels bad... */
	pk_backend_spawn_kill (backend);
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_allow_interrupt (backend, TRUE);
	pk_backend_spawn_helper (backend, "get-depends.py", package_id, NULL);
}

/**
 * backend_get_description:
 */
static void
backend_get_description (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_allow_interrupt (backend, TRUE);
	pk_backend_spawn_helper (backend, "get-description.py", package_id, NULL);
}

/**
 * backend_get_requires:
 */
static void
backend_get_requires (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_allow_interrupt (backend, TRUE);
	pk_backend_spawn_helper (backend, "get-requires.py", package_id, NULL);
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "get-updates.py", NULL);
}

/**
 * backend_install_package:
 */
static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	/* check network state */
	if (pk_backend_network_is_online (backend) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot install when offline");
		pk_backend_finished (backend, PK_EXIT_ENUM_FAILED);
		return;
	}
	pk_backend_spawn_helper (backend, "install.py", package_id, NULL);
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (backend != NULL);
	/* check network state */
	if (pk_backend_network_is_online (backend) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_finished (backend, PK_EXIT_ENUM_FAILED);
		return;
	}
	pk_backend_spawn_helper (backend, "refresh-cache.py", NULL);
}

/**
 * backend_remove_package:
 */
static void
backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps)
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
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_allow_interrupt (backend, TRUE);
	pk_backend_spawn_helper (backend, "search-details.py", filter, search, NULL);
}

/**
 * backend_search_file:
 */
static void
backend_search_file (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_allow_interrupt (backend, TRUE);
	pk_backend_spawn_helper (backend, "search-file.py", filter, search, NULL);
}

/**
 * backend_search_group:
 */
static void
backend_search_group (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_allow_interrupt (backend, TRUE);
	pk_backend_spawn_helper (backend, "search-group.py", filter, search, NULL);
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_allow_interrupt (backend, TRUE);
	pk_backend_no_percentage_updates (backend);
	pk_backend_spawn_helper (backend, "search-name.py", filter, search, NULL);
}

/**
 * backend_update_package:
 */
static void
backend_update_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	/* check network state */
	if (pk_backend_network_is_online (backend) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot update when offline");
		pk_backend_finished (backend, PK_EXIT_ENUM_FAILED);
		return;
	}
	pk_backend_spawn_helper (backend, "update.py", package_id, NULL);
}

/**
 * backend_update_system:
 */
static void
backend_update_system (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "update-system.py", NULL);
}

PK_BACKEND_OPTIONS (
	"Dummy Backend",			/* description */
	"0.0.1",				/* version */
	"Richard Hughes <richard@hughsie.com>",	/* author */
	NULL,					/* initalize */
	NULL,					/* destroy */
	backend_get_groups,			/* get_groups */
	backend_get_filters,			/* get_filters */
	backend_cancel_job_try,			/* cancel_job_try */
	backend_get_depends,			/* get_depends */
	backend_get_description,		/* get_description */
	backend_get_requires,			/* get_requires */
	NULL,					/* get_update_detail */
	backend_get_updates,			/* get_updates */
	backend_install_package,		/* install_package */
	backend_refresh_cache,			/* refresh_cache */
	backend_remove_package,			/* remove_package */
	backend_search_details,			/* search_details */
	backend_search_file,			/* search_file */
	backend_search_group,			/* search_group */
	backend_search_name,			/* search_name */
	backend_update_package,			/* update_package */
	backend_update_system			/* update_system */
);

