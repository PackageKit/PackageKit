/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
 * Copyright (C) 2008, 2009 Valeriy Lyasotskiy <onestep@ukr.net>
 * Copyright (C) 2010 Jonathan Conder <j@skurvy.no-ip.org>
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

#include <pacman.h>
#include "backend-error.h"
#include "backend-packages.h"
#include "backend-pacman.h"
#include "backend-transaction.h"
#include "backend-remove.h"

static PacmanList *
backend_remove_list_targets (PkBackend *backend)
{
	gchar **package_ids;
	guint iterator;
	PacmanList *list = NULL;

	g_return_val_if_fail (backend != NULL, NULL);

	package_ids = pk_backend_get_strv (backend, "package_ids");

	g_return_val_if_fail (package_ids != NULL, NULL);

	for (iterator = 0; package_ids[iterator] != NULL; ++iterator) {
		gchar **package_id_data = pk_package_id_split (package_ids[iterator]);
		list = pacman_list_add (list, g_strdup (package_id_data[PK_PACKAGE_ID_NAME]));
		g_strfreev (package_id_data);
	}

	return list;
}

static gboolean
backend_remove_packages_thread (PkBackend *backend)
{
	PacmanList *list;
	gboolean allow_deps;
	gboolean autoremove;

	PacmanTransaction *transaction = NULL;
	PacmanTransactionFlags flags = PACMAN_TRANSACTION_FLAGS_NONE;

	g_return_val_if_fail (backend != NULL, FALSE);

	allow_deps = pk_backend_get_bool (backend, "allow_deps");
	autoremove = pk_backend_get_bool (backend, "autoremove");

	/* remove packages that depend on those to be removed */
	if (allow_deps) {
		flags |= PACMAN_TRANSACTION_FLAGS_REMOVE_CASCADE;
	}
	/* remove unneeded packages that were required by those to be removed */
	if (autoremove) {
		flags |= PACMAN_TRANSACTION_FLAGS_REMOVE_RECURSIVE;
	}

	/* run the transaction */
	list = backend_remove_list_targets (backend);
	if (list != NULL) {
		transaction = backend_transaction_run (backend, PACMAN_TRANSACTION_REMOVE, flags, list);
		pacman_list_free_full (list, g_free);
	}

	return backend_transaction_finished (backend, transaction);
}

/**
 * backend_remove_packages:
 **/
void
backend_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (package_ids != NULL);

	backend_run (backend, PK_STATUS_ENUM_SETUP, backend_remove_packages_thread);
}

static gboolean
backend_simulate_remove_packages_thread (PkBackend *backend)
{
	PacmanList *list;
	gboolean autoremove;

	PacmanTransaction *transaction = NULL;
	PacmanTransactionFlags flags = PACMAN_TRANSACTION_FLAGS_REMOVE_CASCADE;

	g_return_val_if_fail (backend != NULL, FALSE);

	autoremove = pk_backend_get_bool (backend, "autoremove");

	/* remove unneeded packages that were required by those to be removed */
	if (autoremove) {
		flags |= PACMAN_TRANSACTION_FLAGS_REMOVE_RECURSIVE;
	}

	/* prepare the transaction */
	list = backend_remove_list_targets (backend);
	if (list != NULL) {
		transaction = backend_transaction_simulate (backend, PACMAN_TRANSACTION_REMOVE, flags, list);
		pacman_list_free_full (list, g_free);

		if (transaction != NULL) {
			/* emit packages that would have been installed or removed */
			backend_transaction_packages (backend, transaction);
		}
	}

	return backend_transaction_finished (backend, transaction);
}

/**
 * backend_simulate_remove_packages:
 **/
void
backend_simulate_remove_packages (PkBackend *backend, gchar **package_ids, gboolean autoremove)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (package_ids != NULL);

	backend_run (backend, PK_STATUS_ENUM_SETUP, backend_simulate_remove_packages_thread);
}
