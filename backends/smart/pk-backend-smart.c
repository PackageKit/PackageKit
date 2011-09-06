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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <pk-backend.h>
#include <pk-backend-spawn.h>

static PkBackendSpawn *spawn;

static const gchar* BACKEND_FILE = "smartBackend.py";

/**
 * pk_backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
void
pk_backend_initialize (PkBackend *backend)
{
	g_debug ("backend: initialize");
	spawn = pk_backend_spawn_new ();
	pk_backend_spawn_set_name (spawn, "smart");
}

/**
 * pk_backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
void
pk_backend_destroy (PkBackend *backend)
{
	g_debug ("backend: destroy");
	g_object_unref (spawn);
}

/**
 * pk_backend_get_groups:
 */
PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_GROUP_ENUM_COLLECTIONS,
	/* FIXME: These depend on the Smart backends: */
		PK_GROUP_ENUM_ACCESSIBILITY,
		PK_GROUP_ENUM_ACCESSORIES,
		PK_GROUP_ENUM_ADMIN_TOOLS,
		PK_GROUP_ENUM_COMMUNICATION,
		PK_GROUP_ENUM_DESKTOP_GNOME,
		PK_GROUP_ENUM_DESKTOP_KDE,
		PK_GROUP_ENUM_DESKTOP_OTHER,
		PK_GROUP_ENUM_DESKTOP_XFCE,
		PK_GROUP_ENUM_DOCUMENTATION,
		PK_GROUP_ENUM_EDUCATION,
		PK_GROUP_ENUM_ELECTRONICS,
		PK_GROUP_ENUM_FONTS,
		PK_GROUP_ENUM_GAMES,
		PK_GROUP_ENUM_GRAPHICS,
		PK_GROUP_ENUM_INTERNET,
		PK_GROUP_ENUM_LEGACY,
		PK_GROUP_ENUM_LOCALIZATION,
		PK_GROUP_ENUM_MULTIMEDIA,
		PK_GROUP_ENUM_NETWORK,
		PK_GROUP_ENUM_OFFICE,
		PK_GROUP_ENUM_OTHER,
		PK_GROUP_ENUM_PROGRAMMING,
		PK_GROUP_ENUM_PUBLISHING,
		PK_GROUP_ENUM_SCIENCE,
		PK_GROUP_ENUM_SECURITY,
		PK_GROUP_ENUM_SYSTEM,
		PK_GROUP_ENUM_VIRTUALIZATION,
		-1);
}

/**
 * pk_backend_get_filters:
 */
PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_NEWEST,
		PK_FILTER_ENUM_ARCH,
		PK_FILTER_ENUM_GUI,
		PK_FILTER_ENUM_DEVELOPMENT,
		PK_FILTER_ENUM_BASENAME,
		PK_FILTER_ENUM_COLLECTIONS,
	/* FIXME: These depend on a newer Smart API: */
		PK_FILTER_ENUM_FREE,
		-1);
}

/**
 * pk_backend_get_mime_types:
 */
gchar *
pk_backend_get_mime_types (PkBackend *backend)
{
	return g_strdup ("application/x-rpm;"
	                 "application/x-deb;"
	                 "application/x-compressed-tar;"	/* .tgz */
			 "application/x-bzip-compressed-tar;"	/* .tbz */
			 "application/x-lzma-compressed-tar;"	/* .tlz */
			 "application/x-xz-compressed-tar;"	/* .txz */
	                 "application/x-arch-pkg");
}

/**
 * pk_backend_cancel:
 */
void
pk_backend_cancel (PkBackend *backend)
{
	/* this feels bad... */
	pk_backend_spawn_kill (spawn);
}

/**
 * pk_backend_download_packages:
 */
void
pk_backend_download_packages (PkBackend *backend, gchar **package_ids, const gchar *directory)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "download-packages", directory, package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_depends:
 */
void
pk_backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	gchar *filters_text;
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "get-depends", filters_text, package_ids_temp, pk_backend_bool_to_string (recursive), NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_details:
 */
void
pk_backend_get_details (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "get-details", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_files:
 */
void
pk_backend_get_files (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "get-files", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_requires:
 */
void
pk_backend_get_requires (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	gchar *package_ids_temp;
	gchar *filters_text;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "get-requires", filters_text, package_ids_temp, pk_backend_bool_to_string (recursive), NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_updates:
 */
void
pk_backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "get-updates", filters_text, NULL);
	g_free (filters_text);
}

/**
 * pk_backend_get_packages:
 */
void
pk_backend_get_packages (PkBackend *backend, PkBitfield filters)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "get-packages", filters_text, NULL);
	g_free (filters_text);
}

/**
 * pk_backend_get_update_detail:
 */
void
pk_backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "get-update-detail", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_install_packages:
 */
void
pk_backend_install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
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
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "install-packages", pk_backend_bool_to_string (only_trusted), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_install_files:
 */
void
pk_backend_install_files (PkBackend *backend, gboolean only_trusted, gchar **full_paths)
{
	gchar *full_paths_temp;

	/* send the complete list as stdin */
	full_paths_temp = g_strjoinv (PK_BACKEND_SPAWN_FILENAME_DELIM, full_paths);
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "install-files", pk_backend_bool_to_string (only_trusted), full_paths_temp, NULL);
	g_free (full_paths_temp);
}

/**
 * pk_backend_refresh_cache:
 */
void
pk_backend_refresh_cache (PkBackend *backend, gboolean force)
{
	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_finished (backend);
		return;
	}

	pk_backend_spawn_helper (spawn, BACKEND_FILE, "refresh-cache", pk_backend_bool_to_string (force), NULL);
}

/**
 * pk_backend_remove_packages:
 */
void
pk_backend_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "remove-packages", pk_backend_bool_to_string (allow_deps), pk_backend_bool_to_string (autoremove), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_search_details:
 */
void
pk_backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "search-details", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

/**
 * pk_backend_search_files:
 */
void
pk_backend_search_files (PkBackend *backend, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "search-file", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

/**
 * pk_backend_search_groups:
 */
void
pk_backend_search_groups (PkBackend *backend, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "search-group", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

/**
 * pk_backend_search_names:
 */
void
pk_backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "search-name", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

/**
 * pk_backend_update_packages:
 */
void
pk_backend_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
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
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "update-packages", pk_backend_bool_to_string (only_trusted), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_update_system:
 */
void
pk_backend_update_system (PkBackend *backend, gboolean only_trusted)
{
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "update-system", pk_backend_bool_to_string (only_trusted), NULL);
}

/**
 * pk_backend_resolve:
 */
void
pk_backend_resolve (PkBackend *backend, PkBitfield filters, gchar **package_ids)
{
	gchar *filters_text;
	gchar *package_ids_temp;
	filters_text = pk_filter_bitfield_to_string (filters);
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "resolve", filters_text, package_ids_temp, NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_repo_list:
 */
void
pk_backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "get-repo-list", filters_text, NULL);
	g_free (filters_text);
}

/**
 * pk_backend_repo_enable:
 */
void
pk_backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
	if (enabled == TRUE) {
		pk_backend_spawn_helper (spawn, BACKEND_FILE, "repo-enable", rid, "true", NULL);
	} else {
		pk_backend_spawn_helper (spawn, BACKEND_FILE, "repo-enable", rid, "false", NULL);
	}
}

/**
 * pk_backend_repo_set_data:
 */
void
pk_backend_repo_set_data (PkBackend *backend, const gchar *rid, const gchar *parameter, const gchar *value)
{
	pk_backend_spawn_helper (spawn, BACKEND_FILE, "repo-set-data", rid, parameter, value, NULL);
}

/**
 * pk_backend_get_description:
 */
gchar *
pk_backend_get_description (PkBackend *backend)
{
	return g_strdup ("Smart");
}

/**
 * pk_backend_get_author:
 */
gchar *
pk_backend_get_author (PkBackend *backend)
{
	return g_strdup ("James Bowes <jbowes@dangerouslyinc.com>, "
	                 "Anders F Björklund <afb@users.sourceforge.net>");
}
