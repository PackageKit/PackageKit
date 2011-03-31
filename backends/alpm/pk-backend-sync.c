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
#include "pk-backend-error.h"
#include "pk-backend-sync.h"
#include "pk-backend-transaction.h"

static gint
alpm_add_dbtarget (const gchar *repo, const gchar *name)
{
	const alpm_list_t *i;
	pmpkg_t *pkg;

	g_return_val_if_fail (repo != NULL, -1);
	g_return_val_if_fail (name != NULL, -1);

	for (i = alpm_option_get_syncdbs (); i != NULL; i = i->next) {
		if (g_strcmp0 (alpm_db_get_name (i->data), repo) == 0) {
			break;
		}
	}

	if (i == NULL) {
		pm_errno = PM_ERR_DB_NOT_FOUND;
		return -1;
	}

	pkg = alpm_db_get_pkg (i->data, name);
	if (pkg == NULL) {
		pm_errno = PM_ERR_PKG_NOT_FOUND;
		return -1;
	}

	return alpm_add_pkg (pkg);
}

static gboolean
pk_backend_transaction_sync_targets (PkBackend *self, GError **error)
{
	gchar **packages;

	g_return_val_if_fail (self != NULL, FALSE);

	packages = pk_backend_get_strv (self, "package_ids");

	g_return_val_if_fail (packages != NULL, FALSE);

	for (; *packages != NULL; ++packages) {
		gchar **package = pk_package_id_split (*packages);
		gchar *repo = package[PK_PACKAGE_ID_DATA];
		gchar *name = package[PK_PACKAGE_ID_NAME];

		if (alpm_add_dbtarget (repo, name) < 0) {
			g_set_error (error, ALPM_ERROR, pm_errno, "%s/%s: %s",
				     repo, name, alpm_strerrorlast ());
			g_strfreev (package);
			return FALSE;
		}

		g_strfreev (package);
	}

	return TRUE;
}

static gboolean
pk_backend_download_packages_thread (PkBackend *self)
{
	alpm_list_t *cachedirs;
	const gchar *directory;
	pmtransflag_t flags = 0;
	GError *error = NULL;

	g_return_val_if_fail (self != NULL, FALSE);

	directory = pk_backend_get_string (self, "directory");

	if (directory != NULL) {
		/* download files to a PackageKit directory */
		cachedirs = alpm_list_strdup (alpm_option_get_cachedirs ());
		directory = strdup (directory);
		alpm_option_set_cachedirs (alpm_list_add (NULL, directory));
	}

	flags |= PM_TRANS_FLAG_NODEPS;
	flags |= PM_TRANS_FLAG_NOCONFLICTS;
	flags |= PM_TRANS_FLAG_DOWNLOADONLY;

	if (pk_backend_transaction_initialize (self, flags, &error) &&
	    pk_backend_transaction_sync_targets (self, &error) &&
	    pk_backend_transaction_simulate (self, &error)) {
		pk_backend_transaction_commit (self, &error);
	}

	if (directory != NULL) {
		alpm_option_set_cachedirs (cachedirs);
	}

	return pk_backend_transaction_finish (self, error);
}

void
pk_backend_download_packages (PkBackend *self, gchar **package_ids,
			      const gchar *directory)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (package_ids != NULL);
	g_return_if_fail (directory != NULL);

	pk_backend_run (self, PK_STATUS_ENUM_SETUP,
			pk_backend_download_packages_thread);
}

static gboolean
pk_backend_simulate_install_packages_thread (PkBackend *self)
{
	GError *error = NULL;

	g_return_val_if_fail (self != NULL, FALSE);

	if (pk_backend_transaction_initialize (self, 0, &error) &&
	    pk_backend_transaction_sync_targets (self, &error) &&
	    pk_backend_transaction_simulate (self, &error)) {
		pk_backend_transaction_packages (self);
	}

	return pk_backend_transaction_finish (self, error);
}

static gboolean
pk_backend_install_packages_thread (PkBackend *self)
{
	GError *error = NULL;

	g_return_val_if_fail (self != NULL, FALSE);

	if (pk_backend_transaction_initialize (self, 0, &error) &&
	    pk_backend_transaction_sync_targets (self, &error) &&
	    pk_backend_transaction_simulate (self, &error)) {
		pk_backend_transaction_commit (self, &error);
	}

	return pk_backend_transaction_finish (self, error);
}

void
pk_backend_simulate_install_packages (PkBackend *self, gchar **package_ids)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (package_ids != NULL);

	pk_backend_run (self, PK_STATUS_ENUM_SETUP,
			pk_backend_simulate_install_packages_thread);
}

void
pk_backend_install_packages (PkBackend *self, gboolean only_trusted,
			     gchar **package_ids)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (package_ids != NULL);

	pk_backend_run (self, PK_STATUS_ENUM_SETUP,
			pk_backend_install_packages_thread);
}

static gboolean
pk_backend_replaces_dependencies (PkBackend *self, pmpkg_t *pkg)
{
	const alpm_list_t *i, *replaces;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (pkg != NULL, FALSE);

	replaces = alpm_pkg_get_replaces (pkg);
	for (i = alpm_trans_get_remove (); i != NULL; i = i->next) {
		pmpkg_t *rpkg = (pmpkg_t *) i->data;
		const gchar *rname = alpm_pkg_get_name (rpkg);

		if (pk_backend_cancelled (self)) {
			return FALSE;
		} else if (alpm_list_find_str (replaces, rname) == NULL) {
			continue;
		}

		if (alpm_pkg_get_reason (rpkg) == PM_PKG_REASON_EXPLICIT) {
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
pk_backend_update_packages_thread (PkBackend *self)
{
	const alpm_list_t *i;
	alpm_list_t *asdeps = NULL;
	GError *error = NULL;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (localdb != NULL, FALSE);

	if (!pk_backend_transaction_initialize (self, 0, &error) ||
	    !pk_backend_transaction_sync_targets (self, &error) ||
	    !pk_backend_transaction_simulate (self, &error)) {
		goto out;
	}

	/* change the install reason of packages that replace dependencies */
	for (i = alpm_trans_get_add (); i != NULL; i = i->next) {
		pmpkg_t *pkg = (pmpkg_t *) i->data;
		const gchar *name = alpm_pkg_get_name (pkg);

		if (pk_backend_cancelled (self)) {
			goto out;
		} else if (alpm_db_get_pkg (localdb, name) != NULL) {
			continue;
		}

		if (pk_backend_replaces_dependencies (self, pkg)) {
			asdeps = alpm_list_add (asdeps, g_strdup (name));
		}
	}

	if (!pk_backend_transaction_commit (self, &error)) {
		goto out;
	}

	for (i = asdeps; i != NULL; i = i->next) {
		const gchar *name = (const gchar *) i->data;
		alpm_db_set_pkgreason (localdb, name, PM_PKG_REASON_DEPEND);
	}

out:
	alpm_list_free_inner (asdeps, g_free);
	alpm_list_free (asdeps);

	return pk_backend_transaction_finish (self, error);
}

void
pk_backend_simulate_update_packages (PkBackend *self, gchar **package_ids)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (package_ids != NULL);

	pk_backend_run (self, PK_STATUS_ENUM_SETUP,
			pk_backend_simulate_install_packages_thread);
}

void
pk_backend_update_packages (PkBackend *self, gboolean only_trusted,
			    gchar **package_ids)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (package_ids != NULL);

	pk_backend_run (self, PK_STATUS_ENUM_SETUP,
			pk_backend_update_packages_thread);
}
