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
#include "pk-backend-error.h"
#include "pk-backend-sync.h"
#include "pk-backend-transaction.h"

static gboolean
pk_backend_transaction_sync_targets (PkBackendJob *job, const gchar **packages, GError **error)
{
	g_return_val_if_fail (job != NULL, FALSE);
	g_return_val_if_fail (alpm != NULL, FALSE);
	g_return_val_if_fail (packages != NULL, FALSE);

	for (; *packages != NULL; ++packages) {
		gchar **package = pk_package_id_split (*packages);
		gchar *repo = package[PK_PACKAGE_ID_DATA];
		gchar *name = package[PK_PACKAGE_ID_NAME];

		const alpm_list_t *i = alpm_get_syncdbs (alpm);
		alpm_pkg_t *pkg;

		for (; i != NULL; i = i->next) {
			if (g_strcmp0 (alpm_db_get_name (i->data), repo) == 0) {
				break;
			}
		}

		if (i == NULL) {
			alpm_errno_t errno = ALPM_ERR_DB_NOT_FOUND;
			g_set_error (error, ALPM_ERROR, errno, "%s/%s: %s",
				     repo, name, alpm_strerror (errno));
			g_strfreev (package);
			return FALSE;
		}

		pkg = alpm_db_get_pkg (i->data, name);
		if (pkg == NULL || alpm_add_pkg (alpm, pkg) < 0) {
			alpm_errno_t errno = alpm_errno (alpm);
			g_set_error (error, ALPM_ERROR, errno, "%s/%s: %s",
				     repo, name, alpm_strerror (errno));
			g_strfreev (package);
			return FALSE;
		}

		g_strfreev (package);
	}

	return TRUE;
}

static void
pk_backend_download_packages_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	alpm_list_t *cachedirs = NULL;
	const gchar *directory;
	const gchar **package_ids;
	alpm_transflag_t flags = 0;
	GError *error = NULL;

	pkalpm_end_job_if_fail (job != NULL, job);
	pkalpm_end_job_if_fail (alpm != NULL, job);

	g_variant_get(params, "(^a&ss)",
				  &package_ids,
				  &directory);

	if (directory != NULL) {
		/* download files to a PackageKit directory */
		gchar *cachedir = strdup (directory);
		const alpm_list_t *old = alpm_option_get_cachedirs (alpm);
		alpm_list_t *new = alpm_list_add (NULL, cachedir);

		cachedirs = alpm_list_strdup (old);
		alpm_option_set_cachedirs (alpm, new);
	}

	flags |= ALPM_TRANS_FLAG_NODEPS;
	flags |= ALPM_TRANS_FLAG_NOCONFLICTS;
	flags |= ALPM_TRANS_FLAG_DOWNLOADONLY;

	if (pk_backend_transaction_initialize (job, flags, directory, &error) &&
	    pk_backend_transaction_sync_targets (job, package_ids, &error) &&
	    pk_backend_transaction_simulate (&error)) {
		pk_backend_transaction_commit (job, &error);
	}

	if (directory != NULL) {
		g_assert(cachedirs);
		alpm_option_set_cachedirs (alpm, cachedirs);
	}

	pk_backend_transaction_finish (job, error);
}

void
pk_backend_download_packages (PkBackend *self,
							  PkBackendJob   *job,
							  gchar      **package_ids,
							  const gchar    *directory)
{
	g_return_if_fail (job != NULL);
	g_return_if_fail (package_ids != NULL);
	g_return_if_fail (directory != NULL);

	pkalpm_backend_run (job, PK_STATUS_ENUM_SETUP,
			pk_backend_download_packages_thread, NULL);
}

static void
pk_backend_install_packages_thread (PkBackendJob *job, GVariant* params, gpointer p)
{
	gboolean only_trusted;
	PkBitfield transaction_flags = 0;
	const gchar **package_ids;
	GError *error = NULL;

	pkalpm_end_job_if_fail (job != NULL, job);

	g_variant_get(params, "(t^a&s)",
				  &transaction_flags,
				  &package_ids);
	only_trusted = transaction_flags & PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED;

	if (!only_trusted && !pkalpm_backend_disable_signatures (&error)) {
		goto out;
	}

	if (pk_backend_transaction_initialize (job, 0, NULL, &error) &&
		pk_backend_transaction_sync_targets (job, package_ids, &error) &&
	    pk_backend_transaction_simulate (&error)) {
		pk_backend_transaction_commit (job, &error);
	}

	pk_backend_transaction_end (job, (error == NULL) ? &error : NULL);
out:
	if (!only_trusted) {
		GError **e = (error == NULL) ? &error : NULL;
		pkalpm_backend_enable_signatures (e);
	}

	pk_backend_finish (job, error);
}

void
pk_backend_install_packages (PkBackend  *self,
                             PkBackendJob   *job,
                             PkBitfield  transaction_flags,
                             gchar      **package_ids)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (package_ids != NULL);

    pkalpm_backend_run (job, PK_STATUS_ENUM_SETUP,
			pk_backend_install_packages_thread, NULL);
}

static gboolean
pk_backend_replaces_dependencies (PkBackendJob *job, alpm_pkg_t *pkg)
{
	const alpm_list_t *i, *replaces;

	pkalpm_end_job_if_fail (job != NULL, job);
	pkalpm_end_job_if_fail (pkg != NULL, job);
	pkalpm_end_job_if_fail (alpm != NULL, job);

	replaces = alpm_pkg_get_replaces (pkg);
	for (i = alpm_trans_get_remove (alpm); i != NULL; i = i->next) {
		alpm_pkg_t *rpkg = (alpm_pkg_t *) i->data;
		const gchar *rname = alpm_pkg_get_name (rpkg);

		if (pk_backend_cancelled (job)) {
			return FALSE;
		} else if (alpm_list_find_str (replaces, rname) == NULL) {
			continue;
		}

		if (alpm_pkg_get_reason (rpkg) == ALPM_PKG_REASON_EXPLICIT) {
			return FALSE;
		}
	}

	return TRUE;
}

static void
pk_backend_update_packages_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	PkBitfield flags;
	gboolean only_trusted;
	const alpm_list_t *i;
	alpm_list_t *asdeps = NULL;
	const gchar** package_ids;
	GError *error = NULL;

	pkalpm_end_job_if_fail (job != NULL, job);
	pkalpm_end_job_if_fail (alpm != NULL, job);
	pkalpm_end_job_if_fail (localdb != NULL, job);

	g_variant_get(params, "(t^a&s)",
				  &flags,
			      &package_ids);
	only_trusted = flags & PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED;

	if (!only_trusted && !pkalpm_backend_disable_signatures (&error)) {
		goto out;
	}

	if (!pk_backend_transaction_initialize (job, 0, NULL, &error) ||
		!pk_backend_transaction_sync_targets (job, package_ids, &error) ||
	    !pk_backend_transaction_simulate (&error)) {
		goto out;
	}

	/* change the install reason of packages that replace dependencies */
	for (i = alpm_trans_get_add (alpm); i != NULL; i = i->next) {
		alpm_pkg_t *pkg = (alpm_pkg_t *) i->data;
		const gchar *name = alpm_pkg_get_name (pkg);

		if (pk_backend_cancelled (job)) {
			goto out;
		} else if (alpm_db_get_pkg (localdb, name) != NULL) {
			continue;
		}

		if (pk_backend_replaces_dependencies (job, pkg)) {
			asdeps = alpm_list_add (asdeps, g_strdup (name));
		}
	}

	if (!pk_backend_transaction_commit (job, &error)) {
		goto out;
	}

	for (i = asdeps; i != NULL; i = i->next) {
		const gchar *name = (const gchar *) i->data;
		alpm_pkg_t *pkg = alpm_db_get_pkg (localdb, name);
		alpm_pkg_set_reason (pkg, ALPM_PKG_REASON_DEPEND);
	}

out:
	pk_backend_transaction_end (job, (error == NULL) ? &error : NULL);

	if (!only_trusted) {
		GError **e = (error == NULL) ? &error : NULL;
		pkalpm_backend_enable_signatures (e);
	}

	alpm_list_free_inner (asdeps, g_free);
	alpm_list_free (asdeps);

	pk_backend_finish (job, error);
}

void
pk_backend_update_packages (PkBackend   *self,
                            PkBackendJob   *job,
                            PkBitfield  transaction_flags,
                            gchar      **package_ids)
{
    g_return_if_fail (self != NULL);
	g_return_if_fail (package_ids != NULL);

    pkalpm_backend_run (job, PK_STATUS_ENUM_SETUP,
			pk_backend_update_packages_thread, NULL);
}
