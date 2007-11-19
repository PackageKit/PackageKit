/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Ali Sabil <ali.sabil@gmail.com>
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
#include <pk-backend-python.h>
#include "pk-apt-search.h"

/**
 * backend_get_groups:
 */
static void
backend_get_groups (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);
	pk_enum_list_append_multiple (elist,
				      PK_GROUP_ENUM_ACCESSORIES,
				      PK_GROUP_ENUM_GAMES,
				      PK_GROUP_ENUM_GRAPHICS,
				      PK_GROUP_ENUM_INTERNET,
				      PK_GROUP_ENUM_OFFICE,
				      PK_GROUP_ENUM_OTHER,
				      PK_GROUP_ENUM_PROGRAMMING,
				      PK_GROUP_ENUM_MULTIMEDIA,
				      PK_GROUP_ENUM_SYSTEM,
				      -1);
}

/**
 * backend_get_filters:
 */
static void
backend_get_filters (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);
	pk_enum_list_append_multiple (elist,
				      PK_FILTER_ENUM_GUI,
				      PK_FILTER_ENUM_INSTALLED,
				      PK_FILTER_ENUM_DEVELOPMENT,
				      -1);
}

PK_BACKEND_OPTIONS (
	"Apt",				/* description */
	"Ali Sabil <ali.sabil@gmail.com>",	/* author */
	backend_init_search,					/* initalize */
	NULL,					/* destroy */
	backend_get_groups,			/* get_groups */
	backend_get_filters,			/* get_filters */
	NULL,					/* cancel */
	NULL,					/* get_depends */
	backend_get_description,		/* get_description */
	NULL,					/* get_files */
	NULL,					/* get_requires */
	NULL,					/* get_update_detail */
	pk_backend_python_get_updates,			/* get_updates */
	pk_backend_python_install_package,		/* install_package */
	NULL,					/* install_file */
	pk_backend_python_refresh_cache,			/* refresh_cache */
	pk_backend_python_remove_package,			/* remove_package */
	pk_backend_python_resolve,					/* resolve */
	NULL,					/* rollback */
	backend_search_details,			/* search_details */
	NULL,					/* search_file */
	backend_search_group,			/* search_group */
	backend_search_name,			/* search_name */
	pk_backend_python_update_package,			/* update_package */
	pk_backend_python_update_system,			/* update_system */
	NULL,					/* get_repo_list */
	NULL,					/* repo_enable */
	NULL					/* repo_set_data */
);
