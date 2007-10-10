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
#include <string.h>
#include <pk-backend.h>
#include <pk-debug.h>
#include <pk-package-id.h>
#include "config.h"

#include <apt-pkg/pkgcachegen.h>
#include <apt-pkg/pkgcache.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/progress.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/init.h>
#include <apt-pkg/pkgrecords.h>
#include <apt-pkg/sourcelist.h>
#include <apt-pkg/error.h>
#include <apt-pkg/acquire.h>
#include <apt-pkg/acquire-item.h>

#include <string.h>
#include <math.h>

#include "pk-backend-apt.h"
#include "sqlite-pkg-cache.h"

static pkgCacheFile *fileCache = NULL;
static pkgSourceList *SrcList = 0;
static gboolean inited = FALSE;

#define APT_DB DATABASEDIR "/apt.db"

struct desc_task {
	PkPackageId *pi;
};

static void backend_initialize(PkBackend *backend)
{
	if (!inited)
	{
		if (pkgInitConfig(*_config) == false)
			pk_debug("pkginitconfig was false");
		if (pkgInitSystem(*_config, _system) == false)
			pk_debug("pkginitsystem was false");
		init_sqlite_cache(backend, APT_DB, apt_build_db);
		inited = TRUE;
	}
}

static pkgCacheFile *getCache(PkBackend *backend)
{
	if (fileCache == NULL)
	{
		MMap *Map = 0;
		OpTextProgress Prog;
		// Open the cache file
		SrcList = new pkgSourceList;
		SrcList->ReadMainList();

		// Generate it and map it
		pkgMakeStatusCache(*SrcList, Prog, &Map, true);

		fileCache = new pkgCacheFile();

		if (fileCache->Open(Prog, FALSE) == FALSE)
		{
			pk_debug("I need more privelges");
			fileCache->Close();
			fileCache = NULL;
		}
		else
			pk_debug("cache inited");
	}
	return fileCache;
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

/**
 * backend_refresh_cache:
 **/
static void backend_refresh_cache(PkBackend * backend, gboolean force)
{
	/* check network state */
	if (pk_backend_network_is_online(backend) == FALSE)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_finished(backend);
		return;
	}

	pk_backend_spawn_helper (backend, "refresh-cache.py", NULL);
}

static GHashTable *PackageRecord(PkBackend *backend, pkgCache::VerIterator V)
{
	GHashTable *ret = NULL;

	pkgCache & pkgCache = *(getCache(backend));
	// Find an appropriate file
	pkgCache::VerFileIterator Vf = V.FileList();
	for (; Vf.end() == false; Vf++)
	{
		if ((Vf.File()->Flags & pkgCache::Flag::NotSource) == 0)
			break;
		if (Vf.end() == true)
			Vf = V.FileList();
	}

	// Check and load the package list file
	pkgCache::PkgFileIterator I = Vf.File();
	if (I.IsOk() == false)
		return NULL;

	FileFd PkgF(I.FileName(),FileFd::ReadOnly);
	if (_error->PendingError() == true)
		return NULL;

	// Read the record
	char *Buffer = new char[pkgCache.HeaderP->MaxVerFileSize+1];
	Buffer[V.FileList()->Size] = '\0';
	if (PkgF.Seek(V.FileList()->Offset) == false ||
		 PkgF.Read(Buffer,V.FileList()->Size) == false)
	{
		delete [] Buffer;
		return NULL;
	}
	//pk_debug("buffer: '%s'\n",Buffer);
	ret = g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
	gchar ** lines = g_strsplit(Buffer,"\n",-1);
	guint i;
	for (i=0;i<g_strv_length(lines);i++)
	{
		gchar ** parts = g_strsplit_set(lines[i],": ",2);
		if (g_strv_length(parts)>1)
		{
			//pk_debug("entry =  '%s' : '%s'",parts[0],parts[1]);
			if (parts[0][0]=='\0')
			{
				gchar *oldval = g_strdup((const gchar*)g_hash_table_lookup(ret,"Description"));
				g_hash_table_insert(ret,g_strdup("Description"),g_strconcat(oldval, "\n",parts[1],NULL));
				//pk_debug("new entry =  '%s'",(const gchar*)g_hash_table_lookup(ret,"Description"));
				g_free(oldval);
			}
			else
				g_hash_table_insert(ret,g_strdup(parts[0]),g_strdup(parts[1]));
		}
		g_strfreev(parts);
	}
	g_strfreev(lines);
	return ret;

}

// backend_get_description_thread
static gboolean backend_get_description_thread (PkBackend *backend, gpointer data)
{
	desc_task *dt = (desc_task *) data;

	pk_backend_change_status(backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates(backend);

	pk_debug("finding %s", dt->pi->name);
	pkgCache & pkgCache = *(getCache(backend));
	pkgDepCache::Policy Plcy;

	// Map versions that we want to write out onto the VerList array.
	for (pkgCache::PkgIterator P = pkgCache.PkgBegin(); P.end() == false; P++)
	{
		if (strcmp(dt->pi->name, P.Name())!=0)
			continue;

		// Find the proper version to use.
		pkgCache::VerIterator V = Plcy.GetCandidateVer(P);
		GHashTable *pkg = PackageRecord(backend,V);
		pk_backend_description(backend,dt->pi->name,
			"unknown", PK_GROUP_ENUM_OTHER,(const gchar*)g_hash_table_lookup(pkg,"Description"),"");
		g_hash_table_unref(pkg);
	}
	return NULL;
}

/**
 * backend_get_description:
 */
static void
backend_get_description (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	desc_task *data = g_new(struct desc_task, 1);
	if (data == NULL)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory for search task");
		pk_backend_finished(backend);
		return;
	}

	data->pi = pk_package_id_new_from_string(package_id);
	if (data->pi == NULL)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_backend_finished(backend);
		return;
	}

	pk_backend_thread_helper (backend, backend_get_description_thread, data);
	return;
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
	backend_get_description,		/* get_description */
	NULL,					/* get_requires */
	NULL,					/* get_update_detail */
	NULL,					/* get_updates */
	NULL,					/* install_package */
	NULL,					/* install_name */
	backend_refresh_cache,			/* refresh_cache */
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

