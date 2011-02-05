/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
 * Copyright (C) 2008-2010 Valeriy Lyasotskiy <onestep@ukr.net>
 * Copyright (C) 2010-2011 Jonathan Conder <jonno.conder@gmail.com>
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

#include <alpm.h>
#include <pk-backend.h>
#include <string.h>

#include "pk-backend-alpm.h"
#include "pk-backend-databases.h"
#include "pk-backend-groups.h"
#include "pk-backend-packages.h"
#include "pk-backend-search.h"

static gpointer
pk_backend_pattern_needle (const gchar *needle, GError **error)
{
	return (gpointer) needle;
}

static gpointer
pk_backend_pattern_regex (const gchar *needle, GError **error)
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
pk_backend_pattern_chroot (const gchar *needle, GError **error)
{
	g_return_val_if_fail (needle != NULL, NULL);

	if (G_IS_DIR_SEPARATOR (*needle)) {
		const gchar *file = needle, *root = alpm_option_get_root ();

		/* adjust needle to the correct prefix */
		for (; *file == *root; ++file, ++root) {
			if (*root == '\0') {
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
pk_backend_match_all (pmpkg_t *pkg, gpointer pattern)
{
	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (pattern != NULL, FALSE);

	/* match all packages */
	return TRUE;
}

static gboolean
pk_backend_match_details (pmpkg_t *pkg, GRegex *regex)
{
	const gchar *desc;
	pmdb_t *db;
	const alpm_list_t *i;

	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (regex != NULL, FALSE);

	/* match the name first... */
	if (g_regex_match (regex, alpm_pkg_get_name (pkg), 0, NULL)) {
		return TRUE;
	}

	/* ... then the description... */
	desc = alpm_pkg_get_desc (pkg);
	if (desc != NULL && g_regex_match (regex, desc, 0, NULL)) {
		return TRUE;
	}

	/* ... then the database... */
	db = alpm_pkg_get_db (pkg);
	if (db != NULL && g_regex_match (regex, alpm_db_get_name (db),
					 G_REGEX_MATCH_ANCHORED, NULL)) {
		return TRUE;
	}

	/* ... then the licenses */
	for (i = alpm_pkg_get_licenses (pkg); i != NULL; i = i->next) {
		if (g_regex_match (regex, i->data, G_REGEX_MATCH_ANCHORED,
				   NULL)) {
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
pk_backend_match_file (pmpkg_t *pkg, const gchar *needle)
{
	const alpm_list_t *i;

	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (needle != NULL, FALSE);

	/* match any file the package contains */
	if (G_IS_DIR_SEPARATOR (*needle)) {
		for (i = alpm_pkg_get_files (pkg); i != NULL; i = i->next) {
			/* match the full path of file */
			if (g_strcmp0 (i->data, needle + 1) == 0) {
				return TRUE;
			}
		}
	} else {
		for (i = alpm_pkg_get_files (pkg); i != NULL; i = i->next) {
			const gchar *file = strrchr (i->data, G_DIR_SEPARATOR);
			if (file == NULL) {
				file = i->data;
			} else {
				++file;
			}

			/* match the basename of file */
			if (g_strcmp0 (file, needle) == 0) {
				return TRUE;
			}
		}
	}

	return FALSE;
}

static gboolean
pk_backend_match_group (pmpkg_t *pkg, const gchar *needle)
{
	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (needle != NULL, FALSE);

	/* match the group the package is in */
	return g_strcmp0 (needle, alpm_pkg_get_group (pkg)) == 0;
}

static gboolean
pk_backend_match_name (pmpkg_t *pkg, GRegex *regex)
{
	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (regex != NULL, FALSE);

	/* match the name of the package */
	return g_regex_match (regex, alpm_pkg_get_name (pkg), 0, NULL);
}

static gboolean
pk_backend_match_provides (pmpkg_t *pkg, gpointer pattern)
{
	/* TODO: implement GStreamer codecs, Pango fonts, etc. */
	const alpm_list_t *i;

	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (pattern != NULL, FALSE);

	/* match features provided by package */
	for (i = alpm_pkg_get_provides (pkg); i != NULL; i = i->next) {
		const gchar *needle = pattern, *name = i->data;

		for (; *needle == *name; ++needle, ++name) {
			if (*needle == '\0') {
				if (*name == '\0' || *name == '=') {
					return TRUE;
				} else {
					break;
				}
			}
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
typedef gboolean (*MatchFunc) (pmpkg_t *pkg, gpointer pattern);

static PatternFunc pattern_funcs[] = {
	pk_backend_pattern_needle,
	pk_backend_pattern_regex,
	pk_backend_pattern_chroot,
	pk_backend_pattern_needle,
	pk_backend_pattern_regex,
	pk_backend_pattern_needle
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
	pk_backend_match_all,
	(MatchFunc) pk_backend_match_details,
	(MatchFunc) pk_backend_match_file,
	(MatchFunc) pk_backend_match_group,
	(MatchFunc) pk_backend_match_name,
	pk_backend_match_provides
};

static gboolean
alpm_pkg_is_local (pmpkg_t *pkg)
{
	pmpkg_t *local;

	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (localdb != NULL, FALSE);

	/* find an installed package with the same name */
	local = alpm_db_get_pkg (localdb, alpm_pkg_get_name (pkg));
	if (local == NULL) {
		return FALSE;
	}

	/* make sure the installed version is the same */
	if (alpm_pkg_vercmp (alpm_pkg_get_version (local),
			     alpm_pkg_get_version (pkg)) != 0) {
		return FALSE;
	}

	/* make sure the installed arch is the same */
	if (g_strcmp0 (alpm_pkg_get_arch (local),
		       alpm_pkg_get_arch (pkg)) != 0) {
		return FALSE;
	}

	return TRUE;
}

static void
pk_backend_search_db (PkBackend *self, pmdb_t *db, MatchFunc match,
		      const alpm_list_t *patterns)
{
	const alpm_list_t *i, *j;

	g_return_if_fail (self != NULL);
	g_return_if_fail (db != NULL);
	g_return_if_fail (match != NULL);

	/* emit packages that match all search terms */
	for (i = alpm_db_get_pkgcache (db); i != NULL; i = i->next) {
		if (pk_backend_cancelled (self)) {
			break;
		}

		for (j = patterns; j != NULL; j = j->next) {
			if (!match (i->data, j->data)) {
				break;
			}
		}

		/* all search terms matched */
		if (j == NULL) {
			if (db == localdb) {
				pk_backend_pkg (self, i->data,
						PK_INFO_ENUM_INSTALLED);
			} else if (!alpm_pkg_is_local (i->data)) {
				pk_backend_pkg (self, i->data,
						PK_INFO_ENUM_AVAILABLE);
			}
		}
	}
}

static gboolean
pk_backend_search_thread (PkBackend *self)
{
	gchar **needles;
	SearchType type;

	PatternFunc pattern_func;
	GDestroyNotify pattern_free;
	MatchFunc match_func;

	PkBitfield filters;
	gboolean skip_local, skip_remote;

	const alpm_list_t *i;
	alpm_list_t *patterns = NULL;
	GError *error = NULL;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (localdb != NULL, FALSE);

	needles = pk_backend_get_strv (self, "search");
	type = pk_backend_get_uint (self, "search-type");

	g_return_val_if_fail (needles != NULL, FALSE);
	g_return_val_if_fail (type < SEARCH_TYPE_LAST, FALSE);

	pattern_func = pattern_funcs[type];
	pattern_free = pattern_frees[type];
	match_func = match_funcs[type];

	g_return_val_if_fail (pattern_func != NULL, FALSE);
	g_return_val_if_fail (match_func != NULL, FALSE);

	filters = pk_backend_get_uint (self, "filters");
	skip_local = pk_bitfield_contain (filters,
					  PK_FILTER_ENUM_NOT_INSTALLED);
	skip_remote = pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED);

	/* convert search terms to the pattern requested */
	for (; *needles != NULL; ++needles) {
		gpointer pattern = pattern_func (*needles, &error);

		if (pattern == NULL) {
			goto out;
		}

		patterns = alpm_list_add (patterns, pattern);
	}

	/* find installed packages first */
	if (!skip_local) {
		pk_backend_search_db (self, localdb, match_func, patterns);
	}

	if (skip_remote) {
		goto out;
	}

	for (i = alpm_option_get_syncdbs (); i != NULL; i = i->next) {
		if (pk_backend_cancelled (self)) {
			break;
		}

		pk_backend_search_db (self, i->data, match_func, patterns);
	}

out:
	if (pattern_free != NULL) {
		alpm_list_free_inner (patterns, pattern_free);
	}
	alpm_list_free (patterns);
	return pk_backend_finish (self, error);
}

void
pk_backend_get_packages (PkBackend *self, PkBitfield filters)
{
	g_return_if_fail (self != NULL);

	/* provide a dummy needle */
	pk_backend_set_strv (self, "search", g_strsplit ("", ";", 0));

	pk_backend_set_uint (self, "search-type", SEARCH_TYPE_ALL);
	pk_backend_run (self, PK_STATUS_ENUM_QUERY, pk_backend_search_thread);
}

void
pk_backend_search_details (PkBackend *self, PkBitfield filters, gchar **values)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (values != NULL);

	pk_backend_set_uint (self, "search-type", SEARCH_TYPE_DETAILS);
	pk_backend_run (self, PK_STATUS_ENUM_QUERY, pk_backend_search_thread);
}

void
pk_backend_search_files (PkBackend *self, PkBitfield filters, gchar **values)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (values != NULL);

	/* speed up search by restricting it to local database */
	pk_bitfield_add (filters, PK_FILTER_ENUM_INSTALLED);
	pk_backend_set_uint (self, "filters", filters);

	pk_backend_set_uint (self, "search-type", SEARCH_TYPE_FILES);
	pk_backend_run (self, PK_STATUS_ENUM_QUERY, pk_backend_search_thread);
}

void
pk_backend_search_groups (PkBackend *self, PkBitfield filters, gchar **values)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (values != NULL);

	pk_backend_set_uint (self, "search-type", SEARCH_TYPE_GROUP);
	pk_backend_run (self, PK_STATUS_ENUM_QUERY, pk_backend_search_thread);
}

void
pk_backend_search_names (PkBackend *self, PkBitfield filters, gchar **values)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (values != NULL);

	pk_backend_set_uint (self, "search-type", SEARCH_TYPE_NAME);
	pk_backend_run (self, PK_STATUS_ENUM_QUERY, pk_backend_search_thread);
}

void
pk_backend_what_provides (PkBackend *self, PkBitfield filters,
			  PkProvidesEnum provides, gchar **values)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (values != NULL);

	pk_backend_set_uint (self, "search-type", SEARCH_TYPE_PROVIDES);
	pk_backend_run (self, PK_STATUS_ENUM_QUERY, pk_backend_search_thread);
}
