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

#include "pk-debug.h"
#include "pk-task.h"
#include "pk-task-common.h"
#include "config.h"
#include "pk-network.h"

static void pk_task_class_init(PkTaskClass * klass);
static void pk_task_init(PkTask * task);
static void pk_task_finalize(GObject * object);

#define PK_TASK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK, PkTaskPrivate))

struct PkTaskPrivate
{
	guint progress_percentage;
	PkNetwork *network;
};

static guint signals[PK_TASK_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE(PkTask, pk_task, G_TYPE_OBJECT)

static pkgCacheFile *fileCache = NULL;
pkgSourceList *SrcList = 0;

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


/**
 * pk_task_get_actions
 **/
gchar *
pk_task_get_actions (void)
{
	gchar *actions;
	actions = pk_task_action_build (PK_TASK_ACTION_INSTALL,
				        PK_TASK_ACTION_REMOVE,
				        PK_TASK_ACTION_UPDATE,
				        PK_TASK_ACTION_GET_UPDATES,
				        PK_TASK_ACTION_REFRESH_CACHE,
				        PK_TASK_ACTION_UPDATE_SYSTEM,
				        PK_TASK_ACTION_SEARCH_NAME,
				        PK_TASK_ACTION_SEARCH_DETAILS,
				        PK_TASK_ACTION_SEARCH_GROUP,
				        PK_TASK_ACTION_SEARCH_FILE,
				        PK_TASK_ACTION_GET_DEPS,
				        PK_TASK_ACTION_GET_DESCRIPTION,
				        0);
	return actions;
}

/**
 * pk_task_get_updates:
 **/
gboolean pk_task_get_updates(PkTask * task)
{
	g_return_val_if_fail(task != NULL, FALSE);
	g_return_val_if_fail(PK_IS_TASK(task), FALSE);

	if (pk_task_assign(task) == FALSE)
	{
		return FALSE;
	}

 	pk_task_not_implemented_yet(task, "GetUpdates");
	return FALSE;
}

typedef struct
{
	PkTask *task;
} UpdateData;

class UpdatePercentage:public pkgAcquireStatus
{
	virtual bool MediaChange(string Media,string Drive)
	{
		pk_debug("PANIC!: we don't handle mediachange");
		return FALSE;
	}
	
	virtual bool Pulse(pkgAcquire *Owner)
	{
		return true;	
	}
};

// DoUpdate - Update the package lists 
// Swiped from apt-get's update mode
void *DoUpdate(gpointer data)
{
	UpdateData *ud = (UpdateData*)data;
	pkgCacheFile *Cache;
	bool Failed = false;
	bool TransientNetworkFailure = false;
	OpTextProgress Prog;
	
	/* easy as that */
	pk_task_change_job_status(ud->task, PK_TASK_STATUS_REFRESH_CACHE);

	Cache = getCache();

	// Get the source list
	pkgSourceList List;
	if (List.ReadMainList() == false)
	{
		pk_task_error_code(ud->task, PK_TASK_ERROR_CODE_UNKNOWN, "Failure reading lists");
		pk_task_finished(ud->task, PK_TASK_EXIT_FAILED);
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
			pk_task_error_code(ud->task, PK_TASK_ERROR_CODE_UNKNOWN, "Unable to lock the list directory");
			pk_task_finished(ud->task, PK_TASK_EXIT_FAILED);
			return NULL;
		}
	}

	// Create the download object
	UpdatePercentage *Stat = new UpdatePercentage();
	pkgAcquire Fetcher(Stat);

	// Populate it with the source selection
	if (List.GetIndexes(&Fetcher) == false)
	{
		pk_task_error_code(ud->task, PK_TASK_ERROR_CODE_UNKNOWN, "Generic Error");
		goto do_update_clean;
	}

	// Run it
	if (Fetcher.Run() == pkgAcquire::Failed)
	{
		pk_task_error_code(ud->task, PK_TASK_ERROR_CODE_UNKNOWN, "Generic Error");
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
		if (Fetcher.Clean(_config->FindDir("Dir::State::lists")) == false || Fetcher.Clean(_config->FindDir("Dir::State::lists") + "partial/") == false)
		{
			pk_task_error_code(ud->task, PK_TASK_ERROR_CODE_UNKNOWN, "Generic Error");
			goto do_update_clean;
		}
	}

	// Prepare the cache.   
	Cache = getCache();
	if (Cache->BuildCaches(Prog,false) == false)
	{
		pk_task_error_code(ud->task, PK_TASK_ERROR_CODE_UNKNOWN, "Generic Error");
		goto do_update_clean;
	}

	if (TransientNetworkFailure == true)
		pk_debug("Some index files failed to download, they have been ignored, or old ones used instead.");
	else if (Failed == true)
	{
		pk_task_error_code(ud->task, PK_TASK_ERROR_CODE_UNKNOWN, "Generic Error");
		goto do_update_clean;
	}

	delete Stat;
	pk_task_finished(ud->task, PK_TASK_EXIT_SUCCESS);
	return NULL;

	do_update_clean:
	delete Stat;
	pk_task_finished(ud->task, PK_TASK_EXIT_FAILED);
	return NULL;
}

/**
 * pk_task_refresh_cache:
 **/
gboolean pk_task_refresh_cache(PkTask * task, gboolean force)
{
	g_return_val_if_fail(task != NULL, FALSE);
	g_return_val_if_fail(PK_IS_TASK(task), FALSE);

	if (pk_task_assign(task) == FALSE)
	{
		return FALSE;
	}

	/* check network state */
	if (pk_network_is_online(task->priv->network) == FALSE)
	{
		pk_task_error_code(task, PK_TASK_ERROR_CODE_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_task_finished(task, PK_TASK_EXIT_FAILED);
		return TRUE;
	}

	UpdateData *data = g_new(UpdateData, 1);
	if (data == NULL)
	{
		pk_task_error_code(task, PK_TASK_ERROR_CODE_UNKNOWN, "can't allocate memory for update task");
		pk_task_finished(task, PK_TASK_EXIT_FAILED);
	}
	else
	{
		data->task = task;
		if (g_thread_create(DoUpdate,data, false, NULL) == NULL)
		{
			pk_task_error_code(task, PK_TASK_ERROR_CODE_UNKNOWN, "can't spawn update thread");
			pk_task_finished(task, PK_TASK_EXIT_FAILED);
		}
	}
	return TRUE;
}

/**
 * pk_task_update_system:
 **/
gboolean pk_task_update_system(PkTask * task)
{
	g_return_val_if_fail(task != NULL, FALSE);
	g_return_val_if_fail(PK_IS_TASK(task), FALSE);

	if (pk_task_assign(task) == FALSE)
	{
		return FALSE;
	}

	/* not implimented yet */
	return FALSE;
}

#ifdef APT_PKG_RPM
typedef pkgCache::VerFile AptCompFile;
#elif defined(APT_PKG_DEB)
typedef pkgCache::DescFile AptCompFile;
#else
#error Need either rpm or deb defined
#endif

struct ExDescFile
{
	AptCompFile *Df;
	const char *verstr;
	const char *arch;
	gboolean installed;
	gboolean available;
	char *repo;
	bool NameMatch;
};

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

typedef enum {SEARCH_NAME=1, SEARCH_DETAILS} SearchDepth;

struct search_task
{
	PkTask *task;
	gchar *search;
	gchar *filter;
	SearchDepth depth;
};

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

// do_search_task
// Swiped from apt-cache's search mode
static void *do_search_task(gpointer data)
{
	search_task *st = (search_task *) data;
	ExDescFile *DFList = NULL;

	pk_task_change_job_status(st->task, PK_TASK_STATUS_QUERY);
	pk_task_no_percentage_updates(st->task);

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
		pk_task_error_code(st->task, PK_TASK_ERROR_CODE_UNKNOWN, "regex compilation error");
		pk_task_finished(st->task, PK_TASK_EXIT_FAILED);
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

		if (Match == true)// && pk_task_filter_package_name(st->task,P.Name().c_str()))
		{
			gchar *pid = pk_task_package_ident_build(P.Name().c_str(),J->verstr,J->arch,J->repo);
			pk_task_package(st->task, J->installed, pid, P.ShortDesc().c_str());
			g_free(pid);
		}
	}

	pk_task_finished(st->task, PK_TASK_EXIT_SUCCESS);

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
 * pk_task_search
 **/
static gboolean
pk_task_search(PkTask * task, const gchar * filter, const gchar * search, SearchDepth which)
{
	g_return_val_if_fail(task != NULL, FALSE);
	g_return_val_if_fail(PK_IS_TASK(task), FALSE);

	if (pk_task_assign(task) == FALSE)
	{
		return FALSE;
	}

	if (pk_task_check_filter(filter) == FALSE)
	{
		pk_task_error_code(task, PK_TASK_ERROR_CODE_FILTER_INVALID, "filter '%s' not valid", filter);
		pk_task_finished(task, PK_TASK_EXIT_FAILED);
		return TRUE;
	}

	search_task *data = g_new(struct search_task, 1);
	if (data == NULL)
	{
		pk_task_error_code(task, PK_TASK_ERROR_CODE_UNKNOWN, "can't allocate memory for search task");
		pk_task_finished(task, PK_TASK_EXIT_FAILED);
	}
	else
	{
		data->task = task;
		data->search = g_strdup(search);
		data->filter = g_strdup(filter);
		data->depth = which;

		if (g_thread_create(do_search_task, data, false, NULL) == NULL)
		{
			pk_task_error_code(task, PK_TASK_ERROR_CODE_UNKNOWN, "can't spawn thread");
			pk_task_finished(task, PK_TASK_EXIT_FAILED);
		}
	}
	return TRUE;
}

/**
 * pk_task_search_details:
 **/
gboolean pk_task_search_details(PkTask * task, const gchar * filter, const gchar * search)
{
	return pk_task_search(task, filter, search, SEARCH_DETAILS);
}

/**
 * pk_task_search_name:
 **/
gboolean pk_task_search_name(PkTask * task, const gchar * filter, const gchar * search)
{
	return pk_task_search(task, filter, search, SEARCH_NAME);
}

/**
 * pk_task_search_group:
 **/
gboolean pk_task_search_group(PkTask * task, const gchar * filter, const gchar * search)
{
	pk_task_not_implemented_yet(task, "SearchGroup");
	return TRUE;
}

/**
 * pk_task_search_file:
 **/
gboolean pk_task_search_file(PkTask * task, const gchar * filter, const gchar * search)
{
	pk_task_not_implemented_yet(task, "SearchFile");
	return TRUE;
}

/**
 * pk_task_get_deps:
 **/
gboolean pk_task_get_deps(PkTask * task, const gchar * package)
{
	g_return_val_if_fail(task != NULL, FALSE);
	g_return_val_if_fail(PK_IS_TASK(task), FALSE);

	if (pk_task_assign(task) == FALSE)
	{
		return FALSE;
	}

 	pk_task_not_implemented_yet (task, "GetDeps");
	return FALSE;
}

/**
 * pk_task_get_description:
 **/
gboolean pk_task_get_description(PkTask * task, const gchar * package)
{
	g_return_val_if_fail(task != NULL, FALSE);
	g_return_val_if_fail(PK_IS_TASK(task), FALSE);

	if (pk_task_assign(task) == FALSE)
	{
		return FALSE;
	}

 	pk_task_not_implemented_yet (task, "GetDescription");
	return FALSE;
}

/**
 * pk_task_remove_package:
 **/
gboolean pk_task_remove_package(PkTask * task, const gchar * package, gboolean allow_deps)
{
	g_return_val_if_fail(task != NULL, FALSE);
	g_return_val_if_fail(PK_IS_TASK(task), FALSE);

	if (pk_task_assign(task) == FALSE)
	{
		return FALSE;
	}

 	pk_task_not_implemented_yet(task, "RemovePackage");
	return FALSE;
}

/**
 * pk_task_install_package:
 **/
gboolean pk_task_install_package(PkTask * task, const gchar * package)
{
	g_return_val_if_fail(task != NULL, FALSE);
	g_return_val_if_fail(PK_IS_TASK(task), FALSE);

	if (pk_task_assign(task) == FALSE)
	{
		return FALSE;
	}

 	pk_task_not_implemented_yet(task, "InstallPackage");
	return FALSE;
}

/**
 * pk_task_cancel_job_try:
 **/
gboolean pk_task_cancel_job_try(PkTask * task)
{
	g_return_val_if_fail(task != NULL, FALSE);
	g_return_val_if_fail(PK_IS_TASK(task), FALSE);

	/* check to see if we have an action */
	if (task->assigned == FALSE)
	{
		pk_warning("Not assigned");
		return FALSE;
	}

 	pk_task_not_implemented_yet (task, "CancelJobTry");
	return FALSE;
}

/**
 * pk_task_update_package:
 **/
gboolean
pk_task_update_package (PkTask *task, const gchar *package_id)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	pk_task_not_implemented_yet (task, "UpdatePackage");
	return TRUE;
}

/**
 * pk_task_class_init:
 **/
static void pk_task_class_init(PkTaskClass * klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = pk_task_finalize;
	pk_task_setup_signals(object_class, signals);
	g_type_class_add_private(klass, sizeof(PkTaskPrivate));
}

/**
 * pk_task_init:
 **/
static void pk_task_init(PkTask * task)
{
	task->priv = PK_TASK_GET_PRIVATE(task);
	task->priv->network = pk_network_new();
	task->signals = signals;
	pk_task_clear(task);
}

/**
 * pk_task_finalize:
 **/
static void pk_task_finalize(GObject * object)
{
	PkTask *task;
	g_return_if_fail(object != NULL);
	g_return_if_fail(PK_IS_TASK(object));
	task = PK_TASK(object);
	g_return_if_fail(task->priv != NULL);
	g_free(task->package);
	g_object_unref(task->priv->network);
	G_OBJECT_CLASS(pk_task_parent_class)->finalize(object);
}

/**
 * pk_task_new:
 **/
PkTask *pk_task_new(void)
{
	PkTask *task;
	task = (PkTask *) g_object_new(PK_TYPE_TASK, NULL);
	return PK_TASK(task);
}

