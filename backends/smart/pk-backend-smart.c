/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2007 James Bowes <jbowes@redhat.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <pk-backend.h>

/**
 * backend_remove_package:
 */
static void
backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps)
{
	g_return_if_fail (backend != NULL);
	const gchar *deps;
	if (allow_deps == TRUE) {
		deps = "yes";
	} else {
		deps = "no";
	}
	pk_backend_spawn_helper (backend, "remove.py", deps, package_id, NULL);
}

/**
 * backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, const gchar *filter, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "resolve.py", filter, package_id, NULL);
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "search-name.py", filter, search, NULL);
}

PK_BACKEND_OPTIONS (
	"SMART",					/* description */
	"0.0.1",					/* version */
	"James Bowes <jbowes@dangerouslyinc.com>",	/* author */
	NULL,						/* initalize */
	NULL,						/* destroy */
	NULL,						/* get_groups */
	NULL,						/* get_filters */
	NULL,						/* cancel */
	NULL,						/* get_depends */
	NULL,						/* get_description */
	NULL,						/* get_requires */
	NULL,						/* get_update_detail */
	NULL,						/* get_updates */
	NULL,						/* install_package */
	NULL,						/* install_file */
	NULL,						/* refresh_cache */
	backend_remove_package,				/* remove_package */
	backend_resolve,				/* resolve */
	NULL,						/* rollback */
	NULL,						/* search_details */
	NULL,						/* search_file */
	NULL,						/* search_group */
	backend_search_name,				/* search_name */
	NULL,						/* update_package */
	NULL,						/* update_system */
	NULL,						/* get_repo_list */
	NULL,						/* repo_enable */
	NULL						/* repo_set_data */
);
