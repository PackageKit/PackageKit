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

#include <string.h>
#include "backend-error.h"
#include "backend-packages.h"
#include "backend-pacman.h"
#include "backend-repos.h"
#include "backend-transaction.h"

typedef struct {
	guint complete;
	guint total;

	PacmanPackage *package;
	GString *files;
} BackendDownloadData;

static GHashTable *downloads = NULL;

gboolean
backend_initialize_downloads (PkBackend *backend, GError **error)
{
	g_return_val_if_fail (backend != NULL, FALSE);

	downloads = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	return TRUE;
}

void
backend_destroy_downloads (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);

	if (downloads != NULL) {
		g_hash_table_unref (downloads);
	}
}

static void
transaction_download_end (PacmanTransaction *transaction, BackendDownloadData *download, PkBackend *backend) {
	g_return_if_fail (transaction != NULL);
	g_return_if_fail (download != NULL);
	g_return_if_fail (backend != NULL);

	/* emit the finished signal for the old package */
	backend_package (backend, download->package, PK_INFO_ENUM_FINISHED);

	/* emit the list of files downloaded for DownloadPackages */
	if (download->files != NULL) {
		gchar *package_id, *files;

		package_id = pacman_package_make_id (download->package);
		files = g_string_free (download->files, FALSE);

		pk_backend_files (backend, package_id, files);

		g_free (package_id);
		g_free (files);
	}

	download->package = NULL;
	download->files = NULL;
}

static gchar *
backend_filename_make_path (PkBackend *backend, const gchar *filename)
{
	const gchar *directory;

	g_return_val_if_fail (backend != NULL, NULL);
	g_return_val_if_fail (filename != NULL, NULL);

	directory = pk_backend_get_string (backend, "directory");

	g_return_val_if_fail (directory != NULL, NULL);

	return g_build_filename (directory, filename, NULL);
}

static void
transaction_download_start (PacmanTransaction *transaction, BackendDownloadData *download, const gchar *filename, PkBackend *backend)
{
	const PacmanList *packages;

	g_return_if_fail (transaction != NULL);
	g_return_if_fail (download != NULL);
	g_return_if_fail (filename != NULL);
	g_return_if_fail (backend != NULL);

	/* continue or finish downloading the old package */
	if (download->package != NULL) {
		if (pacman_package_has_filename (download->package, filename)) {
			if (download->files != NULL) {
				gchar *path = backend_filename_make_path (backend, filename);
				g_string_append_printf (download->files, ";%s", path);
				g_free (path);
			}
			return;
		} else {
			transaction_download_end (transaction, download, backend);
		}
	}

	/* find a new package for the current file */
	for (packages = pacman_transaction_get_installs (transaction); packages != NULL; packages = pacman_list_next (packages)) {
		PacmanPackage *package = (PacmanPackage *) pacman_list_get (packages);
		if (pacman_package_has_filename (package, filename)) {
			download->package = package;
			break;
		}
	}

	/* emit the downloading signal and start collecting files for the new package */
	if (download->package != NULL) {
		backend_package (backend, download->package, PK_INFO_ENUM_DOWNLOADING);

		/* only emit files downloaded for DownloadPackages */
		if (pk_backend_get_role (backend) == PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
			gchar *path = backend_filename_make_path (backend, filename);
			download->files = g_string_new (path);
			g_free (path);
		}
	}
}

static void
transaction_download_cb (PacmanTransaction *transaction, const gchar *filename, guint complete, guint total, gpointer user_data)
{
	BackendDownloadData *download;

	g_return_if_fail (pacman != NULL);
	g_return_if_fail (transaction != NULL);
	g_return_if_fail (user_data != NULL);

	download = (BackendDownloadData *) g_hash_table_lookup (downloads, transaction);

	if (filename == NULL) {
		if (download == NULL) {
			/* start a new download */
			download = g_new0 (BackendDownloadData, 1);
			download->complete = complete;
			download->total = total;
			g_hash_table_insert (downloads, transaction, download);
		} else {
			/* finish the current download */
			if (download->package != NULL) {
				transaction_download_end (transaction, download, (PkBackend *) user_data);
			}
			g_hash_table_remove (downloads, transaction);
		}
	} else {
		guint percentage = 100, sub_percentage = 100;

		g_return_if_fail (download != NULL);

		if (total > 0) {
			sub_percentage = complete * 100 / total;
		}

		if (strstr (filename, ".db.tar.") != NULL) {
			const PacmanList *databases = pacman_manager_get_sync_databases (pacman);
			guint database_total = pacman_list_length (databases);

			/* report download progress for databases */
			if (database_total > 0) {
				percentage = (sub_percentage + download->complete * 100) / database_total;
			}

			if (complete == 0) {
				egg_debug ("pacman: downloading database %s", filename);
				pk_backend_set_status ((PkBackend *) user_data, PK_STATUS_ENUM_REFRESH_CACHE);
			}

			if (complete == total) {
				download->complete += 1;
			}
		} else {
			/* report download progress for package or delta files */
			if (download->total > 0) {
				percentage = (download->complete + complete) * 100 / download->total;
			}

			if (complete == 0) {
				egg_debug ("pacman: downloading package %s", filename);
				pk_backend_set_status ((PkBackend *) user_data, PK_STATUS_ENUM_DOWNLOAD);
				transaction_download_start (transaction, download, filename, (PkBackend *) user_data);
			}

			if (complete == total) {
				download->complete += complete;
			}
		}

		pk_backend_set_sub_percentage ((PkBackend *) user_data, sub_percentage);
		pk_backend_set_percentage ((PkBackend *) user_data, percentage);
	}
}

static void
transaction_progress_cb (PacmanTransaction *transaction, PacmanTransactionProgress type, const gchar *target, guint percent, guint current, guint targets, gpointer user_data)
{
	g_return_if_fail (transaction != NULL);
	g_return_if_fail (user_data != NULL);

	g_return_if_fail (percent >= 0);
	g_return_if_fail (percent <= 100);
	g_return_if_fail (current >= 1);
	g_return_if_fail (current <= targets);

	/* update transaction progress */
	switch (type) {
		case PACMAN_TRANSACTION_PROGRESS_INSTALL:
		case PACMAN_TRANSACTION_PROGRESS_UPGRADE:
		case PACMAN_TRANSACTION_PROGRESS_REMOVE:
		case PACMAN_TRANSACTION_PROGRESS_FILE_CONFLICT_CHECK:
		{
			egg_debug ("pacman: progress for %s (%u of %u) is %u%%", target, current, targets, percent);
			pk_backend_set_sub_percentage ((PkBackend *) user_data, percent);
			pk_backend_set_percentage ((PkBackend *) user_data, (percent + (current - 1) * 100) / targets);
			break;
		}
		default:
			egg_debug ("pacman: progress of type %d (%u of %u) is %u%%", type, current, targets, percent);
			break;
	}
}

static gboolean
transaction_question_cb (PacmanTransaction *transaction, PacmanTransactionQuestion question, const gchar *message, gpointer user_data)
{
	g_return_val_if_fail (transaction != NULL, FALSE);
	g_return_val_if_fail (user_data != NULL, FALSE);

	switch (question) {
		case PACMAN_TRANSACTION_QUESTION_INSTALL_IGNORE_PACKAGE:
		{
			PkRoleEnum role = pk_backend_get_role ((PkBackend *) user_data);
			if (role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
				gchar *packages = pacman_package_make_list (pacman_transaction_get_marked_packages (transaction));
				gchar *warning = g_strdup_printf ("The following packages were marked as ignored:\n%s\n", packages);

				/* ignored packages are blocked in updates, can be explicitly installed */
				egg_warning ("pacman: %s", warning);
				backend_message ((PkBackend *) user_data, warning);

				g_free (warning);
				g_free (packages);
				return TRUE;
			} else if (role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES || role == PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES) {
				return TRUE;
			} else {
				return FALSE;
			}
		}
		case PACMAN_TRANSACTION_QUESTION_SKIP_UNRESOLVABLE_PACKAGES:
		case PACMAN_TRANSACTION_QUESTION_REMOVE_HOLD_PACKAGES:
		case PACMAN_TRANSACTION_QUESTION_SYNC_FIRST:
			/* none of these actions are safe */
			egg_warning ("pacman: ignoring question '%s'", message);
			return FALSE;

		case PACMAN_TRANSACTION_QUESTION_REPLACE_PACKAGE:
		case PACMAN_TRANSACTION_QUESTION_REMOVE_CONFLICTING_PACKAGE:
		case PACMAN_TRANSACTION_QUESTION_INSTALL_OLDER_PACKAGE:
		case PACMAN_TRANSACTION_QUESTION_DELETE_CORRUPTED_PACKAGE:
			/* these actions are mostly harmless */
			egg_warning ("pacman: confirming question '%s'", message);
			return TRUE;

		default:
			egg_warning ("pacman: unrecognised question '%s'", message);
			return FALSE;
	}
}

static void
transaction_status_cb (PacmanTransaction *transaction, PacmanTransactionStatus status, const gchar *message, gpointer user_data)
{
	PkStatusEnum state;
	PkInfoEnum info;

	g_return_if_fail (transaction != NULL);
	g_return_if_fail (user_data != NULL);

	/* figure out the backend status and package info */
	switch (status) {
		case PACMAN_TRANSACTION_STATUS_INSTALL_START:
			state = PK_STATUS_ENUM_INSTALL;
			info = PK_INFO_ENUM_INSTALLING;
			break;

		case PACMAN_TRANSACTION_STATUS_UPGRADE_START:
			if (pk_backend_get_role ((PkBackend *) user_data) == PK_ROLE_ENUM_INSTALL_FILES) {
				state = PK_STATUS_ENUM_INSTALL;
				info = PK_INFO_ENUM_INSTALLING;
			} else {
				state = PK_STATUS_ENUM_UPDATE;
				info = PK_INFO_ENUM_UPDATING;
			}
			break;

		case PACMAN_TRANSACTION_STATUS_REMOVE_START:
			state = PK_STATUS_ENUM_REMOVE;
			info = PK_INFO_ENUM_REMOVING;
			break;

		case PACMAN_TRANSACTION_STATUS_INSTALL_END:
		case PACMAN_TRANSACTION_STATUS_UPGRADE_END:
		case PACMAN_TRANSACTION_STATUS_REMOVE_END:
			state = PK_STATUS_ENUM_UNKNOWN;
			info = PK_INFO_ENUM_FINISHED;
			break;

		case PACMAN_TRANSACTION_STATUS_DEPENDENCY_CHECK_START:
		case PACMAN_TRANSACTION_STATUS_DEPENDENCY_RESOLVE_START:
			state = PK_STATUS_ENUM_DEP_RESOLVE;
			info = PK_INFO_ENUM_UNKNOWN;
			break;

		case PACMAN_TRANSACTION_STATUS_FILE_CONFLICT_CHECK_START:
		case PACMAN_TRANSACTION_STATUS_CONFLICT_CHECK_START:
		case PACMAN_TRANSACTION_STATUS_PACKAGE_INTEGRITY_CHECK_START:
		case PACMAN_TRANSACTION_STATUS_DELTA_INTEGRITY_CHECK_START:
			state = PK_STATUS_ENUM_TEST_COMMIT;
			info = PK_INFO_ENUM_UNKNOWN;
			break;

		default:
			state = PK_STATUS_ENUM_UNKNOWN;
			info = PK_INFO_ENUM_UNKNOWN;
			egg_debug ("pacman: %s", message);
			break;
	}

	/* update the backend status */
	if (state != PK_STATUS_ENUM_UNKNOWN) {
		pk_backend_set_status ((PkBackend *) user_data, state);
	}

	/* update the package info */
	if (info != PK_INFO_ENUM_UNKNOWN) {
		const PacmanList *packages;

		for (packages = pacman_transaction_get_marked_packages (transaction); packages != NULL; packages = pacman_list_next (packages)) {
			PacmanPackage *package;

			/* only report the old versions */
			if (status == PACMAN_TRANSACTION_STATUS_UPGRADE_START || status == PACMAN_TRANSACTION_STATUS_UPGRADE_END) {
				packages = pacman_list_next (packages);
				if (packages == NULL) {
					break;
				}
			}

			package = (PacmanPackage *) pacman_list_get (packages);
			backend_package ((PkBackend *) user_data, package, info);
		}
	}
}

static void
transaction_cancelled_cb (GCancellable *object, gpointer user_data)
{
	g_return_if_fail (user_data != NULL);

	pacman_transaction_cancel ((PacmanTransaction *) user_data, NULL);
}

PacmanTransaction *
backend_transaction_simulate (PkBackend *backend, PacmanTransactionType type, guint32 flags, const PacmanList *targets)
{
	PacmanTransaction *transaction;
	GError *error = NULL;

	g_return_val_if_fail (pacman != NULL, NULL);
	g_return_val_if_fail (cancellable != NULL, NULL);
	g_return_val_if_fail (backend != NULL, NULL);
	g_return_val_if_fail (type < PACMAN_TRANSACTION_LAST, NULL);

	switch (type) {
		case PACMAN_TRANSACTION_INSTALL:
			transaction = pacman_manager_install (pacman, flags, &error);
			break;
		case PACMAN_TRANSACTION_MODIFY:
			transaction = pacman_manager_modify (pacman, flags, &error);
			break;
		case PACMAN_TRANSACTION_REMOVE:
			transaction = pacman_manager_remove (pacman, flags, &error);
			break;
		case PACMAN_TRANSACTION_SYNC:
			transaction = pacman_manager_sync (pacman, flags, &error);
			break;
		case PACMAN_TRANSACTION_UPDATE:
			transaction = pacman_manager_update (pacman, flags, &error);
			break;
		default:
			g_return_val_if_reached (NULL);
	}

	if (transaction == NULL) {
		backend_error (backend, error);
		return NULL;
	}

	g_signal_connect (transaction, "download", G_CALLBACK (transaction_download_cb), backend);
	g_signal_connect (transaction, "progress", G_CALLBACK (transaction_progress_cb), backend);
	g_signal_connect (transaction, "question", G_CALLBACK (transaction_question_cb), backend);
	g_signal_connect (transaction, "status", G_CALLBACK (transaction_status_cb), backend);

	if (g_cancellable_connect (cancellable, G_CALLBACK (transaction_cancelled_cb), transaction, NULL) == 0 && backend_cancelled (backend)) {
		return transaction;
	}

	if (!pacman_transaction_prepare (transaction, targets, &error)) {
		backend_error (backend, error);
		g_object_unref (transaction);
		return NULL;
	}

	return transaction;
}

PacmanTransaction *
backend_transaction_run (PkBackend *backend, PacmanTransactionType type, guint32 flags, const PacmanList *targets)
{
	PacmanTransaction *transaction;

	g_return_val_if_fail (backend != NULL, NULL);

	transaction = backend_transaction_simulate (backend, type, flags, targets);

	return backend_transaction_commit (backend, transaction);
}

void
backend_transaction_packages (PkBackend *backend, PacmanTransaction *transaction)
{
	const PacmanList *installs, *removes;
	PkInfoEnum info;

	g_return_if_fail (local_database != NULL);
	g_return_if_fail (backend != NULL);
	g_return_if_fail (transaction != NULL);

	/* emit packages that would have been installed */
	for (installs = pacman_transaction_get_installs (transaction); installs != NULL; installs = pacman_list_next (installs)) {
		PacmanPackage *install = (PacmanPackage *) pacman_list_get (installs);

		if (backend_cancelled (backend)) {
			break;
		} else {
			const gchar *name = pacman_package_get_name (install);
			if (pacman_database_find_package (local_database, name) != NULL) {
				backend_package (backend, install, PK_INFO_ENUM_UPDATING);
			} else {
				backend_package (backend, install, PK_INFO_ENUM_INSTALLING);
			}
		}
	}

	if (pk_backend_get_role (backend) == PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES) {
		info = PK_INFO_ENUM_OBSOLETING;
	} else {
		info = PK_INFO_ENUM_REMOVING;
	}

	/* emit packages that would have been removed */
	for (removes = pacman_transaction_get_removes (transaction); removes != NULL; removes = pacman_list_next (removes)) {
		PacmanPackage *remove = (PacmanPackage *) pacman_list_get (removes);

		if (backend_cancelled (backend)) {
			break;
		} else {
			backend_package (backend, remove, info);
		}
	}
}

PacmanTransaction *
backend_transaction_commit (PkBackend *backend, PacmanTransaction *transaction)
{
	GError *error = NULL;

	if (transaction != NULL && !backend_cancelled (backend)) {
		pk_backend_set_status (backend, PK_STATUS_ENUM_RUNNING);

		if (!pacman_transaction_commit (transaction, &error)) {
			backend_error (backend, error);
			g_hash_table_remove (downloads, transaction);
			g_object_unref (transaction);
			return NULL;
		}
	}

	return transaction;
}

gboolean
backend_transaction_finished (PkBackend *backend, PacmanTransaction *transaction)
{
	g_return_val_if_fail (backend != NULL, FALSE);

	if (transaction != NULL) {
		g_object_unref (transaction);
		backend_finished (backend);
		return TRUE;
	} else {
		backend_finished (backend);
		return FALSE;
	}
}
