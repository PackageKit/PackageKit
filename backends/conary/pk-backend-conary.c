/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2007 Ken VanDine <ken@vandine.org>
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
 * backend_get_description:
 */
static void
backend_get_description (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_allow_interrupt (backend, TRUE);
	pk_backend_set_job_role (backend, PK_TASK_ROLE_QUERY, package_id);
	pk_backend_spawn_helper (backend, "get-description.py", package_id, NULL);
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_job_role (backend, PK_TASK_ROLE_QUERY, NULL);
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
		pk_backend_error_code (backend, PK_TASK_ERROR_CODE_NO_NETWORK, "Cannot install when offline");
		pk_backend_finished (backend, PK_TASK_EXIT_FAILED);
		return;
	}

	pk_backend_set_job_role (backend, PK_TASK_ROLE_PACKAGE_INSTALL, package_id);
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
		pk_backend_error_code (backend, PK_TASK_ERROR_CODE_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_finished (backend, PK_TASK_EXIT_FAILED);
		return;
	}
	pk_backend_set_job_role (backend, PK_TASK_ROLE_REFRESH_CACHE, NULL);
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

	pk_backend_set_job_role (backend, PK_TASK_ROLE_PACKAGE_REMOVE, package_id);
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
	pk_backend_set_job_role (backend, PK_TASK_ROLE_QUERY, search);
	pk_backend_spawn_helper (backend, "search-details.py", filter, search, NULL);
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
	pk_backend_set_job_role (backend, PK_TASK_ROLE_QUERY, search);
	pk_backend_spawn_helper (backend, "search-name.py", filter, search, NULL);
}

PK_BACKEND_OPTIONS (
	"Conary Backend",		/* description */
	"0.0.1",			/* version */
	"ken@vandine.org",		/* author */
	NULL,				/* initalize */
	NULL,				/* destroy */
	NULL,				/* cancel_job_try */
	NULL,				/* get_depends */
	backend_get_description,	/* get_description */
	NULL,				/* get_requires */
	backend_get_updates,		/* get_updates */
	backend_install_package,	/* install_package */
	backend_refresh_cache,		/* refresh_cache */
	backend_remove_package,		/* remove_package */
	backend_search_details,		/* search_details */
	NULL,				/* search_file */
	NULL,				/* search_group */
	backend_search_name,		/* search_name */
	NULL,				/* update_package */
	NULL				/* update_system */
);

