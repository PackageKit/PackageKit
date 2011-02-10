/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2007 Ken VanDine <ken@vandine.org>
 * Copyright (C) 2009-2010 Andres Vargas <zodman@foresightlinux.org>
 *                         Scott Parkerson <scott.parkerson@gmail.com>
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

#include <pk-backend.h>
#include <pk-backend-spawn.h>

static PkBackendSpawn *spawn;

/**
 * backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_initialize (PkBackend *backend)
{
	g_debug ("backend: initialize");
	spawn = pk_backend_spawn_new ();
	pk_backend_spawn_set_name (spawn, "conary");
}

/**
 * backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_destroy (PkBackend *backend)
{
	g_debug ("backend: destroy");
	g_object_unref (spawn);
}

/**
 * backend_get_groups:
*/
static PkBitfield
backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_GROUP_ENUM_ACCESSIBILITY,
		PK_GROUP_ENUM_ACCESSORIES,
		PK_GROUP_ENUM_EDUCATION,
		PK_GROUP_ENUM_GAMES,
		PK_GROUP_ENUM_GRAPHICS,
		PK_GROUP_ENUM_INTERNET,
		PK_GROUP_ENUM_OFFICE,
		PK_GROUP_ENUM_OTHER,
		PK_GROUP_ENUM_PROGRAMMING,
		PK_GROUP_ENUM_MULTIMEDIA,
		PK_GROUP_ENUM_SYSTEM,
		-1);
}


/**
 * backend_get_filters:
 */
static PkBitfield
backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1);
}

/**
 * backend_get_roles:
 */
PkBitfield
backend_get_roles (PkBackend *backend)
{
	PkBitfield roles;
	roles = pk_bitfield_from_enums (
		PK_ROLE_ENUM_CANCEL,
		PK_ROLE_ENUM_GET_DETAILS,
		PK_ROLE_ENUM_GET_FILES,
		PK_ROLE_ENUM_GET_PACKAGES,
		PK_ROLE_ENUM_GET_UPDATES,
		PK_ROLE_ENUM_INSTALL_PACKAGES,
		PK_ROLE_ENUM_REFRESH_CACHE,
		PK_ROLE_ENUM_REMOVE_PACKAGES,
		PK_ROLE_ENUM_RESOLVE,
		PK_ROLE_ENUM_SEARCH_DETAILS,
		PK_ROLE_ENUM_SEARCH_FILE,
		PK_ROLE_ENUM_SEARCH_GROUP,
		PK_ROLE_ENUM_SEARCH_NAME,
		PK_ROLE_ENUM_UPDATE_PACKAGES,
		PK_ROLE_ENUM_UPDATE_SYSTEM,
		PK_ROLE_ENUM_GET_REPO_LIST,
		PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES,
		PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES,
		PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES,
		-1);

	return roles;
}

/**
 * pk_backend_cancel:
 */
static void
backend_cancel (PkBackend *backend)
{
	/* this feels bad... */
	pk_backend_spawn_kill (spawn);
}

/**
 * backend_get_details:
 */
static void
backend_get_details (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "get-details", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_get_files:
 */
static void
backend_get_files (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "get-files", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "get-updates", filters_text, NULL);
	g_free (filters_text);
}

/**
 * backend_get_update_detail:
 */
static void
backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "get-update-detail", package_ids_temp, NULL);
	g_free (package_ids_temp);
}
/**
 * backend_install_packages:
*/

static void
backend_install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot install when offline");
		pk_backend_finished (backend);
		return;
	}

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "install-packages", pk_backend_bool_to_string (only_trusted), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_install_files:
 */
/**
static void
backend_install_files (PkBackend *backend, gboolean only_trusted, const gchar *full_paths)
{
	gchar *package_ids_temp;

	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "install-files", pk_backend_bool_to_string (only_trusted), full_paths, NULL);
	g_free (package_ids_temp);
}
*/
/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_finished (backend);
		return;
	}

	pk_backend_spawn_helper (spawn, "conaryBackend.py", "refresh-cache", pk_backend_bool_to_string (force), NULL);
}

/**
 * pk_backend_remove_packages:
 */

/*
 zodman notes:
 python conaryBackend.py  remove-packages none  "pastebinit;0.7-1-1;x86;/foresight.rpath.org@fl:2-qa/1222042924.172:0.7-1-1,1#x86"

 */
static void
backend_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "remove-packages", pk_backend_bool_to_string (allow_deps), pk_backend_bool_to_string (autoremove), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&",values);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "search-name", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

/**
    pk_backend_search_groups
*/
static void
backend_search_group (PkBackend *backend, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&",values);

	pk_backend_spawn_helper (spawn, "conaryBackend.py", "search-group", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}




/**
    pk_backend_search_details
*/
static void
backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&",values);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "search-details", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

/**
 * pk_backend_update_packages:
 */
static void
backend_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot update when offline");
		pk_backend_finished (backend);
		return;
	}
	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug("Updates Packages");
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "update-packages", pk_backend_bool_to_string (only_trusted), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_update_system:
 */
static void
backend_update_system (PkBackend *backend, gboolean only_trusted)
{
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "update-system", pk_backend_bool_to_string (only_trusted), NULL);
}

/**
 * pk_backend_resolve:
 */

/* zodman note

# python conaryBackend.py  resolve installed pastebinit
allow-cancel	true
no-percentage-updates
status	info
allow-cancel	true
no-percentage-updates
status	query
package	available	pastebinit;0.7-1-1;x86;/foresight.rpath.org@fl:2-qa/1222042924.172:0.7-1-1,1#x86
finished

 */
static void
backend_resolve (PkBackend *backend, PkBitfield filters, gchar **package_ids)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "resolve", filters_text, package_ids[0], NULL);
	g_free (filters_text);
}


/**
 * backend_get_packages:
 */
static void
backend_get_packages (PkBackend *backend, PkBitfield filters)
{
    gchar *filters_text;
    filters_text = pk_filter_bitfield_to_string (filters);
    pk_backend_spawn_helper (spawn, "conaryBackend.py", "get-packages", filters_text, NULL);
    g_free (filters_text);
}
/**
 * pk_backend_search_file:
 */
static void
backend_search_file (PkBackend *backend, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&",values);


        pk_backend_spawn_helper (spawn, "conaryBackend.py", "search-file", filters_text, search, NULL);
        g_free (filters_text);
}

/**
 * backend_get_distro_upgrades:
static void
backend_get_distro_upgrades (PkBackend *backend)
{
    pk_backend_spawn_helper (spawn, "conaryBackend.py", "get-distro-upgrades", NULL);
}

 */


/**
 * pk_backend_get_repo_list:
 */
static void backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
    gchar *filters_text;
    filters_text = pk_filter_bitfield_to_string (filters);
    pk_backend_spawn_helper (spawn, "conaryBackend.py", "get-repo-list", filters_text, NULL);
    g_free (filters_text);
}

/**
 * backend_simulate_install_packages:
 */
static void
backend_simulate_install_packages (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot install when offline");
		pk_backend_finished (backend);
		return;
	}

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "simulate-install-packages", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_simulate_remove_packages:
 */
static void
backend_simulate_remove_packages (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "simulate-remove-packages", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_simulate_update_packages:
 */
static void
backend_simulate_update_packages (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot update when offline");
		pk_backend_finished (backend);
		return;
	}
	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug("Updates Packages");
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "simulate-update-packages", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/* FIXME: port this away from PK_BACKEND_OPTIONS */
PK_BACKEND_OPTIONS (
	"Conary with XMLCache",				/* description */
	"Andres Vargas <zodman@foresightlinux.org>", /* author */
	backend_initialize,			/* initalize */
	backend_destroy,			/* destroy */
	backend_get_groups,			/* get_groups */
	backend_get_filters,			/* get_filters */
	backend_get_roles,			/* get_roles */
	NULL,					/* get_mime_types */
	backend_cancel,				/* cancel */
	NULL,					/* download_packages */
	NULL,					/* get_categories */
	NULL,					/* get_depends */
	backend_get_details,			/* get_details */
	NULL,	                /* get_distro_upgrades */
	backend_get_files,			/* get_files */
	backend_get_packages,					/* get_packages */
	backend_get_repo_list,					/* get_repo_list */
	NULL,					/* get_requires */
	NULL,//	backend_get_update_detail,              /* get_update_detail */
	backend_get_updates,			/* get_updates */
	NULL,					/* install_files */
	backend_install_packages,		/* install_packages */
	NULL,					/* install_signature */
	backend_refresh_cache,			/* refresh_cache */
	backend_remove_packages,		/* remove_packages */
	NULL,					/* repo_enable */
	NULL,					/* repo_set_data */
	backend_resolve,			/* resolve */
	NULL,					/* rollback */
	backend_search_details,					/* search_details */
	backend_search_file,					/* search_file */
	backend_search_group,					/* search_group */
	backend_search_name,			/* search_name */
	backend_update_packages,		/* update_packages */
	backend_update_system,			/* update_system */
	NULL,					/* what_provides */
	NULL,					/* simulate_install_files */
	backend_simulate_install_packages,	/* simulate_install_packages */
	backend_simulate_remove_packages,	/* simulate_remove_packages */
	backend_simulate_update_packages,	/* simulate_update_packages */
	NULL,					/* upgrade_system */
	NULL,					/* transaction_start */
	NULL					/* transaction_stop */
);

