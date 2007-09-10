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

#include <regex.h>
#include <string.h>
#include <math.h>

static pkgCacheFile *fileCache = NULL;
pkgSourceList *SrcList = 0;

typedef struct {
	PkBackend *backend;
} UpdateData;

typedef enum {
	SEARCH_NAME = 1,
	SEARCH_DETAILS,
	SEARCH_FILE
} SearchDepth;

struct search_task {
	PkBackend *backend;
	gchar *search;
	gchar *filter;
	SearchDepth depth;
};

struct desc_task {
	PkBackend *backend;
	PkPackageId *pi;
};

#ifdef APT_PKG_RPM
typedef pkgCache::VerFile AptCompFile;
#elif defined(APT_PKG_DEB)
typedef pkgCache::DescFile AptCompFile;
#else
#error Need either rpm or deb defined
#endif

struct ExDescFile {
	AptCompFile *Df;
	const char *verstr;
	const char *arch;
	gboolean installed;
	gboolean available;
	char *repo;
	bool NameMatch;
};


static pkgCacheFile *getCache()
{
	if (fileCache == NULL)
	{
		MMap *Map = 0;
		OpTextProgress Prog;
		if (pkgInitConfig(*_config) == false)
			pk_debug("pkginitconfig was false");
		if (pkgInitSystem(*_config, _system) == false)
			pk_debug("pkginitsystem was false");
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

// do_update_thread - Update the package lists 
// Swiped from apt-get's update mode
void *do_update_thread(gpointer data)
{
	UpdateData *ud = (UpdateData*)data;
	pkgCacheFile *Cache;
	bool Failed = false;
	bool TransientNetworkFailure = false;
	OpTextProgress Prog;
	
	/* easy as that */
	pk_backend_change_job_status(ud->backend, PK_STATUS_ENUM_REFRESH_CACHE);

	Cache = getCache();

	// Get the source list
	pkgSourceList List;
	if (List.ReadMainList() == false)
	{
		pk_backend_error_code(ud->backend, PK_ERROR_ENUM_UNKNOWN, "Failure reading lists");
		pk_backend_finished(ud->backend, PK_EXIT_ENUM_FAILED);
		return NULL;
	}

	// Lock the list directory
	FileFd Lock;
	if (_config->FindB("Debug::NoLocking", false) == false)
	{
		Lock.Fd(GetLock(_config->FindDir("Dir::State::Lists") + "lock"));
		if (_error->PendingError() == true)
		{
			_error->DumpErrors();
			pk_backend_error_code(ud->backend, PK_ERROR_ENUM_UNKNOWN, "Unable to lock the list directory");
			pk_backend_finished(ud->backend, PK_EXIT_ENUM_FAILED);
			return NULL;
		}
	}

	// Create the download object
	UpdatePercentage *Stat = new UpdatePercentage(ud->backend);
	pkgAcquire Fetcher(Stat);

	// Populate it with the source selection
	if (List.GetIndexes(&Fetcher) == false)
	{
		pk_backend_error_code(ud->backend, PK_ERROR_ENUM_UNKNOWN, "Failed to populate the source selection");
		goto do_update_clean;
	}

	// Run it
	if (Fetcher.Run() == pkgAcquire::Failed)
	{
		pk_backend_error_code(ud->backend, PK_ERROR_ENUM_UNKNOWN, "Failed to run the fetcher");
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
			pk_backend_error_code(ud->backend, PK_ERROR_ENUM_UNKNOWN, "Failed to clean out any old list files");
			goto do_update_clean;
		}
	}

	// Prepare the cache.   
	Cache = getCache();
	if (Cache->BuildCaches(Prog,false) == false)
	{
		pk_backend_error_code(ud->backend, PK_ERROR_ENUM_UNKNOWN, "Failed to prepare the cache");
		goto do_update_clean;
	}

	if (TransientNetworkFailure == true)
		pk_debug("Some index files failed to download, they have been ignored, or old ones used instead.");
	else if (Failed == true)
	{
		pk_backend_error_code(ud->backend, PK_ERROR_ENUM_UNKNOWN, "Generic Error");
		goto do_update_clean;
	}

	delete Stat;
	pk_backend_finished(ud->backend, PK_EXIT_ENUM_SUCCESS);
	return NULL;

	do_update_clean:
	delete Stat;
	pk_backend_finished(ud->backend, PK_EXIT_ENUM_FAILED);
	return NULL;
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
		pk_backend_finished(backend, PK_EXIT_ENUM_FAILED);
		return;
	}

	UpdateData *data = g_new(UpdateData, 1);
	if (data == NULL)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory for update task");
		pk_backend_finished(backend, PK_EXIT_ENUM_FAILED);
	}
	else
	{
		data->backend = backend;
		if (g_thread_create(do_update_thread, data, false, NULL) == NULL)
		{
			pk_backend_error_code(backend, PK_ERROR_ENUM_CREATE_THREAD_FAILED, "Failed to create update thread");
			pk_backend_finished(backend, PK_EXIT_ENUM_FAILED);
		}
	}
}

// LocalitySort - Sort a version list by package file locality		/*{{{*/
// ---------------------------------------------------------------------
/* */
static int LocalityCompare(const void *a, const void *b)
{
	pkgCache::VerFile *A = *(pkgCache::VerFile **)a;
	pkgCache::VerFile *B = *(pkgCache::VerFile **)b;

	if (A == 0 && B == 0)
		return 0;
	if (A == 0)
		return 1;
	if (B == 0)
		return -1;

	if (A->File == B->File)
		return A->Offset - B->Offset;
	return A->File - B->File;
}

static void LocalitySort(AptCompFile **begin,
		  unsigned long Count,size_t Size)
{
	qsort(begin,Count,Size,LocalityCompare);
}

static gboolean buildExDesc(ExDescFile *DFList, unsigned int pid, pkgCache::VerIterator V)
{
	// Find the proper version to use. 
	DFList[pid].available = false;
	if (V.end() == false)
	{
	#ifdef APT_PKG_RPM
		DFList[pid].Df = V.FileList();
	#else	
		DFList[pid].Df = V.DescriptionList().FileList();
	#endif
		DFList[pid].verstr = V.VerStr();
		DFList[pid].arch = V.Arch();
		for (pkgCache::VerFileIterator VF = V.FileList(); VF.end() == false; VF++)
		{
			// Locate the associated index files so we can derive a description
			pkgIndexFile *Indx;
			bool hasLocal = _system->FindIndex(VF.File(),Indx);
			if (SrcList->FindIndex(VF.File(),Indx) == false && !hasLocal)
			{
			   pk_debug("Cache is out of sync, can't x-ref a package file");
			   break;
			}
			gchar** items = g_strsplit_set(Indx->Describe(true).c_str()," \t",-1);
			DFList[pid].repo = g_strdup(items[1]); // should be in format like "http://ftp.nl.debian.org unstable/main Packages"
			DFList[pid].installed = hasLocal;
			g_strfreev(items);
			DFList[pid].available = true;
			if (hasLocal)
				break;
		}	 
	}
	return DFList[pid].available;
}

// get_search_thread
// Swiped from apt-cache's search mode
static void *get_search_thread(gpointer data)
{
	search_task *st = (search_task *) data;
	ExDescFile *DFList = NULL;

	pk_backend_change_job_status(st->backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates(st->backend);

	pk_debug("finding %s", st->search);
	pkgCache & pkgCache = *(getCache());
	pkgDepCache::Policy Plcy;
	// Create the text record parser
	pkgRecords Recs(pkgCache);

	// Compile the regex pattern
	regex_t *Pattern = new regex_t;
	memset(Pattern, 0, sizeof(*Pattern));
	if (regcomp(Pattern, st->search, REG_EXTENDED | REG_ICASE | REG_NOSUB) != 0)
	{
		pk_backend_error_code(st->backend, PK_ERROR_ENUM_UNKNOWN, "regex compilation error");
		pk_backend_finished(st->backend, PK_EXIT_ENUM_FAILED);
		goto search_task_cleanup;
	}

	DFList = new ExDescFile[pkgCache.HeaderP->PackageCount + 1];
	memset(DFList, 0, sizeof(*DFList) * pkgCache.HeaderP->PackageCount + 1);

	// Map versions that we want to write out onto the VerList array.
	for (pkgCache::PkgIterator P = pkgCache.PkgBegin(); P.end() == false; P++)
	{
		DFList[P->ID].NameMatch = true;
		if (regexec(Pattern, P.Name(), 0, 0, 0) == 0)
			DFList[P->ID].NameMatch &= true;
		else
			DFList[P->ID].NameMatch = false;

		// Doing names only, drop any that dont match..
		if (st->depth == SEARCH_NAME && DFList[P->ID].NameMatch == false)
			continue;

		// Find the proper version to use. 
		pkgCache::VerIterator V = Plcy.GetCandidateVer(P);
		buildExDesc(DFList, P->ID, V);
	}

	// Include all the packages that provide matching names too
	for (pkgCache::PkgIterator P = pkgCache.PkgBegin(); P.end() == false; P++)
	{
		if (DFList[P->ID].NameMatch == false)
			continue;

		for (pkgCache::PrvIterator Prv = P.ProvidesList(); Prv.end() == false; Prv++)
		{
			pkgCache::VerIterator V = Plcy.GetCandidateVer(Prv.OwnerPkg());
			if (buildExDesc(DFList, Prv.OwnerPkg()->ID, V))
				DFList[Prv.OwnerPkg()->ID].NameMatch = true;
		}
	}

	LocalitySort(&DFList->Df, pkgCache.HeaderP->PackageCount, sizeof(*DFList));

	// Iterate over all the version records and check them
	for (ExDescFile * J = DFList; J->Df != 0; J++)
	{
#ifdef APT_PKG_RPM
		pkgRecords::Parser & P = Recs.Lookup(pkgCache::VerFileIterator(pkgCache, J->Df));
#else
		pkgRecords::Parser & P = Recs.Lookup(pkgCache::DescFileIterator(pkgCache, J->Df));
#endif

		gboolean Match = true;
		if (J->NameMatch == false)
		{
			string LongDesc = P.LongDesc();
			if (regexec(Pattern, LongDesc.c_str(), 0, 0, 0) == 0)
				Match = true;
			else
				Match = false;
		}

		if (Match == true)// && pk_backend_filter_package_name(st->backend,P.Name().c_str()))
		{
			gchar *pid = pk_package_id_build(P.Name().c_str(),J->verstr,J->arch,J->repo);
			pk_backend_package(st->backend, J->installed, pid, P.ShortDesc().c_str());
			g_free(pid);
		}
	}

	pk_backend_finished(st->backend, PK_EXIT_ENUM_SUCCESS);

search_task_cleanup:
	for (ExDescFile * J = DFList; J->Df != 0; J++)
	{
		g_free(J->repo);
	}
	delete[]DFList;
	regfree(Pattern);
	g_free(st->search);
	g_free(st);

	return NULL;
}

/**
 * pk_backend_search
 **/
static void
pk_backend_search(PkBackend * backend, const gchar * filter, const gchar * search, SearchDepth which, void *(*search_thread)(gpointer data))
{
	g_return_if_fail (backend != NULL);
	search_task *data = g_new(struct search_task, 1);
	if (data == NULL)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory for search task");
		pk_backend_finished(backend, PK_EXIT_ENUM_FAILED);
	}
	else
	{
		data->backend = backend;
		data->search = g_strdup(search);
		data->filter = g_strdup(filter);
		data->depth = which;

		if (g_thread_create(search_thread, data, false, NULL) == NULL)
		{
			pk_backend_error_code(backend, PK_ERROR_ENUM_CREATE_THREAD_FAILED, "Failed to spawn thread");
			pk_backend_finished(backend, PK_EXIT_ENUM_FAILED);
		}
	}
}

static GHashTable *PackageRecord(pkgCache::VerIterator V)
{
	GHashTable *ret = NULL;
	
	pkgCache & pkgCache = *(getCache());
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

// get_description_thread
static void *get_description_thread(gpointer data)
{
	desc_task *dt = (desc_task *) data;

	pk_backend_change_job_status(dt->backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates(dt->backend);

	pk_debug("finding %s", dt->pi->name);
	pkgCache & pkgCache = *(getCache());
	pkgDepCache::Policy Plcy;

	// Map versions that we want to write out onto the VerList array.
	for (pkgCache::PkgIterator P = pkgCache.PkgBegin(); P.end() == false; P++)
	{
		if (strcmp(dt->pi->name, P.Name())!=0)
			continue;

		// Find the proper version to use. 
		pkgCache::VerIterator V = Plcy.GetCandidateVer(P);
		GHashTable *pkg = PackageRecord(V);
		pk_backend_description(dt->backend,dt->pi->name,
					PK_GROUP_ENUM_OTHER,(const gchar*)g_hash_table_lookup(pkg,"Description"),"");
		g_hash_table_unref(pkg);
	}
	pk_backend_finished(dt->backend, PK_EXIT_ENUM_SUCCESS);
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
		pk_backend_finished(backend, PK_EXIT_ENUM_FAILED);
		return;
	}

	data->backend = backend;
	data->pi = pk_package_id_new_from_string(package_id);
	if (data->pi == NULL)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_backend_finished(backend, PK_EXIT_ENUM_FAILED);
		return;
	}

	if (g_thread_create(get_description_thread, data, false, NULL) == NULL)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_CREATE_THREAD_FAILED, "Failed to spawn description thread");
		pk_backend_finished(backend, PK_EXIT_ENUM_FAILED);
	}
	return;
}

/**
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, const gchar *filter, const gchar *search)
{
	pk_backend_search(backend, filter, search, SEARCH_DETAILS, get_search_thread);
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	pk_backend_search(backend, filter, search, SEARCH_NAME, get_search_thread);
}

static void *do_search_file(gpointer data)
{
	search_task *st = (search_task*)data;
	gchar *sdir = g_path_get_dirname(_config->Find("Dir::State::status").c_str());
	gchar *ldir = g_build_filename(sdir,"info",NULL);
	g_free(sdir);
	GError *error = NULL;
	GDir *list = g_dir_open(ldir,0,&error);
	if (error!=NULL)
	{
		pk_backend_error_code(st->backend, PK_ERROR_ENUM_INTERNAL_ERROR, "can't open %s",ldir);
		g_free(ldir);
		g_error_free(error);
		pk_backend_finished(st->backend, PK_EXIT_ENUM_FAILED);
		return NULL;
	}
	const gchar * fname = NULL;
	while ((fname = g_dir_read_name(list))!=NULL)
	{
		//pk_backend_package(st->backend, J->installed, pid, P.ShortDesc().c_str());
	}
	pk_backend_error_code(st->backend, PK_ERROR_ENUM_INTERNAL_ERROR, "search file is incomplete");
	pk_backend_finished(st->backend, PK_EXIT_ENUM_FAILED);
	g_dir_close(list);
	g_free(ldir);
	//pk_backend_finished(st->backend, PK_EXIT_ENUM_SUCCESS);
	return NULL;
}

/**
 * backend_search_file:
 **/
static void backend_search_file(PkBackend *backend, const gchar *filter, const gchar *search)
{
	pk_backend_search(backend, filter, search, SEARCH_FILE, do_search_file);
}

extern "C" PK_BACKEND_OPTIONS (
	"APT Backend",				/* description */
	"0.0.1",				/* version */
	"Richard Hughes <richard@hughsie.com>",	/* author */
	NULL,					/* initalize */
	NULL,					/* destroy */
	backend_get_groups,			/* get_groups */
	backend_get_filters,			/* get_filters */
	NULL,					/* cancel_job_try */
	NULL,					/* get_depends */
	backend_get_description,		/* get_description */
	NULL,					/* get_requires */
	NULL,					/* get_updates */
	NULL,					/* install_package */
	backend_refresh_cache,			/* refresh_cache */
	NULL,					/* remove_package */
	backend_search_details,			/* search_details */
	backend_search_file,			/* search_file */
	NULL,					/* search_group */
	backend_search_name,			/* search_name */
	NULL,					/* update_package */
	NULL					/* update_system */
);

