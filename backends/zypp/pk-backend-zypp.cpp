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
/**
 * backend_install_package:
 */

static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);

	zypp::filesystem::Pathname pathname("/");
	//zypp::ZYpp zypper;

	//zypper.initializeTarget(pathname);


	/*
	if(strcmp(package_id,"signedpackage;1.0-1.fc8;i386;fedora") == 0) {
		pk_backend_repo_signature_required(backend, "updates", "http://example.com/gpgkey",
						   "Test Key (Fedora) fedora@example.com", "BB7576AC",
						   "D8CC 06C2 77EC 9C53 372F  C199 B1EE 1799 F24F 1B08",
						   "2007-10-04", PK_SIGTYPE_ENUM_GPG);
		pk_backend_error_code (backend, PK_ERROR_ENUM_GPG_FAILURE,
				       "GPG signed package could not be verified");
		pk_backend_finished (backend);
	}

	progress_percentage = 0;
	pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
			    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
			    "An HTML widget for GTK+ 2.0");
	g_timeout_add (1000, backend_install_timeout, backend);
	*/
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
	NULL,					/* resolve */
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
