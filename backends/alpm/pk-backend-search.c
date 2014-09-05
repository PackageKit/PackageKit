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
#include "pk-backend-groups.h"
#include "pk-backend-packages.h"

static gpointer
pk_backend_pattern_needle (const gchar *needle, GError **error)
{
	return (gpointer) needle;
}

static gpointer
pk_backend_pattern_regex (const gchar *needle, GError **error)
{
	_cleanup_free_ gchar *pattern = NULL;
	g_return_val_if_fail (needle != NULL, NULL);
	pattern = g_regex_escape_string (needle, -1);
	return g_regex_new (pattern, G_REGEX_CASELESS, 0, error);
}

static gpointer
pk_backend_pattern_chroot (const gchar *needle, GError **error)
{
	g_return_val_if_fail (needle != NULL, NULL);
	g_return_val_if_fail (alpm != NULL, NULL);

	if (G_IS_DIR_SEPARATOR (*needle)) {
		const gchar *file = needle, *root = alpm_option_get_root (alpm);

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
pk_backend_match_all (alpm_pkg_t *pkg, gpointer pattern)
{
	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (pattern != NULL, FALSE);

	/* match all packages */
	return TRUE;
}

static gboolean
pk_backend_match_details (alpm_pkg_t *pkg, GRegex *regex)
{
	const gchar *desc;
	alpm_db_t *db;
	const alpm_list_t *i;

	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (regex != NULL, FALSE);

	/* match the name first... */
	if (g_regex_match (regex, alpm_pkg_get_name (pkg), 0, NULL))
		return TRUE;

	/* ... then the description... */
	desc = alpm_pkg_get_desc (pkg);
	if (desc != NULL && g_regex_match (regex, desc, 0, NULL))
		return TRUE;

	/* ... then the database... */
	db = alpm_pkg_get_db (pkg);
	if (db != NULL && g_regex_match (regex, alpm_db_get_name (db),
					 G_REGEX_MATCH_ANCHORED, NULL))
		return TRUE;

	/* ... then the licenses */
	for (i = alpm_pkg_get_licenses (pkg); i != NULL; i = i->next) {
		if (g_regex_match (regex, i->data, G_REGEX_MATCH_ANCHORED, NULL))
			return TRUE;
	}

	return FALSE;
}

static gboolean
pk_backend_match_file (alpm_pkg_t *pkg, const gchar *needle)
{
	alpm_filelist_t *files;
	gsize i;

	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (needle != NULL, FALSE);

	files = alpm_pkg_get_files (pkg);

	/* match any file the package contains */
	if (G_IS_DIR_SEPARATOR (*needle)) {
		for (i = 0; i < files->count; ++i) {
			const gchar *file = files->files[i].name;
			/* match the full path of file */
			if (g_strcmp0 (file, needle + 1) == 0)
				return TRUE;
		}
	} else {
		for (i = 0; i < files->count; ++i) {
			const gchar *file = files->files[i].name;
			const gchar *name = strrchr (file, G_DIR_SEPARATOR);

			if (name == NULL) {
				name = file;
			} else {
				++name;
			}

			/* match the basename of file */
			if (g_strcmp0 (name, needle) == 0)
				return TRUE;
		}
	}

	return FALSE;
}

static gboolean
pk_backend_match_group (alpm_pkg_t *pkg, const gchar *needle)
{
	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (needle != NULL, FALSE);

	/* match the group the package is in */
	return g_strcmp0 (needle, pkalpm_pkg_get_group (pkg)) == 0;
}

static gboolean
pk_backend_match_name (alpm_pkg_t *pkg, GRegex *regex)
{
	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (regex != NULL, FALSE);

	/* match the name of the package */
	return g_regex_match (regex, alpm_pkg_get_name (pkg), 0, NULL);
}

static gboolean
pk_backend_match_provides (alpm_pkg_t *pkg, gpointer pattern)
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
				if (*name == '\0' || *name == '=')
					return TRUE;
				break;
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
typedef gboolean (*MatchFunc) (alpm_pkg_t *pkg, gpointer pattern);

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
alpm_pkg_is_local (alpm_pkg_t *pkg)
{
	alpm_pkg_t *local;

	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (localdb != NULL, FALSE);

	/* find an installed package with the same name */
	local = alpm_db_get_pkg (localdb, alpm_pkg_get_name (pkg));
	if (local == NULL)
		return FALSE;

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
pk_backend_search_db (PkBackendJob *job, alpm_db_t *db, MatchFunc match,
		      const alpm_list_t *patterns)
{
	const alpm_list_t *i, *j;

	g_return_if_fail (db != NULL);
	g_return_if_fail (match != NULL);

	/* emit packages that match all search terms */
	for (i = alpm_db_get_pkgcache (db); i != NULL; i = i->next) {
		if (pkalpm_is_backend_cancelled (job))
			break;

		for (j = patterns; j != NULL; j = j->next) {
			if (!match (i->data, j->data))
				break;
		}

		/* all search terms matched */
		if (j == NULL) {
			if (db == localdb) {
				pkalpm_backend_pkg (job, i->data,
						PK_INFO_ENUM_INSTALLED);
			} else if (!alpm_pkg_is_local (i->data)) {
				pkalpm_backend_pkg (job, i->data,
						PK_INFO_ENUM_AVAILABLE);
			}
		}
	}
}

static void
pk_backend_search_thread (PkBackendJob *job, GVariant* params, gpointer p)
{
	gchar **needles = NULL;
	SearchType type;

	PatternFunc pattern_func;
	GDestroyNotify pattern_free;
	MatchFunc match_func;

	PkRoleEnum role;
	PkBitfield filters;
	gboolean skip_local, skip_remote;

	const alpm_list_t *i;
	alpm_list_t *patterns = NULL;
	_cleanup_error_free_ GError *error = NULL;

	g_return_if_fail (alpm != NULL);
	g_return_if_fail (localdb != NULL);
	g_return_if_fail (p == NULL);

	role = pk_backend_job_get_role(job);
	switch(role) {
	case PK_ROLE_ENUM_GET_PACKAGES:
		type = SEARCH_TYPE_ALL;
		g_variant_get (params, "(t)", &filters);
		break;
	case PK_ROLE_ENUM_GET_DETAILS:
		type = SEARCH_TYPE_DETAILS;
		g_variant_get (params, "(^a&s)", &needles);
		break;
	case PK_ROLE_ENUM_SEARCH_FILE:
		type = SEARCH_TYPE_FILES;
		g_variant_get (params, "(t^a&s)", &filters, &needles);
		break;
	case PK_ROLE_ENUM_SEARCH_GROUP:
		type = SEARCH_TYPE_GROUP;
		g_variant_get (params, "(t^a&s)", &filters, &needles);
		break;
	case PK_ROLE_ENUM_SEARCH_NAME:
		type = SEARCH_TYPE_NAME;
		g_variant_get (params, "(t^a&s)", &filters, &needles);
		break;
	case PK_ROLE_ENUM_WHAT_PROVIDES:
		type = SEARCH_TYPE_PROVIDES;
		g_variant_get (params, "(t^a&s)", &filters, &needles);
		break;
	case PK_ROLE_ENUM_SEARCH_DETAILS:
		type = SEARCH_TYPE_DETAILS;
		g_variant_get (params, "(t^a&s)",
					  &filters,
					  &needles);
		break;
	default:
		type = 0;
		g_assert_not_reached ();
		break;
	}

	g_return_if_fail (type < SEARCH_TYPE_LAST);

	pattern_func = pattern_funcs[type];
	pattern_free = pattern_frees[type];
	match_func = match_funcs[type];

	g_return_if_fail (pattern_func != NULL);
	g_return_if_fail (match_func != NULL);

	skip_local = pk_bitfield_contain (filters,
					  PK_FILTER_ENUM_NOT_INSTALLED);
	skip_remote = pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED);

	/* convert search terms to the pattern requested */
	if (needles) {
		for (; *needles != NULL; ++needles) {
			gpointer pattern = pattern_func (*needles, &error);

			if (pattern == NULL)
				goto out;

			patterns = alpm_list_add (patterns, pattern);
		}
	}

	/* find installed packages first */
	if (!skip_local)
		pk_backend_search_db (job, localdb, match_func, patterns);

	if (skip_remote)
		goto out;

	for (i = alpm_get_syncdbs (alpm); i != NULL; i = i->next) {
		if (pkalpm_is_backend_cancelled (job))
			break;

		pk_backend_search_db (job, i->data, match_func, patterns);
	}
out:
	if (pattern_free != NULL)
		alpm_list_free_inner (patterns, pattern_free);
	alpm_list_free (patterns);
	pk_backend_finish (job, error);
}

void
pk_backend_get_packages (PkBackend  *self,
			 PkBackendJob *job,
			 PkBitfield filters)
{
	pkalpm_backend_run (job, PK_STATUS_ENUM_QUERY, pk_backend_search_thread, NULL);
}

void
pk_backend_search_details (PkBackend    *self,
			   PkBackendJob *job,
			   PkBitfield filters,
			   gchar      **search)
{
	g_return_if_fail (search != NULL);

	pkalpm_backend_run (job, PK_STATUS_ENUM_QUERY, pk_backend_search_thread, NULL);
}

void
pk_backend_search_files (PkBackend  *self,
			 PkBackendJob *job,
			 PkBitfield filters,
			 gchar      **search)
{
	g_return_if_fail (search != NULL);

// 	/* speed up search by restricting it to local database */
// 	pk_bitfield_add (filters, PK_FILTER_ENUM_INSTALLED);
// 	pk_backend_set_uint (self, "filters", filters);

	pkalpm_backend_run (job, PK_STATUS_ENUM_QUERY, pk_backend_search_thread, NULL);
}

void
pk_backend_search_groups (PkBackend *self,
			  PkBackendJob *job,
			  PkBitfield filters,
			  gchar      **search)
{
	g_return_if_fail (search != NULL);

	pkalpm_backend_run (job, PK_STATUS_ENUM_QUERY, pk_backend_search_thread, NULL);
}

void
pk_backend_search_names (PkBackend  *self,
			 PkBackendJob *job,
			 PkBitfield filters,
			 gchar      **search)
{
	g_return_if_fail (search != NULL);

	pkalpm_backend_run (job, PK_STATUS_ENUM_QUERY, pk_backend_search_thread, NULL);
}

void
pk_backend_what_provides (PkBackend *self,
			  PkBackendJob *job,
			  PkBitfield filters,
			  gchar      **search)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (search != NULL);

	pkalpm_backend_run (job, PK_STATUS_ENUM_QUERY, pk_backend_search_thread, NULL);
}
