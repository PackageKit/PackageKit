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
#include <pacman.h>
#include <glib/gstdio.h>
#include "backend-error.h"
#include "backend-pacman.h"
#include "backend-packages.h"
#include "backend-repos.h"
#include "backend-transaction.h"
#include "backend-update.h"

static gchar *
pacman_package_make_replaces_ids (PacmanPackage *package)
{
	const PacmanList *list;
	GString *string = NULL;

	g_return_val_if_fail (local_database != NULL, NULL);
	g_return_val_if_fail (package != NULL, NULL);

	/* make a list of the packages that package replaces */
	for (list = pacman_package_get_replaces (package); list != NULL; list = pacman_list_next (list)) {
		const gchar *name = pacman_list_get (list);
		PacmanPackage *replaces = pacman_database_find_package (local_database, name);

		if (replaces != NULL) {
			gchar *package_id = pacman_package_make_id (replaces);
			if (string == NULL) {
				string = g_string_new (package_id);
			} else {
				g_string_append_printf (string, "&%s", package_id);
			}
			g_free (package_id);
		}
	}

	if (string != NULL) {
		return g_string_free (string, FALSE);
	} else {
		return NULL;
	}
}

static gchar *
pacman_package_make_vendor_url (PacmanPackage *package)
{
	GString *string = g_string_new ("");
#ifdef PACMAN_PACKAGE_URL
	const gchar *name, *arch, *repo, *url;
#else
	const gchar *url;
#endif

	g_return_val_if_fail (package != NULL, NULL);

	/* grab the URL of the package... */
	url = pacman_package_get_url (package);
	if (url != NULL) {
		g_string_append_printf (string, "%s;Package website;", url);
	}

#ifdef PACMAN_PACKAGE_URL
	/* ... and construct the distro URL if possible */
	name = pacman_package_get_name (package);
	arch = pacman_package_get_arch (package);
	repo = pacman_database_get_name (pacman_package_get_database (package));

	g_string_append_printf (string, PACMAN_PACKAGE_URL ";Distribution website;", repo, arch, name);
#endif

	g_string_truncate (string, string->len - 1);
	return g_string_free (string, FALSE);
}

static gint
pacman_package_compare_pkgver (PacmanPackage *a, PacmanPackage *b)
{
	gint result;
	const gchar *version_a, *version_b, *last_a, *last_b;
	gchar *pkgver_a, *pkgver_b;

	g_return_val_if_fail (a != NULL, (b == NULL) ? 0 : -1);
	g_return_val_if_fail (b != NULL, 1);

	version_a = pacman_package_get_version (a);
	version_b = pacman_package_get_version (b);

	last_a = strrchr (version_a, '-');
	last_b = strrchr (version_b, '-');

	if (last_a != NULL) {
		pkgver_a = g_strndup (version_a, last_a - version_a);
	} else {
		pkgver_a = g_strdup (version_a);
	}

	if (last_b != NULL) {
		pkgver_b = g_strndup (version_b, last_b - version_b);
	} else {
		pkgver_b = g_strdup (version_b);
	}

	result = pacman_package_compare_version (pkgver_a, pkgver_b);

	g_free (pkgver_a);
	g_free (pkgver_b);

	return result;
}

static gboolean
backend_get_update_detail_thread (PkBackend *backend)
{
	guint iterator;

	gchar **package_ids;

	g_return_val_if_fail (local_database != NULL, FALSE);
	g_return_val_if_fail (backend != NULL, FALSE);

	package_ids = pk_backend_get_strv (backend, "package_ids");

	g_return_val_if_fail (package_ids != NULL, FALSE);

	/* collect details about updates */
	for (iterator = 0; package_ids[iterator] != NULL; ++iterator) {
		PacmanPackage *package, *upgrades;
		PacmanDatabase *database;

		gchar *upgrades_id, *replaces_ids, *vendor_url;
		const gchar *message;

		PkRestartEnum restart;
		PkUpdateStateEnum state;

		GTimeVal built = { 0 }, installed = { 0 };
		gchar *issued, *updated;

		if (backend_cancelled (backend)) {
			break;
		}

		package = backend_get_package (backend, package_ids[iterator]);
		if (package == NULL) {
			backend_finished (backend);
			return FALSE;
		}

		upgrades = pacman_database_find_package (local_database, pacman_package_get_name (package));
		if (upgrades != NULL) {
			upgrades_id = pacman_package_make_id (upgrades);
			if (pacman_package_compare_pkgver (package, upgrades) != 0) {
				message = "Update to newest upstream version";
			} else {
				message = "Update to newest release";
			}
		} else {
			upgrades_id = NULL;
			message = "Install as a replacement for an older package";
		}

		database = pacman_package_get_database (package);
		replaces_ids = pacman_package_make_replaces_ids (package);
		vendor_url = pacman_package_make_vendor_url (package);

		if (g_str_has_prefix (pacman_package_get_name (package), "kernel")) {
			restart = PK_RESTART_ENUM_SYSTEM;
		} else {
			restart = PK_RESTART_ENUM_NONE;
		}

		if (g_str_has_suffix (pacman_database_get_name (database), "testing")) {
			state = PK_UPDATE_STATE_ENUM_TESTING;
		} else {
			state = PK_UPDATE_STATE_ENUM_STABLE;
		}

		built.tv_sec = pacman_package_get_build_date (package);
		if (built.tv_sec > 0) {
			issued = g_time_val_to_iso8601 (&built);
		} else {
			issued = NULL;
		}

		if (upgrades != NULL) {
			installed.tv_sec = pacman_package_get_install_date (upgrades);
			if (installed.tv_sec > 0) {
				updated = g_time_val_to_iso8601 (&installed);
			} else {
				updated = NULL;
			}
		} else {
			updated = NULL;
		}

		pk_backend_update_detail (backend, package_ids[iterator], upgrades_id, replaces_ids, vendor_url, NULL, NULL, restart, message, NULL, state, issued, updated);

		g_free (issued);
		g_free (updated);

		g_free (vendor_url);
		g_free (replaces_ids);
		g_free (upgrades_id);
	}

	backend_finished (backend);
	return TRUE;
}

/**
 * backend_get_update_detail:
 **/
void
backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (package_ids != NULL);

	backend_run (backend, PK_STATUS_ENUM_QUERY, backend_get_update_detail_thread);
}

static gboolean
pacman_package_should_ignore (PacmanPackage *package)
{
	const PacmanList *groups;
	const PacmanList *ignore_packages;
	const PacmanList *ignore_groups;

	g_return_val_if_fail (pacman != NULL, TRUE);
	g_return_val_if_fail (package != NULL, TRUE);

	ignore_packages = pacman_manager_get_ignore_packages (pacman);

	/* check if package is an IgnorePkg */
	if (pacman_list_find_string (ignore_packages, pacman_package_get_name (package)) != NULL) {
		return TRUE;
	}

	ignore_groups = pacman_manager_get_ignore_groups (pacman);

	/* check if package is in an IgnoreGroup */
	for (groups = pacman_package_get_groups (package); groups != NULL; groups = pacman_list_next (groups)) {
		if (pacman_list_find_string (ignore_groups, (const gchar *) pacman_list_get (groups)) != NULL) {
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
pacman_package_should_sync_first (PacmanPackage *package)
{
	const PacmanList *sync_firsts;

	g_return_val_if_fail (pacman != NULL, FALSE);
	g_return_val_if_fail (package != NULL, FALSE);

	sync_firsts = pacman_manager_get_sync_firsts (pacman);

	/* check if package is in SyncFirst */
	if (pacman_list_find_string (sync_firsts, pacman_package_get_name (package)) != NULL) {
		return TRUE;
	}

	return FALSE;
}

static gboolean
backend_get_updates_thread (PkBackend *backend)
{
	struct stat cache;
	time_t one_hour_ago;

	PacmanTransaction *transaction = NULL;
	PacmanTransactionFlags flags = PACMAN_TRANSACTION_FLAGS_NONE;

	const PacmanList *packages;

	g_return_val_if_fail (local_database != NULL, FALSE);
	g_return_val_if_fail (pacman != NULL, FALSE);
	g_return_val_if_fail (backend != NULL, FALSE);

	time (&one_hour_ago);
	one_hour_ago -= 60 * 60;

	/* refresh databases if they are older than an hour */
	if (g_stat (PACMAN_CACHE_PATH, &cache) < 0 || cache.st_mtime < one_hour_ago) {
		transaction = backend_transaction_run (backend, PACMAN_TRANSACTION_UPDATE, flags, NULL);

		if (transaction != NULL) {
			g_object_unref (transaction);
		} else {
			backend_finished (backend);
			return FALSE;
		}
	} else {
		g_debug ("pacman: databases have been refreshed recently");
	}

	/* find outdated and replacement packages */
	for (packages = pacman_database_get_packages (local_database); packages != NULL; packages = pacman_list_next (packages)) {
		PacmanPackage *package = (PacmanPackage *) pacman_list_get (packages);
		PacmanPackage *upgrade = pacman_package_find_upgrade (package, pacman_manager_get_sync_databases (pacman));

		if (backend_cancelled (backend)) {
			break;
		}

		if (upgrade != NULL) {
			PkInfoEnum info;

			if (pacman_package_should_ignore (upgrade)) {
				info = PK_INFO_ENUM_BLOCKED;
			} else if (pacman_package_should_sync_first (upgrade)) {
				info = PK_INFO_ENUM_IMPORTANT;
			} else {
				info = PK_INFO_ENUM_NORMAL;
			}

			backend_package (backend, upgrade, info);
		}
	}

	backend_finished (backend);
	return TRUE;
}

/**
 * backend_get_updates:
 **/
void
backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	g_return_if_fail (backend != NULL);

	backend_run (backend, PK_STATUS_ENUM_QUERY, backend_get_updates_thread);
}

static gboolean
backend_refresh_cache_thread (PkBackend *backend)
{
	gboolean force;

	PacmanTransaction *transaction = NULL;
	PacmanTransactionFlags flags = PACMAN_TRANSACTION_FLAGS_NONE;

	g_return_val_if_fail (backend != NULL, FALSE);

	force = pk_backend_get_bool (backend, "force");

	/* download databases even if they are older than current */
	if (force) {
		flags |= PACMAN_TRANSACTION_FLAGS_UPDATE_ALLOW_DOWNGRADE;
	}

	/* run the transaction */
	transaction = backend_transaction_run (backend, PACMAN_TRANSACTION_UPDATE, flags, NULL);

	return backend_transaction_finished (backend, transaction);
}

/**
 * backend_refresh_cache:
 **/
void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (backend != NULL);

	backend_run (backend, PK_STATUS_ENUM_SETUP, backend_refresh_cache_thread);
}
