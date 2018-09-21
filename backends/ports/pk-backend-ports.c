/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009, 2013 Anders F Bjorklund <afb@users.sourceforge.net>
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

static PkBackendSpawn *spawn = 0;
static const gchar* BACKEND_FILE = "portsBackend.rb";

void
pk_backend_start_job (PkBackend *backend, PkBackendJob *job)
{
	if (pk_backend_spawn_is_busy (spawn)) {
		pk_backend_job_error_code (job,
					   PK_ERROR_ENUM_LOCK_REQUIRED,
					   "spawned backend requires lock");
		pk_backend_job_finished (job);
		return;
	}
}

void
pk_backend_stop_job (PkBackend *backend, PkBackendJob *job)
{
}


void
pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
	g_debug ("backend: initialize");

	spawn = pk_backend_spawn_new (conf);
	pk_backend_spawn_set_name (spawn, "ports");
	/* allowing sigkill as long as no one complain */
	pk_backend_spawn_set_allow_sigkill (spawn, TRUE);
}

void
pk_backend_destroy (PkBackend *backend)
{
	g_debug ("backend: destroy");
	g_object_unref (spawn);
}

PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (
			PK_GROUP_ENUM_ACCESSIBILITY,
			PK_GROUP_ENUM_ACCESSORIES,
			PK_GROUP_ENUM_ADMIN_TOOLS,
			PK_GROUP_ENUM_COMMUNICATION,
			PK_GROUP_ENUM_DESKTOP_GNOME,
			PK_GROUP_ENUM_DESKTOP_KDE,
			PK_GROUP_ENUM_DESKTOP_OTHER,
			PK_GROUP_ENUM_DESKTOP_XFCE,
		/*	PK_GROUP_ENUM_EDUCATION, */
			PK_GROUP_ENUM_FONTS,
			PK_GROUP_ENUM_GAMES,
			PK_GROUP_ENUM_GRAPHICS,
			PK_GROUP_ENUM_INTERNET,
		/*	PK_GROUP_ENUM_LEGACY, */
			PK_GROUP_ENUM_LOCALIZATION,
		/*	PK_GROUP_ENUM_MAPS, */
			PK_GROUP_ENUM_MULTIMEDIA,
			PK_GROUP_ENUM_NETWORK,
		/*	PK_GROUP_ENUM_OFFICE, */
			PK_GROUP_ENUM_OTHER,
		/*	PK_GROUP_ENUM_POWER_MANAGEMENT, */
			PK_GROUP_ENUM_PROGRAMMING,
			PK_GROUP_ENUM_PUBLISHING,
		/*	PK_GROUP_ENUM_REPOS, */
			PK_GROUP_ENUM_SECURITY,
			PK_GROUP_ENUM_SERVERS,
			PK_GROUP_ENUM_SYSTEM,
			PK_GROUP_ENUM_VIRTUALIZATION,
			PK_GROUP_ENUM_SCIENCE,
			PK_GROUP_ENUM_DOCUMENTATION,
		/*	PK_GROUP_ENUM_ELECTRONICS, */
		/*	PK_GROUP_ENUM_COLLECTIONS, */
		/*	PK_GROUP_ENUM_VENDOR, */
			PK_GROUP_ENUM_NEWEST,
			-1);
}

PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
			PK_FILTER_ENUM_INSTALLED,
			-1);
}

gchar **
pk_backend_get_mime_types (PkBackend *backend)
{
	const gchar *mime_types[] = {
				"application/x-compressed-tar",		/* .tgz */
				"application/x-bzip-compressed-tar",	/* .tbz */
				"application/x-xz-compressed-tar",	/* .txz */
				NULL };
	return g_strdupv ((gchar **) mime_types);
}

void
pk_backend_cancel (PkBackend *backend, PkBackendJob *job)
{
	/* this feels bad... */
	pk_backend_spawn_kill (spawn);
}

void
pk_backend_download_packages (PkBackend *backend, PkBackendJob *job, gchar **package_ids, const gchar *directory)
{
	gchar *package_ids_temp;

	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "download-packages", directory, package_ids_temp, NULL);
	g_free (package_ids_temp);
}

void
pk_backend_depends_on (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	gchar *filters_text;
	gchar *package_ids_temp;

	package_ids_temp = pk_package_ids_to_string (package_ids);
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "depends-on", filters_text, package_ids_temp, pk_backend_bool_to_string (recursive), NULL);
	g_free (package_ids_temp);
	g_free (filters_text);
}

void
pk_backend_get_details (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	gchar *package_ids_temp;

	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "get-details", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

void
pk_backend_get_files (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	gchar *package_ids_temp;

	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "get-files", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

void
pk_backend_get_update_detail (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	gchar *package_ids_temp;

	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "get-update-detail", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

void
pk_backend_get_updates (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	gchar *filters_text;

	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "get-updates", filters_text, NULL);
	g_free (filters_text);
}

void
pk_backend_install_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	gchar *package_ids_temp;
	gchar *transaction_flags_temp;

	package_ids_temp = pk_package_ids_to_string (package_ids);
	transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "install-packages", transaction_flags_temp, package_ids_temp, NULL);
	g_free (transaction_flags_temp);
	g_free (package_ids_temp);
}

void
pk_backend_install_files (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **full_paths)
{
	gchar *full_paths_temp;
	gchar *transaction_flags_temp;

	full_paths_temp = g_strjoinv (PK_BACKEND_SPAWN_FILENAME_DELIM, full_paths);
	transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "install-files", transaction_flags_temp, full_paths_temp, NULL);
	g_free (transaction_flags_temp);
	g_free (full_paths_temp);
}

void
pk_backend_refresh_cache (PkBackend *backend, PkBackendJob *job, gboolean force)
{
	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_job_finished (job);
		return;
	}

	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "refresh-cache", pk_backend_bool_to_string (force), NULL);
}

void
pk_backend_remove_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	gchar *package_ids_temp;
	gchar *transaction_flags_temp;

	package_ids_temp = pk_package_ids_to_string (package_ids);
	transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "remove-packages", transaction_flags_temp, package_ids_temp, pk_backend_bool_to_string (allow_deps), pk_backend_bool_to_string (autoremove), NULL);
	g_free (transaction_flags_temp);
	g_free (package_ids_temp);
}

void
pk_backend_resolve (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids)
{
	gchar *filters_text;
	gchar *package_ids_temp;

	filters_text = pk_filter_bitfield_to_string (filters);
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "resolve", filters_text, package_ids_temp, NULL);
	g_free (package_ids_temp);
	g_free (filters_text);
}

void
pk_backend_search_details (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;

	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "search-details", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

void
pk_backend_search_files (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;

	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "search-file", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

void
pk_backend_search_groups (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;

	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "search-group", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

void
pk_backend_search_names (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;

	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "search-name", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

void
pk_backend_update_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	gchar *package_ids_temp;
	gchar *transaction_flags_temp;

	package_ids_temp = pk_package_ids_to_string (package_ids);
	transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "update-packages", transaction_flags_temp, package_ids_temp, NULL);
	g_free (transaction_flags_temp);
	g_free (package_ids_temp);
}

void
pk_backend_get_packages (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	gchar *filters_text;

	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "get-packages", filters_text, NULL);
	g_free (filters_text);
}

void
pk_backend_get_repo_list (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	gchar *filters_text;

	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "get-repo-list", filters_text, NULL);
	g_free (filters_text);
}

void
pk_backend_required_by (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	gchar *package_ids_temp;
	gchar *filters_text;

	package_ids_temp = pk_package_ids_to_string (package_ids);
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, job, BACKEND_FILE, "required-by", filters_text, package_ids_temp, pk_backend_bool_to_string (recursive), NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return "Ports";
}

const gchar *
pk_backend_get_author (PkBackend *backend)
{
	return "Anders F Björklund <afb@users.sourceforge.net>";
}

