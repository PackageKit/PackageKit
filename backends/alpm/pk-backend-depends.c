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
#include "pk-backend-depends.h"
#include "pk-backend-error.h"
#include "pk-backend-packages.h"

static pmpkg_t *
alpm_list_find_pkg (const alpm_list_t *pkgs, const gchar *name)
{
	g_return_val_if_fail (name != NULL, NULL);

	for (; pkgs != NULL; pkgs = pkgs->next) {
		if (g_strcmp0 (name, alpm_pkg_get_name (pkgs->data)) == 0) {
			return pkgs->data;
		}
	}

	return NULL;
}

static alpm_list_t *
pk_backend_find_provider (PkBackend *self, alpm_list_t *pkgs,
			  const gchar *depend, GError **error)
{
	PkBitfield filters;
	gboolean recursive, skip_local, skip_remote;

	pmpkg_t *provider;
	alpm_list_t *pkgcache, *syncdbs;

	g_return_val_if_fail (self != NULL, pkgs);
	g_return_val_if_fail (depend != NULL, pkgs);
	g_return_val_if_fail (localdb != NULL, pkgs);

	recursive = pk_backend_get_bool (self, "recursive");
	filters = pk_backend_get_uint (self, "filters");
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
			pk_backend_pkg (self, provider, PK_INFO_ENUM_INSTALLED);
			/* assume later dependencies will also be local */
			if (recursive) {
				pkgs = alpm_list_add (pkgs, provider);
			}
		}

		return pkgs;
	}

	/* look for remote dependencies */
	syncdbs = alpm_option_get_syncdbs ();
	provider = alpm_find_dbs_satisfier (syncdbs, depend);

	if (provider != NULL) {
		if (!skip_remote) {
			pk_backend_pkg (self, provider, PK_INFO_ENUM_AVAILABLE);
		}
		/* keep looking for local dependencies */
		if (recursive) {
			pkgs = alpm_list_add (pkgs, provider);
		}
	} else {
		int code = PM_ERR_UNSATISFIED_DEPS;
		g_set_error (error, ALPM_ERROR, code, "%s: %s", depend,
			     alpm_strerror (code));
	}

	return pkgs;
}

static alpm_list_t *
pk_backend_find_requirer (PkBackend *self, alpm_list_t *pkgs, const gchar *name,
			  GError **error)
{
	pmpkg_t *requirer;

	g_return_val_if_fail (self != NULL, pkgs);
	g_return_val_if_fail (name != NULL, pkgs);
	g_return_val_if_fail (localdb != NULL, pkgs);

	if (alpm_list_find_pkg (pkgs, name) != NULL) {
		return pkgs;
	}

	/* look for local requirers */
	requirer = alpm_db_get_pkg (localdb, name);

	if (requirer != NULL) {
		pk_backend_pkg (self, requirer, PK_INFO_ENUM_INSTALLED);
		if (pk_backend_get_bool (self, "recursive")) {
			pkgs = alpm_list_add (pkgs, requirer);
		}
	} else {
		int code = PM_ERR_PKG_NOT_FOUND;
		g_set_error (error, ALPM_ERROR, code, "%s: %s", name,
			     alpm_strerror (code));
	}

	return pkgs;
}

static gboolean
pk_backend_get_depends_thread (PkBackend *self)
{
	gchar **packages;
	alpm_list_t *i, *pkgs = NULL;
	GError *error = NULL;

	g_return_val_if_fail (self != NULL, FALSE);

	packages = pk_backend_get_strv (self, "package_ids");

	g_return_val_if_fail (packages != NULL, FALSE);

	/* construct an initial package list */
	for (; *packages != NULL; ++packages) {
		pmpkg_t *pkg;

		if (pk_backend_cancelled (self)) {
			break;
		}

		pkg = pk_backend_find_pkg (self, *packages, &error);
		if (pkg == NULL) {
			break;
		}

		pkgs = alpm_list_add (pkgs, pkg);
	}

	/* package list might be modified along the way but that is ok */
	for (i = pkgs; i != NULL; i = i->next) {
		const alpm_list_t *depends;

		if (pk_backend_cancelled (self) || error != NULL) {
			break;
		}

		depends = alpm_pkg_get_depends (i->data);
		for (; depends != NULL; depends = depends->next) {
			gchar *depend;

			if (pk_backend_cancelled (self) || error != NULL) {
				break;
			}

			depend = alpm_dep_compute_string (depends->data);
			pkgs = pk_backend_find_provider (self, pkgs, depend,
							 &error);
			g_free (depend);
		}
	}

	alpm_list_free (pkgs);
	return pk_backend_finish (self, NULL);
}

static gboolean
pk_backend_get_requires_thread (PkBackend *self)
{
	gchar **packages;
	alpm_list_t *i, *pkgs = NULL;
	GError *error = NULL;

	g_return_val_if_fail (self != NULL, FALSE);

	packages = pk_backend_get_strv (self, "package_ids");

	g_return_val_if_fail (packages != NULL, FALSE);

	/* construct an initial package list */
	for (; *packages != NULL; ++packages) {
		pmpkg_t *pkg;

		if (pk_backend_cancelled (self)) {
			break;
		}

		pkg = pk_backend_find_pkg (self, *packages, &error);
		if (pkg == NULL) {
			break;
		}

		pkgs = alpm_list_add (pkgs, pkg);
	}

	/* package list might be modified along the way but that is ok */
	for (i = pkgs; i != NULL; i = i->next) {
		alpm_list_t *requiredby;

		if (pk_backend_cancelled (self) || error != NULL) {
			break;
		}

		requiredby = alpm_pkg_compute_requiredby (i->data);
		for (; requiredby != NULL; requiredby = requiredby->next) {
			if (pk_backend_cancelled (self) || error != NULL) {
				break;
			}

			pkgs = pk_backend_find_requirer (self, pkgs,
							 requiredby->data,
							 &error);
		}

		FREELIST (requiredby);
	}

	alpm_list_free (pkgs);
	return pk_backend_finish (self, error);
}

void
pk_backend_get_depends (PkBackend *self, PkBitfield filters,
			gchar **package_ids, gboolean recursive)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (package_ids != NULL);

	pk_backend_run (self, PK_STATUS_ENUM_QUERY,
			pk_backend_get_depends_thread);
}

void
pk_backend_get_requires (PkBackend *self, PkBitfield filters,
			 gchar **package_ids, gboolean recursive)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (package_ids != NULL);

	pk_backend_run (self, PK_STATUS_ENUM_QUERY,
			pk_backend_get_requires_thread);
}
