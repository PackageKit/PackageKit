/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 *
 * Most of this code was taken from Zif, libzif/zif-repos.c
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <pk-backend.h>

#include "hif-package.h"
#include "hif-repos.h"
#include "hif-utils.h"

/**
 * hif_repos_get_sources:
 */
GPtrArray *
hif_repos_get_sources (GKeyFile *config,
		       HifSourceScanFlags flags,
		       GError **error)
{
	const gchar *file;
	gboolean ret;
	gchar *path_tmp;
	gchar *repo_path;
	GDir *dir;
	GPtrArray *array = NULL;
	GPtrArray *sources = NULL;

	/* get the repo dir */
	repo_path = g_key_file_get_string (config,
					   HIF_CONFIG_GROUP_NAME,
					   "ReposDir", error);
	if (repo_path == NULL)
		goto out;

	/* open dir */
	dir = g_dir_open (repo_path, 0, error);
	if (dir == NULL)
		goto out;

	/* find all the .repo files */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) hif_source_free);
	while ((file = g_dir_read_name (dir)) != NULL) {
		if (!g_str_has_suffix (file, ".repo"))
			continue;
		path_tmp = g_build_filename (repo_path, file, NULL);
		ret = hif_source_parse (config, array, path_tmp, flags, error);
		g_free (path_tmp);
		if (!ret)
			goto out;
	}

	/* all okay */
	sources = g_ptr_array_ref (array);
out:
	g_free (repo_path);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (dir != NULL)
		g_dir_close (dir);
	return sources;
}

/**
 * hif_repos_get_source_by_id:
 */
HifSource *
hif_repos_get_source_by_id (GPtrArray *sources, const gchar *id, GError **error)
{
	guint i;
	HifSource *tmp;
	HifSource *src = NULL;

	g_return_val_if_fail (sources != NULL, NULL);
	g_return_val_if_fail (id != NULL, NULL);

	for (i = 0; i < sources->len; i++) {
		tmp = g_ptr_array_index (sources, i);
		if (g_strcmp0 (hif_source_get_id (tmp), id) == 0) {
			src = tmp;
			goto out;
		}
	}

	/* we didn't find anything */
	g_set_error (error,
		     HIF_ERROR,
		     PK_ERROR_ENUM_REPO_NOT_FOUND,
		     "failed to find %s", id);
out:
	return src;
}
