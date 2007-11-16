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
#include <unistd.h>
#include <pk-debug.h>

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
#include <zypp/RepoInfo.h>
#include <zypp/repo/RepoException.h>
#include <zypp/parser/ParseException.h>
#include <zypp/Pathname.h>
#include <sqlite3.h>

enum PkgSearchType {
	SEARCH_TYPE_NAME = 0,
	SEARCH_TYPE_DETAILS = 1,
	SEARCH_TYPE_FILE = 2,
	SEARCH_TYPE_RESOLVE = 3
};

typedef struct {
	gchar *search;
	gchar *filter;
	gint mode;
} FindData;

typedef struct {
	gchar *name;
	gchar *filter;
} ResolveData;

/* make sure and keep the struct in sync with the enum */
enum SqlQuerySchema {
	SQL_NAME = 0,
	SQL_VERSION,
	SQL_RELEASE,
	SQL_REPO,
	SQL_ARCH
};

typedef struct {
	gchar *name;
	gchar *version;
	gchar *release;
	gchar *repo;
	gchar *arch;
} SQLData;

typedef struct {
	gchar *package_id;
	gint type;
} ThreadData;

typedef struct {
	gboolean force;
} RefreshData;

// some typedefs and functions to shorten Zypp names
typedef zypp::ResPoolProxy ZyppPool;
inline ZyppPool zyppPool() { return zypp::getZYpp()->poolProxy(); }
typedef zypp::ui::Selectable::Ptr ZyppSelectable;
typedef zypp::ui::Selectable*		ZyppSelectablePtr;
typedef zypp::ResObject::constPtr	ZyppObject;
typedef zypp::Package::constPtr		ZyppPackage;
typedef zypp::Patch::constPtr		ZyppPatch;
typedef zypp::Pattern::constPtr		ZyppPattern;
typedef zypp::Language::constPtr	ZyppLanguage;
inline ZyppPackage tryCastToZyppPkg (ZyppObject obj)
	{ return zypp::dynamic_pointer_cast <const zypp::Package> (obj); }

/**
 * Initialize Zypp (Factory method)
 */
static zypp::ZYpp::Ptr
get_zypp ()
{
	static gboolean initialized = FALSE;
	zypp::ZYpp::Ptr zypp = NULL;

	zypp = zypp::ZYppFactory::instance ().getZYpp ();
	
	// TODO: Make this threadsafe
	if (initialized == FALSE) {
		zypp::filesystem::Pathname pathname("/");
		zypp->initializeTarget (pathname);

		initialized = TRUE;
	}

	return zypp;
}

static gboolean
backend_get_description_thread (PkBackend *backend, gpointer data)
{
	PkPackageId *pi;
	ThreadData *d = (ThreadData*) data;

	pi = pk_package_id_new_from_string (d->package_id);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_package_id_free (pi);
		g_free (d->package_id);
		g_free (d);
		return FALSE;
	}

	pk_backend_change_status (backend, PK_STATUS_ENUM_QUERY);

	// FIXME: Call libzypp here to get the "Selectable"
	try
	{
		zypp::RepoManager manager;
		//zypp::Repository repository(manager.createFromCach(repo));
	}
	catch ( const zypp::Exception &e)
	{
	}

	pk_backend_description (backend,
				pi->name,		// package_id
				"unknown",		// const gchar *license
				PK_GROUP_ENUM_OTHER,	// PkGroupEnum group
				"FIXME: put package description here",	// const gchar *description
				"FIXME: add package URL here",			// const gchar *url
				0,			// gulong size
				"FIXME: put package filelist here");			// const gchar *filelist

	pk_package_id_free (pi);
	g_free (d->package_id);
	g_free (d);

	return TRUE;
}

/**
 * backend_get_description:
 */
static void
backend_get_description (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);

	ThreadData *data = g_new0(ThreadData, 1);
	if (data == NULL) {
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory in backend_get_description");
		pk_backend_finished (backend);
	} else {
		data->package_id = g_strdup(package_id);
		pk_backend_thread_helper (backend, backend_get_description_thread, data);
	}
}

static gboolean
backend_install_package_thread (PkBackend *backend, gpointer data)
{
	gchar *package_id = (gchar *)data;

	pk_backend_change_status (backend, PK_STATUS_ENUM_QUERY);

	PkPackageId *pi = pk_package_id_new_from_string (package_id);
        if (pi == NULL) {
                pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
                g_free (package_id);

                return FALSE;
        }

	zypp::Target_Ptr target;

	zypp::ZYpp::Ptr zypp;
	zypp = get_zypp ();

	target = zypp->target ();

	// Load all the local system "resolvables" (packages)
	zypp->addResolvables (target->resolvables(), TRUE);

	// Load resolvables from all the enabled repositories
	pk_backend_change_status (backend, PK_STATUS_ENUM_WAIT);
	zypp::RepoManager manager;
	std::list <zypp::RepoInfo> repos;
	try
	{
		// TODO: Split the code up so it's not all just in one bit try/catch
		repos = manager.knownRepositories();
		for (std::list <zypp::RepoInfo>::iterator it = repos.begin(); it != repos.end(); it++) {
			zypp::Repository repository = manager.createFromCache (*it);
			zypp->addResolvables (repository.resolvables ());
		}

		// Iterate over the resolvables and mark the ones we want to install
		//zypp->start ();
		for (zypp::ResPoolProxy::const_iterator it = zypp->poolProxy().byKindBegin <zypp::Package>();
				it != zypp->poolProxy().byKindEnd <zypp::Package>(); it++) {
			zypp::ui::Selectable::Ptr selectable = *it;
			if (strcmp (selectable->name().c_str(), pi->name) == 0) {
				switch (selectable->status ()) {
					case zypp::ui::S_Update:	// Have installedObj
					case zypp::ui::S_NoInst:	// No installedObj
						break;
					default:
						continue;
						break;
				}

				// This package matches the name we're looking for and
				// is available for update/install.
				zypp::ResObject::constPtr installable = selectable->candidateObj();
				const char *edition_str = installable->edition().asString().c_str();

				if (strcmp (edition_str, pi->version) == 0) {
printf ("WOOT!  Marking the package to be installed!\n");
					// this is the one, mark it to be installed
					selectable->set_status (zypp::ui::S_Install);
				}
			}
		}

printf ("Resolving dependencies...\n");
		// Gather up any dependencies
		pk_backend_change_status (backend, PK_STATUS_ENUM_DEP_RESOLVE);
		if (zypp->resolver ()->resolvePool () == FALSE) {
			// Manual intervention required to resolve dependencies
			// TODO: Figure out what we need to do with PackageKit
			// to pull off interactive problem solving.
			pk_backend_error_code (backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "Couldn't resolve the package dependencies.");
			g_free (package_id);
			pk_package_id_free (pi);
			return FALSE;
		}

printf ("Performing installation...\n");
		// Perform the installation
		pk_backend_change_status (backend, PK_STATUS_ENUM_COMMIT);
		// TODO: If this were an update, you should use PK_INFO_ENUM_UPDATING instead
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLING, package_id, "TODO: Put the package summary here");
		zypp::ZYppCommitPolicy policy;
		policy.restrictToMedia (0);	// 0 - install all packages regardless to media
		zypp::ZYppCommitResult result = zypp->commit (policy);
printf ("Finished the installation.\n");

		// TODO: Check result for success
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLED, package_id, "TODO: Put the package summary here");
	} catch (const zypp::repo::RepoNotFoundException &ex) {
		// TODO: make sure this dumps out the right sring.
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str() );
		g_free (package_id);
		pk_package_id_free (pi);
		return FALSE;
	} catch (const zypp::Exception &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Error enumerating repositories");
		g_free (package_id);
		pk_package_id_free (pi);
		return FALSE;
	}

	g_free (package_id);
	pk_package_id_free (pi);
	return TRUE;
}

/**
 * backend_install_package:
 */
static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);

	printf("package_id is %s\n", package_id);
	gchar *package_to_install = g_strdup (package_id);
	pk_backend_thread_helper (backend, backend_install_package_thread, package_to_install);
}

static int
select_callback (void* data,int argc ,char** argv, char** cl_name)
{
	printf ("Enter select_callback\n");

	SQLData *sql_data = (SQLData *) data;
	/* FIXME - for now we prefer later versions, i586 */
	if (sql_data->name == NULL ||
			(g_ascii_strcasecmp (argv[SQL_VERSION], sql_data->version) > 0) ||
			!g_ascii_strcasecmp ("i586", argv[SQL_ARCH]) ) {
		printf ("Adding to data struct\n");
		g_free (sql_data->name);
		g_free (sql_data->version);
		g_free (sql_data->release);
		g_free (sql_data->repo);
		g_free (sql_data->arch);
		sql_data->name = g_strdup (argv[SQL_NAME]);
		sql_data->version = g_strdup (argv[SQL_VERSION]);
		sql_data->release = g_strdup (argv[SQL_RELEASE]);
		sql_data->repo = g_strdup (argv[SQL_REPO]);
		sql_data->arch = g_strdup (argv[SQL_ARCH]);
	}
	// /*
	for (int i = 0; i < argc; i++) {
		printf ("%s=%s\n", cl_name[i], argv[i] ? argv[i] : "null");
	}
	// */
	return 0;
}

static gboolean
backend_resolve_thread (PkBackend *backend, gpointer data)
{
	char * error_string;
	const char * select_statement_template = "SELECT p.name,p.version,p.release,r.alias,t.name FROM resolvables p JOIN repositories r ON p.repository_id = r.id JOIN types t ON p.arch = t.id WHERE p.name LIKE \"%s\"";
	gchar *select_statement;
	gchar *package_id;
	gchar *full_version;
	SQLData *sql_data = g_new0(SQLData, 1);
							  

	printf("\n\nEnter backend_resolve_thread\n");
	ResolveData *rdata = (ResolveData*) data;

	sqlite3 *db;
	if (sqlite3_open("/var/cache/zypp/zypp.db", &db) != 0) {
		pk_backend_error_code(backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Failed to open database");
		return FALSE;
	}

	select_statement = g_strdup_printf (select_statement_template, rdata->name);
	sqlite3_exec (db, select_statement, select_callback, sql_data, &error_string);

	if (sql_data->name == NULL) {
		//did not get any matches
		return FALSE;
	}
	full_version = g_strconcat (sql_data->version, "-", sql_data->release, NULL);
	package_id = pk_package_id_build(sql_data->name, full_version, sql_data->arch, sql_data->repo);
	printf("about to return package_id of:%s\n", package_id);
	//FIXME - return real PK_INFO_ENUM_*
	pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
				package_id, "description generated by zypp backend");

	g_free (rdata->name);
	g_free (rdata->filter);
	g_free (rdata);
	g_free (full_version);
	g_free (package_id);
	g_free (select_statement);
	return TRUE;
}

/**
 * backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, const gchar *filter, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	printf("Enter backend_resolve - filter:%s, package_id:%s\n", filter, package_id);
	ResolveData *data = g_new0(ResolveData, 1);
	if (data == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory in backend_resolve");
		pk_backend_finished (backend);
	} else {
		data->name = g_strdup (package_id);
		data->filter = g_strdup (filter);
		pk_backend_thread_helper (backend, backend_resolve_thread, data);
	}
}

/*
static gboolean
backend_refresh_cache_thread (PkBackend *backend, gpointer data)
{
	RefreshData *d = (RefreshData*) data;
	pk_backend_change_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
	pk_backend_no_percentage_updates (backend);
	unsigned processed = 0;
	unsigned repo_count = 0;

	zypp::RepoManager manager;
	std::list <zypp::RepoInfo> repos;
	try
	{
		repos = manager.knownRepositories();
	}
	catch ( const zypp::Exception &e)
	{
		// FIXME: make sure this dumps out the right sring.
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, e.asUserString().c_str() );
		g_free (d);
		return FALSE;
	}

	repo_count = repos.size ();

	for (std::list <zypp::RepoInfo>::iterator it = repos.begin(); it != repos.end(); it++) {
		if (it->enabled()) {
			//refresh_raw_metadata (it, false);

			// Build the Cache
			try {
				manager.buildCache (*it,
						    d->force ?
							zypp::RepoManager::BuildForced :
							zypp::RepoManager::BuildIfNeeded);
			} catch (const zypp::parser::ParseException &ex) {
				pk_backend_error_code (backend,
						       PK_ERROR_ENUM_INTERNAL_ERROR,
						       "Error parsing metadata for '%s'",
						       it->alias().c_str());
				continue;
			}

			processed++;
			pk_backend_change_percentage (backend, (int) ((processed / repo_count) * 100));
		}
	}

	g_free (d);
	pk_backend_finished (backend);

	return TRUE;
}

//
// backend_refresh_cache:
//
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (backend != NULL);
	// check network state
	if (pk_backend_network_is_online (backend) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_finished (backend);
		return;
	}

	RefreshData *data = g_new0(RefreshData, 1);
	if (data == NULL) {
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory in backend_refresh_cache");
		pk_backend_finished (backend);
	} else {
		data->force = force;
		pk_backend_thread_helper (backend, backend_refresh_cache_thread, data);
	}
}
*/

/* TODO: this was taken directly from pk-backend-box.c.  Perhaps this
 * ought to be part of libpackagekit? */
static void
parse_filter (const gchar *filter, gboolean *installed, gboolean *available,
	      gboolean *devel, gboolean *nondevel, gboolean *gui, gboolean *text)
{
	gchar **sections = NULL;
	gint i = 0;

	*installed = TRUE;
	*available = TRUE;
	*devel = TRUE;
	*nondevel = TRUE;
	*gui = TRUE;
	*text = TRUE;

	sections = g_strsplit (filter, ";", 0);
	while (sections[i]) {
		if (strcmp(sections[i], "installed") == 0)
			*available = FALSE;
		if (strcmp(sections[i], "~installed") == 0)
			*installed = FALSE;
		if (strcmp(sections[i], "devel") == 0)
			*nondevel = FALSE;
		if (strcmp(sections[i], "~devel") == 0)
			*devel = FALSE;
		if (strcmp(sections[i], "gui") == 0)
			*text = FALSE;
		if (strcmp(sections[i], "~gui") == 0)
			*gui = FALSE;
		i++;
	}
	g_strfreev (sections);
}

static void
find_packages_real (PkBackend *backend, const gchar *search, const gchar *filter, gint mode)
{
	//GList *list = NULL;
	gboolean installed;
	gboolean available;
	gboolean devel;
	gboolean nondevel;
	gboolean gui;
	gboolean text;

	g_return_if_fail (backend != NULL);

	pk_backend_change_status (backend, PK_STATUS_ENUM_QUERY);

	parse_filter (filter, &installed, &available, &devel, &nondevel, &gui, &text);

	pk_backend_no_percentage_updates (backend);

/*
	if (mode == SEARCH_TYPE_FILE) {
		if (installed == FALSE && available == FALSE) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_UNKNOWN, "invalid search mode");
		} else	{
			list = box_db_repos_search_file_with_filter (db, search, search_filter);
			add_packages_from_list (backend, list, FALSE);
			box_db_repos_package_list_free (list);
		}
	} else if (mode == SEARCH_TYPE_RESOLVE) {
		list = box_db_repos_packages_search_one (db, (gchar *)search);
		add_packages_from_list (backend, list, FALSE);
		box_db_repos_package_list_free (list);
	} else {
		if (installed == FALSE && available == FALSE) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_UNKNOWN, "invalid search mode");
		} else	{
			if (installed == TRUE && available == TRUE) {
				list = box_db_repos_packages_search_all(db, (gchar *)search, search_filter);
			} else if (installed == TRUE) {
				list = box_db_repos_packages_search_installed(db, (gchar *)search, search_filter);
			} else if (available == TRUE) {
				list = box_db_repos_packages_search_available(db, (gchar *)search, search_filter);
			}
			add_packages_from_list (backend, list, FALSE);
			box_db_repos_package_list_free (list);
		}
	}
*/
}

static gboolean
backend_find_packages_thread (PkBackend *backend, gpointer data)
{
	FindData *d = (FindData*) data;

	g_return_val_if_fail (backend != NULL, FALSE);

	find_packages_real (backend, d->search, d->filter, d->mode);

	g_free(d->search);
	g_free(d->filter);
	g_free(d);

	return TRUE;
}

static void
find_packages (PkBackend *backend, const gchar *search, const gchar *filter, gint mode)
{
	FindData *data = g_new0(FindData, 1);

	g_return_if_fail (backend != NULL);

	if (data == NULL) {
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory");
		pk_backend_finished (backend);
	} else {
		data->search = g_strdup(search);
		data->filter = g_strdup(filter);
		data->mode = mode;
		pk_backend_thread_helper (backend, backend_find_packages_thread, data);
	}
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	find_packages (backend, search, filter, SEARCH_TYPE_NAME);
}

/**
 * backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);

	pk_backend_change_status (backend, PK_STATUS_ENUM_QUERY);

	zypp::RepoManager manager;
	std::list <zypp::RepoInfo> repos;
	try
	{
		repos = manager.knownRepositories();
	}
	catch ( const zypp::Exception &e)
	{
		// FIXME: make sure this dumps out the right sring.
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, e.asUserString().c_str() );
		pk_backend_finished (backend);
		return;
	}

	for (std::list <zypp::RepoInfo>::iterator it = repos.begin(); it != repos.end(); it++) {
		// RepoInfo::alias - Unique identifier for this source.
		// RepoInfo::name - Short label or description of the
		// repository, to be used on the user interface
		pk_backend_repo_detail (backend,
					it->alias().c_str(),
					it->name().c_str(),
					it->enabled());
	}

	pk_backend_finished (backend);
}

/**
 * backend_repo_enable:
 */
static void
backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
	g_return_if_fail (backend != NULL);

	zypp::RepoManager manager;
	zypp::RepoInfo repo;
	
	try {
		repo = manager.getRepositoryInfo (rid);
		repo.setEnabled (enabled);
		manager.modifyRepository (rid, repo);
	} catch (const zypp::repo::RepoNotFoundException &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "Couldn't find the specified repository");
		pk_backend_finished (backend);
		return;
	} catch (const zypp::Exception &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Could not enable/disable the repo");
	}

	pk_backend_finished (backend);
}

extern "C" PK_BACKEND_OPTIONS (
	"Zypp",					/* description */
	"Boyd Timothy <btimothy@gmail.com>, Scott Reeves <sreeves@novell.com>",	/* author */
	NULL,					/* initalize */
	NULL,					/* destroy */
	NULL,					/* get_groups */
	NULL,					/* get_filters */
	NULL,					/* cancel */
	NULL,					/* get_depends */
	backend_get_description,		/* get_description */
	NULL,					/* get_files */
	NULL,					/* get_requires */
	NULL,					/* get_update_detail */
	NULL,					/* get_updates */
	backend_install_package,		/* install_package */
	NULL,					/* install_file */
	NULL,//backend_refresh_cache,			/* refresh_cache */
	NULL,					/* remove_package */
	backend_resolve,		/* resolve */
	NULL,					/* rollback */
	NULL,					/* search_details */
	NULL,					/* search_file */
	NULL,					/* search_group */
	backend_search_name,			/* search_name */
	NULL,					/* update_package */
	NULL,					/* update_system */
	backend_get_repo_list,			/* get_repo_list */
	backend_repo_enable,			/* repo_enable */
	NULL					/* repo_set_data */
);
