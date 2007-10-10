/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2007 Tom Parker <palfrey@tevp.net>
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
#include <glib/gprintf.h>

#include <math.h>
#include <string.h>

#include <pk-backend.h>
#include <pk-debug.h>
#include <pk-package-id.h>
#include "config.h"

#include <apt-pkg/configuration.h>
#include <apt-pkg/init.h>

#include "pk-backend-apt.h"
#include "sqlite-pkg-cache.h"
#include "python-backend-common.h"

static gboolean inited = FALSE;

#define APT_DB DATABASEDIR "/apt.db"

static void backend_initialize(PkBackend *backend)
{
	if (!inited)
	{
		gchar *apt_fname = NULL;
		if (pkgInitConfig(*_config) == false)
			pk_debug("pkginitconfig was false");
		if (pkgInitSystem(*_config, _system) == false)
			pk_debug("pkginitsystem was false");

		apt_fname = g_strconcat(
				_config->Find("Dir").c_str(),
				_config->Find("Dir::Cache").c_str(),
				_config->Find("Dir::Cache::pkgcache").c_str(),
				NULL);

		sqlite_init_cache(backend, APT_DB, apt_fname, apt_build_db);
		g_free(apt_fname);
		inited = TRUE;
	}
}

/**
 * backend_get_groups:
 */
static void
backend_get_groups (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);
	pk_enum_list_append_multiple (elist,
				      PK_GROUP_ENUM_ACCESSIBILITY,
				      PK_GROUP_ENUM_GAMES,
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

static gboolean backend_search_file_thread (PkBackend *backend, gpointer data)
{
	//search_task *st = (search_task*)data;
	gchar *sdir = g_path_get_dirname(_config->Find("Dir::State::status").c_str());
	gchar *ldir = g_build_filename(sdir,"info",NULL);
	g_free(sdir);
	GError *error = NULL;
	GDir *list = g_dir_open(ldir,0,&error);
	if (error!=NULL)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_INTERNAL_ERROR, "can't open %s",ldir);
		g_free(ldir);
		g_error_free(error);
		return FALSE;
	}
	const gchar * fname = NULL;
	while ((fname = g_dir_read_name(list))!=NULL)
	{
		//pk_backend_package(backend, J->installed, pid, P.ShortDesc().c_str());
	}
	pk_backend_error_code(backend, PK_ERROR_ENUM_INTERNAL_ERROR, "search file is incomplete");
	g_dir_close(list);
	g_free(ldir);
	return TRUE;
}

/**
 * backend_search_file:
 **/
static void backend_search_file(PkBackend *backend, const gchar *filter, const gchar *search)
{
	backend_search_common(backend, filter, search, SEARCH_FILE, backend_search_file_thread);
}

extern "C" PK_BACKEND_OPTIONS (
	"APT",					/* description */
	"0.0.1",				/* version */
	"Richard Hughes <richard@hughsie.com>, Tom Parker <palfrey@tevp.net>",	/* author */
	backend_initialize,			/* initalize */
	NULL,					/* destroy */
	backend_get_groups,			/* get_groups */
	backend_get_filters,			/* get_filters */
	NULL,					/* cancel */
	NULL,					/* get_depends */
	sqlite_get_description,		/* get_description */
	NULL,					/* get_requires */
	NULL,					/* get_update_detail */
	NULL,					/* get_updates */
	NULL,					/* install_package */
	NULL,					/* install_name */
	python_refresh_cache,			/* refresh_cache */
	NULL,					/* remove_package */
	NULL,					/* resolve */
	NULL,					/* rollback */
	sqlite_search_details,			/* search_details */
	backend_search_file,			/* search_file */
	NULL,					/* search_group */
	sqlite_search_name,			/* search_name */
	NULL,					/* update_package */
	NULL					/* update_system */
);

