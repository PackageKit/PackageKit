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
pk_backend_transaction_remove_targets (PkBackend *self, GError **error)
{
	gchar **packages;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (alpm != NULL, FALSE);
	g_return_val_if_fail (localdb != NULL, FALSE);

	packages = pk_backend_get_strv (self, "package_ids");

	g_return_val_if_fail (packages != NULL, FALSE);

	for (; *packages != NULL; ++packages) {
		gchar **package = pk_package_id_split (*packages);
		gchar *name = package[PK_PACKAGE_ID_NAME];

		pmpkg_t *pkg = alpm_db_get_pkg (localdb, name);
		if (pkg == NULL || alpm_remove_pkg (alpm, pkg) < 0) {
			enum _alpm_errno_t errno = alpm_errno (alpm);
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
pk_backend_transaction_remove_simulate (PkBackend *self, GError **error)
{
	const alpm_list_t *i;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (alpm != NULL, FALSE);

	if (!pk_backend_transaction_simulate (self, error)) {
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

static gboolean
pk_backend_simulate_remove_packages_thread (PkBackend *self)
{
	pmtransflag_t flags = ALPM_TRANS_FLAG_CASCADE;
	GError *error = NULL;

	g_return_val_if_fail (self != NULL, FALSE);

	/* remove unneeded packages that were required by those to be removed */
	if (pk_backend_get_bool (self, "autoremove")) {
		flags |= ALPM_TRANS_FLAG_RECURSE;
	}

	if (pk_backend_transaction_initialize (self, flags, &error) &&
	    pk_backend_transaction_remove_targets (self, &error) &&
	    pk_backend_transaction_remove_simulate (self, &error)) {
		pk_backend_transaction_packages (self);
	}

	return pk_backend_transaction_finish (self, error);
}

static gboolean
pk_backend_remove_packages_thread (PkBackend *self)
{
	pmtransflag_t flags = 0;
	GError *error = NULL;

	g_return_val_if_fail (self != NULL, FALSE);

	/* remove packages that depend on those to be removed */
	if (pk_backend_get_bool (self, "allow_deps")) {
		flags |= ALPM_TRANS_FLAG_CASCADE;
	}
	/* remove unneeded packages that were required by those to be removed */
	if (pk_backend_get_bool (self, "autoremove")) {
		flags |= ALPM_TRANS_FLAG_RECURSE;
	}

	if (pk_backend_transaction_initialize (self, flags, &error) &&
	    pk_backend_transaction_remove_targets (self, &error) &&
	    pk_backend_transaction_remove_simulate (self, &error)) {
		pk_backend_transaction_commit (self, &error);
	}

	return pk_backend_transaction_finish (self, error);
}

void
pk_backend_simulate_remove_packages (PkBackend *self, gchar **package_ids,
				     gboolean autoremove)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (package_ids != NULL);

	pk_backend_run (self, PK_STATUS_ENUM_SETUP,
			pk_backend_simulate_remove_packages_thread);
}

void
pk_backend_remove_packages (PkBackend *self, gchar **package_ids,
			    gboolean allow_deps, gboolean autoremove)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (package_ids != NULL);

	pk_backend_run (self, PK_STATUS_ENUM_SETUP,
			pk_backend_remove_packages_thread);
}
