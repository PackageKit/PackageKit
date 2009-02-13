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

#include <packagekit-glib/packagekit.h>
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
	egg_debug ("backend: initialize");
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
	egg_debug ("backend: destroy");
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
	package_ids_temp = pk_package_ids_to_text (package_ids);
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
	package_ids_temp = pk_package_ids_to_text (package_ids);
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
	filters_text = pk_filter_bitfield_to_text (filters);
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
	package_ids_temp = pk_package_ids_to_text (package_ids);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "get-update-detail", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_install_packages:
 */

/*
python conaryBackend.py update-packages "gimp;2.4.6-0.2-1;x86;/foresight.rpath.org@fl:devel//2-qa/1222042924.132:2.4.6-0.2-1,1#x86"

 */
static void
backend_install_packages (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot install when offline");
		pk_backend_finished (backend);
		return;
	}

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_text (package_ids);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "install-packages", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_install_files:
 */
/**
static void
backend_install_files (PkBackend *backend, gboolean trusted, const gchar *full_paths)
{
	gchar *package_ids_temp;

	package_ids_temp = pk_package_ids_to_text (package_ids);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "install-files", pk_backend_bool_to_text (trusted), full_paths, NULL);
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

	pk_backend_spawn_helper (spawn, "conaryBackend.py", "refresh-cache", NULL);
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
	package_ids_temp = pk_package_ids_to_text (package_ids);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "remove-packages", pk_backend_bool_to_text (allow_deps), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_text (filters);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "search-name", filters_text, search, NULL);
	g_free (filters_text);
}

/**
    pk_backend_search_groups
*/
static void
backend_search_group (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_text (filters);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "search-group", filters_text, search, NULL);
	g_free (filters_text);
}



/**
    pk_backend_search_details
*/
static void
backend_search_details (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_text (filters);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "search-details", filters_text, search, NULL);
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
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot update when offline");
		pk_backend_finished (backend);
		return;
	}
	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_text (package_ids);
	egg_debug("Updates Packages");
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "update-packages", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_update_system:
 */
static void
backend_update_system (PkBackend *backend)
{
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "update-system", NULL);
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
	filters_text = pk_filter_bitfield_to_text (filters);
	pk_backend_spawn_helper (spawn, "conaryBackend.py", "resolve", filters_text, package_ids[0], NULL);
	g_free (filters_text);
}

PK_BACKEND_OPTIONS (
	"Conary",				/* description */
	"Ken VanDine <ken@vandine.org>",
						/* author */
	backend_initialize,			/* initalize */
	backend_destroy,			/* destroy */
	backend_get_groups,			/* get_groups */
	backend_get_filters,			/* get_filters */
	NULL,					/* get_mime_types */
	backend_cancel,				/* cancel */
	NULL,					/* download_packages */
	NULL,					/* get_categories */
	NULL,					/* get_depends */
	backend_get_details,			/* get_details */
	NULL,					/* get_distro_upgrades */
	backend_get_files,			/* get_files */
	NULL,					/* get_packages */
	NULL,					/* get_repo_list */
	NULL,					/* get_requires */
	backend_get_update_detail,              /* get_update_detail */
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
	NULL,					/* search_file */
	backend_search_group,					/* search_group */
	backend_search_name,			/* search_name */
	backend_update_packages,		/* update_packages */
	backend_update_system,			/* update_system */
	NULL					/* what_provides */
);

