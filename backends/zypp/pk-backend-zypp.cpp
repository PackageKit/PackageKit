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
#include <pk-backend.h>
#include <pk-backend-thread.h>
#include <unistd.h>
#include <pk-debug.h>
#include <string>
#include <set>

#include <zypp/ZYppFactory.h>
#include <zypp/ResObject.h>
#include <zypp/ResPoolProxy.h>
#include <zypp/ui/Selectable.h>
#include <zypp/Patch.h>
#include <zypp/Package.h>
#include <zypp/Pattern.h>
#include <zypp/Product.h>
#include <zypp/Repository.h>
#include <zypp/RepoManager.h>
#include <zypp/RepoInfo.h>
#include <zypp/repo/RepoException.h>
#include <zypp/parser/ParseException.h>
#include <zypp/Pathname.h>
#include <zypp/RelCompare.h>
#include <zypp/ResFilters.h>
#include <zypp/base/Algorithm.h>
#include <zypp/target/rpm/RpmDb.h>
#include <zypp/target/rpm/RpmHeader.h>
#include <zypp/target/rpm/RpmException.h>

#include <zypp/sat/Solvable.h>

#include <map>
#include <list>

#include "zypp-utils.h"
#include "zypp-events.h"

enum PkgSearchType {
	SEARCH_TYPE_NAME = 0,
	SEARCH_TYPE_DETAILS = 1,
	SEARCH_TYPE_FILE = 2,
	SEARCH_TYPE_RESOLVE = 3
};

enum DepsType {
	DEPS_TYPE_DEPENDS = 0,
	DEPS_TYPE_REQUIRES = 1
};

enum DepsBehavior {
	DEPS_ALLOW = 0,
	DEPS_NO_ALLOW = 1
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

typedef struct {
	gchar *package_id;
	gint type;
} ThreadData;

typedef struct {
	gboolean force;
} RefreshData;

typedef struct {
	gchar *package_id;
	gint deps_behavior;
} RemovePackageData;

/**
 * A map to keep track of the EventDirector objects for
 * each zypp backend that is created.
 */
static std::map<PkBackend *, EventDirector *> _eventDirectors;

static PkBackendThread *thread;

/**
 * backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_initialize (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	fprintf (stderr, "\n\n*** zypp_backend_initialize ***\n\n");
	EventDirector *eventDirector = new EventDirector (backend);
	_eventDirectors [backend] = eventDirector;

	thread = pk_backend_thread_new ();
}

/**
 * backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_destroy (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
fprintf (stderr, "\n\n*** zypp_backend_destroy ***\n\n");
	EventDirector *eventDirector = _eventDirectors [backend];
	if (eventDirector != NULL) {
		delete (eventDirector);
		_eventDirectors.erase (backend);
	}

	g_object_unref (thread);
}

/**
 * backend_get_groups:
 */
static void
backend_get_groups (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);
	pk_enum_list_append_multiple (elist,
                                      PK_GROUP_ENUM_GAMES,
                                      PK_GROUP_ENUM_GRAPHICS,
                                      PK_GROUP_ENUM_OFFICE,
                                      PK_GROUP_ENUM_PROGRAMMING,
                                      PK_GROUP_ENUM_MULTIMEDIA,
                                      PK_GROUP_ENUM_SYSTEM,
                                      PK_GROUP_ENUM_DESKTOP_GNOME,
                                      PK_GROUP_ENUM_DESKTOP_KDE,
                                      PK_GROUP_ENUM_DESKTOP_XFCE,
                                      PK_GROUP_ENUM_DESKTOP_OTHER,
                                      PK_GROUP_ENUM_PUBLISHING,
                                      PK_GROUP_ENUM_ADMIN_TOOLS,
                                      PK_GROUP_ENUM_LOCALIZATION,
                                      PK_GROUP_ENUM_SECURITY,
                                      PK_GROUP_ENUM_EDUCATION,
                                      PK_GROUP_ENUM_COMMUNICATION,
                                      PK_GROUP_ENUM_NETWORK,
                                      PK_GROUP_ENUM_UNKNOWN,
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
				      PK_FILTER_ENUM_INSTALLED,
				      -1);
}

static gboolean
backend_get_depends_thread (PkBackendThread *thread, gpointer data)
{
	PkBackend *backend;
	backend = pk_backend_thread_get_backend (thread);

	ThreadData *d = (ThreadData*) data;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	PkPackageId *pi = pk_package_id_new_from_string (d->package_id);
        if (pi == NULL) {
                pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		g_free (d->package_id);
		g_free (d);
		pk_backend_finished (backend);
                return FALSE;
        }

	zypp::ZYpp::Ptr zypp;
	zypp = get_zypp ();

	try
	{
                pk_backend_set_percentage (backend, 20);
	        // Load resolvables from all the enabled repositories
                zypp::ResPool pool = zypp_build_pool(true);

		zypp::PoolItem pool_item;
		gboolean pool_item_found = FALSE;
		// Iterate over the resolvables and mark the one we want to check its dependencies
		for (zypp::ResPool::byIdent_iterator it = pool.byIdentBegin (zypp::ResKind::package, pi->name);
				it != pool.byIdentEnd (zypp::ResKind::package, pi->name); it++) {
			zypp::PoolItem selectable = *it;
			if (strcmp (selectable->name().c_str(), pi->name) == 0) {
				// This package matches the name we're looking
				const char *edition_str = selectable->edition ().asString ().c_str();
		                pk_backend_set_percentage (backend, 20);

				if (strcmp (edition_str, pi->version) == 0) {
					// this is the one, mark it to be installed
                                        fprintf (stderr, "\n\n *** marked a package!!! ***\n\n");
					pool_item = selectable;
					pool_item_found = TRUE;
					break; // Found it, get out of the for loop
				}
			}
		}

		pk_backend_set_percentage (backend, 40);

		if (pool_item_found == FALSE) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "Did not find the specified package.");
			g_free (d->package_id);
			g_free (d);
			pk_backend_finished (backend);
        	        return FALSE;
		}

		// Gather up any dependencies
		pk_backend_set_status (backend, PK_STATUS_ENUM_DEP_RESOLVE);
	        
		pk_backend_set_percentage (backend, 60);

                // get dependencies

                zypp::sat::Solvable solvable = pool_item.satSolvable ();
                zypp::Capabilities req = solvable[zypp::Dep::REQUIRES];
                
                std::map<std::string, zypp::sat::Solvable> caps;
                std::vector<std::string> temp;

		for (zypp::Capabilities::const_iterator it = req.begin ();
				it != req.end ();
				++it) {
                        zypp::sat::WhatProvides provider (*it);
                        for (zypp::sat::WhatProvides::const_iterator it2 = provider.begin ();
                            it2 != provider.end ();
                            it2++) {

                                // Adding Packages only one time
                                std::map<std::string, zypp::sat::Solvable>::iterator mIt;
                                mIt = caps.find(it->asString ());
                                if ( std::find (temp.begin (), temp.end(), it2->name ()) == temp.end()){
                                        if( mIt != caps.end()){
                                                if(it2->isSystem ()){
                                                        caps.erase (mIt);
                                                        caps[it->asString ()] = *it2;
                                                }                       
                                        }else{
                                                caps[it->asString ()] = *it2;
                                        }
                                        temp.push_back(it2->name ());
                                }
                              
                        }
                }
                // print dependencies
                
                for (std::map<std::string, zypp::sat::Solvable>::iterator it = caps.begin ();
                    it != caps.end();
                    it++) {

                        gchar *package_id;
                        package_id = pk_package_id_build ( it->second.name ().c_str(),
                                                           it->second.edition ().asString ().c_str(),
                                                           it->second.arch ().c_str(),
                                                           "opensuse");
                        
                        zypp::PoolItem item = zypp::ResPool::instance ().find (it->second);

                        if (it->second.isSystem ()) {
                                pk_backend_package (backend,
                                                    PK_INFO_ENUM_INSTALLED,
                                                    package_id,
                                                    item->description ().c_str());
                        }else{
                                pk_backend_package (backend,
                                                    PK_INFO_ENUM_AVAILABLE,
                                                    package_id,
                                                    "");
                        }
                        g_free (package_id);
                }
                  
		pk_backend_set_percentage (backend, 100);
	} catch (const zypp::repo::RepoNotFoundException &ex) {
		// TODO: make sure this dumps out the right sring.
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str() );
		g_free (d->package_id);
		g_free (d);
		pk_package_id_free (pi);
		pk_backend_finished (backend);
		return FALSE;
	} catch (const zypp::Exception &ex) {
		//pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Error enumerating repositories");
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str() );
		g_free (d->package_id);
		g_free (d);
		pk_package_id_free (pi);
		pk_backend_finished (backend);
		return FALSE;
	}

	g_free (d->package_id);
	g_free (d);
	pk_backend_finished (backend);
	return TRUE;
}


/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, const gchar *package_id, gboolean recursive)
{
	g_return_if_fail (backend != NULL);
	ThreadData *data = g_new0(ThreadData, 1);
	if (data == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory");
		pk_backend_finished (backend);
		return;
	}

	data->package_id = g_strdup (package_id);
	data->type = DEPS_TYPE_DEPENDS;
	pk_backend_thread_create (thread, backend_get_depends_thread, data);
}

static gboolean
backend_get_description_thread (PkBackendThread *thread, gpointer data)
{
	PkBackend *backend;
	PkPackageId *pi;
	ThreadData *d = (ThreadData*) data;

	backend = pk_backend_thread_get_backend (thread);
	pi = pk_package_id_new_from_string (d->package_id);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_package_id_free (pi);
		g_free (d->package_id);
		g_free (d);
		pk_backend_finished (backend);
		return FALSE;
	}
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	std::vector<zypp::PoolItem> *v;
	v = zypp_get_packages_by_name ((const gchar *)pi->name, TRUE);

	zypp::ResObject::constPtr package;
	for (std::vector<zypp::PoolItem>::iterator it = v->begin ();
			it != v->end (); it++) {
		zypp::ResObject::constPtr pkg = (*it);
		const char *version = pkg->edition ().asString ().c_str ();
                fprintf (stderr, "\n\n *** comparing versions '%s' == '%s'", pi->version, version);
		if (strcmp (pi->version, version) == 0) {
			package = pkg;
			break;
		}
	}

	delete (v);

	if (package == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "couldn't find package");
		pk_package_id_free (pi);
		g_free (d->package_id);
		g_free (d);
		pk_backend_finished (backend);
		return FALSE;
	}
        
        try {
                PkGroupEnum group = zypp_get_group (package);

                // currently it is necessary to access the rpmDB directly to get infos like size for already installed packages
                if (package->isSystem ()){
                        zypp::Target_Ptr target;

                        zypp::ZYpp::Ptr zypp;
                        zypp = get_zypp ();

                        target = zypp->target ();
  
                        zypp::target::rpm::RpmDb &rpm = target->rpmDb (); 
                        rpm.initDatabase();
                        zypp::target::rpm::RpmHeader::constPtr rpmHeader;
                        rpm.getData (package-> name (), package->edition (), rpmHeader);

	                pk_backend_description (backend,
			                	d->package_id,                  		// package_id
				                rpmHeader->tag_license ().c_str (),		// const gchar *license
				                group,                                  	// PkGroupEnum group
				                rpmHeader->tag_description ().c_str (),   	// const gchar *description
				                rpmHeader->tag_url (). c_str (),	  	// const gchar *url
				                (gulong)rpmHeader->tag_size ());		// gulong size

                        rpm.closeDatabase();
                }else{
                        zypp::Package::constPtr pkg = zypp::asKind<zypp::Package>(package);
                        pk_backend_description (backend,
                                                d->package_id,
                                                pkg->license ().c_str (),
                                                group,
                                                pkg->description ().c_str (),
                                                pkg->url ().c_str (),
                                                (gulong)pkg->size ());
                }

        } catch (const zypp::target::rpm::RpmException &ex) {
	        pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "Couldn't open rpm-database");
                pk_backend_finished (backend);
		g_free (d->package_id);
                g_free (d);
                return FALSE;
        } catch (const zypp::Exception &ex) {
                pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString ().c_str ());
                pk_backend_finished (backend);
                g_free (d->package_id);
                g_free (d);
                return FALSE;
        }

	pk_package_id_free (pi);
	g_free (d->package_id);
	g_free (d);
	pk_backend_finished (backend);

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
		pk_backend_thread_create (thread, backend_get_description_thread, data);
	}
}

/**
 * Collect items, select best edition.  This is used to find the best
 * available or installed.  The name of the class is a bit misleading though ...
 */
class LookForArchUpdate : public zypp::resfilter::PoolItemFilterFunctor
{
	public:
		zypp::PoolItem best;

	bool operator() (zypp::PoolItem provider)
	{
		if ((provider.status ().isLocked () == FALSE) && (!best || best->edition ().compare (provider->edition ()) < 0)) {
			best = provider;
		}

		return true;
	}
};

/**
 * The following method was taken directly from zypper code
 *
 * Find best (according to edition) uninstalled item
 * with the same kind/name/arch as item.
 * Similar to zypp::solver::detail::Helper::findUpdateItem
 * but that allows changing the arch (#222140).
 */
static zypp::PoolItem
findArchUpdateItem (const zypp::ResPool & pool, zypp::PoolItem item)
{
	LookForArchUpdate info;

	invokeOnEach (pool.byIdentBegin (item),
			pool.byIdentEnd (item),
			// get uninstalled, equal kind and arch, better edition
			zypp::functor::chain (
				zypp::functor::chain (
					zypp::functor::chain (
						zypp::resfilter::ByUninstalled (),
						zypp::resfilter::ByKind (item->kind ())),
					zypp::resfilter::byArch<zypp::CompareByEQ<zypp::Arch> > (item->arch ())),
				zypp::resfilter::byEdition<zypp::CompareByGT<zypp::Edition> > (item->edition ())),
			zypp::functor::functorRef<bool,zypp::PoolItem> (info));

	return info.best;
}

static gboolean
backend_get_updates_thread (PkBackendThread *thread, gpointer data)
{
	PkBackend *backend;
	
	backend = pk_backend_thread_get_backend (thread);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	zypp::ResPool pool = zypp_build_pool (TRUE);
	pk_backend_set_percentage (backend, 40);

	Candidates candidates;
	zypp::ResObject::Kind kind = zypp::ResTraits<zypp::Package>::kind;
	zypp::ResPool::byKind_iterator it = pool.byKindBegin (kind);
	zypp::ResPool::byKind_iterator e = pool.byKindEnd (kind);
	for (; it != e; ++it) {
		if (it->status ().isUninstalled ())
			continue;
		zypp::PoolItem candidate = findArchUpdateItem (pool, *it);
		if (!candidate.resolvable ())
			continue;

		candidates.insert (candidate);
	}

	pk_backend_set_percentage (backend, 80);
	Candidates::iterator cb = candidates.begin (), ce = candidates.end (), ci;
	for (ci = cb; ci != ce; ++ci) {
		zypp::ResObject::constPtr res = ci->resolvable();

		// Emit the package
		gchar *package_id = zypp_build_package_id_from_resolvable (res);
		pk_backend_package (backend,
				    PK_INFO_ENUM_AVAILABLE,
				    package_id,
				    res->description ().c_str ());
		g_free (package_id);
	}

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
	return TRUE;
}


/**
 * backend_get_updates
 */
static void
backend_get_updates (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_thread_create (thread, backend_get_updates_thread, NULL);
}

static gboolean
backend_install_package_thread (PkBackendThread *thread, gpointer data)
{
	PkBackend *backend;

	backend = pk_backend_thread_get_backend (thread);
	gchar *package_id = (gchar *)data;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	PkPackageId *pi = pk_package_id_new_from_string (package_id);
        if (pi == NULL) {
                pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
                g_free (package_id);

		pk_backend_finished (backend);
                return FALSE;
        }

	zypp::ZYpp::Ptr zypp;
	zypp = get_zypp ();

	try
	{

                zypp::ResPool pool = zypp_build_pool (TRUE);
	        pk_backend_set_percentage (backend, 10);

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
//printf ("WOOT!  Marking the package to be installed!\n");
					// this is the one, mark it to be installed
					selectable->set_status (zypp::ui::S_Install);
					break; // Found it, get out of the for loop
				}
			}
		}

		pk_backend_set_percentage (backend, 40);

//printf ("Resolving dependencies...\n");
		// Gather up any dependencies
		pk_backend_set_status (backend, PK_STATUS_ENUM_DEP_RESOLVE);
		if (zypp->resolver ()->resolvePool () == FALSE) {
			// Manual intervention required to resolve dependencies
			// TODO: Figure out what we need to do with PackageKit
			// to pull off interactive problem solving.
			pk_backend_error_code (backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "Couldn't resolve the package dependencies.");
			g_free (package_id);
			pk_package_id_free (pi);
			pk_backend_finished (backend);
			return FALSE;
		}

		pk_backend_set_percentage (backend, 60);

//printf ("Performing installation...\n");
		// Perform the installation
		// TODO: If this were an update, you should use PK_INFO_ENUM_UPDATING instead
		zypp::ZYppCommitPolicy policy;
		policy.restrictToMedia (0);	// 0 - install all packages regardless to media
		zypp::ZYppCommitResult result = zypp->commit (policy);
printf ("Finished the installation.\n");

		pk_backend_set_percentage (backend, 100);

		// TODO: Check result for success
	} catch (const zypp::repo::RepoNotFoundException &ex) {
		// TODO: make sure this dumps out the right sring.
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str() );
		g_free (package_id);
		pk_package_id_free (pi);
		pk_backend_finished (backend);
		return FALSE;
	} catch (const zypp::Exception &ex) {
		//pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Error enumerating repositories");
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str() );
		g_free (package_id);
		pk_package_id_free (pi);
		pk_backend_finished (backend);
		return FALSE;
	}

	g_free (package_id);
	pk_package_id_free (pi);
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_install_package:
 */
static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);

	// For now, don't let the user cancel the install once it's started
	pk_backend_set_allow_cancel (backend, FALSE);

	//printf("package_id is %s\n", package_id);
	gchar *package_to_install = g_strdup (package_id);
	pk_backend_thread_create (thread, backend_install_package_thread, package_to_install);
}

static gboolean
backend_refresh_cache_thread (PkBackendThread *thread, gpointer data)
{
	PkBackend *backend;
	RefreshData *d = (RefreshData*) data;

	backend = pk_backend_thread_get_backend (thread);
	gboolean force = d->force;
	g_free (d);

	pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
	pk_backend_set_percentage (backend, 0);

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
		return FALSE;
	}

	int i = 1;
	int num_of_repos = repos.size ();
	int percentage_increment = 100 / num_of_repos;
	for (std::list <zypp::RepoInfo>::iterator it = repos.begin(); it != repos.end(); it++, i++) {
		zypp::RepoInfo repo (*it);

		// skip disabled repos
		if (repo.enabled () == false)
			continue;

		// skip changeable meda (DVDs and CDs).  Without doing this,
		// the disc would be required to be physically present.
		if (zypp_is_changeable_media (*repo.baseUrlsBegin ()) == true)
			continue;

		try {
fprintf (stderr, "\n\n *** Refreshing metadata ***\n\n");
			manager.refreshMetadata (repo, force == TRUE ?
				zypp::RepoManager::RefreshForced :
				zypp::RepoManager::RefreshIfNeeded);
		} catch (const zypp::Exception &ex) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString ().c_str ());
			pk_backend_finished (backend);
			return FALSE;
		}

		try {
fprintf (stderr, "\n\n *** Building cache ***\n\n");
			manager.buildCache (repo, force == TRUE ?
				zypp::RepoManager::BuildForced :
				zypp::RepoManager::BuildIfNeeded);
		//} catch (const zypp::repo::RepoNoUrlException &ex) {
		//} catch (const zypp::repo::RepoNoAliasException &ex) {
		//} catch (const zypp::repo::RepoUnknownTypeException &ex) {
		//} catch (const zypp::repo::RepoException &ex) {
		} catch (const zypp::Exception &ex) {
			// TODO: Handle the exceptions in manager.refreshMetadata
			pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str() );
			pk_backend_finished (backend);
			return FALSE;
		}

		// Update the percentage completed
		pk_backend_set_percentage (backend,
					      i == num_of_repos ?
						100 :
						i * percentage_increment);
	}

	pk_backend_finished (backend);
	return TRUE;
}

static gboolean
backend_remove_package_thread (PkBackendThread *thread, gpointer data)
{
	PkBackend *backend;
	RemovePackageData *d = (RemovePackageData *)data;

	backend = pk_backend_thread_get_backend (thread);
	PkPackageId *pi;

	pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);
	pk_backend_set_percentage (backend, 0);

	pi = pk_package_id_new_from_string (d->package_id);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		g_free (d->package_id);
		g_free (d);
		pk_backend_finished (backend);
		return FALSE;
	}

	zypp::Target_Ptr target;

	zypp::ZYpp::Ptr zypp;
	zypp = get_zypp ();

	target = zypp->target ();

	// Load all the local system "resolvables" (packages)
        target->load ();
	pk_backend_set_percentage (backend, 10);

	try
	{
		// Iterate over the resolvables and mark the ones we want to remove
		//zypp->start ();
		for (zypp::ResPoolProxy::const_iterator it = zypp->poolProxy().byKindBegin <zypp::Package>();
				it != zypp->poolProxy().byKindEnd <zypp::Package>(); it++) {
			zypp::ui::Selectable::Ptr selectable = *it;
			if (strcmp (selectable->name().c_str(), pi->name) == 0) {
				if (selectable->status () == zypp::ui::S_KeepInstalled) {
					selectable->set_status (zypp::ui::S_Del);
					break;
				}
			}
		}

		pk_backend_set_percentage (backend, 40);

//printf ("Resolving dependencies...\n");
// TODO: Figure out what to do about d->deps_behavior
		// Gather up any dependencies
		pk_backend_set_status (backend, PK_STATUS_ENUM_DEP_RESOLVE);
		if (zypp->resolver ()->resolvePool () == FALSE) {
			// Manual intervention required to resolve dependencies
			// TODO: Figure out what we need to do with PackageKit
			// to pull off interactive problem solving.
			pk_backend_error_code (backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "Couldn't resolve the package dependencies.");
			g_free (d->package_id);
			g_free (d);
			pk_package_id_free (pi);
			pk_backend_finished (backend);
			return FALSE;
		}

		pk_backend_set_percentage (backend, 60);

		zypp::ZYppCommitPolicy policy;
		zypp::ZYppCommitResult result = zypp->commit (policy);
printf ("Finished the removal.\n");

		pk_backend_set_percentage (backend, 100);

		// TODO: Check result for success
	} catch (const zypp::repo::RepoNotFoundException &ex) {
		// TODO: make sure this dumps out the right sring.
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str() );
		g_free (d->package_id);
		g_free (d);
		pk_package_id_free (pi);
		pk_backend_finished (backend);
		return FALSE;
	} catch (const zypp::Exception &ex) {
		//pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Error enumerating repositories");
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str() );
		g_free (d->package_id);
		g_free (d);
		pk_package_id_free (pi);
		pk_backend_finished (backend);
		return FALSE;
	}

	

	g_free (d->package_id);
	g_free (d);
	pk_package_id_free (pi);
	pk_backend_finished (backend);
	return TRUE;
}


/**
 * backend_remove_package:
 */
static void
backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps)
{
	g_return_if_fail (backend != NULL);

	RemovePackageData *data = g_new0 (RemovePackageData, 1);
	data->package_id = g_strdup (package_id);
	data->deps_behavior = allow_deps == TRUE ? DEPS_ALLOW : DEPS_NO_ALLOW;

	pk_backend_thread_create (thread, backend_remove_package_thread, data);
}

/**
 * backend_refresh_cache
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (backend != NULL);

	// check network state
	/*
	if (pk_network_is_online (backend) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache while offline");
		pk_backend_finished (backend);
		return;
	}
	*/
	RefreshData *data = g_new(RefreshData, 1);
	if (data == NULL) {
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory in backend_refresh_cache");
		pk_backend_finished (backend);
	} else {
		data->force = force;
		pk_backend_thread_create (thread, backend_refresh_cache_thread, data);
	}
}

static gboolean
backend_resolve_thread (PkBackendThread *thread, gpointer data)
{
	PkBackend *backend;
	gchar *package_id;

	backend = pk_backend_thread_get_backend (thread);
	ResolveData *rdata = (ResolveData*) data;
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	std::vector<zypp::PoolItem> *v;
	v = zypp_get_packages_by_name ((const gchar *)rdata->name, TRUE);

	zypp::ResObject::constPtr package = NULL;
	for (std::vector<zypp::PoolItem>::iterator it = v->begin ();
			it != v->end (); it++) {
		zypp::ResObject::constPtr pkg = (*it);
		const char *version = pkg->edition ().asString ().c_str ();
		if (package == NULL) {
			package = pkg;
		} else if (g_ascii_strcasecmp (version, package->edition ().asString ().c_str ()) > 0) {
			package = pkg;
		}
	}

	delete (v);

	if (package == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "couldn't find package");
		g_free (rdata->name);
		g_free (rdata->filter);
		g_free (rdata);
		pk_backend_finished (backend);
		return FALSE;
	}

	package_id = zypp_build_package_id_from_resolvable (package);
	// TODO: Determine whether the package is installed and emit either PK_INFO_ENUM_AVAILABLE or PK_INFO_ENUM_INSTALLED
	pk_backend_package (backend,
			    PK_INFO_ENUM_AVAILABLE,
			    package_id,
			    package->description ().c_str ());

	g_free (rdata->name);
	g_free (rdata->filter);
	g_free (rdata);
	g_free (package_id);
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, const gchar *filter, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	//printf("Enter backend_resolve - filter:%s, package_id:%s\n", filter, package_id);
	ResolveData *data = g_new0(ResolveData, 1);
	if (data == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory in backend_resolve");
		pk_backend_finished (backend);
	} else {
		data->name = g_strdup (package_id);
		data->filter = g_strdup (filter);
		pk_backend_thread_create (thread, backend_resolve_thread, data);
	}
}

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

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	parse_filter (filter, &installed, &available, &devel, &nondevel, &gui, &text);

	pk_backend_no_percentage_updates (backend);

        std::vector<zypp::PoolItem> *v = new std::vector<zypp::PoolItem>;

	switch (mode) {
		case SEARCH_TYPE_NAME:
			v = zypp_get_packages_by_name (search, TRUE);
			break;
	
                case SEARCH_TYPE_DETAILS:
                        v = zypp_get_packages_by_details (search, TRUE);
                        break;      
                case SEARCH_TYPE_FILE:
                        v = zypp_get_packages_by_file (search);
                        break;
        };
        
	zypp_emit_packages_in_list (backend, v);
	delete (v);
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
backend_find_packages_thread (PkBackendThread *thread, gpointer data)
{
	PkBackend *backend;
	FindData *d = (FindData*) data;

	backend = pk_backend_thread_get_backend (thread);
	g_return_val_if_fail (backend != NULL, FALSE);

	find_packages_real (backend, d->search, d->filter, d->mode);

	g_free(d->search);
	g_free(d->filter);
	g_free(d);
	pk_backend_finished (backend);

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
		pk_backend_thread_create (thread, backend_find_packages_thread, data);
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
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	find_packages (backend, search, filter, SEARCH_TYPE_DETAILS);
}

/**
 * backend_search_file:
 */
static void
backend_search_file (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	find_packages (backend, search, filter, SEARCH_TYPE_FILE);
}

/**
 * backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

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

static gboolean
backend_get_files_thread (PkBackendThread *thread, gpointer data) {
        
        PkPackageId *pi;
        PkBackend *backend;
        
        /* get current backend */
        backend = pk_backend_thread_get_backend (thread);
	ThreadData *d = (ThreadData*) data;

	pi = pk_package_id_new_from_string (d->package_id);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_package_id_free (pi);
		g_free (d->package_id);
		g_free (d);
		pk_backend_finished (backend);
		return FALSE;
	}
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	std::vector<zypp::PoolItem> *v;
	v = zypp_get_packages_by_name ((const gchar *)pi->name, TRUE);

	zypp::ResObject::constPtr package;
	for (std::vector<zypp::PoolItem>::iterator it = v->begin ();
			it != v->end (); it++) {
		zypp::ResObject::constPtr pkg = (*it);
		const char *version = pkg->edition ().asString ().c_str ();
		if (strcmp (pi->version, version) == 0) {
			package = pkg;
			break;
		}
	}

	delete (v);

	if (package == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "couldn't find package");
		pk_package_id_free (pi);
		g_free (d->package_id);
		g_free (d);
		pk_backend_finished (backend);
		return FALSE;
	}

        std::string temp;
        if (package->isSystem ()){
                zypp::Target_Ptr target;

               	zypp::ZYpp::Ptr zypp;
              	zypp = get_zypp ();

               	target = zypp->target ();
                try {
                        zypp::target::rpm::RpmDb &rpm = target->rpmDb (); 
                        rpm.initDatabase();
                        zypp::target::rpm::RpmHeader::constPtr rpmHeader;
                        rpm.getData (package-> name (), package->edition (), rpmHeader);
                        std::list<std::string> files = rpmHeader->tag_filenames ();

                        for (std::list<std::string>::iterator it = files.begin (); it != files.end (); it++) {
                                temp.append (*it);
                                temp.append (";");
                        }
                        rpm.closeDatabase();
                } catch (const zypp::target::rpm::RpmException &ex) {
		        pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "Couldn't open rpm-database");
                        pk_backend_finished (backend);
		        return FALSE;
                }
        }else{
                temp = "Only available for installed packages";
        }

	pk_backend_files (backend,
	        	d->package_id,		// package_id
			temp.c_str ());		// file_list

        pk_package_id_free (pi);
	g_free (d->package_id);
	g_free (d);
	pk_backend_finished (backend);

        return TRUE;
}

/**
  * backend_get_files:
  */
static void
backend_get_files(PkBackend *backend, const gchar *package_id)
{
        g_return_if_fail (backend != NULL);

        ThreadData *data = g_new0(ThreadData, 1);
        if (data == NULL) {
                pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory in backend_get_files");
                pk_backend_finished (backend);
        } else {
                data->package_id = g_strdup(package_id);
                pk_backend_thread_create (thread, backend_get_files_thread, data);
        }
}

/**
  * backend_get_requires_thread:
  */
static gboolean
backend_get_requires_thread (PkBackendThread *thread, gpointer data) {
        
        PkPackageId *pi;
        PkBackend *backend;
        
        /* get current backend */
        backend = pk_backend_thread_get_backend (thread);
	ThreadData *d = (ThreadData*) data;

	pi = pk_package_id_new_from_string (d->package_id);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_package_id_free (pi);
		g_free (d->package_id);
		g_free (d);
		pk_backend_finished (backend);
		return FALSE;
	}
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);
        
        zypp::ResPool pool = zypp_build_local_pool();        

        zypp::PoolItem package;
        gboolean found = FALSE;
        for (zypp::ResPool::byIdent_iterator it = pool.byIdentBegin (zypp::ResKind::package, pi->name);
                        it != pool.byIdentEnd (zypp::ResKind::package, pi->name); it++) {
                package = (*it);
                found = TRUE;
        }

	if (found == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED, "Package is not installed");
		pk_package_id_free (pi);
		g_free (d->package_id);
		g_free (d);
		pk_backend_finished (backend);
		return FALSE;
	}

        // set Package as to be installed
        package.status ().setToBeUninstalled (zypp::ResStatus::USER);

	pk_backend_set_percentage (backend, 40);

        // solver run
        zypp::Resolver solver(pool);
        
        // DEBUG https://bugzilla.novell.com/show_bug.cgi?id=363545
        if (solver.forceResolve () == FALSE) {
                std::list<zypp::ResolverProblem_Ptr> problems = solver.problems ();
                if(problems.begin() == problems.end())
                        fprintf(stderr,"\n_____________NO ERRORS AVAILABLE !?!?!_____________________\n");
                for(std::list<zypp::ResolverProblem_Ptr>::iterator it = problems.begin (); it != problems.end (); it++){
                    fprintf(stderr,"\n__ERROR: %s_______________________\n", (*it)->description ().c_str ());
                }
		pk_backend_error_code (backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "Resolution failed");
		pk_package_id_free (pi);
		g_free (d->package_id);
		g_free (d);
		pk_backend_finished (backend);
		return FALSE;
	}

	pk_backend_set_percentage (backend, 60);

        // look for packages which would be uninstalled
        for (zypp::ResPool::byIdent_iterator it = pool.byIdentBegin (zypp::ResKind::package, pi->name);
                        it != pool.byIdentEnd (zypp::ResKind::package, pi->name); it++) {
                if (it->status () == zypp::ResStatus::toBeUninstalled || it->status () == zypp::ResStatus::toBeUninstalledSoft) {
                        gchar *package_id;
                        package_id = pk_package_id_build ( it->resolvable ()->name ().c_str(),
                                                           it->resolvable ()->edition ().asString ().c_str(),
                                                           it->resolvable ()->arch ().c_str(),
                                                           "opensuse");
                        pk_backend_package (backend,
			                    PK_INFO_ENUM_INSTALLED,
			                    package_id,
			                    it->resolvable ()->description ().c_str ());
                        g_free (package_id);
                }
        }

        pk_package_id_free (pi);
	g_free (d->package_id);
	g_free (d);
	pk_backend_finished (backend);

        return TRUE;
}

/**
  * backend_get_requires:
  */
static void
backend_get_requires(PkBackend *backend, const gchar *package_id, gboolean recursive) {
        g_return_if_fail (backend != NULL);

        ThreadData *data = g_new0(ThreadData, 1);
        if (data == NULL) {
                pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory in backend_get_requires");
                pk_backend_finished (backend);
        } else {
                data->package_id = g_strdup(package_id);
                pk_backend_thread_create (thread, backend_get_requires_thread, data);
        }
}

extern "C" PK_BACKEND_OPTIONS (
	"Zypp",					/* description */
	"Boyd Timothy <btimothy@gmail.com>, Scott Reeves <sreeves@novell.com>, Stefan Haas <shaas@suse.de>",	/* author */
	backend_initialize,			/* initalize */
	backend_destroy,			/* destroy */
	backend_get_groups,			/* get_groups */
	backend_get_filters,			/* get_filters */
	NULL,					/* cancel */
	backend_get_depends,			/* get_depends */
	backend_get_description,		/* get_description */
	backend_get_files,			/* get_files */
	backend_get_requires,			/* get_requires */
	NULL,					/* get_update_detail */
	backend_get_updates,			/* get_updates */
	backend_install_package,		/* install_package */
	NULL,					/* install_file */
	backend_refresh_cache,			/* refresh_cache */
	backend_remove_package,			/* remove_package */
	backend_resolve,			/* resolve */
	NULL,					/* rollback */
	backend_search_details,			/* search_details */
	backend_search_file,			/* search_file */
	NULL,					/* search_group */
	backend_search_name,			/* search_name */
	NULL,					/* update_package */
	NULL,					/* update_system */
	backend_get_repo_list,			/* get_repo_list */
	backend_repo_enable,			/* repo_enable */
	NULL,					/* repo_set_data */
        NULL                                    /* service_pack */
);
