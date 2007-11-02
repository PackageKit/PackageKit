/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2007 S.Çağlar Onur <caglar@pardus.org.tr>
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
#include <pk-backend-python.h>

/**
 * backend_get_groups:
 */
static void
backend_get_groups (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);
	pk_enum_list_append_multiple (elist,
				         /* PK_GROUP_ENUM_ACCESSIBILITY, */
				         PK_GROUP_ENUM_ACCESSORIES,
				         PK_GROUP_ENUM_EDUCATION,
				         PK_GROUP_ENUM_GAMES,
				         /* PK_GROUP_ENUM_GRAPHICS, */
				         PK_GROUP_ENUM_INTERNET,
				         /* PK_GROUP_ENUM_OFFICE, */
				         PK_GROUP_ENUM_OTHER,
				         PK_GROUP_ENUM_PROGRAMMING,
				         PK_GROUP_ENUM_MULTIMEDIA,
				         PK_GROUP_ENUM_SYSTEM,
				         PK_GROUP_ENUM_DESKTOPS,
				         PK_GROUP_ENUM_PUBLISHING,
				         PK_GROUP_ENUM_SERVERS,
				         PK_GROUP_ENUM_FONTS,
				         PK_GROUP_ENUM_ADMIN_TOOLS,
				         /* PK_GROUP_ENUM_LEGACY, */
				         PK_GROUP_ENUM_LOCALIZATION,
				         PK_GROUP_ENUM_VIRTUALIZATION,
				         PK_GROUP_ENUM_SECURITY,
				         PK_GROUP_ENUM_POWER_MANAGEMENT,
				         PK_GROUP_ENUM_UNKNOWN,
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
				      /* PK_FILTER_ENUM_GUI, */
				      PK_FILTER_ENUM_INSTALLED,
				      /* PK_FILTER_ENUM_DEVELOPMENT, */
				      -1);
}

PK_BACKEND_OPTIONS (
	"PiSi",						/* description */
	"S.Çağlar Onur <caglar@pardus.org.tr>",		/* author */
	NULL,						/* initalize */
	NULL,						/* destroy */
	backend_get_groups,				/* get_groups */
	backend_get_filters,				/* get_filters */
	pk_backend_python_cancel,			/* cancel */
	pk_backend_python_get_depends,			/* get_depends */
	pk_backend_python_get_description,		/* get_description */
	pk_backend_python_get_files,				/* get_files */
	pk_backend_python_get_requires,			/* get_requires */
	NULL,						/* get_update_detail */
	pk_backend_python_get_updates,			/* get_updates */
	pk_backend_python_install_package,		/* install_package */
	pk_backend_python_install_file,			/* install_file */
	pk_backend_python_refresh_cache,		/* refresh_cache */
	pk_backend_python_remove_package,		/* remove_package */
	pk_backend_python_resolve,			/* resolve */
	NULL,						/* rollback */
	pk_backend_python_search_details,		/* search_details */
	pk_backend_python_search_file,			/* search_file */
	pk_backend_python_search_group,			/* search_group */
	pk_backend_python_search_name,			/* search_name */
	pk_backend_python_update_package,		/* update_package */
	pk_backend_python_update_system,		/* update_system */
	pk_backend_python_get_repo_list,		/* get_repo_list */
	NULL,						/* repo_enable */
	pk_backend_python_repo_set_data			/* repo_set_data */
);
