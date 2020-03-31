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
#include "pk-alpm-databases.h"
#include "pk-alpm-error.h"
#include "pk-alpm-transaction.h"

static gboolean
pk_alpm_transaction_sync_targets (PkBackendJob *job, const gchar **packages, gboolean update, GError **error)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);

	g_return_val_if_fail (packages != NULL, FALSE);

	for (; *packages != NULL; ++packages) {
		g_auto(GStrv) package = pk_package_id_split (*packages);
		gchar *repo = package[PK_PACKAGE_ID_DATA];
		gchar *name = package[PK_PACKAGE_ID_NAME];

		const alpm_list_t *i = alpm_get_syncdbs (priv->alpm);
		alpm_pkg_t *pkg;

		for (; i != NULL; i = i->next) {
			if (g_strcmp0 (alpm_db_get_name (i->data), repo) == 0)
				break;
		}

		if (i == NULL) {
			alpm_errno_t errno = ALPM_ERR_DB_NOT_FOUND;
			g_set_error (error, PK_ALPM_ERROR, errno, "%s/%s: %s",
				     repo, name, alpm_strerror (errno));
			return FALSE;
		}

		pkg = alpm_db_get_pkg (i->data, name);

		if (update) { // libalpm only checks for ignorepkgs on an update
			const alpm_list_t *ignorepkgs, *ignoregroups, *group_iter;

			ignorepkgs = alpm_option_get_ignorepkgs (priv->alpm);
			if (alpm_list_find_str (ignorepkgs, alpm_pkg_get_name (pkg)) != NULL)
				goto cont;

			ignoregroups = alpm_option_get_ignoregroups (priv->alpm);
			for (group_iter = alpm_pkg_get_groups (pkg); group_iter != NULL; group_iter = group_iter->next) {
				if (alpm_list_find_str (ignoregroups, i->data) != NULL)
					pk_alpm_pkg_emit (job, pkg, PK_INFO_ENUM_BLOCKED); goto cont;
			}
		}

		if (pkg == NULL || alpm_add_pkg (priv->alpm, pkg) < 0) {
			alpm_errno_t errno = alpm_errno (priv->alpm);
			g_set_error (error, PK_ALPM_ERROR, errno, "%s/%s: %s",
				     repo, name, alpm_strerror (errno));
			return FALSE;
		}

cont:
		continue;
	}

	return TRUE;
}

static void
pk_backend_download_packages_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	alpm_list_t *cachedirs = NULL;
	const gchar *directory;
	const gchar **package_ids;
	alpm_transflag_t flags = 0;
	g_autoptr(GError) error = NULL;

	g_variant_get (params, "(^a&ss)",
				  &package_ids,
				  &directory);

	if (directory != NULL) {
		/* download files to a PackageKit directory */
		gchar *cachedir = strdup (directory);
		const alpm_list_t *old = alpm_option_get_cachedirs (priv->alpm);
		alpm_list_t *new = alpm_list_add (NULL, cachedir);

		cachedirs = alpm_list_strdup (old);
		alpm_option_set_cachedirs (priv->alpm, new);
	}

	flags |= ALPM_TRANS_FLAG_NODEPS;
	flags |= ALPM_TRANS_FLAG_NOCONFLICTS;
	flags |= ALPM_TRANS_FLAG_DOWNLOADONLY;

	if (pk_alpm_transaction_initialize (job, flags, directory, &error) &&
	    pk_alpm_transaction_sync_targets (job, package_ids, FALSE, &error) &&
	    pk_alpm_transaction_simulate (job, &error)) {
		pk_alpm_transaction_commit (job, &error);
	}

	if (directory != NULL) {
		g_assert (cachedirs);
		alpm_option_set_cachedirs (priv->alpm, cachedirs);
	}

	pk_alpm_transaction_finish (job, error);
}

void
pk_backend_download_packages (PkBackend *self,
			      PkBackendJob *job,
			      gchar **package_ids,
			      const gchar *directory)
{
	g_return_if_fail (directory != NULL);

	pk_alpm_run (job, PK_STATUS_ENUM_SETUP, pk_backend_download_packages_thread, NULL);
}

static gboolean
pk_alpm_replaces_dependencies (PkBackendJob *job, alpm_pkg_t *pkg)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	const alpm_list_t *i, *replaces;

	g_return_val_if_fail (pkg != NULL, FALSE);

	replaces = alpm_pkg_get_replaces (pkg);
	for (i = alpm_trans_get_remove (priv->alpm); i != NULL; i = i->next) {
		alpm_pkg_t *rpkg = (alpm_pkg_t *) i->data;
		const gchar *rname = alpm_pkg_get_name (rpkg);

		if (pk_backend_job_is_cancelled (job))
			return FALSE;
		if (alpm_list_find_str (replaces, rname) == NULL)
			continue;

		if (alpm_pkg_get_reason (rpkg) == ALPM_PKG_REASON_EXPLICIT)
			return FALSE;
	}

	return TRUE;
}

static void
pk_backend_sync_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	PkBitfield flags;
	gboolean only_trusted;
	const alpm_list_t *i;
	alpm_list_t *asdeps = NULL, *asexplicit = NULL;
	alpm_transflag_t alpm_flags = 0;
	const gchar** package_ids;
	g_autoptr(GError) error = NULL;

	g_variant_get (params, "(t^a&s)", &flags, &package_ids);
	only_trusted = pk_bitfield_contain (flags, PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED);

	if (!only_trusted && !pk_alpm_disable_signatures (backend, &error))
		goto out;

	/* download only */
	if (pk_bitfield_contain (flags, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD))
		alpm_flags |= ALPM_TRANS_FLAG_DOWNLOADONLY;

	if (pk_alpm_transaction_initialize (job, alpm_flags, NULL, &error) &&
	    pk_alpm_transaction_sync_targets (job, package_ids, (gboolean)p, &error) &&
	    pk_alpm_transaction_simulate (job, &error)) {

		if (pk_bitfield_contain (flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) { /* simulation */
			pk_alpm_transaction_packages (job);
		}
		else {/* real installation */
			/* change the install reason of packages that replace dependencies */
			for (i = alpm_trans_get_add (priv->alpm); i != NULL; i = i->next) {
				alpm_pkg_t *pkg = (alpm_pkg_t *) i->data;
				const gchar *name = alpm_pkg_get_name (pkg);

				if (pk_backend_job_is_cancelled (job))
					goto out;
				if (alpm_db_get_pkg (priv->localdb, name) != NULL)
					continue;

				if (pk_alpm_replaces_dependencies (job, pkg))
					asdeps = alpm_list_add (asdeps, g_strdup (name));

				if (alpm_pkg_get_reason (pkg) == ALPM_PKG_REASON_EXPLICIT)
					asexplicit = alpm_list_add (asexplicit, g_strdup (name));
			}

			pk_alpm_transaction_commit (job, &error);

			for (i = asdeps; i != NULL; i = i->next) {
				const gchar *name = (const gchar *) i->data;
				alpm_pkg_t *pkg = alpm_db_get_pkg (priv->localdb, name);
				alpm_pkg_set_reason (pkg, ALPM_PKG_REASON_DEPEND);
			}

			for (i = asexplicit; i != NULL; i = i->next) {
				const gchar *name = (const gchar *) i->data;
				alpm_pkg_t *pkg = alpm_db_get_pkg (priv->localdb, name);
				alpm_pkg_set_reason (pkg, ALPM_PKG_REASON_EXPLICIT);
			}
		}
	}


out:
	pk_alpm_transaction_end (job, (error == NULL) ? &error : NULL);

	if (!only_trusted) {
		GError **e = (error == NULL) ? &error : NULL;
		pk_alpm_enable_signatures (backend, e);
	}

	alpm_list_free_inner (asdeps, g_free);
	alpm_list_free (asdeps);
	alpm_list_free_inner (asexplicit, g_free);
	alpm_list_free (asexplicit);

	pk_alpm_finish (job, error);
}

void
pk_backend_update_packages (PkBackend *self,
			    PkBackendJob *job,
			    PkBitfield transaction_flags,
			    gchar **package_ids)
{
	pk_alpm_run (job, PK_STATUS_ENUM_SETUP, pk_backend_sync_thread, TRUE);
}

void
pk_backend_install_packages (PkBackend  *self,
			     PkBackendJob *job,
			     PkBitfield transaction_flags,
			     gchar **package_ids)
{
	pk_alpm_run (job, PK_STATUS_ENUM_SETUP, pk_backend_sync_thread, FALSE);
}
