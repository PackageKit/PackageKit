/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2007 S.Çağlar Onur <caglar@pardus.org.tr>
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
 * pk_backend_start_job:
 */
void
pk_backend_start_job (PkBackend *backend, PkBackendJob *job)
{
	if (pk_backend_spawn_is_busy (spawn)) {
		pk_backend_job_error_code (job,
					   PK_ERROR_ENUM_LOCK_REQUIRED,
					   "spawned backend requires lock");
		return;
	}
}

/**
 * pk_backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
void
pk_backend_initialize (PkBackend *backend)
{
	g_debug ("backend: initialize");

	/* BACKEND MAINTAINER: feel free to remove this when you've
	 * added support for ONLY_DOWNLOAD and merged the simulate
	 * methods as specified in backends/PORTING.txt */
	/*g_error ("Backend needs to be ported to 0.8.x -- "
		 "see backends/PORTING.txt for details"); */

	spawn = pk_backend_spawn_new ();
	pk_backend_spawn_set_name (spawn, "pisi");
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
		PK_GROUP_ENUM_ACCESSORIES,
		PK_GROUP_ENUM_EDUCATION,
		PK_GROUP_ENUM_GAMES,
		PK_GROUP_ENUM_INTERNET,
		PK_GROUP_ENUM_OTHER,
		PK_GROUP_ENUM_PROGRAMMING,
		PK_GROUP_ENUM_MULTIMEDIA,
		PK_GROUP_ENUM_SYSTEM,
		PK_GROUP_ENUM_DESKTOP_GNOME,
		PK_GROUP_ENUM_DESKTOP_KDE,
		PK_GROUP_ENUM_DESKTOP_OTHER,
		PK_GROUP_ENUM_PUBLISHING,
		PK_GROUP_ENUM_SERVERS,
		PK_GROUP_ENUM_FONTS,
		PK_GROUP_ENUM_ADMIN_TOOLS,
		PK_GROUP_ENUM_LOCALIZATION,
		PK_GROUP_ENUM_VIRTUALIZATION,
		PK_GROUP_ENUM_SECURITY,
		PK_GROUP_ENUM_POWER_MANAGEMENT,
		PK_GROUP_ENUM_UNKNOWN,
		-1);
}

/**
 * pk_backend_get_filters:
 */
PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums(
		PK_FILTER_ENUM_GUI,
		PK_FILTER_ENUM_INSTALLED,
		-1);
}

/**
 * pk_backend_cancel:
 */
void
pk_backend_cancel (PkBackend *backend, PkBackendJob *job)
{
	/* this feels bad... */
	pk_backend_spawn_kill (spawn);
}

/**
 * pk_backend_download_packages:
 */
void
pk_backend_download_packages (PkBackend *backend, PkBackendJob *job, gchar **package_ids, const gchar *directory)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "download-packages", directory, package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_depends:
 */
void
pk_backend_get_depends (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	gchar *filters_text;
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "get-depends", filters_text, package_ids_temp, pk_backend_bool_to_string (recursive), NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_details:
 */
void
pk_backend_get_details (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "get-details", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_files:
 */
void
pk_backend_get_files (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "get-files", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_requires:
 */
void
pk_backend_get_requires (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	gchar *filters_text;
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "get-requires", filters_text, package_ids_temp, pk_backend_bool_to_string (recursive), NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_updates:
 */
void
pk_backend_get_updates (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "get-updates", filters_text, NULL);
	g_free (filters_text);
}

/**
 * pk_backend_get_update_detail:
 */
void
pk_backend_get_update_detail (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "get-update-detail", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_install_packages:
 */
void
pk_backend_install_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	gchar *package_ids_temp;
    gchar *transaction_flags_temp;

	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_NO_NETWORK, "Cannot install when offline");
		pk_backend_job_finished (job);
		return;
	}

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
    transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
    
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "install-packages", transaction_flags_temp, package_ids_temp, NULL);
	g_free (package_ids_temp);
    g_free (transaction_flags_temp);
}

/**
 * pk_backend_install_files:
 */
void
pk_backend_install_files (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **full_paths)
{
	gchar *package_ids_temp;
    gchar *transaction_flags_temp;

	/* send the complete list as stdin */
	package_ids_temp = g_strjoinv (PK_BACKEND_SPAWN_FILENAME_DELIM, full_paths);
    transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
    
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "install-files", transaction_flags_temp, package_ids_temp, NULL);
	g_free (package_ids_temp);
    g_free (transaction_flags_temp);
}

/**
 * pk_backend_refresh_cache:
 */
void
pk_backend_refresh_cache (PkBackend *backend, PkBackendJob *job, gboolean force)
{
	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_job_finished (job);
		return;
	}

	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "refresh-cache", pk_backend_bool_to_string (force), NULL);
}

/**
 * pk_backend_remove_packages:
 */
void
pk_backend_remove_packages (PkBackend *backend, PkBackendJob *job,
			    PkBitfield transaction_flags,
			    gchar **package_ids,
			    gboolean allow_deps,
			    gboolean autoremove)
{
	gchar *package_ids_temp;
    gchar *transaction_flags_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
    transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	pk_backend_spawn_helper (spawn,
        job, "pisiBackend.py",
        "remove-packages",
        transaction_flags_temp,
        package_ids_temp,
        pk_backend_bool_to_string (allow_deps),
        pk_backend_bool_to_string (autoremove), 
        NULL);
        
    g_free (transaction_flags_temp);
	g_free (package_ids_temp);
}

/**
 * pk_backend_search_details:
 */
void
pk_backend_search_details (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "search-details", filters_text, search, NULL);
	g_free (search);
	g_free (filters_text);
}

/**
 * pk_backend_search_files:
 */
void
pk_backend_search_files (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "search-file", filters_text, search, NULL);
	g_free (search);
	g_free (filters_text);
}

/**
 * pk_backend_search_groups:
 */
void
pk_backend_search_groups (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "search-group", filters_text, search, NULL);
	g_free (search);
	g_free (filters_text);
}

/**
 * pk_backend_search_names:
 */
void
pk_backend_search_names (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "search-name", filters_text, search, NULL);
	g_free (search);
	g_free (filters_text);
}

/**
 * pk_backend_update_packages:
 */
void
pk_backend_update_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	gchar *package_ids_temp;
    gchar *transaction_flags_temp;

	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_NO_NETWORK, "Cannot install when offline");
		pk_backend_job_finished (job);
		return;
	}

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
    transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
    
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "update-packages", transaction_flags_temp, package_ids_temp, NULL);
	g_free (package_ids_temp);
    g_free (transaction_flags_temp);
}

/**
 * pk_backend_update_system:
 */
void
pk_backend_update_system (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags)
{
    gchar *transaction_flags_temp;
    transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
    
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "update-system", transaction_flags_temp, NULL);
    
    g_free (transaction_flags_temp);
}

/**
 * pk_backend_resolve:
 */
void
pk_backend_resolve (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids)
{
	gchar *filters_text;
	gchar *package_ids_temp;
	filters_text = pk_filter_bitfield_to_string (filters);
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "resolve", filters_text, package_ids_temp, NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_repo_list:
 */
void
pk_backend_get_repo_list (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "get-repo-list", filters_text, NULL);
	g_free (filters_text);
}

/**
 * pk_backend_repo_set_data:
 */
void
pk_backend_repo_set_data (PkBackend *backend, PkBackendJob *job, const gchar *rid, const gchar *parameter, const gchar *value)
{
	pk_backend_spawn_helper (spawn, job, "pisiBackend.py", "repo-set-data", rid, parameter, value, NULL);
}

/**
 * pk_backend_get_description:
 */
const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return "PiSi";
}

/**
 * pk_backend_get_author:
 */
const gchar *
pk_backend_get_author (PkBackend *backend)
{
	return "S.Çağlar Onur <caglar@pardus.org.tr>\nIkey Doherty <ikey@solusos.com>";
}
