/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
 * Copyright (C) 2008, 2009 Valeriy Lyasotskiy <onestep@ukr.net>
 * Copyright (C) 2010 Jonathan Conder <j@skurvy.no-ip.org>
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

#include <string.h>
#include <pacman.h>
#include "backend-error.h"
#include "backend-groups.h"
#include "backend-packages.h"
#include "backend-pacman.h"
#include "backend-repos.h"
#include "backend-search.h"

static gpointer
backend_pattern_needle (const gchar *needle, GError **error)
{
	return (gpointer) needle;
}

static gpointer
backend_pattern_regex (const gchar *needle, GError **error)
{
	gchar *pattern;
	GRegex *regex;

	g_return_val_if_fail (needle != NULL, NULL);

	pattern = g_regex_escape_string (needle, -1);
	regex = g_regex_new (pattern, G_REGEX_CASELESS, 0, error);
	g_free (pattern);

	return regex;
}

static gpointer
backend_pattern_chroot (const gchar *needle, GError **error)
{
	g_return_val_if_fail (pacman != NULL, FALSE);
	g_return_val_if_fail (needle != NULL, NULL);

	if (G_IS_DIR_SEPARATOR (*needle)) {
		const gchar *file = needle, *path = pacman_manager_get_root_path (pacman);

		/* adjust needle to the correct prefix */
		while (*file++ == *path++) {
			if (*path == '\0') {
				needle = file - 1;
				break;
			} else if (*file == '\0') {
				break;
			}
		}
	}

	return (gpointer) needle;
}

static gboolean
backend_match_all (PacmanPackage *package, gpointer pattern)
{
	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (pattern != NULL, FALSE);

	/* match all packages */
	return TRUE;
}

static gboolean
backend_match_details (PacmanPackage *package, gpointer pattern)
{
	const gchar *description;
	PacmanDatabase *database;
	const PacmanList *licenses;

	GRegex *regex = (GRegex *) pattern;

	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (regex != NULL, FALSE);

	/* match the name first... */
	if (g_regex_match (regex, pacman_package_get_name (package), 0, NULL)) {
		return TRUE;
	}

	/* ... then the description... */
	description = pacman_package_get_description (package);
	if (description != NULL && g_regex_match (regex, description, 0, NULL)) {
		return TRUE;
	}

	/* ... then the database... */
	database = pacman_package_get_database (package);
	if (database != NULL && g_regex_match (regex, pacman_database_get_name (database), G_REGEX_MATCH_ANCHORED, NULL)) {
		return TRUE;
	}

	/* ... then the licenses */
	for (licenses = pacman_package_get_licenses (package); licenses != NULL; licenses = pacman_list_next (licenses)) {
		const gchar *license = (const gchar *) pacman_list_get (licenses);
		if (g_regex_match (regex, license, G_REGEX_MATCH_ANCHORED, NULL)) {
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
backend_match_file (PacmanPackage *package, gpointer pattern)
{
	const PacmanList *files;
	const gchar *needle = (const gchar *) pattern;

	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (needle != NULL, FALSE);

	/* match any file the package contains */
	if (G_IS_DIR_SEPARATOR (*needle)) {
		for (files = pacman_package_get_files (package); files != NULL; files = pacman_list_next (files)) {
			const gchar *file = (const gchar *) pacman_list_get (files);

			/* match the full path of file */
			if (g_strcmp0 (file, needle + 1) == 0) {
				return TRUE;
			}
		}
	} else {
		for (files = pacman_package_get_files (package); files != NULL; files = pacman_list_next (files)) {
			const gchar *file = (const gchar *) pacman_list_get (files);
			file = strrchr (file, G_DIR_SEPARATOR);

			/* match the basename of file */
			if (file != NULL && g_strcmp0 (file + 1, needle) == 0) {
				return TRUE;
			}
		}
	}

	return FALSE;
}

static gboolean
backend_match_group (PacmanPackage *package, gpointer pattern)
{
	const gchar *needle = (const gchar *) pattern;

	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (needle != NULL, FALSE);

	/* match the group the package is in */
	return g_strcmp0 (needle, pacman_package_get_group (package)) == 0;
}

static gboolean
backend_match_name (PacmanPackage *package, gpointer pattern)
{
	GRegex *regex = (GRegex *) pattern;

	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (regex != NULL, FALSE);

	/* match the name of the package */
	return g_regex_match (regex, pacman_package_get_name (package), 0, NULL);
}

static gboolean
backend_match_provides (PacmanPackage *package, gpointer pattern)
{
	/* TODO: implement GStreamer codecs, Pango fonts, etc. */
	const PacmanList *provides;
	const gchar *needle = (const gchar *) pattern;

	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (needle != NULL, FALSE);

	/* match features provided by package */
	for (provides = pacman_package_get_provides (package); provides != NULL; provides = pacman_list_next (provides)) {
		const gchar *name = (const gchar *) pacman_list_get (provides);
		if (g_strcmp0 (needle, name) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

typedef enum {
	SEARCH_TYPE_ALL,
	SEARCH_TYPE_DETAILS,
	SEARCH_TYPE_FILES,
	SEARCH_TYPE_GROUP,
	SEARCH_TYPE_NAME,
	SEARCH_TYPE_PROVIDES,
	SEARCH_TYPE_LAST
} SearchType;

typedef gpointer (*PatternFunc) (const gchar *needle, GError **error);
typedef gboolean (*MatchFunc) (PacmanPackage *package, gpointer pattern);

static PatternFunc pattern_funcs[] = {
	backend_pattern_needle,
	backend_pattern_regex,
	backend_pattern_chroot,
	backend_pattern_needle,
	backend_pattern_regex,
	backend_pattern_needle
};

static GDestroyNotify pattern_frees[] = {
	NULL,
	(GDestroyNotify) g_regex_unref,
	NULL,
	NULL,
	(GDestroyNotify) g_regex_unref,
	NULL
};

static MatchFunc match_funcs[] = {
	backend_match_all,
	backend_match_details,
	backend_match_file,
	backend_match_group,
	backend_match_name,
	backend_match_provides
};

static gboolean
pacman_package_is_installed (PacmanPackage *package)
{
	PacmanPackage *installed;

	g_return_val_if_fail (local_database != NULL, FALSE);
	g_return_val_if_fail (package != NULL, FALSE);

	/* find an installed package with the same name */
	installed = pacman_database_find_package (local_database, pacman_package_get_name (package));
	if (installed == NULL) {
		return FALSE;
	}

	/* make sure the installed version is the same */
	if (pacman_package_compare_version (pacman_package_get_version (installed), pacman_package_get_version (package)) != 0) {
		return FALSE;
	}

	/* make sure the installed arch is the same */
	if (g_strcmp0 (pacman_package_get_arch (installed), pacman_package_get_arch (package)) != 0) {
		return FALSE;
	}

	return TRUE;
}

static void
backend_search_database (PkBackend *backend, PacmanDatabase *database, MatchFunc match, const PacmanList *patterns)
{
	const PacmanList *packages, *list;

	g_return_if_fail (backend != NULL);
	g_return_if_fail (database != NULL);
	g_return_if_fail (match != NULL);

	/* emit packages that match all search terms */
	for (packages = pacman_database_get_packages (database); packages != NULL; packages = pacman_list_next (packages)) {
		PacmanPackage *package = (PacmanPackage *) pacman_list_get (packages);

		if (backend_cancelled (backend)) {
			break;
		}

		for (list = patterns; list != NULL; list = pacman_list_next (list)) {
			if (!match (package, pacman_list_get (list))) {
				break;
			}
		}

		/* all search terms matched */
		if (list == NULL) {
			if (database == local_database) {
				backend_package (backend, package, PK_INFO_ENUM_INSTALLED);
			} else if (!pacman_package_is_installed (package)) {
				backend_package (backend, package, PK_INFO_ENUM_AVAILABLE);
			}
		}
	}
}

static gboolean
backend_search_thread (PkBackend *backend)
{
	gchar **search;
	SearchType search_type;

	PatternFunc pattern_func;
	GDestroyNotify pattern_free;
	MatchFunc match_func;

	PkBitfield filters;
	gboolean search_installed;
	gboolean search_not_installed;

	guint iterator;
	PacmanList *patterns = NULL;
	GError *error = NULL;

	g_return_val_if_fail (pacman != NULL, FALSE);
	g_return_val_if_fail (local_database != NULL, FALSE);
	g_return_val_if_fail (backend != NULL, FALSE);

	search = pk_backend_get_strv (backend, "search");
	search_type = (SearchType) pk_backend_get_uint (backend, "search-type");

	g_return_val_if_fail (search != NULL, FALSE);
	g_return_val_if_fail (search_type < SEARCH_TYPE_LAST, FALSE);

	pattern_func = pattern_funcs[search_type];
	pattern_free = pattern_frees[search_type];
	match_func = match_funcs[search_type];

	g_return_val_if_fail (pattern_func != NULL, FALSE);
	g_return_val_if_fail (match_func != NULL, FALSE);

	filters = pk_backend_get_uint (backend, "filters");
	search_installed = pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED);
	search_not_installed = pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED);

	/* convert search terms to the pattern requested */
	for (iterator = 0; search[iterator] != NULL; ++iterator) {
		gpointer pattern = pattern_func (search[iterator], &error);

		if (pattern != NULL) {
			patterns = pacman_list_add (patterns, pattern);
		} else {
			backend_error (backend, error);
			if (pattern_free != NULL) {
				pacman_list_free_full (patterns, pattern_free);
			} else {
				pacman_list_free (patterns);
			}
			backend_finished (backend);
			return FALSE;
		}
	}

	/* find installed packages first */
	if (!search_not_installed) {
		backend_search_database (backend, local_database, match_func, patterns);
	}

	if (!search_installed) {
		const PacmanList *databases;

		for (databases = pacman_manager_get_sync_databases (pacman); databases != NULL; databases = pacman_list_next (databases)) {
			PacmanDatabase *database = (PacmanDatabase *) pacman_list_get (databases);

			if (backend_cancelled (backend)) {
				break;
			}

			backend_search_database (backend, database, match_func, patterns);
		}
	}

	if (pattern_free != NULL) {
		pacman_list_free_full (patterns, pattern_free);
	} else {
		pacman_list_free (patterns);
	}
	backend_finished (backend);
	return TRUE;
}

/**
 * backend_get_packages:
 **/
void
backend_get_packages (PkBackend	*backend, PkBitfield filters)
{
	g_return_if_fail (backend != NULL);

	/* provide a dummy needle */
	pk_backend_set_strv (backend, "search", g_strsplit ("", ";", 0));

	pk_backend_set_uint (backend, "search-type", SEARCH_TYPE_ALL);
	backend_run (backend, PK_STATUS_ENUM_QUERY, backend_search_thread);
}

/**
 * backend_search_details:
 **/
void
backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (values != NULL);

	pk_backend_set_uint (backend, "search-type", SEARCH_TYPE_DETAILS);
	backend_run (backend, PK_STATUS_ENUM_QUERY, backend_search_thread);
}

/**
 * backend_search_files:
 **/
void
backend_search_files (PkBackend *backend, PkBitfield filters, gchar **values)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (values != NULL);

	/* speed up search by restricting it to local database */
	pk_bitfield_add (filters, PK_FILTER_ENUM_INSTALLED);
	pk_backend_set_uint (backend, "filters", filters);

	pk_backend_set_uint (backend, "search-type", SEARCH_TYPE_FILES);
	backend_run (backend, PK_STATUS_ENUM_QUERY, backend_search_thread);
}

/**
 * backend_search_groups:
 **/
void
backend_search_groups (PkBackend *backend, PkBitfield filters, gchar **values)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (values != NULL);

	pk_backend_set_uint (backend, "search-type", SEARCH_TYPE_GROUP);
	backend_run (backend, PK_STATUS_ENUM_QUERY, backend_search_thread);
}

/**
 * backend_search_names:
 **/
void
backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (values != NULL);

	pk_backend_set_uint (backend, "search-type", SEARCH_TYPE_NAME);
	backend_run (backend, PK_STATUS_ENUM_QUERY, backend_search_thread);
}

/**
 * backend_what_provides:
 **/
void
backend_what_provides (PkBackend *backend, PkBitfield filters, PkProvidesEnum provides, gchar **values)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (values != NULL);

	pk_backend_set_uint (backend, "search-type", SEARCH_TYPE_PROVIDES);
	backend_run (backend, PK_STATUS_ENUM_QUERY, backend_search_thread);
}
