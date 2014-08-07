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

#include "pk-backend-alpm.h"
#include "pk-backend-error.h"
#include "pk-backend-packages.h"
#include "pk-backend-transaction.h"
#include "pk-backend-update.h"

static gchar *
alpm_pkg_build_replaces (alpm_pkg_t *pkg)
{
	const alpm_list_t *i;
	GString *string = NULL;

	g_return_val_if_fail (pkg != NULL, NULL);
	g_return_val_if_fail (localdb != NULL, NULL);

	/* make a list of the packages that package replaces */
	for (i = alpm_pkg_get_replaces (pkg); i != NULL; i = i->next) {
		alpm_pkg_t *replaces = alpm_db_get_pkg (localdb, i->data);

		if (replaces != NULL) {
			gchar *package = alpm_pkg_build_id (replaces);
			if (string == NULL) {
				string = g_string_new (package);
			} else {
				g_string_append_printf (string, "&%s", package);
			}
			g_free (package);
		}
	}

	if (string != NULL) {
		return g_string_free (string, FALSE);
	} else {
		return NULL;
	}
}

static gchar *
alpm_pkg_build_urls (alpm_pkg_t *pkg)
{
	GString *string = g_string_new ("");
#ifdef ALPM_PACKAGE_URL
	const gchar *name, *arch, *repo, *url;
#else
	const gchar *url;
#endif

	g_return_val_if_fail (pkg != NULL, NULL);

	/* grab the URL of the package... */
	url = alpm_pkg_get_url (pkg);
	if (url != NULL) {
		g_string_append_printf (string, "%s;Package website;", url);
	}

#ifdef ALPM_PACKAGE_URL
	/* ... and construct the distro URL if possible */
	name = alpm_pkg_get_name (pkg);
	arch = alpm_pkg_get_arch (pkg);
	repo = alpm_db_get_name (alpm_pkg_get_db (pkg));

	g_string_append_printf (string, ALPM_PACKAGE_URL ";Distribution page;",
				repo, arch, name);
#endif

	g_string_truncate (string, string->len - 1);
	return g_string_free (string, FALSE);
}

static gboolean
alpm_pkg_same_pkgver (alpm_pkg_t *a, alpm_pkg_t *b)
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

	if (length_a != length_b) {
		return FALSE;
	} else {
		return strncmp (version_a, version_b, length_a) == 0;
	}
}

static gchar *
alpm_time_to_iso8601 (alpm_time_t time)
{
	GDateTime *date = g_date_time_new_from_unix_utc (time);

	if (date != NULL) {
		gchar *result = g_date_time_format (date, "%FT%TZ");
		g_date_time_unref (date);
		return result;
	} else {
		return NULL;
	}
}

static void
pk_backend_get_update_detail_thread (PkBackendJob *job, GVariant* params, gpointer p)
{
	gchar **packages;
	GError *error = NULL;

	pkalpm_end_job_if_fail (job != NULL, job);
	pkalpm_end_job_if_fail (localdb != NULL, job);

	packages = (gchar**) p;

	pkalpm_end_job_if_fail (packages != NULL, job);

	/* collect details about updates */
	for (; *packages != NULL; ++packages) {
		alpm_pkg_t *pkg, *old;
		alpm_db_t *db;

		gchar *upgrades, *replaces, *urls;
		const gchar *reason;

		PkRestartEnum restart;
		PkUpdateStateEnum state;

		alpm_time_t built, installed;
		gchar *issued, *updated;

		if (pk_backend_cancelled (job)) {
			break;
		}

		pkg = pkalpm_backend_find_pkg (job, *packages, &error);
		if (pkg == NULL) {
			break;
		}

		old = alpm_db_get_pkg (localdb, alpm_pkg_get_name (pkg));
		if (old != NULL) {
			upgrades = alpm_pkg_build_id (old);
			if (alpm_pkg_same_pkgver (pkg, old)) {
				reason = "Update to a newer release";
			} else {
				reason = "Update to a new upstream version";
			}
		} else {
			upgrades = NULL;
			reason = "Install to replace an older package";
		}

		db = alpm_pkg_get_db (pkg);
		replaces = alpm_pkg_build_replaces (pkg);
		urls = alpm_pkg_build_urls (pkg);

		if (g_str_has_prefix (alpm_pkg_get_name (pkg), "kernel")) {
			restart = PK_RESTART_ENUM_SYSTEM;
		} else {
			restart = PK_RESTART_ENUM_NONE;
		}

		if (g_str_has_suffix (alpm_db_get_name (db), "testing")) {
			state = PK_UPDATE_STATE_ENUM_TESTING;
		} else {
			state = PK_UPDATE_STATE_ENUM_STABLE;
		}

		built = alpm_pkg_get_builddate (pkg);
		if (built > 0) {
			issued = alpm_time_to_iso8601 (built);
		} else {
			issued = NULL;
		}

		if (upgrades != NULL) {
			installed = alpm_pkg_get_installdate (old);
			if (installed > 0) {
				updated = alpm_time_to_iso8601 (installed);
			} else {
				updated = NULL;
			}
		} else {
			updated = NULL;
		}

		pk_backend_job_update_detail (job, *packages, &upgrades,
					      &replaces, &urls, NULL, NULL,
					      restart, reason, NULL, state,
					      issued, updated);

		g_free (issued);
		g_free (updated);

		g_free (urls);
		g_free (replaces);
		g_free (upgrades);
	}

	pk_backend_finish (job, error);
}

void
pk_backend_get_update_detail (PkBackend * self,
                              PkBackendJob   *job,
                              gchar      **package_ids)
{
	g_return_if_fail (job != NULL);
	g_return_if_fail (package_ids != NULL);

	pkalpm_backend_run (job, PK_STATUS_ENUM_QUERY,
			pk_backend_get_update_detail_thread, package_ids);
}

static gboolean
pk_backend_update_databases (PkBackendJob *job, gint force, GError **error)
{
	alpm_cb_download dlcb;
	alpm_cb_totaldl totaldlcb;
	const alpm_list_t *i;

	g_return_val_if_fail (job != NULL, FALSE);
	g_return_val_if_fail (alpm != NULL, FALSE);

	if (!pk_backend_transaction_initialize (job, 0, NULL, error)) {
		return FALSE;
	}

	alpm_logaction (alpm, PK_LOG_PREFIX, "synchronizing package lists\n");
	pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST);

	dlcb = alpm_option_get_dlcb (alpm);
	totaldlcb = alpm_option_get_totaldlcb (alpm);

	/* set total size to minus the number of databases */
	i = alpm_get_syncdbs (alpm);
	totaldlcb (-alpm_list_count (i));

	for (; i != NULL; i = i->next) {
		gint result;

		if (pk_backend_cancelled (job)) {
			/* pretend to be finished */
			i = NULL;
			break;
		}

		result = alpm_db_update (force, i->data);

		if (result > 0) {
			/* fake the download when already up to date */
			dlcb ("", 1, 1);
		} else if (result < 0) {
			alpm_errno_t errno = alpm_errno (alpm);
			g_set_error (error, ALPM_ERROR, errno, "[%s]: %s",
				     alpm_db_get_name (i->data),
				     alpm_strerror (errno));
			break;
		}
	}

	totaldlcb (0);

	if (i == NULL) {
		return pk_backend_transaction_end (job, error);
	} else {
		pk_backend_transaction_end (job, NULL);
		return FALSE;
	}
}

static gboolean
alpm_pkg_is_ignorepkg (alpm_pkg_t *pkg)
{
	const alpm_list_t *ignorepkgs, *ignoregroups, *i;

	g_return_val_if_fail (pkg != NULL, TRUE);
	g_return_val_if_fail (alpm != NULL, TRUE);

	ignorepkgs = alpm_option_get_ignorepkgs (alpm);
	if (alpm_list_find_str (ignorepkgs, alpm_pkg_get_name (pkg)) != NULL) {
		return TRUE;
	}

	ignoregroups = alpm_option_get_ignoregroups (alpm);
	for (i = alpm_pkg_get_groups (pkg); i != NULL; i = i->next) {
		if (alpm_list_find_str (ignoregroups, i->data) != NULL) {
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
alpm_pkg_is_syncfirst (alpm_pkg_t *pkg)
{
	g_return_val_if_fail (pkg != NULL, FALSE);

	if (alpm_list_find_str (syncfirsts, alpm_pkg_get_name (pkg)) != NULL) {
		return TRUE;
	}

	return FALSE;
}

static gboolean
alpm_pkg_replaces (alpm_pkg_t *pkg, const gchar *name)
{
	g_return_val_if_fail (pkg != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	return alpm_list_find_str (alpm_pkg_get_replaces (pkg), name) != NULL;
}


static alpm_pkg_t *
alpm_pkg_find_update (alpm_pkg_t *pkg, const alpm_list_t *dbs)
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
			} else {
				return NULL;
			}
		}

		i = alpm_db_get_pkgcache (dbs->data);
		for (; i != NULL; i = i->next) {
			if (alpm_pkg_replaces (i->data, name)) {
				return i->data;
			}
		}
	}

	return NULL;
}

static void
pk_backend_get_updates_thread (PkBackendJob *job, GVariant* params, gpointer p)
{
	struct stat cache;
	time_t one_hour_ago;
	const alpm_list_t *i, *syncdbs;

	pkalpm_end_job_if_fail (job != NULL, job);
	pkalpm_end_job_if_fail (alpm != NULL, job);
	pkalpm_end_job_if_fail (localdb != NULL, job);

	time (&one_hour_ago);
	one_hour_ago -= 60 * 60;

	/* refresh databases if they are older than an hour */
	if (g_stat (ALPM_CACHE_PATH, &cache) < 0 ||
	    cache.st_mtime < one_hour_ago) {
		GError *error = NULL;
		/* show updates even if the databases could not be updated */
        if (!pk_backend_update_databases (job, 0, &error)) {
			g_warning ("%s", error->message);
		}
	} else {
		g_debug ("databases have been refreshed recently");
	}

	/* find outdated and replacement packages */
	syncdbs = alpm_get_syncdbs (alpm);
	for (i = alpm_db_get_pkgcache (localdb); i != NULL; i = i->next) {
		alpm_pkg_t *upgrade = alpm_pkg_find_update (i->data, syncdbs);

        if (pk_backend_cancelled (job)) {
			break;
		} else if (upgrade != NULL) {
			PkInfoEnum info;

			if (alpm_pkg_is_ignorepkg (upgrade)) {
				info = PK_INFO_ENUM_BLOCKED;
			} else if (alpm_pkg_is_syncfirst (upgrade)) {
				info = PK_INFO_ENUM_IMPORTANT;
			} else {
				info = PK_INFO_ENUM_NORMAL;
			}

			pkalpm_backend_pkg (job, upgrade, info);
		}
	}

	pk_backend_finish (job, NULL);
}

void
pk_backend_get_updates (PkBackend   *self,
                        PkBackendJob   *job,
                        PkBitfield  filters)
{
	g_return_if_fail (job != NULL);

	pkalpm_backend_run (job, PK_STATUS_ENUM_QUERY, pk_backend_get_updates_thread, NULL);
}

static void
pk_backend_refresh_cache_thread (PkBackendJob *job, GVariant* params, gpointer p)
{
	gint force;
	GError *error = NULL;

	g_assert (job != NULL);

	/* download databases even if they are older than current */
    g_variant_get (params, "(b)", &force);

    pk_backend_update_databases (job, force, &error);
    pk_backend_finish (job, error);
}

void
pk_backend_refresh_cache (PkBackend *self,
                          PkBackendJob   *job,
                          gboolean    force)
{
	g_return_if_fail (job != NULL);

	pkalpm_backend_run (job, PK_STATUS_ENUM_SETUP, pk_backend_refresh_cache_thread, NULL);
}
