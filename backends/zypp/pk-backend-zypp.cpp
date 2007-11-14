/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (c) 2007 Novell, Inc.
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

#include <zypp/ZYppFactory.h>
#include <zypp/ResObject.h>
#include <zypp/ResPoolProxy.h>
#include <zypp/ui/Selectable.h>
#include <zypp/Patch.h>
#include <zypp/Selection.h>
#include <zypp/Package.h>
#include <zypp/Pattern.h>
#include <zypp/Language.h>
#include <zypp/Product.h>
#include <zypp/Repository.h>
#include <zypp/RepoManager.h>

/**
 * backend_get_groups:
 */
/*
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
*/

/**
 * backend_get_filters:
 */
/*
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
*/

/**
 * backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);

	zypp::RepoManager manager;
	std::list <zypp::RepoInfo> repos = manager.knownRepositories();
	for (std::list <zypp::RepoInfo>::iterator it = repos.begin(); it != repos.end(); it++) {
		pk_backend_repo_detail (backend,
					it->name().c_str(),
					it->alias().c_str(),
					it->enabled());
	}

	pk_backend_finished (backend);
}

extern "C" PK_BACKEND_OPTIONS (
	"Zypp",					/* description */
	"Boyd Timothy <btimothy@gmail.com>, Scott Reeves <sreeves@novell.com>",	/* author */
	NULL,					/* initalize */
	NULL,					/* destroy */
	NULL,			/* get_groups */
	NULL,			/* get_filters */
	NULL,					/* cancel */
	NULL,					/* get_depends */
	NULL,		/* get_description */
	NULL,					/* get_files */
	NULL,					/* get_requires */
	NULL,					/* get_update_detail */
	NULL,			/* get_updates */
	NULL,		/* install_package */
	NULL,					/* install_file */
	NULL,			/* refresh_cache */
	NULL,			/* remove_package */
	NULL,					/* resolve */
	NULL,					/* rollback */
	NULL,			/* search_details */
	NULL,					/* search_file */
	NULL,			/* search_group */
	NULL,			/* search_name */
	NULL,			/* update_package */
	NULL,			/* update_system */
	backend_get_repo_list,					/* get_repo_list */
	NULL,					/* repo_enable */
	NULL					/* repo_set_data */
);
