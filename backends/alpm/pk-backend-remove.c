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
#include "pk-backend-error.h"
#include "pk-backend-remove.h"
#include "pk-backend-transaction.h"

static gboolean
pk_backend_transaction_remove_targets (PkBackendJob *job, gchar** packages, GError **error)
{
	g_return_val_if_fail (job != NULL, FALSE);
	g_return_val_if_fail (alpm != NULL, FALSE);
	g_return_val_if_fail (localdb != NULL, FALSE);
	g_return_val_if_fail (packages != NULL, FALSE);

	for (; *packages != NULL; ++packages) {
		gchar **package = pk_package_id_split (*packages);
		gchar *name = package[PK_PACKAGE_ID_NAME];

		alpm_pkg_t *pkg = alpm_db_get_pkg (localdb, name);
		if (pkg == NULL || alpm_remove_pkg (alpm, pkg) < 0) {
			alpm_errno_t errno = alpm_errno (alpm);
			g_set_error (error, ALPM_ERROR, errno, "%s: %s", name,
				     alpm_strerror (errno));
			g_strfreev (package);
			return FALSE;
		}

		g_strfreev (package);
	}

	return TRUE;
}

static gboolean
pk_backend_transaction_remove_simulate (GError **error)
{
	const alpm_list_t *i;

	g_return_val_if_fail (alpm != NULL, FALSE);

	if (!pk_backend_transaction_simulate (error)) {
		return FALSE;
	}

	for (i = alpm_trans_get_remove (alpm); i != NULL; i = i->next) {
		const gchar *name = alpm_pkg_get_name (i->data);
		if (alpm_list_find_str (holdpkgs, name)) {
			g_set_error (error, ALPM_ERROR, ALPM_ERR_PKG_HELD,
				     "%s: %s", name,
				     "could not remove HoldPkg");
			return FALSE;
		}
	}

	return TRUE;
}

static void
pk_backend_remove_packages_thread (PkBackendJob *job, GVariant* params, gpointer p)
{
	alpm_transflag_t flags = 0;
	GError *error = NULL;
	gboolean allow_deps, autoremove;
	gchar** package_ids;
	PkBitfield  transaction_flags;

	pkalpm_end_job_if_fail (job != NULL, job);
	g_variant_get(params, "(t^a&sbb)",
					&transaction_flags,
					&package_ids,
					&allow_deps,
					&autoremove);

	/* remove packages that depend on those to be removed */
	if (allow_deps) {
		flags |= ALPM_TRANS_FLAG_CASCADE;
	}
	/* remove unneeded packages that were required by those to be removed */
	if (autoremove) {
		flags |= ALPM_TRANS_FLAG_RECURSE;
	}

	if (pk_backend_transaction_initialize (job, flags, NULL, &error) &&
		pk_backend_transaction_remove_targets (job, package_ids, &error) &&
		pk_backend_transaction_remove_simulate (&error)) {
		pk_backend_transaction_commit (job, &error);
	}

	pk_backend_transaction_finish (job, error);
}

void
pk_backend_remove_packages (PkBackend   *self,
                            PkBackendJob   *job,
                            PkBitfield  transaction_flags,
                            gchar      **package_ids,
                            gboolean    allow_deps,
                            gboolean    autoremove)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (package_ids != NULL);

	pkalpm_backend_run (job, PK_STATUS_ENUM_SETUP,
			pk_backend_remove_packages_thread, NULL);
}
