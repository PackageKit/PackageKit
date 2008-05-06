/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2008 James Bowes <jbowes@redhat.com>
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
#include <pk-package-ids.h>

static PkBackendSpawn *spawn;
static PkNetwork *network;

/**
 * backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_initialize (PkBackend *backend)
{
	pk_debug ("FILTER: initialize");
	network = pk_network_new ();
	spawn = pk_backend_spawn_new ();
	pk_backend_spawn_set_name (spawn, "smart");
}

/**
 * backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_destroy (PkBackend *backend)
{
	pk_debug ("FILTER: destroy");
	g_object_unref (network);
	g_object_unref (spawn);
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, PkFilterEnum filters, const gchar *package_id, gboolean recursive)
{
	gchar *filters_text;
	/* FIXME: Use recursive and filter here */
	filters_text = pk_filter_enums_to_text (filters);
	pk_backend_spawn_helper (spawn, "get-depends.py", package_id, NULL);
	g_free (filters_text);
}

/**
 * backend_get_details:
 */
static void
backend_get_details (PkBackend *backend, const gchar *package_id)
{
	pk_backend_spawn_helper (spawn, "get-details.py", package_id, NULL);
}

/**
 * backend_get_files:
 */
static void
backend_get_files (PkBackend *backend, const gchar *package_id)
{
	pk_backend_spawn_helper (spawn, "get-files.py", package_id, NULL);
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend, PkFilterEnum filters)
{
	gchar *filters_text;
	filters_text = pk_filter_enums_to_text (filters);
	pk_backend_spawn_helper (spawn, "get-updates.py", filters_text, NULL);
	g_free (filters_text);
}

/**
 * backend_install_package:
 */
static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
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
backend_install_file (PkBackend *backend, gboolean trusted, const gchar *full_path)
{
	pk_backend_spawn_helper (spawn, "install-file.py", pk_backend_bool_to_text (trusted), full_path, NULL);
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
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
backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps, gboolean autoremove)
{
	/* FIXME: Use allow_deps and autoremove */
	pk_backend_spawn_helper (spawn, "remove.py", package_id, NULL);
}

/**
 * pk_backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, PkFilterEnum filters, const gchar *package_id)
{
	gchar *filters_text;
	filters_text = pk_filter_enums_to_text (filters);
	pk_backend_spawn_helper (spawn, "resolve.py", filters_text, package_id, NULL);
	g_free (filters_text);
}

/**
 * pk_backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, PkFilterEnum filters, const gchar *search)
{
	gchar *filters_text;
	filters_text = pk_filter_enums_to_text (filters);
	pk_backend_spawn_helper (spawn, "search-details.py", filters_text, search, NULL);
	g_free (filters_text);
}

/**
 * pk_backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, PkFilterEnum filters, const gchar *search)
{
	gchar *filters_text;
	filters_text = pk_filter_enums_to_text (filters);
	pk_backend_spawn_helper (spawn, "search-name.py", filters_text, search, NULL);
	g_free (filters_text);
}

/**
 * pk_backend_update_packages:
 */
static void
backend_update_packages (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;


	/* check network state */
	if (pk_network_is_online (network) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot install when offline");
		pk_backend_finished (backend);
		return;
	}

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_text (package_ids, "|");
	pk_backend_spawn_helper (spawn, "update.py", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_update_system:
 */
static void
backend_update_system (PkBackend *backend)
{
	pk_backend_spawn_helper (spawn, "update-system.py", NULL);
}

/**
 * pk_backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend, PkFilterEnum filters)
{
	gchar *filters_text;
	filters_text = pk_filter_enums_to_text (filters);
	pk_backend_spawn_helper (spawn, "get-repo-list.py", filters_text, NULL);
	g_free (filters_text);
}

/**
 * pk_backend_repo_enable:
 */
static void
backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
	if (enabled == TRUE) {
		pk_backend_spawn_helper (spawn, "repo-enable.py", rid, "true", NULL);
	} else {
		pk_backend_spawn_helper (spawn, "repo-enable.py", rid, "false", NULL);
	}
}

PK_BACKEND_OPTIONS (
	"SMART",					/* description */
	"James Bowes <jbowes@dangerouslyinc.com>",	/* author */
	backend_initialize,				/* initialize */
	backend_destroy,				/* destroy */
	NULL,						/* get_groups */
	NULL,						/* get_filters */
	NULL,						/* cancel */
	backend_get_depends,				/* get_depends */
	backend_get_details,			/* get_details */
	backend_get_files,				/* get_files */
	NULL,						/* get_packages */
	backend_get_repo_list,				/* get_repo_list */
	NULL,						/* get_requires */
	NULL,						/* get_update_detail */
	backend_get_updates,				/* get_updates */
	backend_install_file,				/* install_file */
	backend_install_package,			/* install_package */
	NULL,						/* install_signature */
	backend_refresh_cache,				/* refresh_cache */
	backend_remove_package,				/* remove_package */
	backend_repo_enable,				/* repo_enable */
	NULL,						/* repo_set_data */
	backend_resolve,				/* resolve */
	NULL,						/* rollback */
	backend_search_details,				/* search_details */
	NULL,						/* search_file */
	NULL,						/* search_group */
	backend_search_name,				/* search_name */
	NULL,						/* service_pack */
	backend_update_packages,			/* update_packages */
	backend_update_system,				/* update_system */
	NULL						/* what_provides */
);
