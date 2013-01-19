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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <gmodule.h>
#include <glib.h>
#include <pk-backend.h>
#include <pk-backend-spawn.h>
#include <pk-shared.h>
#include <unistd.h>
#include <string>
#include <set>
#include <map>
#include <list>
#include <glib/gi18n.h>
#include <sys/vfs.h>

#include <zypp/base/Logger.h>
#include <zypp/ZYppFactory.h>
#include <zypp/ResObject.h>
#include <zypp/ResPoolProxy.h>
#include <zypp/ui/Selectable.h>
#include <zypp/Patch.h>
#include <zypp/Package.h>
#include <zypp/SrcPackage.h>
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
#include <zypp/base/Functional.h>
#include <zypp/parser/ProductFileReader.h>
#include <zypp/TmpPath.h>
#include <zypp/PathInfo.h>
#include <zypp/repo/PackageProvider.h>
#include <zypp/repo/SrcPackageProvider.h>

#include <zypp/sat/Solvable.h>

#include "pk-backend-zypp-private.h"

#include "zypp-utils.h"
#include "zypp-events.h"

using namespace std;
using namespace zypp;

PkBackendZYppPrivate *priv = 0L;

enum PkgSearchType {
	SEARCH_TYPE_NAME = 0,
	SEARCH_TYPE_DETAILS = 1,
	SEARCH_TYPE_FILE = 2,
	SEARCH_TYPE_RESOLVE = 3
};

// helper function to restore the pool status
// after doing operations on it
class PoolStatusSaver : private base::NonCopyable
{
public:
	PoolStatusSaver() {
		ResPool::instance().proxy().saveState();
	}

	~PoolStatusSaver() {
		ResPool::instance().proxy().restoreState();
	}
};

/**
 * We do not pretend we're thread safe when all we do is having a huge mutex
 */
gboolean
pk_backend_supports_parallelization (PkBackend *backend)
{
        return FALSE;
}


/**
 * pk_backend_get_description:
 */
const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return g_strdup ("ZYpp package manager");
}

/**
 * pk_backend_get_author:
 */
const gchar *
pk_backend_get_author (PkBackend *backend)
{
	return g_strdup ("Boyd Timothy <btimothy@gmail.com>, "
			 "Scott Reeves <sreeves@novell.com>, "
			 "Stefan Haas <shaas@suse.de>"
			 "ZYpp developers <zypp-devel@opensuse.org>");
}

/**
 * pk_backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
void
pk_backend_initialize (PkBackend *backend)
{
	/* create private area */
	priv = new PkBackendZYppPrivate;
	priv->currentJob = 0;
	priv->zypp_mutex = PTHREAD_MUTEX_INITIALIZER;
	zypp_logging ();

	g_debug ("zypp_backend_initialize");
	//_updating_self = FALSE;
}

/**
 * pk_backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
void
pk_backend_destroy (PkBackend *backend)
{
	g_debug ("zypp_backend_destroy");

	g_free (_repoName);
	delete priv;
}


static bool
zypp_is_no_solvable (const sat::Solvable &solv)
{
	return solv.id() == sat::detail::noSolvableId;
}

/**
  * backend_get_requires_thread:
  */
static void
backend_get_requires_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;

	PkBitfield _filters;
	gchar **package_ids;
	gboolean recursive;
	g_variant_get(params, "(t^a&sb)",
		      &_filters,
		      &package_ids,
		      &recursive);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	pk_backend_job_set_percentage (job, 10);

	PoolStatusSaver saver;
	ResPool pool = zypp_build_pool (zypp, true);
	for (uint i = 0; package_ids[i]; i++) {
		sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);

		if (zypp_is_no_solvable(solvable)) {
			zypp_backend_finished_error (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						     "Package couldn't be found");
			return;
		}

		PoolItem package = PoolItem(solvable);

		// get-requires only works for installed packages. It's meaningless for stuff in the repo
		// same with yum backend
		if (!solvable.isSystem ())
			continue;
		// set Package as to be uninstalled
		package.status ().setToBeUninstalled (ResStatus::USER);

		// solver run
		ResPool pool = ResPool::instance ();
		Resolver solver(pool);

		solver.setForceResolve (true);
		solver.setIgnoreAlreadyRecommended (TRUE);

		if (!solver.resolvePool ()) {
			string problem = "Resolution failed: ";
			list<ResolverProblem_Ptr> problems = solver.problems ();
			for (list<ResolverProblem_Ptr>::iterator it = problems.begin (); it != problems.end (); ++it){
				problem += (*it)->description ();
			}
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
				problem.c_str());
			return;
		}

		// look for packages which would be uninstalled
		bool error = false;
		for (ResPool::byKind_iterator it = pool.byKindBegin (ResKind::package);
				it != pool.byKindEnd (ResKind::package); ++it) {

			if (!error && !zypp_filter_solvable (_filters, it->resolvable()->satSolvable()))
				error = !zypp_backend_pool_item_notify (job, *it);
		}

		solver.setForceResolve (false);
	}

	pk_backend_job_finished (job);
}

/**
  * pk_backend_get_requires:
  */
void
pk_backend_get_requires(PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_job_thread_create (job, backend_get_requires_thread, NULL, NULL);
}

/**
 * pk_backend_get_groups:
 */
PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_GROUP_ENUM_ADMIN_TOOLS,
		PK_GROUP_ENUM_COMMUNICATION,
		PK_GROUP_ENUM_DESKTOP_GNOME,
		PK_GROUP_ENUM_DESKTOP_KDE,
		PK_GROUP_ENUM_DESKTOP_OTHER,
		PK_GROUP_ENUM_DESKTOP_XFCE,
		PK_GROUP_ENUM_EDUCATION,
		PK_GROUP_ENUM_GAMES,
		PK_GROUP_ENUM_GRAPHICS,
		PK_GROUP_ENUM_LOCALIZATION,
		PK_GROUP_ENUM_MULTIMEDIA,
		PK_GROUP_ENUM_NETWORK,
		PK_GROUP_ENUM_OFFICE,
		PK_GROUP_ENUM_PROGRAMMING,
		PK_GROUP_ENUM_PUBLISHING,
		PK_GROUP_ENUM_SECURITY,
		PK_GROUP_ENUM_SYSTEM,
		-1);
}

/**
 * pk_backend_get_filters:
 */
PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
				       PK_FILTER_ENUM_ARCH,
				       PK_FILTER_ENUM_NEWEST,
				       PK_FILTER_ENUM_SOURCE,
				       -1);
}

/*
 * This method is a bit of a travesty of the complexity of
 * solving dependencies. We try to give a simple answer to
 * "what packages are required for these packages" - but,
 * clearly often there is no simple answer.
 */
static void
backend_get_depends_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	PkBitfield _filters;
	gchar **package_ids;
	gboolean recursive;
	g_variant_get (params, "(t^a&sb)",
		       &_filters,
		       &package_ids,
		       &recursive);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}
	
	MIL << package_ids[0] << " " << pk_filter_bitfield_to_string (_filters) << endl;

	try
	{
		sat::Solvable solvable = zypp_get_package_by_id(package_ids[0]);
		
		pk_backend_job_set_percentage (job, 20);

		if (zypp_is_no_solvable(solvable)) {
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
				"Did not find the specified package.");
			return;
		}

		// Gather up any dependencies
		pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);
		pk_backend_job_set_percentage (job, 60);

		// get dependencies
		Capabilities req = solvable[Dep::REQUIRES];

		// which package each capability
		map<string, sat::Solvable> caps;
		// packages already providing a capability
		vector<string> pkg_names;

		for (Capabilities::const_iterator cap = req.begin (); cap != req.end (); ++cap) {
			g_debug ("get_depends - capability '%s'", cap->asString().c_str());

			if (caps.find (cap->asString ()) != caps.end()) {
				g_debug ("Interesting ! already have capability '%s'", cap->asString().c_str());
				continue;
			}

			// Look for packages providing each capability
			bool have_preference = false;
			sat::Solvable preferred;

			sat::WhatProvides prov_list (*cap);
			for (sat::WhatProvides::const_iterator provider = prov_list.begin ();
			     provider != prov_list.end (); provider++) {

				g_debug ("provider: '%s'", provider->asString().c_str());

				// filter out caps like "rpmlib(PayloadFilesHavePrefix) <= 4.0-1" (bnc#372429)
				if (zypp_is_no_solvable (*provider))
					continue;

				// Is this capability provided by a package we already have listed ?
				if (find (pkg_names.begin (), pkg_names.end(),
					       provider->name ()) != pkg_names.end()) {
					preferred = *provider;
					have_preference = true;
					break;
				}

				// Something is better than nothing
				if (!have_preference) {
					preferred = *provider;
					have_preference = true;

				// Prefer system packages
				} else if (provider->isSystem()) {
					preferred = *provider;
					break;

				} // else keep our first love
			}

			if (have_preference &&
			    find (pkg_names.begin (), pkg_names.end(),
				       preferred.name ()) == pkg_names.end()) {
				caps[cap->asString()] = preferred;
				pkg_names.push_back (preferred.name ());
			}
		}

		// print dependencies
		for (map<string, sat::Solvable>::iterator it = caps.begin ();
		     it != caps.end();
		     ++it) {
			
			// backup sanity check for no-solvables
			if (! it->second.name ().c_str() ||
			    it->second.name ().c_str()[0] == '\0')
				continue;
			
			PoolItem item(it->second);
			PkInfoEnum info = it->second.isSystem () ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;

			g_debug ("add dep - '%s' '%s' %d [%s]", it->second.name().c_str(),
				 info == PK_INFO_ENUM_INSTALLED ? "installed" : "available",
				 it->second.isSystem(),
				 zypp_filter_solvable (_filters, it->second) ? "don't add" : "add" );

			if (!zypp_filter_solvable (_filters, it->second)) {
				zypp_backend_package (job, info, it->second,
						      item->summary ().c_str());
			}
		}

		pk_backend_job_set_percentage (job, 100);
	} catch (const repo::RepoNotFoundException &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str());
		return;
	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
		return;
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_get_depends:
 */
void
pk_backend_get_depends (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_job_thread_create (job, backend_get_depends_thread, NULL, NULL);
}

static void
backend_get_details_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;

	gchar **package_ids;
	g_variant_get (params, "(^a&s)",
		       &package_ids);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	for (uint i = 0; package_ids[i]; i++) {
		MIL << package_ids[i] << endl;

		sat::Solvable solv = zypp_get_package_by_id( package_ids[i] );

		ResObject::constPtr obj = make<ResObject>( solv );
		if (obj == NULL) {
			zypp_backend_finished_error (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, 
						     "couldn't find package");
			return;
		}

		try {
			Package::constPtr pkg = make<Package>( solv );	// or NULL if not a Package
			Patch::constPtr patch = make<Patch>( solv );	// or NULL if not a Patch

			ByteCount size;
			if ( patch ) {
				Patch::Contents contents( patch->contents() );
				for_( it, contents.begin(), contents.end() ) {
					size += make<ResObject>(*it)->downloadSize();
				}
			}
			else {
				size = obj->isSystem() ? obj->installSize() : obj->downloadSize();
			}

			pk_backend_job_details (job,
				package_ids[i],				// package_id
				(pkg ? pkg->license().c_str() : "" ),	// license is Package attribute
				get_enum_group(pkg ? pkg->group() : ""),// PkGroupEnum
				obj->description().c_str(),		// description is common attibute
				(pkg ? pkg->url().c_str() : "" ),	// url is Package attribute
				(gulong)size);
		} catch (const Exception &ex) {
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString ().c_str ());
			return;
		}
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_get_details:
 */
void
pk_backend_get_details (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	pk_backend_job_thread_create (job, backend_get_details_thread, NULL, NULL);
}

static void
backend_get_distro_upgrades_thread(PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();
	
	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	// refresh the repos before checking for updates
	if (!zypp_refresh_cache (job, zypp, FALSE)) {
		pk_backend_job_finished (job);
		return;
	}

	vector<parser::ProductFileData> result;
	if (!parser::ProductFileReader::scanDir (functor::getAll (back_inserter (result)), "/etc/products.d")) {
		zypp_backend_finished_error (job, PK_ERROR_ENUM_INTERNAL_ERROR, 
					     "Could not parse /etc/products.d");
		return;
	}

	for (vector<parser::ProductFileData>::iterator it = result.begin (); it != result.end (); ++it) {
		vector<parser::ProductFileData::Upgrade> upgrades = it->upgrades();
		for (vector<parser::ProductFileData::Upgrade>::iterator it2 = upgrades.begin (); it2 != upgrades.end (); it2++) {
			if (it2->notify ()){
				PkDistroUpgradeEnum status = PK_DISTRO_UPGRADE_ENUM_UNKNOWN;
				if (it2->status () == "stable") {
					status = PK_DISTRO_UPGRADE_ENUM_STABLE;
				} else if (it2->status () == "unstable") {
					status = PK_DISTRO_UPGRADE_ENUM_UNSTABLE;
				}
				pk_backend_job_distro_upgrade (job,
							   status,
							   it2->name ().c_str (),
							   it2->summary ().c_str ());
			}
		}
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_get_distro_upgrades:
 */
void
pk_backend_get_distro_upgrades (PkBackend *backend, PkBackendJob *job)
{
	pk_backend_job_thread_create (job, backend_get_distro_upgrades_thread, NULL, NULL);
}

static void
backend_refresh_cache_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gboolean force;
	g_variant_get (params, "(b)",
		       &force);

	MIL << force << endl;
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	zypp_refresh_cache (job, zypp, force);
	pk_backend_job_finished (job);
}

/**
 * pk_backend_refresh_cache
 */
void
pk_backend_refresh_cache (PkBackend *backend, PkBackendJob *job, gboolean force)
{
	pk_backend_job_thread_create (job, backend_refresh_cache_thread, NULL, NULL);
}

/* If a critical self update (see qualifying steps below) is available then only show/install that update first.
 1. there is a patch available with the <restart_suggested> tag set
 2. The patch contains the package "PackageKit" or "gnome-packagekit
*/
/*static gboolean
check_for_self_update (PkBackend *backend, set<PoolItem> *candidates)
{
	set<PoolItem>::iterator cb = candidates->begin (), ce = candidates->end (), ci;
	for (ci = cb; ci != ce; ++ci) {
		ResObject::constPtr res = ci->resolvable();
		if (isKind<Patch>(res)) {
			Patch::constPtr patch = asKind<Patch>(res);
			//g_debug ("restart_suggested is %d",(int)patch->restartSuggested());
			if (patch->restartSuggested ()) {
				if (!strcmp (PACKAGEKIT_RPM_NAME, res->satSolvable ().name ().c_str ()) ||
						!strcmp (GNOME_PACKAGKEKIT_RPM_NAME, res->satSolvable ().name ().c_str ())) {
					g_free (update_self_patch_name);
					update_self_patch_name = zypp_build_package_id_from_resolvable (res->satSolvable ());
					return TRUE;
				}
			}
		}
	}
	return FALSE;
}*/

static void
backend_get_updates_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	PkBitfield _filters;
	g_variant_get (params, "(t)",
		       &_filters);

	MIL << pk_filter_bitfield_to_string(_filters) << endl;
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	typedef set<PoolItem>::iterator pi_it_t;

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	// refresh the repos before checking for updates
	if (!zypp_refresh_cache (job, zypp, FALSE)) {
		pk_backend_job_finished (job);
		return;
	}

	ResPool pool = zypp_build_pool (zypp, TRUE);
	pk_backend_job_set_percentage (job, 40);

	// check if the repositories may be dead (feature #301904)
	warn_outdated_repos (job, pool);

	set<PoolItem> candidates;
	zypp_get_updates (job, zypp, candidates);

	pk_backend_job_set_percentage (job, 80);

	pi_it_t cb = candidates.begin (), ce = candidates.end (), ci;
	for (ci = cb; ci != ce; ++ci) {
		ResObject::constPtr res = ci->resolvable();

		// Emit the package
		PkInfoEnum infoEnum = PK_INFO_ENUM_ENHANCEMENT;
		if (isKind<Patch>(res)) {
			Patch::constPtr patch = asKind<Patch>(res);
			if (patch->category () == "recommended") {
				infoEnum = PK_INFO_ENUM_IMPORTANT;
			} else if (patch->category () == "optional") {
				infoEnum = PK_INFO_ENUM_LOW;
			} else if (patch->category () == "security") {
				infoEnum = PK_INFO_ENUM_SECURITY;
			} else if (patch->category () == "distupgrade") {
				continue;
			} else {
				infoEnum = PK_INFO_ENUM_NORMAL;
			}
		}

		if (!zypp_filter_solvable (_filters, res->satSolvable())) {
			// some package descriptions generate markup parse failures
			// causing the update to show empty package lines, comment for now
			// res->summary ().c_str ());
			// Test if this still happens!
			zypp_backend_package (job, infoEnum, res->satSolvable (),
					      res->summary ().c_str ());
		}
	}

	pk_backend_job_set_percentage (job, 100);
	pk_backend_job_finished (job);
}

/**
 * pk_backend_get_updates
 */
void
pk_backend_get_updates (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	pk_backend_job_thread_create (job, backend_get_updates_thread, NULL, NULL);
}

static void
backend_install_files_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	RepoManager manager;
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	PkBitfield transaction_flags;
	gchar **full_paths;
	g_variant_get (params, "(t^a&s)",
		       &transaction_flags,
		       &full_paths);
	
	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	// create a temporary directory
	filesystem::TmpDir tmpDir;
	if (tmpDir == NULL) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_LOCAL_INSTALL_FAILED,
			"Could not create a temporary directory");
		return;
	}

	for (guint i = 0; full_paths[i]; i++) {

		// check if file is really a rpm
		Pathname rpmPath (full_paths[i]);
		target::rpm::RpmHeader::constPtr rpmHeader = target::rpm::RpmHeader::readPackage (rpmPath, target::rpm::RpmHeader::NOSIGNATURE);

		if (rpmHeader == NULL) {
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_LOCAL_INSTALL_FAILED,
				"%s is not valid rpm-File", full_paths[i]);
			return;
		}

		// copy the rpm into tmpdir
		string tempDest = tmpDir.path ().asString () + "/" + rpmHeader->tag_name () + ".rpm";
		if (filesystem::copy (full_paths[i], tempDest) != 0) {
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_LOCAL_INSTALL_FAILED,
				"Could not copy the rpm-file into the temp-dir");
			return;
		}
	}

	// create a plaindir-repo and cache it
	RepoInfo tmpRepo;

	try {
		tmpRepo.setType(repo::RepoType::RPMPLAINDIR);
		string url = "dir://" + tmpDir.path ().asString ();
		tmpRepo.addBaseUrl(Url::parseUrl(url));
		tmpRepo.setEnabled (true);
		tmpRepo.setAutorefresh (true);
		tmpRepo.setAlias ("PK_TMP_DIR");
		tmpRepo.setName ("PK_TMP_DIR");

		// add Repo to pool
		manager.addRepository (tmpRepo);

		if (!zypp_refresh_meta_and_cache (manager, tmpRepo)) {
			zypp_backend_finished_error (
			  job, PK_ERROR_ENUM_INTERNAL_ERROR, "Can't refresh repositories");
			return;
		}
		zypp_build_pool (zypp, true);

	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString ().c_str ());
		return;
	}

	Repository repo = ResPool::instance().reposFind("PK_TMP_DIR");

	for_(it, repo.solvablesBegin(), repo.solvablesEnd()){
		MIL << "Setting " << *it << " for installation" << endl;
		PoolItem(*it).status().setToBeInstalled(ResStatus::USER);
	}

	if (!zypp_perform_execution (job, zypp, INSTALL, FALSE, transaction_flags)) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_LOCAL_INSTALL_FAILED, "Could not install the rpm-file.");
	}

	// remove tmp-dir and the tmp-repo
	try {
		manager.removeRepository (tmpRepo);
	} catch (const repo::RepoNotFoundException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str() );
	}

	pk_backend_job_finished (job);
}

/**
  * pk_backend_install_files
  */
void
pk_backend_install_files (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **full_paths)
{
	pk_backend_job_thread_create (job, backend_install_files_thread, NULL, NULL);
}

static void
backend_get_update_detail_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	gchar **package_ids;
	g_variant_get (params, "(^a&s)",
		&package_ids);

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	if (package_ids == NULL) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		return;
	}
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	for (uint i = 0; package_ids[i]; i++) {
		sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);
		MIL << package_ids[i] << " " << solvable << endl;

		Capabilities obs = solvable.obsoletes ();

		GPtrArray *obsoletes = zypp_build_package_id_capabilities (obs, FALSE);

		PkRestartEnum restart = PK_RESTART_ENUM_NONE;

		GPtrArray *bugzilla = g_ptr_array_new();
		GPtrArray *cve = g_ptr_array_new();
		GPtrArray *vendor_urls = g_ptr_array_new();

		if (isKind<Patch>(solvable)) {
			Patch::constPtr patch = make<Patch>(solvable); // may use asKind<Patch> if libzypp-11.6.4 is asserted
			zypp_check_restart (&restart, patch);

			// Building links like "http://www.distro-update.org/page?moo;Bugfix release for kernel;http://www.test.de/bgz;test domain"
			for (Patch::ReferenceIterator it = patch->referencesBegin (); it != patch->referencesEnd (); it ++) {
				if (it.type () == "bugzilla") {
				    g_ptr_array_add(bugzilla, g_strconcat (it.href ().c_str (), (gchar *)NULL));
				} else {
				    g_ptr_array_add(cve, g_strconcat (it.href ().c_str (), (gchar *)NULL));
				}
			}

			sat::SolvableSet content = patch->contents ();

			for (sat::SolvableSet::const_iterator it = content.begin (); it != content.end (); ++it) {
				GPtrArray *nobs = zypp_build_package_id_capabilities (it->obsoletes ());
				int i;
				for (i = 0; nobs->pdata[i]; i++)
				    g_ptr_array_add(obsoletes, nobs->pdata[i]);
			}
		}
		g_ptr_array_add(bugzilla, NULL);
		g_ptr_array_add(cve, NULL);
		g_ptr_array_add(obsoletes, NULL);
		g_ptr_array_add(vendor_urls, NULL);

		pk_backend_job_update_detail (job,
					  package_ids[i],
					  NULL,		// updates TODO with Resolver.installs
					  (gchar **)obsoletes->pdata,
					  (gchar **)vendor_urls->pdata,
					  (gchar **)bugzilla->pdata,	// bugzilla
					  (gchar **)cve->pdata,		// cve
					  restart,	// restart -flag
					  make<ResObject>(solvable)->description().c_str (),	// update-text
					  NULL,		// ChangeLog text
					  PK_UPDATE_STATE_ENUM_UNKNOWN,		// state of the update
					  NULL, // date that the update was issued
					  NULL);	// date that the update was updated

		g_ptr_array_unref(obsoletes);
		g_ptr_array_unref(vendor_urls);
		g_ptr_array_unref(bugzilla);
		g_ptr_array_unref(cve);
	}

	pk_backend_job_finished (job);
}

/**
  * pk_backend_get_update_detail
  */
void
pk_backend_get_update_detail (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	pk_backend_job_thread_create (job, backend_get_update_detail_thread, NULL, NULL);
}

static void
backend_install_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	PoolStatusSaver saver;

	PkBitfield transaction_flags = 0;
	gchar **package_ids;
	
	g_variant_get(params, "(t^a&s)",
		      &transaction_flags,
		      &package_ids);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();
	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	// refresh the repos before installing packages
	if (!zypp_refresh_cache (job, zypp, FALSE)) {
		pk_backend_job_finished (job);
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);
	
	try
	{
		ResPool pool = zypp_build_pool (zypp, TRUE);
		pk_backend_job_set_percentage (job, 10);
		vector<PoolItem> *items = new vector<PoolItem> ();

		guint to_install = 0;
		for (guint i = 0; package_ids[i]; i++) {
			MIL << package_ids[i] << endl;
			sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);
			
			to_install++;
			PoolItem item(solvable);
			// set status to ToBeInstalled
			item.status ().setToBeInstalled (ResStatus::USER);
			items->push_back (item);
		
		}
			
		pk_backend_job_set_percentage (job, 40);

		if (!to_install) {
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_ALL_PACKAGES_ALREADY_INSTALLED,
				"The packages are already all installed");
			return;
		}

		// Todo: ideally we should call pk_backend_job_package (...
		// PK_INFO_ENUM_DOWNLOADING | INSTALLING) for each package.
		if (!zypp_perform_execution (job, zypp, INSTALL, FALSE, transaction_flags)) {
			// reset the status of the marked packages
			for (vector<PoolItem>::iterator it = items->begin (); it != items->end (); ++it) {
				it->statusReset ();
			}
			delete (items);
			pk_backend_job_finished (job);
			return;
		}
		delete (items);

		pk_backend_job_set_percentage (job, 100);

	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
		return;
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_install_packages:
 */
void
pk_backend_install_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	// For now, don't let the user cancel the install once it's started
	pk_backend_job_set_allow_cancel (job, FALSE);
	pk_backend_job_thread_create (job, backend_install_packages_thread, NULL, NULL);
}


static void
backend_install_signature_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	const gchar *key_id;
	const gchar *package_id;

	g_variant_get(params, "(&s&s)",
		&key_id,
		&package_id);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_SIG_CHECK);
	priv->signatures.push_back ((string)(key_id));

	pk_backend_job_finished (job);
}

/**
 * pk_backend_install_signature:
 */
void
pk_backend_install_signature (PkBackend *backend, PkBackendJob *job, PkSigTypeEnum type, const gchar *key_id, const gchar *package_id)
{
	pk_backend_job_thread_create (job, backend_install_signature_thread, NULL, NULL);
}

static void
backend_remove_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	PoolStatusSaver saver;
	PkBitfield transaction_flags = 0;
	gboolean autoremove = false;
	gboolean allow_deps = false;
	gchar **package_ids;
	vector<PoolItem> *items = new vector<PoolItem> ();

	g_variant_get(params, "(t^a&sbb)",
		      &transaction_flags,
		      &package_ids,
		      &allow_deps,
		      &autoremove);
	
	pk_backend_job_set_status (job, PK_STATUS_ENUM_REMOVE);
	pk_backend_job_set_percentage (job, 0);

	Target_Ptr target;
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}
	zypp->resolver()->setCleandepsOnRemove(autoremove);

	target = zypp->target ();

	// Load all the local system "resolvables" (packages)
	target->load ();
	pk_backend_job_set_percentage (job, 10);

	for (guint i = 0; package_ids[i]; i++) {
		sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);
		
		if (zypp_is_no_solvable(solvable)) {
			zypp_backend_finished_error (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						     "couldn't find package");
			return;
		}
		PoolItem item(solvable);
		if (solvable.isSystem ()) {
			item.status ().setToBeUninstalled (ResStatus::USER);
			items->push_back (item);
		} else {
			item.status ().resetTransact (ResStatus::USER);
		}
	}

	pk_backend_job_set_percentage (job, 40);

	try
	{
		if (!zypp_perform_execution (job, zypp, REMOVE, TRUE, transaction_flags)) {
			//reset the status of the marked packages
			for (vector<PoolItem>::iterator it = items->begin (); it != items->end (); ++it) {
				it->statusReset();
			}
			delete (items);
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_TRANSACTION_ERROR,
				"Couldn't remove the package");
			return;
		}

		delete (items);
		pk_backend_job_set_percentage (job, 100);

	} catch (const repo::RepoNotFoundException &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str());
		return;
	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
		return;
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_remove_packages:
 */
void
pk_backend_remove_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags,
			    gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	pk_backend_job_thread_create (job, backend_remove_packages_thread, NULL, NULL);
}

static void
backend_resolve_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	gchar **search;
	PkBitfield _filters;
	
	g_variant_get(params, "(t^a&s)",
		      &_filters,
		      &search);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();
	
	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}
	
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	zypp_build_pool (zypp, TRUE);

	for (uint i = 0; search[i]; i++) {
		MIL << search[i] << " " << pk_filter_bitfield_to_string(_filters) << endl;
		vector<sat::Solvable> v;
		
		/* build a list of packages with this name */
		zypp_get_packages_by_name (search[i], ResKind::package, v);

		/* add source packages */
		if (!pk_bitfield_contain (_filters, PK_FILTER_ENUM_NOT_SOURCE)) {
			vector<sat::Solvable> src;
			zypp_get_packages_by_name (search[i], ResKind::srcpackage, src);
			v.insert (v.end (), src.begin (), src.end ());
		}

		/* include patches too */
		vector<sat::Solvable> v2;
		zypp_get_packages_by_name (search[i], ResKind::patch, v2);
		v.insert (v.end (), v2.begin (), v2.end ());

		/* include patterns too */
		zypp_get_packages_by_name (search[i], ResKind::pattern, v2);
		v.insert (v.end (), v2.begin (), v2.end ());

		sat::Solvable newest;
		vector<sat::Solvable> pkgs;

		/* Filter the list of packages with this name to 'pkgs' */
		for (vector<sat::Solvable>::iterator it = v.begin (); it != v.end (); ++it) {

			MIL << "found " << *it << endl;

			if (zypp_filter_solvable (_filters, *it) ||
			    zypp_is_no_solvable(*it))
				continue;
			
			if (zypp_is_no_solvable(newest)) {
				newest = *it;
			} else if (it->edition() > newest.edition() || Arch::compare(it->arch(), newest.arch()) > 0) {
				newest = *it;
			}
			MIL << "emit " << *it << endl;
			pkgs.push_back (*it);
		}
		
		if (!zypp_is_no_solvable(newest)) {
			
			/* 'newest' filter support */
			if (pk_bitfield_contain (_filters, PK_FILTER_ENUM_NEWEST)) {
				pkgs.clear();
				MIL << "emit just newest " << newest << endl;
				pkgs.push_back (newest);
			} else if (pk_bitfield_contain (_filters, PK_FILTER_ENUM_NOT_NEWEST)) {
				pkgs.erase (find (pkgs.begin (), pkgs.end(), newest));
			}
		}
		
		zypp_emit_filtered_packages_in_list (job, _filters, pkgs);
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_resolve:
 */
void
pk_backend_resolve (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids)
{
	pk_backend_job_thread_create (job, backend_resolve_thread, NULL, NULL);
}

static void
backend_find_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	const gchar *search;
	PkRoleEnum role;

	PkBitfield _filters;
	gchar **values;
	g_variant_get(params, "(t^a&s)",
		&_filters,
		&values);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();
	
	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	// refresh the repos before searching
	if (!zypp_refresh_cache (job, zypp, FALSE)) {
		pk_backend_job_finished (job);
		return;
	}

	search = values[0];  //Fixme - support the possible multiple values (logical OR search)
	role = pk_backend_job_get_role(job);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, PK_BACKEND_PERCENTAGE_INVALID);

	vector<sat::Solvable> v;

	PoolQuery q;
	q.addString( search ); // may be called multiple times (OR'ed)
	q.setCaseSensitive( true );
	q.setMatchSubstring();

	switch (role) {
	case PK_ROLE_ENUM_SEARCH_NAME:
		zypp_build_pool (zypp, TRUE); // seems to be necessary?
		q.addKind( ResKind::package );
		q.addKind( ResKind::srcpackage );
		q.addAttribute( sat::SolvAttr::name );
		// Note: The query result is NOT sorted packages first, then srcpackage.
		// If that's necessary you need to sort the vector accordongly or use
		// two separate queries.
		break;
	case PK_ROLE_ENUM_SEARCH_DETAILS:
		zypp_build_pool (zypp, TRUE); // seems to be necessary?
		q.addKind( ResKind::package );
		//q.addKind( ResKind::srcpackage );
		q.addAttribute( sat::SolvAttr::name );
		q.addAttribute( sat::SolvAttr::description );
		// Note: Don't know if zypp_get_packages_by_details intentionally
		// did not search in srcpackages.
		break;
	case PK_ROLE_ENUM_SEARCH_FILE: {
		zypp_build_pool (zypp, TRUE);
		q.addKind( ResKind::package );
		q.addAttribute( sat::SolvAttr::name );
		q.addAttribute( sat::SolvAttr::description );
		q.addAttribute( sat::SolvAttr::filelist );
		q.setFilesMatchFullPath(true);
		q.setMatchExact();
		break;
	}
	default:
		break;
	};

	if ( ! q.empty() ) {
		copy( q.begin(), q.end(), back_inserter( v ) );
	}
	zypp_emit_filtered_packages_in_list (job, _filters, v);

	pk_backend_job_finished (job);
}

/**
 * pk_backend_search_name:
 */
void
pk_backend_search_names (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create (job, backend_find_packages_thread, NULL, NULL);
}

/**
 * pk_backend_search_details:
 */
void
pk_backend_search_details (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create (job, backend_find_packages_thread, NULL, NULL);
}

static void
backend_search_group_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	const gchar *group;

	gchar **search;
	PkBitfield _filters;
	g_variant_get(params, "(t^a&s)",
		&_filters,
		&search);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	group = search[0];  //Fixme - add support for possible multiple values.

	if (group == NULL) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_GROUP_NOT_FOUND, "Group is invalid.");
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	ResPool pool = zypp_build_pool (zypp, true);

	pk_backend_job_set_percentage (job, 30);

	vector<sat::Solvable> v;
	PkGroupEnum pkGroup = pk_group_enum_from_string (group);

	sat::LookupAttr look (sat::SolvAttr::group);

	for (sat::LookupAttr::iterator it = look.begin (); it != look.end (); ++it) {
		PkGroupEnum rpmGroup = get_enum_group (it.asString ());
		if (pkGroup == rpmGroup)
			v.push_back (it.inSolvable ());
	}

	pk_backend_job_set_percentage (job, 70);

	zypp_emit_filtered_packages_in_list (job, _filters, v);

	pk_backend_job_set_percentage (job, 100);
	pk_backend_job_finished (job);
}

/**
 * pk_backend_search_group:
 */
void
pk_backend_search_groups (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create (job, backend_search_group_thread, NULL, NULL);
}

/**
 * pk_backend_search_file:
 */
void
pk_backend_search_files (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create (job, backend_find_packages_thread, NULL, NULL);
}

/**
 * backend_get_repo_list:
 */
void
pk_backend_get_repo_list (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	MIL << endl;

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	RepoManager manager;
	list <RepoInfo> repos;
	try
	{
		repos = list<RepoInfo>(manager.repoBegin(),manager.repoEnd());
	} catch (const repo::RepoNotFoundException &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str());
		return;
	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
		return;
	}

	for (list <RepoInfo>::iterator it = repos.begin(); it != repos.end(); ++it) {
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT) && zypp_is_development_repo (*it))
			continue;
		// RepoInfo::alias - Unique identifier for this source.
		// RepoInfo::name - Short label or description of the
		// repository, to be used on the user interface
		pk_backend_job_repo_detail (job,
					it->alias().c_str(),
					it->name().c_str(),
					it->enabled());
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_repo_enable:
 */
void
pk_backend_repo_enable (PkBackend *backend, PkBackendJob *job, const gchar *rid, gboolean enabled)
{
	MIL << endl;
	
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	RepoManager manager;
	RepoInfo repo;

	try {
		repo = manager.getRepositoryInfo (rid);
		if (!zypp_is_valid_repo (job, repo)){
			pk_backend_job_finished (job);
			return;
		}
		repo.setEnabled (enabled);
		manager.modifyRepository (rid, repo);
		if (!enabled) {
			Repository repository = sat::Pool::instance ().reposFind (repo.alias ());
			repository.eraseFromPool ();
		}

	} catch (const repo::RepoNotFoundException &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str());
		return;
	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
		return;
	}

	pk_backend_job_finished (job);
}

static void
backend_get_files_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;

	gchar **package_ids;
	g_variant_get(params, "(^a&s)",
		      &package_ids);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	for (uint i = 0; package_ids[i]; i++) {
		pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
		sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);
		
		if (zypp_is_no_solvable(solvable)) {
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				"couldn't find package");
			return;
		}

		string temp;
		if (solvable.isSystem ()){
			try {
				target::rpm::RpmHeader::constPtr rpmHeader = zypp_get_rpmHeader (solvable.name (), solvable.edition ());
				list<string> files = rpmHeader->tag_filenames ();

				for (list<string>::iterator it = files.begin (); it != files.end (); ++it) {
					temp.append (*it);
					temp.append (";");
				}

			} catch (const target::rpm::RpmException &ex) {
				zypp_backend_finished_error (job, PK_ERROR_ENUM_REPO_NOT_FOUND,
							     "Couldn't open rpm-database");
				return;
			}
		} else {
			temp = "Only available for installed packages";
		}

		pk_backend_job_files (job, package_ids[i], temp.c_str ());	// file_list
	}

	pk_backend_job_finished (job);
}

/**
  * pk_backend_get_files:
  */
void
pk_backend_get_files(PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	pk_backend_job_thread_create (job, backend_get_files_thread, NULL, NULL);
}

static void
backend_get_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;

	PkBitfield _filters;
	g_variant_get (params, "(t)",
		       &_filters);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	vector<sat::Solvable> v;

	zypp_build_pool (zypp, TRUE);
	ResPool pool = ResPool::instance ();
	for (ResPool::byKind_iterator it = pool.byKindBegin (ResKind::package); it != pool.byKindEnd (ResKind::package); ++it) {
		v.push_back (it->satSolvable ());
	}

	zypp_emit_filtered_packages_in_list (job, _filters, v);

	pk_backend_job_finished (job);
}
/**
  * pk_backend_get_packages:
  */
void
pk_backend_get_packages (PkBackend *backend, PkBackendJob *job, PkBitfield filter)
{
	pk_backend_job_thread_create (job, backend_get_packages_thread, NULL, NULL);
}

static void
backend_update_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	PoolStatusSaver saver;

	PkBitfield transaction_flags = 0;
	gchar **package_ids;
	g_variant_get(params, "(t^a&s)",
		      &transaction_flags,
		      &package_ids);
	
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}
	ResPool pool = zypp_build_pool (zypp, TRUE);
	PkRestartEnum restart = PK_RESTART_ENUM_NONE;

	for (guint i = 0; package_ids[i]; i++) {
		sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);
		PoolItem item(solvable);
		item.status ().setToBeInstalled (ResStatus::USER);
		Patch::constPtr patch = asKind<Patch>(item.resolvable ());
		zypp_check_restart (&restart, patch);
		if (restart != PK_RESTART_ENUM_NONE){
			pk_backend_job_require_restart (job, restart, package_ids[i]);
			restart = PK_RESTART_ENUM_NONE;
		}
	}

	zypp_perform_execution (job, zypp, UPDATE, FALSE, transaction_flags);

	pk_backend_job_finished (job);
}

/**
  * pk_backend_update_packages
  */
void
pk_backend_update_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	pk_backend_job_thread_create (job, backend_update_packages_thread, NULL, NULL);
}

static void
backend_repo_set_data_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	const gchar *repo_id;
	const gchar *parameter;
	const gchar *value;

	g_variant_get(params, "(&s&s&s)",
		      &repo_id,
		      &parameter,
		      &value);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();
		
	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	RepoManager manager;
	RepoInfo repo;

	try {
		pk_backend_job_set_status(job, PK_STATUS_ENUM_SETUP);
		if (g_ascii_strcasecmp (parameter, "add") != 0) {
			repo = manager.getRepositoryInfo (repo_id);
			if (!zypp_is_valid_repo (job, repo)){
				pk_backend_job_finished (job);
				return;
			}
		}
		// add a new repo
		if (g_ascii_strcasecmp (parameter, "add") == 0) {
			repo.setAlias (repo_id);
			repo.setBaseUrl (Url(value));
			repo.setAutorefresh (TRUE);
			repo.setEnabled (TRUE);

			manager.addRepository (repo);

		// remove a repo
		} else if (g_ascii_strcasecmp (parameter, "remove") == 0) {
			manager.removeRepository (repo);
		// set autorefresh of a repo true/false
		} else if (g_ascii_strcasecmp (parameter, "refresh") == 0) {

			if (g_ascii_strcasecmp (value, "true") == 0) {
				repo.setAutorefresh (TRUE);
			} else if (g_ascii_strcasecmp (value, "false") == 0) {
				repo.setAutorefresh (FALSE);
			} else {
				pk_backend_job_message (job, PK_MESSAGE_ENUM_PARAMETER_INVALID, "Autorefresh a repo: Enter true or false");
			}

			manager.modifyRepository (repo_id, repo);
		} else if (g_ascii_strcasecmp (parameter, "keep") == 0) {

			if (g_ascii_strcasecmp (value, "true") == 0) {
				repo.setKeepPackages (TRUE);
			} else if (g_ascii_strcasecmp (value, "false") == 0) {
				repo.setKeepPackages (FALSE);
			} else {
				pk_backend_job_message (job, PK_MESSAGE_ENUM_PARAMETER_INVALID, "Keep downloaded packages: Enter true or false");
			}

			manager.modifyRepository (repo_id, repo);
		} else if (g_ascii_strcasecmp (parameter, "url") == 0) {
			repo.setBaseUrl (Url(value));
			manager.modifyRepository (repo_id, repo);
		} else if (g_ascii_strcasecmp (parameter, "name") == 0) {
			repo.setName(value);
			manager.modifyRepository (repo_id, repo);
		} else if (g_ascii_strcasecmp (parameter, "prio") == 0) {
			gint prio = 0;
			gint length = strlen (value);

			if (length > 2) {
				pk_backend_job_message (job, PK_MESSAGE_ENUM_PRIORITY_INVALID, "Priorities has to be between 1 (highest) and 99");
			} else {
				for (gint i = 0; i < length; i++) {
					gint tmp = g_ascii_digit_value (value[i]);

					if (tmp == -1) {
						pk_backend_job_message (job, PK_MESSAGE_ENUM_PRIORITY_INVALID, "Priorities has to be a number between 1 (highest) and 99");
						prio = 0;
						break;
					} else {
						if (length == 2 && i == 0) {
							prio = tmp * 10;
						} else {
							prio = prio + tmp;
						}
					}
				}

				if (prio != 0) {
					repo.setPriority (prio);
					manager.modifyRepository (repo_id, repo);
				}
			}

		} else {
			pk_backend_job_error_code (job, PK_ERROR_ENUM_NOT_SUPPORTED, "Valid parameters for set_repo_data are remove/add/refresh/prio/keep/url/name");
		}

	} catch (const repo::RepoNotFoundException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_FOUND, "Couldn't find the specified repository");
	} catch (const repo::RepoAlreadyExistsException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, "This repo already exists");
	} catch (const repo::RepoUnknownTypeException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, "Type of the repo can't be determined");
	} catch (const repo::RepoException &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, "Can't access the given URL");
	} catch (const Exception &ex) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asString ().c_str ());
	}

	pk_backend_job_finished (job);
}

/**
  * pk_backend_repo_set_data
  */
void
pk_backend_repo_set_data (PkBackend *backend, PkBackendJob *job, const gchar *repo_id, const gchar *parameter, const gchar *value)
{
	pk_backend_job_thread_create (job, backend_repo_set_data_thread, NULL, NULL);
}

static void
backend_what_provides_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	
	gchar **values;
	PkBitfield _filters;
	PkProvidesEnum provides;
	g_variant_get(params, "(tu^a&s)",
		      &_filters,
		      &provides,
		      &values);
	
	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	const gchar *search = values[0]; //Fixme - support possible multi1ple search values (logical OR)
	ResPool pool = zypp_build_pool (zypp, true);

	if((provides == PK_PROVIDES_ENUM_HARDWARE_DRIVER) || g_ascii_strcasecmp("drivers_for_attached_hardware", search) == 0) {
		// solver run
		Resolver solver(pool);
		solver.setIgnoreAlreadyRecommended (TRUE);

		if (!solver.resolvePool ()) {
			list<ResolverProblem_Ptr> problems = solver.problems ();
			for (list<ResolverProblem_Ptr>::iterator it = problems.begin (); it != problems.end (); ++it){
				g_warning("Solver problem (This should never happen): '%s'", (*it)->description ().c_str ());
			}
			solver.setIgnoreAlreadyRecommended (FALSE);
			zypp_backend_finished_error (
				job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "Resolution failed");
			return;
		}

		// look for packages which would be installed
		for (ResPool::byKind_iterator it = pool.byKindBegin (ResKind::package);
				it != pool.byKindEnd (ResKind::package); ++it) {
			PkInfoEnum status = PK_INFO_ENUM_UNKNOWN;

			gboolean hit = FALSE;

			if (it->status ().isToBeInstalled ()) {
				status = PK_INFO_ENUM_AVAILABLE;
				hit = TRUE;
			}

			if (hit && !zypp_filter_solvable (_filters, it->resolvable()->satSolvable())) {
				zypp_backend_package (job, status, it->resolvable()->satSolvable(),
						      it->resolvable ()->summary ().c_str ());
			}
			it->statusReset ();
		}
		solver.setIgnoreAlreadyRecommended (FALSE);
	} else {
		Capability cap (search);
		sat::WhatProvides prov (cap);

		for (sat::WhatProvides::const_iterator it = prov.begin (); it != prov.end (); ++it) {
			if (zypp_filter_solvable (_filters, *it))
				continue;

			PkInfoEnum info = it->isSystem () ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;
			zypp_backend_package (job, info, *it,  make<ResObject>(*it)->summary().c_str ());
		}
	}

	pk_backend_job_finished (job);
}

/**
  * pk_backend_what_provides
  */
void
pk_backend_what_provides (PkBackend *backend, PkBackendJob *job, PkBitfield filters, PkProvidesEnum provide, gchar **values)
{
	pk_backend_job_thread_create (job, backend_what_provides_thread, NULL, NULL);
}

gchar **
pk_backend_get_mime_types (PkBackend *backend)
{
	const gchar *mime_types[] = {
				"application/x-rpm",
				NULL };
	return g_strdupv ((gchar **) mime_types);
}

static void
backend_download_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	MIL << endl;
	gchar **package_ids;
	gulong size = 0;
	const gchar *tmpDir;

	g_variant_get(params, "(^a&ss)",
		      &package_ids,
		      &tmpDir);

	ZyppJob zjob(job);
	ZYpp::Ptr zypp = zjob.get_zypp();

	if (zypp == NULL){
		pk_backend_job_finished (job);
		return;
	}

	if (!zypp_refresh_cache (job, zypp, FALSE)) {
		pk_backend_job_finished (job);
		return;
	}

	try
	{
		ResPool pool = zypp_build_pool (zypp, FALSE);

		pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);
		for (guint i = 0; package_ids[i]; i++) {
			sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);

			if (zypp_is_no_solvable(solvable)) {
				zypp_backend_finished_error (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
							     "couldn't find package");
				return;
			}

			PoolItem item(solvable);
			size += 2 * make<ResObject>(solvable)->downloadSize();

			filesystem::Pathname repo_dir = solvable.repository().info().packagesPath();
			struct statfs stat;
			statfs(repo_dir.c_str(), &stat);
			if (size > stat.f_bavail * 4) {
				pk_backend_job_error_code (job, PK_ERROR_ENUM_NO_SPACE_ON_DEVICE,
					"Insufficient space in download directory '%s'.", repo_dir.c_str());
				pk_backend_job_finished (job);
				return;
			}

			repo::RepoMediaAccess access;
			repo::DeltaCandidates deltas;
			ManagedFile tmp_file;
			if (isKind<SrcPackage>(solvable)) {
				SrcPackage::constPtr package = asKind<SrcPackage>(item.resolvable());
				repo::SrcPackageProvider pkgProvider(access);
				tmp_file = pkgProvider.provideSrcPackage(package);
			} else {
				Package::constPtr package = asKind<Package>(item.resolvable());
				repo::PackageProvider pkgProvider(access, package, deltas);
				tmp_file = pkgProvider.providePackage();
			}
			string target = tmpDir;
			// be sure it ends with /
			target += "/";
			target += tmp_file->basename();
			filesystem::hardlinkCopy(tmp_file, target);
			pk_backend_job_files (job, package_ids[i], target.c_str());
			pk_backend_job_package (job, PK_INFO_ENUM_DOWNLOADING, package_ids[i], item->summary ().c_str());
		}
	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			job, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED, ex.asUserString().c_str());
		return;
	}

	pk_backend_job_finished (job);
}

/**
 * pk_backend_download_packages:
 */
void
pk_backend_download_packages (PkBackend *backend, PkBackendJob *job, gchar **package_ids, const gchar *directory)
{
	pk_backend_job_thread_create (job, backend_download_packages_thread, NULL, NULL);
}

/**
 * pk_backend_start_job:
 */
void
pk_backend_start_job (PkBackend *backend, PkBackendJob *job)
{
	gchar *locale;
	gchar *proxy_http;
	gchar *proxy_https;
	gchar *proxy_ftp;
	gchar *uri;
	gchar *proxy_socks;
	gchar *no_proxy;
	gchar *pac;

	locale = pk_backend_job_get_locale(job);
	if (!pk_strzero (locale)) {
		setlocale(LC_ALL, locale);
	}

	/* http_proxy */
	proxy_http = pk_backend_job_get_proxy_http (job);
	if (!pk_strzero (proxy_http)) {
		uri = pk_backend_spawn_convert_uri (proxy_http);
		g_setenv ("http_proxy", uri, TRUE);
		g_free (uri);
	}

	/* https_proxy */
	proxy_https = pk_backend_job_get_proxy_https (job);
	if (!pk_strzero (proxy_https)) {
		uri = pk_backend_spawn_convert_uri (proxy_https);
		g_setenv ("https_proxy", uri, TRUE);
		g_free (uri);
	}

	/* ftp_proxy */
	proxy_ftp = pk_backend_job_get_proxy_ftp (job);
	if (!pk_strzero (proxy_ftp)) {
		uri = pk_backend_spawn_convert_uri (proxy_ftp);
		g_setenv ("ftp_proxy", uri, TRUE);
		g_free (uri);
	}

	/* socks_proxy */
	proxy_socks = pk_backend_job_get_proxy_socks (job);
	if (!pk_strzero (proxy_socks)) {
		uri = pk_backend_spawn_convert_uri (proxy_socks);
		g_setenv ("socks_proxy", uri, TRUE);
		g_free (uri);
	}

	/* no_proxy */
	no_proxy = pk_backend_job_get_no_proxy (job);
	if (!pk_strzero (no_proxy)) {
		g_setenv ("no_proxy", no_proxy, TRUE);
	}

	/* pac */
	pac = pk_backend_job_get_pac (job);
	if (!pk_strzero (pac)) {
		uri = pk_backend_spawn_convert_uri (pac);
		g_setenv ("pac", uri, TRUE);
		g_free (uri);
	}

	g_free (locale);
	g_free (proxy_http);
	g_free (proxy_https);
	g_free (proxy_ftp);
	g_free (proxy_socks);
	g_free (no_proxy);
	g_free (pac);
}

/**
 * pk_backend_stop_job:
 */
void
pk_backend_stop_job (PkBackend *backend, PkBackendJob *job)
{
	/* unset proxy info for this transaction */
	g_unsetenv ("http_proxy");
	g_unsetenv ("ftp_proxy");
	g_unsetenv ("https_proxy");
	g_unsetenv ("no_proxy");
	g_unsetenv ("socks_proxy");
	g_unsetenv ("pac");
}


