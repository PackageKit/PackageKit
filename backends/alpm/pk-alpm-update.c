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
#include <glib/gstdio.h>
#include <pk-backend.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>
#include <errno.h>
#include <stdio.h>
#include <syslog.h>

#include "pk-backend-alpm.h"
#include "pk-alpm-config.h"
#include "pk-alpm-error.h"
#include "pk-alpm-packages.h"
#include "pk-alpm-transaction.h"
#include "pk-alpm-update.h"

static gchar **
pk_alpm_pkg_build_replaces (PkBackendJob *job, alpm_pkg_t *pkg)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	const alpm_list_t *i;
	gchar **replaces = NULL;
	gint count = 0;

	g_return_val_if_fail (pkg != NULL, NULL);

	/* make a list of the packages that package replaces */
	for (i = alpm_pkg_get_replaces (pkg); i != NULL; i = i->next) {
		alpm_pkg_t *package = alpm_db_get_pkg (priv->localdb, i->data);

		if (package != NULL) {
			gchar *id = pk_alpm_pkg_build_id (package);
			if (id) {
				replaces =  g_realloc (replaces, ((++count) + 1) * sizeof(gchar *));
				replaces[count - 1] = id;
				replaces[count] = NULL;
			}
		}
	}

	return replaces;
}

static gchar **
pk_alpm_pkg_build_urls (alpm_pkg_t *pkg)
{
	gchar **urls = g_new0 (gchar *, 2);
	urls[0] = g_strdup_printf ("https://archlinux.org/packages/%s/%s/%s/",
				   alpm_db_get_name (alpm_pkg_get_db (pkg)),
				   alpm_pkg_get_arch (pkg),
				   alpm_pkg_get_name (pkg));
	return urls;
}

static gboolean
pk_alpm_pkg_same_pkgver (alpm_pkg_t *a, alpm_pkg_t *b)
{
	const gchar *version_a, *version_b, *last_a, *last_b;
	gsize length_a, length_b;

	g_return_val_if_fail (a != NULL, (b == NULL));
	g_return_val_if_fail (b != NULL, FALSE);

	version_a = alpm_pkg_get_version (a);
	version_b = alpm_pkg_get_version (b);

	last_a = strrchr (version_a, '-');
	last_b = strrchr (version_b, '-');

	if (last_a != NULL) {
		length_a = last_a - version_a;
	} else {
		length_a = strlen (version_a);
	}

	if (last_b != NULL) {
		length_b = last_b - version_b;
	} else {
		length_b = strlen (version_b);
	}

	if (length_a != length_b)
		return FALSE;
	return strncmp (version_a, version_b, length_a) == 0;
}

static gchar *
pk_alpm_time_to_iso8601 (alpm_time_t time)
{
	GDateTime *date = g_date_time_new_from_unix_utc (time);
	gchar *result;

	if (date == NULL)
		return NULL;

	result = g_date_time_format (date, "%FT%TZ");
	g_date_time_unref (date);
	return result;
}

static void
pk_backend_get_update_detail_thread (PkBackendJob *job, GVariant* params, gpointer p)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	gchar **packages;
	g_autoptr(GError) error = NULL;

	packages = (gchar**) p;

	/* collect details about updates */
	for (; *packages != NULL; ++packages) {
		alpm_pkg_t *pkg, *old;
		alpm_db_t *db;
		const gchar *reason;
		PkRestartEnum restart = PK_RESTART_ENUM_NONE;
		PkUpdateStateEnum state = PK_UPDATE_STATE_ENUM_STABLE;
		alpm_time_t built, installed;
		gchar *upgrades[2] = { NULL, NULL };
		gchar **replaces;
		gchar **charptr;
		g_auto(GStrv) urls = NULL;
		g_autofree gchar *issued = NULL;
		g_autofree gchar *updated = NULL;

		if (pk_backend_job_is_cancelled (job))
			break;

		pkg = pk_alpm_find_pkg (job, *packages, &error);
		if (pkg == NULL)
			break;

		old = alpm_db_get_pkg (priv->localdb, alpm_pkg_get_name (pkg));
		if (old != NULL) {
			upgrades[0] = pk_alpm_pkg_build_id (old);
			if (pk_alpm_pkg_same_pkgver (pkg, old)) {
				reason = "Update to a newer release";
			} else {
				reason = "Update to a new upstream version";
			}
		} else {
			reason = "Install to replace an older package";
		}

		db = alpm_pkg_get_db (pkg);
		replaces = pk_alpm_pkg_build_replaces (job, pkg);
		urls = pk_alpm_pkg_build_urls (pkg);

		if (g_str_has_prefix (alpm_pkg_get_name (pkg), "kernel"))
			restart = PK_RESTART_ENUM_SYSTEM;

		if (g_str_has_suffix (alpm_db_get_name (db), "testing"))
			state = PK_UPDATE_STATE_ENUM_TESTING;

		built = alpm_pkg_get_builddate (pkg);
		if (built > 0)
			issued = pk_alpm_time_to_iso8601 (built);

		if (upgrades != NULL) {
			installed = alpm_pkg_get_installdate (old);
			if (installed > 0)
				updated = pk_alpm_time_to_iso8601 (installed);
		}

		pk_backend_job_update_detail (job, *packages, upgrades,
					      replaces, urls, NULL, NULL,
					      restart, reason, NULL, state,
					      issued, updated);
		if (upgrades[0]) g_free (upgrades[0]);
		if (replaces) {
			for (charptr = replaces; charptr[0]; charptr++)
				g_free (charptr[0]);
			g_free(replaces);
		}
	}

	pk_alpm_finish (job, error);
}

void
pk_backend_get_update_detail (PkBackend * self,
			      PkBackendJob *job,
			      gchar **package_ids)
{
	pk_alpm_run (job, PK_STATUS_ENUM_QUERY, pk_backend_get_update_detail_thread, package_ids);
}

static gchar *
pk_alpm_update_get_db_timestamp_filename (alpm_db_t *db)
{
	return g_strconcat ("/var/cache/PackageKit/alpm/",
			    alpm_db_get_name (db),
			    ".db.timestamp",
			    NULL);
}

static gboolean
pk_alpm_update_set_db_timestamp (alpm_db_t *db, GError **error)
{
	g_autofree gchar *timestamp_filename = NULL;
	struct utimbuf times;

	timestamp_filename = pk_alpm_update_get_db_timestamp_filename (db);

	times.actime = time (NULL);
	times.modtime = time (NULL);

	if (g_mkdir_with_parents ("/var/cache/PackageKit/alpm/", 0755) < 0) {
		g_set_error_literal (error, PK_ALPM_ERROR, errno, strerror(errno));
		return FALSE;
	}

	if (!g_file_set_contents (timestamp_filename, "", 0, error)) {
		return FALSE;
	}

	if (g_utime (timestamp_filename, &times) < 0) {
		g_set_error_literal (error, PK_ALPM_ERROR, errno, strerror(errno));
		return FALSE;
	}

	return TRUE;
}

gboolean
pk_alpm_refresh_databases (PkBackendJob *job, gint force, alpm_list_t *dbs, GError **error)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	alpm_cb_download dlcb;
	gint result;
	alpm_list_t *i;

	dlcb = alpm_option_get_dlcb (priv->alpm);

	if (!force)
		return TRUE;

	result = alpm_db_update (priv->alpm, dbs, force);
	if (result > 0) {
		dlcb (NULL, "", ALPM_DOWNLOAD_COMPLETED, (void *)1);
	} else if (result < 0) {
		g_set_error (error, PK_ALPM_ERROR, alpm_errno (priv->alpm), "failed to uptate database: %s",
				alpm_strerror (errno));
		return FALSE;
	}

	for (i = dbs; i; i = alpm_list_next (i)) {
		if (!pk_alpm_update_set_db_timestamp (i->data, error)) {
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
pk_alpm_update_databases (PkBackendJob *job, gint force, GError **error)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	alpm_list_t *i;

	if (!pk_alpm_transaction_initialize (job, 0, NULL, error))
		return FALSE;

	alpm_logaction (priv->alpm, PK_LOG_PREFIX, "synchronizing package lists\n");
	pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST);

	i = alpm_get_syncdbs (priv->alpm);
	pk_alpm_refresh_databases (job, force, i, error);

	if (i == NULL)
		return pk_alpm_transaction_end (job, error);
	pk_alpm_transaction_end (job, NULL);
	return FALSE;
}

static gboolean
pk_alpm_pkg_is_ignorepkg (PkBackend *backend, alpm_pkg_t *pkg)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	const alpm_list_t *ignorepkgs, *ignoregroups, *i;

	g_return_val_if_fail (pkg != NULL, TRUE);

	ignorepkgs = alpm_option_get_ignorepkgs (priv->alpm);
	if (alpm_list_find_str (ignorepkgs, alpm_pkg_get_name (pkg)) != NULL)
		return TRUE;

	ignoregroups = alpm_option_get_ignoregroups (priv->alpm);
	for (i = alpm_pkg_get_groups (pkg); i != NULL; i = i->next) {
		if (alpm_list_find_str (ignoregroups, i->data) != NULL)
			return TRUE;
	}

	return FALSE;
}

static gboolean
pk_alpm_pkg_is_syncfirst (alpm_list_t *syncfirsts, alpm_pkg_t *pkg)
{
	g_return_val_if_fail (pkg != NULL, FALSE);
	if (alpm_list_find_str (syncfirsts, alpm_pkg_get_name (pkg)) != NULL)
		return TRUE;
	return FALSE;
}

static gboolean
pk_alpm_pkg_replaces (alpm_pkg_t *pkg, const gchar *name)
{
	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	return alpm_list_find_str (alpm_pkg_get_replaces (pkg), name) != NULL;
}

static alpm_pkg_t *
pk_alpm_pkg_find_update (alpm_pkg_t *pkg, const alpm_list_t *dbs)
{
	const gchar *name;
	const alpm_list_t *i;

	g_return_val_if_fail (pkg != NULL, NULL);

	name = alpm_pkg_get_name (pkg);

	for (; dbs != NULL; dbs = dbs->next) {
		alpm_pkg_t *update = alpm_db_get_pkg (dbs->data, name);

		if (update != NULL) {
			if (alpm_pkg_vercmp (alpm_pkg_get_version (update),
					     alpm_pkg_get_version (pkg)) > 0) {
				return update;
			}
			return NULL;
		}

		i = alpm_db_get_pkgcache (dbs->data);
		for (; i != NULL; i = i->next) {
			if (pk_alpm_pkg_replaces (i->data, name))
				return i->data;
		}
	}

	return NULL;
}

static gboolean
pk_alpm_update_is_pkg_downloaded (alpm_pkg_t *pkg)
{
	g_autofree gchar *filename = NULL;

	filename = g_strconcat ("/var/cache/pacman/pkg/",
				alpm_pkg_get_name (pkg),
				"-",
				alpm_pkg_get_version (pkg),
				"-",
				alpm_pkg_get_arch (pkg),
				".pkg.tar.xz",
				NULL);
	return g_file_test (filename, G_FILE_TEST_IS_REGULAR);
}

static void
pk_backend_get_updates_thread (PkBackendJob *job, GVariant* params, gpointer p)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	int update_count = 0;
	alpm_list_t *i, *syncdbs;
	g_autoptr(GError) error = NULL;
	PkBitfield filters = 0;
	FILE *file;
	int stored_count;
	alpm_handle_t* old_handle = priv->alpm;
	alpm_handle_t* handle = pk_alpm_configure (backend, PK_BACKEND_CONFIG_FILE, TRUE, &error);

	alpm_logaction (handle, PK_LOG_PREFIX, "synchronizing package lists\n");
	pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST);

	/* set total size to minus the number of databases */
	i = alpm_get_syncdbs (handle);

	// swap around the handles since the refresh database will grab
	// the main system handle and not the check update handle otherwise
	priv->alpm = handle;
	pk_alpm_refresh_databases (job, TRUE, i, &error);
	priv->alpm = old_handle;

	if (pk_backend_job_get_role (job) == PK_ROLE_ENUM_GET_UPDATES) {
		g_variant_get (params, "(t)", &filters);
	}

	/* find outdated and replacement packages */
	syncdbs = alpm_get_syncdbs (handle);
	for (i = alpm_db_get_pkgcache (priv->localdb); i != NULL; i = i->next) {
		PkInfoEnum info = PK_INFO_ENUM_NORMAL;
		alpm_pkg_t *upgrade = pk_alpm_pkg_find_update (i->data, syncdbs);
		if (upgrade == NULL)
			continue;
		if (pk_backend_job_is_cancelled (job))
			break;
		if (pk_alpm_pkg_is_ignorepkg (backend, upgrade)) {
			info = PK_INFO_ENUM_BLOCKED;
		} else if (pk_alpm_pkg_is_syncfirst (priv->syncfirsts, upgrade)) {
			info = PK_INFO_ENUM_IMPORTANT;
		}

		/* want downloaded packages */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DOWNLOADED) && !pk_alpm_update_is_pkg_downloaded (upgrade))
			continue;

		/* don't want downloaded packages */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DOWNLOADED) && pk_alpm_update_is_pkg_downloaded (upgrade))
			continue;

		update_count++;
		pk_alpm_pkg_emit (job, upgrade, info);
	}

	if (g_file_test("/tmp/packagekit-alpm-updates", G_FILE_TEST_EXISTS)) {
		file = fopen("/tmp/packagekit-alpm-updates", "r");

		if (file != NULL) {
			fscanf(file, "%d", &stored_count);
			if (stored_count != update_count) {
				g_signal_emit_by_name(backend, "updates-changed");
			}
			fclose(file);
		} else {
			syslog( LOG_DAEMON | LOG_WARNING, "Failed to open file /tmp/packagekit-alpm-updates for reading");
			g_signal_emit_by_name(backend, "updates-changed");
		}
	} else {
		g_signal_emit_by_name(backend, "updates-changed");
	}

	file = fopen("/tmp/packagekit-alpm-updates", "w");
	if (file != NULL) {
		fprintf(file, "%d", update_count);
		fclose(file);
	} else {
		syslog( LOG_DAEMON | LOG_WARNING, "Failed to open file /tmp/packagekit-alpm-updates for writing");
	}
}

void
pk_backend_get_updates (PkBackend *self,
			PkBackendJob *job,
			PkBitfield filters)
{
	pk_alpm_run (job, PK_STATUS_ENUM_QUERY, pk_backend_get_updates_thread, NULL);
}

static void
pk_backend_refresh_cache_thread (PkBackendJob *job, GVariant* params, gpointer p)
{
	gint force;
	g_autoptr(GError) error = NULL;

	g_assert (job != NULL);

	/* download databases even if they are older than current */
	g_variant_get (params, "(b)", &force);

	pk_alpm_update_databases (job, force, &error);
	pk_alpm_finish (job, error);
}

void
pk_backend_refresh_cache (PkBackend *self,
			  PkBackendJob *job,
			  gboolean force)
{
	pk_alpm_run (job, PK_STATUS_ENUM_SETUP, pk_backend_refresh_cache_thread, NULL);
}
