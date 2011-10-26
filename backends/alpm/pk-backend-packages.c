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

#include "pk-backend-alpm.h"
#include "pk-backend-error.h"
#include "pk-backend-groups.h"
#include "pk-backend-packages.h"

gchar *
alpm_pkg_build_id (pmpkg_t *pkg)
{
	const gchar *name, *version, *arch, *repo;
	pmdb_t *db;

	g_return_val_if_fail (pkg != NULL, NULL);
	g_return_val_if_fail (localdb != NULL, NULL);

	name = alpm_pkg_get_name (pkg);
	version = alpm_pkg_get_version (pkg);

	arch = alpm_pkg_get_arch (pkg);
	if (arch == NULL) {
		arch = "any";
	}

	db = alpm_pkg_get_db (pkg);
	/* TODO: check */
	if (db == NULL || db == localdb) {
		repo = "installed";
	} else {
		repo = alpm_db_get_name (db);
	}

	return pk_package_id_build (name, version, arch, repo);
}

void
pk_backend_pkg (PkBackend *self, pmpkg_t *pkg, PkInfoEnum info)
{
	gchar *package;

	g_return_if_fail (self != NULL);
	g_return_if_fail (pkg != NULL);

	package = alpm_pkg_build_id (pkg);
	pk_backend_package (self, info, package, alpm_pkg_get_desc (pkg));
	g_free (package);
}

pmpkg_t *
pk_backend_find_pkg (PkBackend *self, const gchar *package_id, GError **error)
{
	gchar **package;
	const gchar *repo_id;
	pmdb_t *db = NULL;
	pmpkg_t *pkg;

	g_return_val_if_fail (self != NULL, NULL);
	g_return_val_if_fail (package_id != NULL, NULL);
	g_return_val_if_fail (alpm != NULL, NULL);
	g_return_val_if_fail (localdb != NULL, NULL);

	package = pk_package_id_split (package_id);
	repo_id = package[PK_PACKAGE_ID_DATA];

	/* find the database to search in */
	if (g_strcmp0 (repo_id, "installed") == 0) {
		db = localdb;
	} else {
		const alpm_list_t *i = alpm_option_get_syncdbs (alpm);
		for (; i != NULL; i = i->next) {
			const gchar *repo = alpm_db_get_name (i->data);

			if (g_strcmp0 (repo, repo_id) == 0) {
				db = i->data;
				break;
			}
		}
	}

	if (db != NULL) {
		pkg = alpm_db_get_pkg (db, package[PK_PACKAGE_ID_NAME]);
	} else {
		pkg = NULL;
	}

	if (pkg != NULL) {
		const gchar *version = alpm_pkg_get_version (pkg);
		if (g_strcmp0 (version, package[PK_PACKAGE_ID_VERSION]) != 0) {
			pkg = NULL;
		}
	}

	if (pkg == NULL) {
		int code = ALPM_ERR_PKG_NOT_FOUND;
		g_set_error (error, ALPM_ERROR, code, "%s: %s", package_id,
			     alpm_strerror (code));
	}
	g_strfreev (package);
	return pkg;
}

static gboolean
pk_backend_resolve_package (PkBackend *self, const gchar *package,
			    GError **error)
{
	pmpkg_t *pkg;
	
	PkBitfield filters;
	gboolean skip_local, skip_remote;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (localdb != NULL, FALSE);

	pkg = pk_backend_find_pkg (self, package, error);
	if (pkg == NULL) {
		return FALSE;
	}

	filters = pk_backend_get_uint (self, "filters");
	skip_local = pk_bitfield_contain (filters,
					  PK_FILTER_ENUM_NOT_INSTALLED);
	skip_remote = pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED);

	if (alpm_pkg_get_db (pkg) == localdb) {
		if (!skip_local) {
			pk_backend_pkg (self, pkg, PK_INFO_ENUM_INSTALLED);
		}
	} else {
		if (!skip_remote) {
			pk_backend_pkg (self, pkg, PK_INFO_ENUM_AVAILABLE);
		}
	}

	return TRUE;
}

static gboolean
pk_backend_resolve_name (PkBackend *self, const gchar *name, GError **error)
{
	pmpkg_t *pkg;
	int code;
	
	PkBitfield filters;
	gboolean skip_local, skip_remote;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (alpm != NULL, FALSE);
	g_return_val_if_fail (localdb != NULL, FALSE);

	filters = pk_backend_get_uint (self, "filters");
	skip_local = pk_bitfield_contain (filters,
					  PK_FILTER_ENUM_NOT_INSTALLED);
	skip_remote = pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED);

	pkg = alpm_db_get_pkg (localdb, name);
	if (pkg != NULL) {
		if (!skip_local) {
			pk_backend_pkg (self, pkg, PK_INFO_ENUM_INSTALLED);
			return TRUE;
		}
	} else if (!skip_remote) {
		const alpm_list_t *i = alpm_option_get_syncdbs (alpm);
		for (; i != NULL; i = i->next) {
			pkg = alpm_db_get_pkg (i->data, name);
			if (pkg != NULL) {
				pk_backend_pkg (self, pkg,
						PK_INFO_ENUM_AVAILABLE);
				return TRUE;
			}
		}
	}

	code = ALPM_ERR_PKG_NOT_FOUND;
	g_set_error (error, ALPM_ERROR, code, "%s: %s", name,
		     alpm_strerror (code));
	return FALSE;
}

static gboolean
pk_backend_resolve_thread (PkBackend *self)
{
	gchar **packages;
	GError *error = NULL;

	g_return_val_if_fail (self != NULL, FALSE);

	packages = pk_backend_get_strv (self, "package_ids");

	g_return_val_if_fail (packages != NULL, FALSE);

	for (; *packages != NULL; ++packages) {
		if (pk_backend_cancelled (self)) {
			break;
		}

		/* find a package with the given id or name */
		if (pk_package_id_check (*packages)) {
			if (!pk_backend_resolve_package (self, *packages,
							 &error)) {
				break;
			}
		} else {
			if (!pk_backend_resolve_name (self, *packages,
						      &error)) {
				break;
			}
		}
	}

	return pk_backend_finish (self, error);
}

void
pk_backend_resolve (PkBackend *self, PkBitfield filters, gchar **package_ids)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (package_ids != NULL);

	pk_backend_run (self, PK_STATUS_ENUM_QUERY, pk_backend_resolve_thread);
}

static gboolean
pk_backend_get_details_thread (PkBackend *self)
{
	gchar **packages;
	GError *error = NULL;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (localdb != NULL, FALSE);

	packages = pk_backend_get_strv (self, "package_ids");

	g_return_val_if_fail (packages != NULL, FALSE);

	for (; *packages != NULL; ++packages) {
		pmpkg_t *pkg;
		const alpm_list_t *i;

		GString *licenses;
		PkGroupEnum group;
		const gchar *desc, *url;
		gulong size;

		if (pk_backend_cancelled (self)) {
			break;
		}

		pkg = pk_backend_find_pkg (self, *packages, &error);
		if (pkg == NULL) {
			break;
		}

		licenses = g_string_new ("");
		i = alpm_pkg_get_licenses (pkg);
		for (; i != NULL; i = i->next) {
			/* assume OR although it may not be correct */
			g_string_append_printf (licenses, " or %s",
						(const gchar *) i->data);
		}
		if (licenses->len == 0) {
			g_string_append (licenses, " or Unknown");
		}

		group = pk_group_enum_from_string (alpm_pkg_get_group (pkg));
		desc = alpm_pkg_get_desc (pkg);
		url = alpm_pkg_get_url (pkg);

		if (alpm_pkg_get_db (pkg) == localdb) {
			size = alpm_pkg_get_isize (pkg);
		} else {
			size = alpm_pkg_download_size (pkg);
		}

		pk_backend_details (self, *packages, licenses->str + 4, group,
				    desc, url, size);
		g_string_free (licenses, TRUE);
	}

	return pk_backend_finish (self, error);
}

void
pk_backend_get_details (PkBackend *self, gchar **package_ids)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (package_ids != NULL);

	pk_backend_run (self, PK_STATUS_ENUM_QUERY,
			pk_backend_get_details_thread);
}

static gboolean
pk_backend_get_files_thread (PkBackend *self)
{
	gchar **packages;
	GError *error = NULL;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (alpm != NULL, FALSE);

	packages = pk_backend_get_strv (self, "package_ids");

	g_return_val_if_fail (packages != NULL, FALSE);

	for (; *packages != NULL; ++packages) {
		pmpkg_t *pkg;
		const gchar *root;

		GString *files;
		alpm_filelist_t *filelist;
		gsize i;

		if (pk_backend_cancelled (self)) {
			break;
		}

		pkg = pk_backend_find_pkg (self, *packages, &error);
		if (pkg == NULL) {
			break;
		}

		root = alpm_option_get_root (alpm);
		files = g_string_new ("");

		filelist = alpm_pkg_get_files (pkg);
		for (i = 0; i < filelist->count; ++i) {
			const gchar *file = filelist->files[i].name;
			g_string_append_printf (files, ";%s%s", root, file);
		}

		pk_backend_files (self, *packages, files->str + 1);
		g_string_free (files, TRUE);
	}

	return pk_backend_finish (self, error);
}

void
pk_backend_get_files (PkBackend *self, gchar **package_ids)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (package_ids != NULL);

	pk_backend_run (self, PK_STATUS_ENUM_QUERY,
			pk_backend_get_files_thread);
}
