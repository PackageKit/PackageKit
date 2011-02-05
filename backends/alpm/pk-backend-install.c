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
#include "pk-backend-install.h"
#include "pk-backend-transaction.h"

static gboolean
pk_backend_transaction_add_targets (PkBackend *self, GError **error)
{
	gchar **paths;

	g_return_val_if_fail (self != NULL, FALSE);

	paths = pk_backend_get_strv (self, "full_paths");

	g_return_val_if_fail (paths != NULL, FALSE);

	for (; *paths != NULL; ++paths) {
		if (alpm_add_target (*paths) < 0) {
			g_set_error (error, ALPM_ERROR, pm_errno, "%s: %s",
				     *paths, alpm_strerrorlast ());
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
pk_backend_simulate_install_files_thread (PkBackend *self)
{
	GError *error = NULL;

	g_return_val_if_fail (self != NULL, FALSE);

	if (pk_backend_transaction_initialize (self, 0, &error) &&
	    pk_backend_transaction_add_targets (self, &error) &&
	    pk_backend_transaction_simulate (self, &error)) {
		pk_backend_transaction_packages (self);
	}

	return pk_backend_transaction_finish (self, error);
}

static gboolean
pk_backend_install_files_thread (PkBackend *self)
{
	GError *error = NULL;

	g_return_val_if_fail (self != NULL, FALSE);

	if (pk_backend_transaction_initialize (self, 0, &error) &&
	    pk_backend_transaction_add_targets (self, &error) &&
	    pk_backend_transaction_simulate (self, &error)) {
		pk_backend_transaction_commit (self, &error);
	}

	return pk_backend_transaction_finish (self, error);
}

void
pk_backend_simulate_install_files (PkBackend *self, gchar **paths)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (paths != NULL);

	pk_backend_run (self, PK_STATUS_ENUM_SETUP,
			pk_backend_simulate_install_files_thread);
}

void
pk_backend_install_files (PkBackend *self, gboolean only_trusted,
			  gchar **full_paths)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (full_paths != NULL);

	pk_backend_run (self, PK_STATUS_ENUM_SETUP,
			pk_backend_install_files_thread);
}
