/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

#include <pk-backend.h>
#include <pk-backend-spawn.h>
#include <pk-package-ids.h>

static PkBackendSpawn *spawn;

/**
 * backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_initialize (PkBackend *backend)
{
	pk_debug ("FILTER: initialize");
	spawn = pk_backend_spawn_new ();
	pk_backend_spawn_set_name (spawn, "urpmi");
}

/**
 * backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_destroy (PkBackend *backend)
{
	pk_debug ("FILTER: destroy");
	g_object_unref (spawn);
}

/**
 * pk_backend_bool_to_text:
 */
static const gchar *
pk_backend_bool_to_text (gboolean value)
{
	if (value == TRUE) {
		return "yes";
	}
	return "no";
}


/**
 * pk_backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, PkFilterEnum filters, const gchar *search)
{
	gchar *filters_text;
	filters_text = pk_filter_enums_to_text (filters);
	pk_backend_spawn_helper (spawn, "search-name.pl", filters_text, search, NULL);
	g_free (filters_text);
}

/**
 * backend_get_details:
 */
static void
backend_get_details (PkBackend *backend, const gchar *package_id)
{
	pk_backend_spawn_helper (spawn, "get-details.pl", package_id, NULL);
}

/**
 * backend_get_files:
 */
static void
backend_get_files (PkBackend *backend, const gchar *package_id)
{
	pk_backend_spawn_helper (spawn, "get-files.pl", package_id, NULL);
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, PkFilterEnum filters, const gchar *package_id, gboolean recursive)
{
	gchar *filters_text;
	filters_text = pk_filter_enums_to_text (filters);
	pk_backend_spawn_helper (spawn, "get-depends.pl", filters_text, package_id, pk_backend_bool_to_text (recursive), NULL);
	g_free (filters_text);
}


PK_BACKEND_OPTIONS (
	"URPMI",					/* description */
	"Aurelien Lefebvre <alefebvre@mandriva.com>",	/* author */
	backend_initialize,			/* initalize */
	backend_destroy,			/* destroy */
	NULL,			/* get_groups */
	NULL,			/* get_filters */
	NULL,				/* cancel */
	backend_get_depends,			/* get_depends */
	backend_get_details,			/* get_details */
	backend_get_files,			/* get_files */
	NULL,			/* get_packages */
	NULL,			/* get_repo_list */
	NULL,			/* get_requires */
	NULL,		/* get_update_detail */
	NULL,			/* get_updates */
	NULL,			/* install_files */
	NULL,		/* install_packages */
	NULL,		/* install_signature */
	NULL,			/* refresh_cache */
	NULL,		/* remove_packages */
	NULL,			/* repo_enable */
	NULL,			/* repo_set_data */
	NULL,			/* resolve */
	NULL,					/* rollback */
	NULL,			/* search_details */
	NULL,			/* search_file */
	NULL,			/* search_group */
	backend_search_name,			/* search_name */
	NULL,					/* service_pack */
	NULL,		/* update_packages */
	NULL,			/* update_system */
	NULL			/* what_provides */
);

