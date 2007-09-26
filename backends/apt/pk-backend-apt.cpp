/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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
#include <sqlite3.h>

static pkgCacheFile *fileCache = NULL;
static pkgSourceList *SrcList = 0;
static gboolean inited = FALSE;
static sqlite3 *db = NULL;

#define APT_DB DATABASEDIR "/apt.db"

typedef enum {
	SEARCH_NAME = 1,
	SEARCH_DETAILS,
	SEARCH_FILE
} SearchDepth;

struct search_task {
	gchar *search;
	gchar *filter;
	SearchDepth depth;
};

struct desc_task {
	PkPackageId *pi;
};

#ifdef APT_PKG_RPM
typedef pkgCache::VerFile AptCompFile;
#elif defined(APT_PKG_DEB)
typedef pkgCache::DescFile AptCompFile;
#else
#error Need either rpm or deb defined
#endif

typedef enum {FIELD_PKG=1,FIELD_VER,FIELD_DEPS,FIELD_ARCH,FIELD_SHORT,FIELD_LONG,FIELD_REPO} Fields;

static void build_db(PkBackend * backend)
{
	GMatchInfo *match_info;
	GError *error = NULL;
	gchar *contents = NULL;
	gchar *sdir;
	const gchar *fname;
	GRegex *origin, *suite, *version, *description;
	GDir *dir;
	GHashTable *releases;

	pk_backend_change_status(backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates(backend);

	sdir = g_build_filename(_config->Find("Dir").c_str(),_config->Find("Dir::State").c_str(),_config->Find("Dir::State::lists").c_str(), NULL);
	dir = g_dir_open(sdir,0,&error);
	if (error!=NULL)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_INTERNAL_ERROR, "can't open %s",dir);
		g_error_free(error);
		goto search_task_cleanup;
	}

	origin = g_regex_new("^Origin: (\\S+)",(GRegexCompileFlags)(G_REGEX_CASELESS|G_REGEX_OPTIMIZE|G_REGEX_MULTILINE),(GRegexMatchFlags)0,NULL);
	suite = g_regex_new("^Suite: (\\S+)",(GRegexCompileFlags)(G_REGEX_CASELESS|G_REGEX_OPTIMIZE|G_REGEX_MULTILINE),(GRegexMatchFlags)0,NULL);

	version = g_regex_new("^Version: (.*)",(GRegexCompileFlags)(G_REGEX_CASELESS|G_REGEX_OPTIMIZE|G_REGEX_MULTILINE),(GRegexMatchFlags)0,NULL);
	description = g_regex_new("^Description: (.*)",(GRegexCompileFlags)(G_REGEX_CASELESS|G_REGEX_OPTIMIZE|G_REGEX_MULTILINE),(GRegexMatchFlags)0,NULL);

	releases = g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
	while ((fname = g_dir_read_name(dir))!=NULL)
	{
		gchar *temp, *parsed_name;
		gchar** items = g_strsplit(fname,"_",-1);
		guint len = g_strv_length(items);
		if(len<=3) // minimum is <source>_<type>_<group>
		{
			g_strfreev(items);
			continue;
		}
		
		/* warning: nasty hack with g_strjoinv */
		temp = items[len-2];
		items[len-2] = NULL;
		parsed_name = g_strjoinv("_",items);
		items[len-2] = temp;
		
		if (g_ascii_strcasecmp(items[len-1],"Release")==0 && g_ascii_strcasecmp(items[len-2],"source")!=0)
		{
			gchar * repo = NULL, *fullname;
			fullname = g_build_filename(sdir,fname,NULL);
			if (g_file_get_contents(fullname,&contents,NULL,NULL) == FALSE)
			{
				pk_backend_error_code(backend, PK_ERROR_ENUM_INTERNAL_ERROR, "error loading %s",fullname);
				goto search_task_cleanup;
			}
			g_free(fullname);

			g_regex_match (origin, contents, (GRegexMatchFlags)0, &match_info);
			if (!g_match_info_matches(match_info))
			{
				pk_backend_error_code(backend, PK_ERROR_ENUM_INTERNAL_ERROR, "origin regex failure in %s",fname);
				goto search_task_cleanup;
			}
			repo = g_match_info_fetch (match_info, 1);

			g_regex_match (suite, contents, (GRegexMatchFlags)0, &match_info);
			if (g_match_info_matches(match_info))
			{
				temp = g_strconcat(repo,"/",g_match_info_fetch (match_info, 1),NULL);
				g_free(repo);
				repo = temp;
			}

			temp = parsed_name;
			parsed_name = g_strconcat(temp,"_",items[len-2],NULL);
			g_free(temp);

			pk_debug("type is %s, group is %s, parsed_name is %s",items[len-2],items[len-1],parsed_name);

			g_hash_table_insert(releases, parsed_name, repo);
			g_free(contents);
			contents = NULL;
		}
		else
			g_free(parsed_name);
		g_strfreev(items);
	}
	g_dir_close(dir);

	/* and then we need to do this again, but this time we're looking for the packages */
	dir = g_dir_open(sdir,0,&error);
	while ((fname = g_dir_read_name(dir))!=NULL)
	{
		gchar** items = g_strsplit(fname,"_",-1);
		guint len = g_strv_length(items);
		if(len<=3) // minimum is <source>_<type>_<group>
		{
			g_strfreev(items);
			continue;
		}

		if (g_ascii_strcasecmp(items[len-1],"Packages")==0)
		{
			const gchar *repo;
			gchar *temp, *parsed_name;
			gchar *fullname;
			/* warning: nasty hack with g_strjoinv */
			if (g_str_has_prefix(items[len-2],"binary-"))
			{
				temp = items[len-3];
				items[len-3] = NULL;
				parsed_name = g_strjoinv("_",items);
				items[len-3] = temp;
			}
			else
			{
				temp = items[len-1];
				items[len-1] = NULL;
				parsed_name = g_strjoinv("_",items);
				items[len-1] = temp;
			}
			
			pk_debug("type is %s, group is %s, parsed_name is %s",items[len-2],items[len-1],parsed_name);
			
			repo = (const gchar *)g_hash_table_lookup(releases,parsed_name);
			if (repo == NULL)
			{
				pk_debug("Can't find repo for %s, marking as \"unknown\"",parsed_name);
				repo = g_strdup("unknown");
				//g_assert(0);
			}
			else
				pk_debug("repo for %s is %s",parsed_name,repo);
			g_free(parsed_name);

			fullname = g_build_filename(sdir,fname,NULL);
			pk_debug("loading %s",fullname);
			if (g_file_get_contents(fullname,&contents,NULL,NULL) == FALSE)
			{
				pk_backend_error_code(backend, PK_ERROR_ENUM_INTERNAL_ERROR, "error loading %s",fullname);
				goto search_task_cleanup;
			}
			gchar *begin = contents, *next;
			glong count = 0;

			sqlite3_stmt *package = NULL;
			int res;
			res = sqlite3_prepare_v2(db, "insert or replace into packages values (?,?,?,?,?,?,?)", -1, &package, NULL);
			if (res!=SQLITE_OK)
				pk_error("sqlite error during insert prepare: %s", sqlite3_errmsg(db));
			res = sqlite3_bind_text(package,FIELD_REPO,repo,-1,SQLITE_STATIC);
			if (res!=SQLITE_OK)
				pk_error("sqlite error during repo bind: %s", sqlite3_errmsg(db));

			gboolean haspk = FALSE;

			sqlite3_exec(db,"begin",NULL,NULL,NULL);

			while (true)
			{
				next = strstr(begin,"\n");
				if (next!=NULL)
				{
					next[0] = '\0';
					next++;
				}

				if (begin[0]=='\0')
				{
					if (haspk)
					{
						res = sqlite3_step(package);
						if (res!=SQLITE_DONE)
							pk_error("sqlite error during step: %s", sqlite3_errmsg(db));
						sqlite3_reset(package);
						//pk_debug("added package");
						haspk = FALSE;
					}
					//g_assert(0);
				}
				else if (begin[0]==' ')
				{
					/*gchar *oldval = g_strdup((const gchar*)g_hash_table_lookup(ret,"Description"));
					g_hash_table_insert(ret,g_strdup("Description"),g_strconcat(oldval, "\n",parts[1],NULL));
					//pk_debug("new entry =  '%s'",(const gchar*)g_hash_table_lookup(ret,"Description"));
					g_free(oldval);*/
				}
				else
				{
					gchar *colon = strchr(begin,':');
					g_assert(colon!=NULL);
					colon[0] = '\0';
					colon+=2;
					/*if (strlen(colon)>3000)
						pk_error("strlen(colon) = %d\ncolon = %s",strlen(colon),colon);*/
					//typedef enum {FIELD_PKG=0,FIELD_VER,FIELD_DEPS,FIELD_ARCH,FIELD_SHORT,FIELD_LONG,FIELD_REPO} Fields;
					//pk_debug("entry = '%s','%s'",begin,colon);
					if (begin[0] == 'P' && g_strcasecmp("Package",begin)==0)
					{
						res=sqlite3_bind_text(package,FIELD_PKG,colon,-1,SQLITE_STATIC);
						haspk = TRUE;
						count++;
						if (count%1000==0)
							pk_debug("Package %ld (%s)",count,colon);
					}
					else if (begin[0] == 'V' && g_strcasecmp("Version",begin)==0)
						res=sqlite3_bind_text(package,FIELD_VER,colon,-1,SQLITE_STATIC);
					else if (begin[0] == 'D' && g_strcasecmp("Depends",begin)==0)
						res=sqlite3_bind_text(package,FIELD_DEPS,colon,-1,SQLITE_STATIC);
					else if (begin[0] == 'A' && g_strcasecmp("Architecture",begin)==0)
						res=sqlite3_bind_text(package,FIELD_ARCH,colon,-1,SQLITE_STATIC);
					else if (begin[0] == 'D' && g_strcasecmp("Description",begin)==0)
						res=sqlite3_bind_text(package,FIELD_SHORT,colon,-1,SQLITE_STATIC);
					if (res!=SQLITE_OK)
						pk_error("sqlite error during %s bind: %s", begin, sqlite3_errmsg(db));
				}
				if (next == NULL)
					break;
				begin = next;	
			}
			sqlite3_exec(db,"commit",NULL,NULL,NULL);
			g_free(contents);
			contents = NULL;
		}
	}

search_task_cleanup:
	g_dir_close(dir);
	g_free(sdir);
	g_free(contents);
}

static void init(PkBackend *backend)
{
	if (!inited)
	{
		gint ret;
		char *errmsg = NULL;
		if (pkgInitConfig(*_config) == false)
			pk_debug("pkginitconfig was false");
		if (pkgInitSystem(*_config, _system) == false)
			pk_debug("pkginitsystem was false");
		ret = sqlite3_open (APT_DB, &db);
		ret = sqlite3_exec(db,"PRAGMA synchronous = OFF",NULL,NULL,NULL);
		g_assert(ret == SQLITE_OK);
		//sqlite3_exec(db,"create table packages (name text, version text, deps text, arch text, short_desc text, long_desc text, repo string, primary key(name,version,arch,repo))",NULL,NULL,&errmsg);
		sqlite3_exec(db,"create table packages (name text, version text, deps text, arch text, short_desc text, long_desc text, repo string)",NULL,NULL,&errmsg);
		if (errmsg == NULL) // success, ergo didn't exist
		{
			build_db(backend);
		}
		else
		{
			sqlite3_free(errmsg);
			/*ret = sqlite3_exec(db,"delete from packages",NULL,NULL,NULL); // clear it!
			g_assert(ret == SQLITE_OK);
			pk_debug("wiped db");*/
		}
		inited = TRUE;
	}
}

static pkgCacheFile *getCache(PkBackend *backend)
{
	if (fileCache == NULL)
	{
		MMap *Map = 0;
		OpTextProgress Prog;
		init(backend);
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

class UpdatePercentage:public pkgAcquireStatus
{
	double old;
	PkBackend *backend;

	public:
	UpdatePercentage(PkBackend *tk)
	{
		old = -1;
		backend = tk;
	}

	virtual bool MediaChange(string Media,string Drive)
	{
		pk_debug("PANIC!: we don't handle mediachange");
		return FALSE;
	}

	virtual bool Pulse(pkgAcquire *Owner)
	{
		pkgAcquireStatus::Pulse(Owner);
		double percent = double(CurrentBytes*100.0)/double(TotalBytes);
		if (old!=percent)
		{
			pk_backend_change_percentage(backend,(guint)percent);
			pk_backend_change_sub_percentage(backend,((guint)(percent*100.0))%100);
			old = percent;
		}
		return true;
	}
};

// backend_refresh_cache_thread - Update the package lists
// Swiped from apt-get's update mode
static gboolean backend_refresh_cache_thread (PkBackend *backend, gpointer data)
{
	pkgCacheFile *Cache;
	bool Failed = false;
	bool TransientNetworkFailure = false;
	OpTextProgress Prog;

	/* easy as that */
	pk_backend_change_status(backend, PK_STATUS_ENUM_REFRESH_CACHE);

	Cache = getCache(backend);

	// Get the source list
	pkgSourceList List;
	if (List.ReadMainList() == false)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_UNKNOWN, "Failure reading lists");
		return FALSE;
	}

	// Lock the list directory
	FileFd Lock;
	if (_config->FindB("Debug::NoLocking", false) == false)
	{
		Lock.Fd(GetLock(_config->FindDir("Dir::State::Lists") + "lock"));
		if (_error->PendingError() == true)
		{
			_error->DumpErrors();
			pk_backend_error_code(backend, PK_ERROR_ENUM_UNKNOWN, "Unable to lock the list directory");
			return FALSE;
		}
	}

	// Create the download object
	UpdatePercentage *Stat = new UpdatePercentage(backend);
	pkgAcquire Fetcher(Stat);

	// Populate it with the source selection
	if (List.GetIndexes(&Fetcher) == false)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_UNKNOWN, "Failed to populate the source selection");
		goto do_update_clean;
	}

	// Run it
	if (Fetcher.Run() == pkgAcquire::Failed)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_UNKNOWN, "Failed to run the fetcher");
		goto do_update_clean;
	}

	for (pkgAcquire::ItemIterator I = Fetcher.ItemsBegin(); I != Fetcher.ItemsEnd(); I++)
	{
		if ((*I)->Status == pkgAcquire::Item::StatDone)
			continue;

		(*I)->Finished();

		fprintf(stderr, "Failed to fetch %s  %s\n", (*I)->DescURI().c_str(), (*I)->ErrorText.c_str());

		if ((*I)->Status == pkgAcquire::Item::StatTransientNetworkError)
		{
			TransientNetworkFailure = true;
			continue;
		}

		Failed = true;
	}

	// Clean out any old list files
	if (!TransientNetworkFailure && _config->FindB("APT::Get::List-Cleanup", true) == true)
	{
		if (Fetcher.Clean(_config->FindDir("Dir::State::lists")) == false ||
		    Fetcher.Clean(_config->FindDir("Dir::State::lists") + "partial/") == false)
		{
			pk_backend_error_code(backend, PK_ERROR_ENUM_UNKNOWN, "Failed to clean out any old list files");
			goto do_update_clean;
		}
	}

	// Prepare the cache.
	Cache = getCache(backend);
	if (Cache->BuildCaches(Prog,false) == false)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_UNKNOWN, "Failed to prepare the cache");
		goto do_update_clean;
	}

	if (TransientNetworkFailure == true)
		pk_debug("Some index files failed to download, they have been ignored, or old ones used instead.");
	else if (Failed == true)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_UNKNOWN, "Generic Error");
		goto do_update_clean;
	}

	delete Stat;
	return TRUE;

	do_update_clean:
	delete Stat;
	return FALSE;
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

	pk_backend_thread_helper(backend, backend_refresh_cache_thread, NULL);
}

// backend_search_packages_thread
// Swiped from apt-cache's search mode
static gboolean backend_search_packages_thread (PkBackend *backend, gpointer data)
{
	search_task *st = (search_task *) data;
	int res;

	init(backend);
	pk_backend_change_status(backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates(backend);

	pk_debug("finding %s", st->search);

	sqlite3_stmt *package = NULL;
	g_strdelimit(st->search," ",'%');
	gchar *sel = g_strdup_printf("select name,version,arch,repo,short_desc from packages where name like '%%%s%%'",st->search);
	pk_debug("statement is '%s'",sel);
	res = sqlite3_prepare_v2(db,sel, -1, &package, NULL);
	g_free(sel);
	if (res!=SQLITE_OK)
		pk_error("sqlite error during select prepare: %s", sqlite3_errmsg(db));
	res = sqlite3_step(package);
	while (res == SQLITE_ROW)
	{
		gchar *pid = pk_package_id_build((const gchar*)sqlite3_column_text(package,0),
				(const gchar*)sqlite3_column_text(package,1),
				(const gchar*)sqlite3_column_text(package,2),
				(const gchar*)sqlite3_column_text(package,3));
		pk_backend_package(backend, FALSE, pid, (const gchar*)sqlite3_column_text(package,4));
		g_free(pid);
		if (res==SQLITE_ROW)
			res = sqlite3_step(package);
	}
	if (res!=SQLITE_DONE)
	{
		pk_debug("sqlite error during step (%d): %s", res, sqlite3_errmsg(db));
		g_assert(0);
	}

	g_free(st->search);
	g_free(st);

	return TRUE;
}

/**
 * backend_search_common
 **/
static void
backend_search_common(PkBackend * backend, const gchar * filter, const gchar * search, SearchDepth which, PkBackendThreadFunc func)
{
	g_return_if_fail (backend != NULL);
	search_task *data = g_new(struct search_task, 1);
	if (data == NULL)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory for search task");
		pk_backend_finished(backend);
	}
	else
	{
		data->search = g_strdup(search);
		data->filter = g_strdup(filter);
		data->depth = which;
		pk_backend_thread_helper (backend, func, data);
	}
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

/**
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, const gchar *filter, const gchar *search)
{
	backend_search_common(backend, filter, search, SEARCH_DETAILS, backend_search_packages_thread);
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	backend_search_common(backend, filter, search, SEARCH_NAME, backend_search_packages_thread);
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
	"Richard Hughes <richard@hughsie.com>",	/* author */
	NULL,					/* initalize */
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
	backend_search_details,			/* search_details */
	backend_search_file,			/* search_file */
	NULL,					/* search_group */
	backend_search_name,			/* search_name */
	NULL,					/* update_package */
	NULL					/* update_system */
);

