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

typedef struct {
	gchar *package_id;
	gint type;
} ThreadData;

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
	} catch (...) { // FIXME: Don't just catch all exceptions
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "Couldn't find the specified repository");
		pk_backend_finished (backend);
		return;
	}

	// FIXME: Do we need to check for errors when calling repo.setEnabled ()?
	repo.setEnabled (enabled);

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
	NULL,					/* install_package */
	NULL,					/* install_file */
	NULL,					/* refresh_cache */
	NULL,					/* remove_package */
	NULL,					/* resolve */
	NULL,					/* rollback */
	NULL,					/* search_details */
	NULL,					/* search_file */
	NULL,					/* search_group */
	NULL,					/* search_name */
	NULL,					/* update_package */
	NULL,					/* update_system */
	backend_get_repo_list,			/* get_repo_list */
	backend_repo_enable,			/* repo_enable */
	NULL					/* repo_set_data */
);
