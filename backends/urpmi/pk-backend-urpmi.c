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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <pk-backend.h>
#include <pk-backend-spawn.h>

static PkBackendSpawn *spawn;

/**
 * pk_backend_get_description:
 */
const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return "URPMI";
}

/**
 * pk_backend_get_author:
 */
const gchar *
pk_backend_get_author (PkBackend *backend)
{
	return "Aurelien Lefebvre <alkh@mandriva.org>, "
		"Per Oyvind Karlsen <peroyvind@mandriva.org>",
		"Thierry Vignaud <thierry.vignaud@gmail.com>";
}

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
pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
	g_debug ("backend: initialize");

	spawn = pk_backend_spawn_new (conf);
	pk_backend_spawn_set_name (spawn, "urpmi");
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
		PK_GROUP_ENUM_DESKTOP_GNOME,
		PK_GROUP_ENUM_DESKTOP_KDE,
		PK_GROUP_ENUM_DESKTOP_XFCE,
		PK_GROUP_ENUM_DESKTOP_OTHER,
		PK_GROUP_ENUM_PUBLISHING,
		PK_GROUP_ENUM_SERVERS,
		PK_GROUP_ENUM_FONTS,
		PK_GROUP_ENUM_ADMIN_TOOLS,
		PK_GROUP_ENUM_LEGACY,
		PK_GROUP_ENUM_LOCALIZATION,
		PK_GROUP_ENUM_VIRTUALIZATION,
		PK_GROUP_ENUM_POWER_MANAGEMENT,
		PK_GROUP_ENUM_SECURITY,
		PK_GROUP_ENUM_COMMUNICATION,
		PK_GROUP_ENUM_NETWORK,
		PK_GROUP_ENUM_MAPS,
		PK_GROUP_ENUM_REPOS,
		-1);
}

/**
 * pk_backend_get_filters:
 */
PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_GUI,
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_DEVELOPMENT,
		PK_FILTER_ENUM_SUPPORTED,
		PK_FILTER_ENUM_FREE,
		PK_FILTER_ENUM_DOWNLOADED,
		PK_FILTER_ENUM_NOT_DOWNLOADED,
		PK_FILTER_ENUM_APPLICATION,
		PK_FILTER_ENUM_NOT_APPLICATION,
		-1);
}

/**
 * pk_backend_get_mime_types:
 */
gchar **
pk_backend_get_mime_types (PkBackend *backend)
{
	const gchar *mime_types[] = {
				"application/x-rpm",
				"application/x-urpmi",
				NULL };
	return g_strdupv ((gchar **) mime_types);
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
 * pk_backend_search_name:
 */
void
pk_backend_search_names (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **search)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "search-name", filters_text, search[0], NULL);
	g_free (filters_text);
}

/**
 * pk_backend_get_details:
 */
void
pk_backend_get_details (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "get-details", package_ids_temp, NULL);
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
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "get-files", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_depends_on:
 */
void
pk_backend_depends_on (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	gchar *filters_text;
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "depends-on", filters_text, package_ids_temp, pk_backend_bool_to_string (recursive), NULL);
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
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "get-updates", filters_text, NULL);
	g_free (filters_text);
}

/**
 * backend_get_update_detail:
 */
void
pk_backend_get_update_detail (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "get-update-detail", package_ids_temp, NULL);
	g_free (package_ids_temp);
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

	pk_backend_job_set_locked (job, TRUE);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "refresh-cache", pk_backend_bool_to_string (force), NULL);
	pk_backend_job_set_locked (job, FALSE);
}

/**
 * pk_backend_install_files:
 */
void
pk_backend_install_files (PkBackend *backend, PkBackendJob *job,
			  PkBitfield transaction_flags,
			  gchar **full_paths)
{
	gchar *full_paths_temp;
	gchar *transaction_flags_temp;

	/* send the complete list as stdin */
	full_paths_temp = g_strjoinv(PK_BACKEND_SPAWN_FILENAME_DELIM, full_paths);
	transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	pk_backend_job_set_locked (job, TRUE);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "install-files", transaction_flags_temp, full_paths_temp, NULL);
	pk_backend_job_set_locked (job, FALSE);
	g_free (full_paths_temp);
	g_free (transaction_flags_temp);
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
	pk_backend_job_set_locked (job, TRUE);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "install-packages", transaction_flags_temp, package_ids_temp, NULL);
	pk_backend_job_set_locked (job, FALSE);
	g_free (package_ids_temp);
	g_free (transaction_flags_temp);
}

/**
 * pk_backend_remove_packages:
 */
void
pk_backend_remove_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	gchar *package_ids_temp;
	gchar *transaction_flags_temp = pk_transaction_flag_bitfield_to_string(transaction_flags);
	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_job_set_locked (job, TRUE);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "remove-packages", pk_backend_bool_to_string (allow_deps), pk_backend_bool_to_string(autoremove), transaction_flags_temp, package_ids_temp, NULL);
	pk_backend_job_set_locked (job, FALSE);
	g_free (package_ids_temp);
	g_free (transaction_flags_temp);
}

/**
 * pk_backend_repo_enable:
 */
void
pk_backend_repo_enable (PkBackend *backend, PkBackendJob *job, const gchar *rid, gboolean enabled)
{
	pk_backend_job_set_locked (job, TRUE);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "repo-enable", rid, pk_backend_bool_to_string (enabled), NULL);
	pk_backend_job_set_locked (job, FALSE);
}

/**
 * pk_backend_search_groups:
 */
void
pk_backend_search_groups (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **search)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "search-group", filters_text, search[0], NULL);
	g_free (filters_text);
}

/**
 * backend_get_packages:
 */
void
pk_backend_get_packages (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "get-packages", filters_text, NULL);
	g_free (filters_text);
}
/**
 * pk_backend_get_repo_list:
 */
void
pk_backend_get_repo_list (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "get-repo-list", NULL);
}

/**
 * pk_backend_required_by:
 */
void
pk_backend_required_by (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	gchar *package_ids_temp;
	gchar *filters_text;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "required-by", filters_text, package_ids_temp, pk_backend_bool_to_string (recursive), NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

/**
 * pk_backend_search_details:
 */
void
pk_backend_search_details (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **search)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "search-details", filters_text, search[0], NULL);
	g_free (filters_text);
}

/**
 * pk_backend_search_files:
 */
void
pk_backend_search_files (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **search)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "search-file", filters_text, search[0], NULL);
	g_free (filters_text);
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
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "resolve", filters_text, package_ids_temp, NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
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
	pk_backend_job_set_locked (job, TRUE);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "update-packages", transaction_flags_temp, package_ids_temp, NULL);
	pk_backend_job_set_locked (job, FALSE);
	g_free (package_ids_temp);
	g_free (transaction_flags_temp);
}

/**
 * backend_get_distro_upgrades:
 */
void
pk_backend_get_distro_upgrades (PkBackend *backend, PkBackendJob *job)
{
	pk_backend_job_set_locked (job, TRUE);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "get-distro-upgrades", NULL);
	pk_backend_job_set_locked (job, FALSE);
}

/**
 * backend_what_provides:
 */
void
pk_backend_what_provides (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	gchar *search_tmp;
	gchar *filters_text;

	filters_text = pk_filter_bitfield_to_string (filters);
	search_tmp = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, job, "urpmi-dispatched-backend.pl", "what-provides", filters_text, "any", search_tmp, NULL);
	g_free (filters_text);
	g_free (search_tmp);
}

