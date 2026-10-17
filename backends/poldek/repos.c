/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Repositories are poldek sources. Several [source] sections may share a
 * name; they are one repository then, enabled if any of them is.
 */

#include <limits.h>

#include <poldek/pkgdir/source.h>
#include <poldek/poldek.h>
#include <vfile/vfile.h>

#include "query.h"
#include "repos.h"

typedef struct
{
	gchar *description;
	gboolean enabled;
} PkPoldekRepo;

static void
pk_poldek_repo_free (PkPoldekRepo *repo)
{
	g_free (repo->description);
	g_free (repo);
}

void
pk_poldek_get_repo_list (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req)
{
	g_autoptr(GHashTable) repos = g_hash_table_new_full (g_str_hash,
							     g_str_equal,
							     g_free,
							     (GDestroyNotify) pk_poldek_repo_free);
	g_autoptr(GPtrArray) order = g_ptr_array_new ();
	tn_array *sources = poldek_get_sources (pc->ctx);

	for (int i = 0; sources != NULL && i < n_array_size (sources); i++) {
		const struct source *src = n_array_nth (sources, i);
		const gchar *name = src->name != NULL ? src->name : src->path;
		PkPoldekRepo *repo;

		/* a group is an alias for other sources; poldek never sets PKGSOURCE_ISGROUP,
		 * and its bit is also an internal option flag that ends up in src->flags */
		if (g_strcmp0 (src->type, source_TYPE_GROUP) == 0 || name == NULL)
			continue;
		repo = g_hash_table_lookup (repos, name);
		if (repo == NULL) {
			char url[PATH_MAX];

			repo = g_new0 (PkPoldekRepo, 1);
			/* any user may list the repositories, the password of a URL is for poldek */
			if (src->path != NULL)
				repo->description = g_strdup (
				    vf_url_hidepasswd (url, sizeof (url), src->path));
			g_hash_table_insert (repos, g_strdup (name), repo);
			g_ptr_array_add (order, (gpointer) name);
		}
		if ((src->flags & PKGSOURCE_NOAUTO) == 0)
			repo->enabled = TRUE;
	}
	for (guint i = 0; i < order->len; i++) {
		const gchar *name = g_ptr_array_index (order, i);
		PkPoldekRepo *repo = g_hash_table_lookup (repos, name);
		json_t *item = json_pack ("{s:o, s:b}",
					  "repo_id",
					  pk_poldek_json_text (name),
					  "enabled",
					  repo->enabled);

		pk_poldek_json_set_text (item, "description", repo->description);
		pk_poldek_job_queue (job, "repo-details", item);
	}
	if (sources != NULL)
		n_array_free (sources);
}
