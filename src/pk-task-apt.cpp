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

#include <string.h>

#include <apt-pkg/pkgcache.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/progress.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/init.h>
#include <apt-pkg/pkgrecords.h>
#include <regex.h>

#include "pk-debug.h"
#include "pk-task.h"
#include "pk-task-common.h"
#include "pk-spawn.h"
#include "config.h"

static void pk_task_class_init(PkTaskClass * klass);
static void pk_task_init(PkTask * task);
static void pk_task_finalize(GObject * object);

#define PK_TASK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK, PkTaskPrivate))

static pkgCacheFile* getCache()
{
	static pkgCacheFile *fileCache = NULL;
	if (fileCache == NULL)
	{
		if (pkgInitConfig(*_config) == false)
			pk_debug("pkginitconfig was false");
		if (pkgInitSystem(*_config, _system) == false)
			pk_debug("pkginitsystem was false");
		fileCache = new pkgCacheFile();

		OpTextProgress Prog;
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

struct PkTaskPrivate
{
	guint progress_percentage;
};

static guint signals[PK_TASK_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE(PkTask, pk_task, G_TYPE_OBJECT)

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

	/* not implimented yet */
	return FALSE;
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

	/* not implimented yet */
	return FALSE;
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

struct search_task
{
	PkTask *task;
	gchar *search;
	guint depth;
	gboolean installed;
	gboolean available;
};

static void * do_search_task(gpointer data)
{
	search_task *st = (search_task*)data;
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
		pk_task_error_code (st->task, PK_TASK_ERROR_CODE_UNKNOWN, "regex compilation error");
		pk_task_finished (st->task, PK_TASK_EXIT_FAILED);
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
		if (st->depth==0 && DFList[P->ID].NameMatch == false)
			continue;

		// Find the proper version to use. 
		pkgCache::VerIterator V = Plcy.GetCandidateVer(P);
		if (V.end() == false)
		#ifdef APT_PKG_RPM
	 		DFList[P->ID].Df = V.FileList();
		#else	
			DFList[P->ID].Df = V.DescriptionList().FileList();
		#endif	
	}

	// Include all the packages that provide matching names too
	for (pkgCache::PkgIterator P = pkgCache.PkgBegin(); P.end() == false; P++)
	{
		if (DFList[P->ID].NameMatch == false)
			continue;

		for (pkgCache::PrvIterator Prv = P.ProvidesList(); Prv.end() == false; Prv++)
		{
			pkgCache::VerIterator V = Plcy.GetCandidateVer(Prv.OwnerPkg());
			if (V.end() == false)
			{
				#ifdef APT_PKG_RPM
				DFList[Prv.OwnerPkg()->ID].Df = V.FileList();
				#else
				DFList[Prv.OwnerPkg()->ID].Df = V.DescriptionList().FileList();
				#endif
				DFList[Prv.OwnerPkg()->ID].NameMatch = true;
			}
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

		if (Match == true && pk_task_filter_package_name(st->task,P.Name().c_str()))
			pk_task_package(st->task, true, P.Name().c_str(), P.ShortDesc().c_str());
	}

	pk_task_finished(st->task, PK_TASK_EXIT_SUCCESS);

	search_task_cleanup:
	delete[] DFList;
	regfree(Pattern);
	g_free(st->search);
	g_free(st);

	return NULL;
}

gboolean pk_task_search_name(PkTask * task, const gchar * search, guint depth, gboolean installed, gboolean available)
{
	g_return_val_if_fail(task != NULL, FALSE);
	g_return_val_if_fail(PK_IS_TASK(task), FALSE);

	if (pk_task_assign(task) == FALSE)
	{
		return FALSE;
	}

	search_task *data = g_new(struct search_task, 1);
	if (data == NULL)
	{
		pk_task_error_code (task, PK_TASK_ERROR_CODE_UNKNOWN, "can't allocate memory for search task");
		pk_task_finished (task, PK_TASK_EXIT_FAILED);
	}
	else
	{
		data->task = task;
		data->search = g_strdup(search);
		data->depth = depth;
		data->installed = installed;
		data->available = available;

		if (g_thread_create(do_search_task,data,false,NULL)==NULL)
		{
			pk_task_error_code (task, PK_TASK_ERROR_CODE_UNKNOWN, "can't spawn thread");
			pk_task_finished (task, PK_TASK_EXIT_FAILED);
		}
	}
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

	/* not implimented yet */
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

	/* not implimented yet */
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

	/* not implimented yet */
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

	/* not implimented yet */
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

	/* not implimented yet */
	return FALSE;
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

