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

#include "pk-backend-alpm.h"
#include "pk-alpm-error.h"
#include "pk-alpm-packages.h"

static alpm_pkg_t *
pk_alpm_list_find_pkgname (const alpm_list_t *pkgs, const gchar *name)
{
	g_return_val_if_fail (name != NULL, NULL);

	for (; pkgs != NULL; pkgs = pkgs->next) {
		if (g_strcmp0 (name, alpm_pkg_get_name (pkgs->data)) == 0)
			return pkgs->data;
	}

	return NULL;
}

static alpm_list_t *
pk_alpm_find_provider (PkBackendJob *job, alpm_list_t *pkgs,
			      const gchar *depend, gboolean recursive,
			      PkBitfield filters, GError **error)
{
	gboolean skip_local, skip_remote;

	alpm_pkg_t *provider;
	alpm_list_t *pkgcache, *syncdbs;

	g_return_val_if_fail (depend != NULL, pkgs);
	g_return_val_if_fail (alpm != NULL, pkgs);
	g_return_val_if_fail (localdb != NULL, pkgs);

	skip_local = pk_bitfield_contain (filters,
					  PK_FILTER_ENUM_NOT_INSTALLED);
	skip_remote = pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED);

	if (alpm_find_satisfier (pkgs, depend) != NULL) {
		return pkgs;
	}

	/* look for local dependencies */
	pkgcache = alpm_db_get_pkgcache (localdb);
	provider = alpm_find_satisfier (pkgcache, depend);

	if (provider != NULL) {
		if (!skip_local) {
			pk_alpm_pkg_emit (job, provider, PK_INFO_ENUM_INSTALLED);
			/* assume later dependencies will also be local */
			if (recursive) {
				pkgs = alpm_list_add (pkgs, provider);
			}
		}

		return pkgs;
	}

	/* look for remote dependencies */
	syncdbs = alpm_get_syncdbs (alpm);
	provider = alpm_find_dbs_satisfier (alpm, syncdbs, depend);

	if (provider != NULL) {
		if (!skip_remote)
			pk_alpm_pkg_emit (job, provider, PK_INFO_ENUM_AVAILABLE);
		/* keep looking for local dependencies */
		if (recursive)
			pkgs = alpm_list_add (pkgs, provider);
	} else {
		int code = ALPM_ERR_UNSATISFIED_DEPS;
		g_set_error (error, PK_ALPM_ERROR, code, "%s: %s", depend,
			     alpm_strerror (code));
	}

	return pkgs;
}

static alpm_list_t *
pk_backend_find_requirer (PkBackendJob *job, alpm_list_t *pkgs, const gchar *name, gboolean recursive,
			  GError **error)
{
	alpm_pkg_t *requirer;

	g_return_val_if_fail (name != NULL, pkgs);
	g_return_val_if_fail (localdb != NULL, pkgs);

	if (pk_alpm_list_find_pkgname (pkgs, name) != NULL)
		return pkgs;

	/* look for local requirers */
	requirer = alpm_db_get_pkg (localdb, name);

	if (requirer != NULL) {
		pk_alpm_pkg_emit (job, requirer, PK_INFO_ENUM_INSTALLED);
		if (recursive)
			pkgs = alpm_list_add (pkgs, requirer);
	} else {
		int code = ALPM_ERR_PKG_NOT_FOUND;
		g_set_error (error, PK_ALPM_ERROR, code, "%s: %s", name,
			     alpm_strerror (code));
	}

	return pkgs;
}

static void
pk_backend_depends_on_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	gchar **packages;
	alpm_list_t *i, *pkgs = NULL;
	_cleanup_error_free_ GError *error = NULL;
	PkBitfield filters;
	gboolean recursive;

	g_variant_get (params, "(t^a&sb)",
		       &filters, &packages, &recursive);

	/* construct an initial package list */
	for (; *packages != NULL; ++packages) {
		alpm_pkg_t *pkg;

		if (pk_alpm_is_backend_cancelled (job))
			break;

		pkg = pk_alpm_find_pkg (job, *packages, &error);
		if (pkg == NULL)
			break;

		pkgs = alpm_list_add (pkgs, pkg);
	}

	/* package list might be modified along the way but that is ok */
	for (i = pkgs; i != NULL; i = i->next) {
		const alpm_list_t *depends;

		if (pk_alpm_is_backend_cancelled (job) || error != NULL)
			break;

		depends = alpm_pkg_get_depends (i->data);
		for (; depends != NULL; depends = depends->next) {
			_cleanup_free_ gchar *depend = NULL;

			if (pk_alpm_is_backend_cancelled (job) || error != NULL)
				break;

			depend = alpm_dep_compute_string (depends->data);
			pkgs = pk_alpm_find_provider (job, pkgs, depend, recursive, filters, &error);
		}
	}

	alpm_list_free (pkgs);
	pk_alpm_finish (job, NULL);
}

static void
pk_backend_required_by_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	gchar **packages;
	alpm_list_t *i, *pkgs = NULL;
	_cleanup_error_free_ GError *error = NULL;
	gboolean recursive;
	PkBitfield filters;

	g_variant_get (params, "(t^a&sb)",
		       &filters, &packages, &recursive);

	/* construct an initial package list */
	for (; *packages != NULL; ++packages) {
		alpm_pkg_t *pkg;

		if (pk_alpm_is_backend_cancelled (job))
			break;

		pkg = pk_alpm_find_pkg (job, *packages, &error);
		if (pkg == NULL)
			break;

		pkgs = alpm_list_add (pkgs, pkg);
	}

	/* package list might be modified along the way but that is ok */
	for (i = pkgs; i != NULL; i = i->next) {
		alpm_list_t *requiredby;

		if (pk_alpm_is_backend_cancelled (job) || error != NULL)
			break;

		requiredby = alpm_pkg_compute_requiredby (i->data);
		for (; requiredby != NULL; requiredby = requiredby->next) {
			if (pk_alpm_is_backend_cancelled (job) || error != NULL)
				break;

			pkgs = pk_backend_find_requirer (job, pkgs,
							 requiredby->data, recursive, &error);
		}

		FREELIST (requiredby);
	}

	alpm_list_free (pkgs);
	pk_alpm_finish (job, error);
}

void
pk_backend_depends_on (PkBackend    *self,
		       PkBackendJob *job,
		       PkBitfield filters,
		       gchar **package_ids,
		       gboolean    recursive)
{
	pk_alpm_run (job, PK_STATUS_ENUM_QUERY, pk_backend_depends_on_thread, NULL);
}

void
pk_backend_required_by (PkBackend *self,
			PkBackendJob *job,
			PkBitfield filters,
			gchar **package_ids,
			gboolean    recursive)
{
	pk_alpm_run (job, PK_STATUS_ENUM_QUERY, pk_backend_required_by_thread, NULL);
}
