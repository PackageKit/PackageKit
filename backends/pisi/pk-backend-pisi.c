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

PK_BACKEND_OPTIONS (
	"PiSi",						/* description */
	"0.0.1",					/* version */
	"S.Çağlar Onur <caglar@pardus.org.tr>",		/* author */
	NULL,						/* initalize */
	NULL,						/* destroy */
	NULL,						/* get_groups */
	NULL,						/* get_filters */
	NULL,						/* cancel */
	pk_backend_python_get_depends,			/* get_depends */
	pk_backend_python_get_description,		/* get_description */
	pk_backend_python_get_requires,			/* get_requires */
	NULL,						/* get_update_detail */
	pk_backend_python_get_updates,			/* get_updates */
	pk_backend_python_install_package,		/* install_package */
	pk_backend_python_install_file,			/* install_file */
	pk_backend_python_refresh_cache,		/* refresh_cache */
	pk_backend_python_remove_package,		/* remove_package */
	pk_backend_python_resolve,			/* resolve */
	NULL,						/* rollback */
	NULL,						/* search_details */
	NULL,						/* search_file */
	NULL,						/* search_group */
	NULL,						/* search_name */
	pk_backend_python_update_package,		/* update_package */
	pk_backend_python_update_system,		/* update_system */
	pk_backend_python_get_repo_list,		/* get_repo_list */
	NULL,						/* repo_enable */
	NULL						/* repo_set_data */
);
