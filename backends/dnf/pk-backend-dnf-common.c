/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
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

#include <config.h>

#include <gmodule.h>
#include <glib.h>
#include <appstream-glib.h>
#include <libdnf/libdnf.h>

#include "pk-shared.h"
#include "pk-backend-dnf-common.h"

gboolean
pk_backend_setup_dnf_context (DnfContext *context, GKeyFile *conf, const gchar *release_ver, GError **error)
{
	const gchar * const *repo_dirs;
	const gchar * const *var_dirs;
	gboolean keep_cache;
	g_autofree gchar *cache_dir = NULL;
	g_autofree gchar *destdir = NULL;
	g_autofree gchar *lock_dir = NULL;
	g_autofree gchar *solv_dir = NULL;

	destdir = g_key_file_get_string (conf, "Daemon", "DestDir", NULL);
	if (destdir == NULL)
		destdir = g_strdup ("/");
	dnf_context_set_install_root (context, destdir);
	cache_dir = g_build_filename (destdir, "/var/cache/PackageKit", release_ver, "metadata", NULL);
	dnf_context_set_cache_dir (context, cache_dir);
	solv_dir = g_build_filename (destdir, "/var/cache/PackageKit", release_ver, "hawkey", NULL);
	dnf_context_set_solv_dir (context, solv_dir);
	lock_dir = g_build_filename (destdir, "/var/run", NULL);
	dnf_context_set_lock_dir (context, lock_dir);
	dnf_context_set_rpm_verbosity (context, "info");

	/* Add prefix to repo directories */
	repo_dirs = dnf_context_get_repos_dir (context);
	if (repo_dirs != NULL && repo_dirs[0] != NULL) {
		g_auto(GStrv) full_repo_dirs = NULL;
		guint len = g_strv_length ((gchar **)repo_dirs);
		full_repo_dirs = g_new0 (gchar*, len + 1);
		for (guint i = 0; i < len; i++)
			full_repo_dirs[i] = g_build_filename (destdir, repo_dirs[i], NULL);
		dnf_context_set_repos_dir (context, (const gchar * const*)full_repo_dirs);
	}

	/* Add prefix to var directories */
	var_dirs = dnf_context_get_vars_dir (context);
	if (var_dirs != NULL && var_dirs[0] != NULL) {
		g_auto(GStrv) full_var_dirs = NULL;
		guint len = g_strv_length ((gchar **)var_dirs);
		full_var_dirs = g_new0 (gchar*, len + 1);
		for (guint i = 0; i < len; i++)
			full_var_dirs[i] = g_build_filename (destdir, var_dirs[i], NULL);
		dnf_context_set_vars_dir (context, (const gchar * const*)full_var_dirs);
	}

	/* use this initial data if repos are not present */
	dnf_context_set_vendor_cache_dir (context, "/usr/share/PackageKit/metadata");
	dnf_context_set_vendor_solv_dir (context, "/usr/share/PackageKit/hawkey");

	/* do we keep downloaded packages */
	keep_cache = g_key_file_get_boolean (conf, "Daemon", "KeepCache", NULL);
	dnf_context_set_keep_cache (context, keep_cache);

	/* set up context */
	return dnf_context_setup (context, NULL, error);
}

gboolean
dnf_utils_refresh_repo_appstream (DnfRepo *repo, GError **error)
{
	const gchar *as_basenames[] = { "appstream", "appstream-icons", NULL };
	for (guint i = 0; as_basenames[i] != NULL; i++) {
		const gchar *tmp = dnf_repo_get_filename_md (repo, as_basenames[i]);
		if (tmp != NULL) {
#if AS_CHECK_VERSION(0,3,4)
			if (!as_utils_install_filename (AS_UTILS_LOCATION_CACHE,
							tmp,
							dnf_repo_get_id (repo),
							NULL,
							error)) {
				return FALSE;
			}
#else
			g_warning ("need to install AppStream metadata %s", tmp);
#endif
		}
	}
	return TRUE;
}
