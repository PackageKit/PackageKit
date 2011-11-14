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
    PoolStatusSaver()
    {
	    ResPool::instance().proxy().saveState();
    }

    ~PoolStatusSaver()
    {
	    ResPool::instance().proxy().restoreState();
    }
};

/**
 * pk_backend_get_description:
 */
gchar *
pk_backend_get_description (PkBackend *backend)
{
	return g_strdup ("ZYpp package manager");
}

/**
 * pk_backend_get_author:
 */
gchar *
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
	zypp_logging ();
	g_debug ("zypp_backend_initialize");
	EventDirector *eventDirector = new EventDirector (backend);
	priv->eventDirectors[backend] = eventDirector;
	vector<string> *signature = new vector<string>();
	priv->signatures[backend] = signature;
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

	delete priv->eventDirectors [backend];
	priv->eventDirectors.erase(backend);
	delete priv->signatures[backend];
	priv->signatures.erase(backend);
	g_free (_repoName);
	delete priv;
}

/**
  * backend_get_requires_thread:
  */
static gboolean
backend_get_requires_thread (PkBackend *backend)
{
	MIL << endl;
	gchar **package_ids;
	PkBitfield _filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}

	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (!pk_package_ids_check (package_ids)) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	//TODO repair percentages
	//pk_backend_set_percentage (backend, 0);

	PoolStatusSaver saver;
	ResPool pool = zypp_build_pool (backend, true);
	for (uint i = 0; package_ids[i]; i++) {
		sat::Solvable solvable = zypp_get_package_by_id (backend, package_ids[i]);
		PoolItem package;

		if (solvable.isSystem ()) {
			gboolean found = FALSE;
			gchar **id_parts = pk_package_id_split (package_ids[i]);

			for (ResPool::byIdent_iterator it = pool.byIdentBegin (ResKind::package, id_parts[PK_PACKAGE_ID_NAME]);
					it != pool.byIdentEnd (ResKind::package, id_parts[PK_PACKAGE_ID_NAME]); it++) {
				if (it->status ().isInstalled ()) {
					package = (*it);
					found = TRUE;
				}
			}
			g_strfreev (id_parts);

			if (!found) {
				return zypp_backend_finished_error (
					backend, PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED,
					"Package is not installed");
			}

			// set Package as to be uninstalled
			package.status ().setToBeUninstalled (ResStatus::USER);
		} else {
			if (solvable == sat::Solvable::noSolvable) {
				return zypp_backend_finished_error (
					backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					"Package couldn't be found");
			}

			ResPool pool = ResPool::instance ();
			package = pool.find (solvable);
			//set Package as to be installed
			package.status ().setToBeInstalled (ResStatus::USER);
		}

		// solver run
		ResPool pool = ResPool::instance ();
		Resolver solver(pool);

		solver.setForceResolve (true);

		if (!solver.resolvePool ()) {
			list<ResolverProblem_Ptr> problems = solver.problems ();
			for (list<ResolverProblem_Ptr>::iterator it = problems.begin (); it != problems.end (); it++){
				g_warning("Solver problem (This should never happen): '%s'", (*it)->description ().c_str ());
			}
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
				"Resolution failed");
		}

		// look for packages which would be uninstalled
		bool error = false;
		for (ResPool::byKind_iterator it = pool.byKindBegin (ResKind::package);
				it != pool.byKindEnd (ResKind::package); it++) {

			if (!error && !zypp_filter_solvable (_filters, it->resolvable()->satSolvable()))
				error = !zypp_backend_pool_item_notify (backend, *it);
		}

		solver.setForceResolve (false);
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
  * pk_backend_get_requires:
  */
void
pk_backend_get_requires(PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_thread_create (backend, backend_get_requires_thread);
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
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_ARCH,
		PK_FILTER_ENUM_NEWEST,
		PK_FILTER_ENUM_SOURCE,
		-1);
}

static bool
zypp_is_no_solvable (const sat::Solvable &solv)
{
	return solv.id() == sat::detail::noSolvableId;
}

/*
 * This method is a bit of a travesty of the complexity of
 * solving dependencies. We try to give a simple answer to
 * "what packages are required for these packages" - but,
 * clearly often there is no simple answer.
 */
static gboolean
backend_get_depends_thread (PkBackend *backend)
{
	MIL << endl;
	gchar **package_ids;
	PkBitfield _filters = (PkBitfield) pk_backend_get_uint (backend, "filters");

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (!pk_package_ids_check (package_ids)) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID,
			"invalid package id");
	}

	ZYpp::Ptr zypp;
	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}

	g_debug ("get_depends with filter '%s'", pk_filter_bitfield_to_string (_filters));

	try
	{
		gchar **id_parts = pk_package_id_split (package_ids[0]);
		pk_backend_set_percentage (backend, 20);
		// Load resolvables from all the enabled repositories
		ResPool pool = zypp_build_pool (backend, true);

		PoolItem pool_item;
		gboolean pool_item_found = FALSE;
		// Iterate over the resolvables and mark the one we want to check its dependencies
		for (ResPool::byIdent_iterator it = pool.byIdentBegin (ResKind::package, id_parts[PK_PACKAGE_ID_NAME]);
				it != pool.byIdentEnd (ResKind::package, id_parts[PK_PACKAGE_ID_NAME]); it++) {
			PoolItem selectable = *it;
			if (strcmp (selectable->name().c_str(), id_parts[PK_PACKAGE_ID_NAME]) == 0) {
				// This package matches the name we're looking
				char *edition_str = g_strdup (selectable->edition ().asString ().c_str());

				if (strcmp (edition_str, id_parts[PK_PACKAGE_ID_VERSION]) == 0) {
					g_free (edition_str);
					// this is the one, mark it to be installed
					pool_item = selectable;
					pool_item_found = TRUE;
					pk_backend_set_percentage (backend, 20);
					break; // Found it, get out of the for loop
				}
				g_free (edition_str);
			}
		}
		g_strfreev (id_parts);

		pk_backend_set_percentage (backend, 40);

		if (!pool_item_found) {
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
				"Did not find the specified package.");
		}

		// Gather up any dependencies
		pk_backend_set_status (backend, PK_STATUS_ENUM_DEP_RESOLVE);
		pk_backend_set_percentage (backend, 60);

		// get dependencies

		sat::Solvable solvable = pool_item.satSolvable ();
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
				it++) {

			// backup sanity check for no-solvables
			if (! it->second.name ().c_str() ||
			    it->second.name ().c_str()[0] == '\0')
				continue;

			PoolItem item = ResPool::instance ().find (it->second);
			PkInfoEnum info = it->second.isSystem () ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;

			g_debug ("add dep - '%s' '%s' %d [%s]", it->second.name().c_str(),
				   info == PK_INFO_ENUM_INSTALLED ? "installed" : "available",
				   it->second.isSystem(),
				   zypp_filter_solvable (_filters, it->second) ? "don't add" : "add" );

			if (!zypp_filter_solvable (_filters, it->second)) {
				zypp_backend_package (backend, info, it->second,
						      item->summary ().c_str());
			}
		}

		pk_backend_set_percentage (backend, 100);
	} catch (const repo::RepoNotFoundException &ex) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str());
	} catch (const Exception &ex) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * pk_backend_get_depends:
 */
void
pk_backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_thread_create (backend, backend_get_depends_thread);
}

static gboolean
backend_get_details_thread (PkBackend *backend)
{
	MIL << endl;
	gchar **package_ids;
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}

	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (!pk_package_ids_check (package_ids)) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
	}
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	for (uint i = 0; package_ids[i]; i++) {
		gchar **id_parts = pk_package_id_split (package_ids[i]);

		vector<sat::Solvable> v;
		vector<sat::Solvable> v2;
		vector<sat::Solvable> v3;
		zypp_get_packages_by_name (backend, (const gchar *)id_parts[PK_PACKAGE_ID_NAME], ResKind::package, v);
		zypp_get_packages_by_name (backend, (const gchar *)id_parts[PK_PACKAGE_ID_NAME], ResKind::patch, v2);
		zypp_get_packages_by_name (backend, (const gchar *)id_parts[PK_PACKAGE_ID_NAME], ResKind::srcpackage, v3);

		v.insert (v.end (), v2.begin (), v2.end ());
		v.insert (v.end (), v3.begin (), v3.end ());

		sat::Solvable package;
		for (vector<sat::Solvable>::iterator it = v.begin ();
				it != v.end (); it++) {
			if (zypp_ver_and_arch_equal (*it, id_parts[PK_PACKAGE_ID_VERSION],
						     id_parts[PK_PACKAGE_ID_ARCH])) {
				package = *it;
				break;
			}
		}
		g_strfreev (id_parts);

		if (package == NULL) {
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "couldn't find package");
		}

		try {
			PkGroupEnum group = get_enum_group (zypp_get_group (package));

			if (package.isSystem ()){
				target::rpm::RpmHeader::constPtr rpmHeader = zypp_get_rpmHeader (package.name (), package.edition ());

				pk_backend_details (backend,
					package_ids[i],			  // package_id
					rpmHeader->tag_license ().c_str (),     // const gchar *license
					group,				  // PkGroupEnum group
					package.lookupStrAttribute (sat::SolvAttr::description).c_str (), //pkg->description ().c_str (),
					rpmHeader->tag_url (). c_str (),	// const gchar *url
					(gulong)rpmHeader->tag_archivesize ());	// gulong size

			} else {
				gulong size = 0;

				if (isKind<Patch>(package)) {
					PoolItem item = ResPool::instance ().find (package);
					Patch::constPtr patch = asKind<Patch>(item);

					sat::SolvableSet content = patch->contents ();
					for (sat::SolvableSet::const_iterator it = content.begin (); it != content.end (); it++)
						size += it->lookupNumAttribute (sat::SolvAttr::downloadsize);
				} else
					size = package.lookupNumAttribute (sat::SolvAttr::downloadsize);

				pk_backend_details (backend,
						    package_ids[i],
						    package.lookupStrAttribute (sat::SolvAttr::license).c_str (),
						    group,
						    package.lookupStrAttribute (sat::SolvAttr::description).c_str (),
						    package.lookupStrAttribute (sat::SolvAttr::url).c_str (),
						    size * 1024);
			}

		} catch (const target::rpm::RpmException &ex) {
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "Couldn't open rpm-database");
		} catch (const Exception &ex) {
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString ().c_str ());
		}
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * pk_backend_get_details:
 */
void
pk_backend_get_details (PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_get_details_thread);
}

static gboolean
backend_get_distro_upgrades_thread(PkBackend *backend)
{
	MIL << endl;
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	// refresh the repos before checking for updates
	if (!zypp_refresh_cache (backend, FALSE)) {
		pk_backend_finished (backend);
		return FALSE;
	}

	vector<parser::ProductFileData> result;
	if (!parser::ProductFileReader::scanDir (functor::getAll (back_inserter (result)), "/etc/products.d")) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Could not parse /etc/products.d");
	}

	for (vector<parser::ProductFileData>::iterator it = result.begin (); it != result.end (); it++) {
		vector<parser::ProductFileData::Upgrade> upgrades = it->upgrades();
		for (vector<parser::ProductFileData::Upgrade>::iterator it2 = upgrades.begin (); it2 != upgrades.end (); it2++) {
			if (it2->notify ()){
				PkDistroUpgradeEnum status = PK_DISTRO_UPGRADE_ENUM_UNKNOWN;
				if (it2->status () == "stable") {
					status = PK_DISTRO_UPGRADE_ENUM_STABLE;
				} else if (it2->status () == "unstable") {
					status = PK_DISTRO_UPGRADE_ENUM_UNSTABLE;
				}
				pk_backend_distro_upgrade (backend,
							   status,
							   it2->name ().c_str (),
							   it2->summary ().c_str ());
			}
		}
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * pk_backend_get_distro_upgrades:
 */
void
pk_backend_get_distro_upgrades (PkBackend *backend)
{
	pk_backend_thread_create (backend, backend_get_distro_upgrades_thread);
}

static gboolean
backend_refresh_cache_thread (PkBackend *backend)
{
	MIL << endl;
	gboolean force = pk_backend_get_bool(backend, "force");
	zypp_refresh_cache (backend, force);
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * pk_backend_refresh_cache
 */
void
pk_backend_refresh_cache (PkBackend *backend, gboolean force)
{
	MIL << endl;
	pk_backend_thread_create (backend, backend_refresh_cache_thread);
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

static gboolean
backend_get_updates_thread (PkBackend *backend)
{
	MIL << endl;
	PkBitfield _filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}

	typedef set<PoolItem>::iterator pi_it_t;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	// refresh the repos before checking for updates
	if (!zypp_refresh_cache (backend, FALSE)) {
		pk_backend_finished (backend);
		return FALSE;
	}

	ResPool pool = zypp_build_pool (backend, TRUE);
	pk_backend_set_percentage (backend, 40);

	// check if the repositories may be dead (feature #301904)
	warn_outdated_repos (backend, pool);

	set<PoolItem> candidates;
	zypp_get_updates (backend, candidates);

	pk_backend_set_percentage (backend, 80);

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
			zypp_backend_package (backend, infoEnum, res->satSolvable (),
					      res->summary ().c_str ());
		}
	}

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * pk_backend_get_updates
 */
void
pk_backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	pk_backend_thread_create (backend, backend_get_updates_thread);
}

static gboolean
backend_install_files_thread (PkBackend *backend)
{
	MIL << endl;
	gchar **full_paths;
	RepoManager manager;
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}

	full_paths = pk_backend_get_strv (backend, "full_paths");

	// create a temporary directory
	filesystem::TmpDir tmpDir;
	if (tmpDir == NULL) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_LOCAL_INSTALL_FAILED,
			"Could not create a temporary directory");
	}

	for (guint i = 0; full_paths[i]; i++) {

		// check if file is really a rpm
		Pathname rpmPath (full_paths[i]);
		target::rpm::RpmHeader::constPtr rpmHeader = target::rpm::RpmHeader::readPackage (rpmPath, target::rpm::RpmHeader::NOSIGNATURE);

		if (rpmHeader == NULL) {
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_LOCAL_INSTALL_FAILED,
				"%s is not valid rpm-File", full_paths[i]);
		}

		// copy the rpm into tmpdir
		string tempDest = tmpDir.path ().asString () + "/" + rpmHeader->tag_name () + ".rpm";
		if (filesystem::copy (full_paths[i], tempDest) != 0) {
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_LOCAL_INSTALL_FAILED,
				"Could not copy the rpm-file into the temp-dir");
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
			return zypp_backend_finished_error (
			  backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Can't refresh repositories");
		}
		zypp_build_pool (backend, true);

	} catch (const Exception &ex) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString ().c_str ());
	}

	bool error = false;

	Repository repo = ResPool::instance().reposFind("PK_TMP_DIR");

	for_(it, repo.solvablesBegin(), repo.solvablesEnd()){
		MIL << "Setting " << *it << " for installation" << endl;
		PoolItem(*it).status().setToBeInstalled(ResStatus::USER);
	}

	if (!zypp_perform_execution (backend, INSTALL, FALSE)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_LOCAL_INSTALL_FAILED, "Could not install the rpm-file.");
	}

	// remove tmp-dir and the tmp-repo
	try {
		manager.removeRepository (tmpRepo);
	} catch (const repo::RepoNotFoundException &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str() );
	}

	pk_backend_finished (backend);
	return !error;
}

/**
  * pk_backend_install_files
  */
void
pk_backend_install_files (PkBackend *backend, gboolean only_trusted, gchar **full_paths)
{
	pk_backend_thread_create (backend, backend_install_files_thread);
}

/**
  * pk_backend_simulate_install_files
  */
void
pk_backend_simulate_install_files (PkBackend *backend, gchar **full_paths)
{
	pk_backend_thread_create (backend, backend_install_files_thread);
}

static gboolean
backend_get_update_detail_thread (PkBackend *backend)
{
	MIL << endl;
	gchar **package_ids;
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}

	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (package_ids == NULL) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
	}
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	for (uint i = 0; package_ids[i]; i++) {
		sat::Solvable solvable = zypp_get_package_by_id (backend, package_ids[i]);

		Capabilities obs = solvable.obsoletes ();

		gchar *obsoletes = zypp_build_package_id_capabilities (obs);

		PkRestartEnum restart = PK_RESTART_ENUM_NONE;

		PoolItem item = ResPool::instance ().find (solvable);

		gchar *bugzilla = new gchar ();
		gchar *cve = new gchar ();

		if (isKind<Patch>(solvable)) {
			Patch::constPtr patch = asKind<Patch>(item);
			zypp_check_restart (&restart, patch);

			// Building links like "http://www.distro-update.org/page?moo;Bugfix release for kernel;http://www.test.de/bgz;test domain"
			for (Patch::ReferenceIterator it = patch->referencesBegin (); it != patch->referencesEnd (); it ++) {
				if (it.type () == "bugzilla") {
					if (strlen (bugzilla) == 0) {
						bugzilla = g_strconcat (it.href ().c_str (), ";", it.title ().c_str (), (gchar *)NULL);
					} else {
						bugzilla = g_strconcat (bugzilla, ";", it.href ().c_str (), ";", it.title ().c_str (), (gchar *)NULL);
					}
				} else {
					if (strlen (cve) == 0) {
						cve = g_strconcat (it.href ().c_str (), ";", it.title ().c_str (), (gchar *)NULL);
					} else {
						cve = g_strconcat (cve, it.href ().c_str (), ";", it.title ().c_str (), ";", (gchar *)NULL);
					}
				}
			}

			sat::SolvableSet content = patch->contents ();

			for (sat::SolvableSet::const_iterator it = content.begin (); it != content.end (); it++) {
				//obsoletes = g_strconcat (obsoletes, zypp_build_package_id_capabilities (it->obsoletes ()), PK_PACKAGE_IDS_DELIM, (gchar *)NULL);
				if (strlen(obsoletes) == 0) {
					obsoletes = zypp_build_package_id_capabilities (it->obsoletes ());
				} else {
					obsoletes = g_strconcat (obsoletes, PK_PACKAGE_IDS_DELIM, zypp_build_package_id_capabilities (it->obsoletes ()), (gchar *)NULL);
				}
			}
		}

		pk_backend_update_detail (backend,
					  package_ids[i],
					  NULL,		// updates TODO with Resolver.installs
					  obsoletes,	// CURRENTLY CAUSES SEGFAULT obsoletes,
					  "",		// CURRENTLY CAUSES SEGFAULT solvable.vendor ().c_str (),
					  bugzilla,	// bugzilla
					  cve,		// cve
					  restart,	// restart -flag
					  solvable.lookupStrAttribute (sat::SolvAttr::description).c_str (),	// update-text
					  NULL,		// ChangeLog text
					  PK_UPDATE_STATE_ENUM_UNKNOWN,		// state of the update
					  NULL, // date that the update was issued
					  NULL);	// date that the update was updated

		g_free (bugzilla);
		g_free (cve);
		g_free (obsoletes);
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
  * pk_backend_get_update_detail
  */
void
pk_backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_get_update_detail_thread);
}

static gboolean
backend_update_system_thread (PkBackend *backend)
{
	MIL << endl;
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* FIXME: support only_trusted */
	ResPool pool = zypp_build_pool (backend, TRUE);
	pk_backend_set_percentage (backend, 40);
	PkRestartEnum restart = PK_RESTART_ENUM_NONE;

	set<PoolItem> candidates;
	zypp_get_updates (backend, candidates);

	if (_updating_self)
		_updating_self = FALSE;

	pk_backend_set_percentage (backend, 80);
	set<PoolItem>::iterator cb = candidates.begin (), ce = candidates.end (), ci;
	for (ci = cb; ci != ce; ++ci) {
		// set the status of the update to ToBeInstalled
		ResStatus &status = ci->status ();
		status.setToBeInstalled (ResStatus::USER);
		if (isKind<Patch>(ci->resolvable ())) {
			zypp_check_restart (&restart, asKind<Patch>(ci->resolvable ()));
		}
	}

	if (!zypp_perform_execution (backend, UPDATE, FALSE)) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_TRANSACTION_ERROR,
			"Couldn't perform the installation.");
	}

	if (restart != PK_RESTART_ENUM_NONE)
		pk_backend_require_restart (backend, restart, "A restart is needed");

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * pk_backend_update_system
 */
void
pk_backend_update_system (PkBackend *backend, gboolean only_trusted)
{
	pk_backend_thread_create (backend, backend_update_system_thread);
}

static gboolean
backend_install_packages_thread (PkBackend *backend)
{
	MIL << endl;
	PoolStatusSaver saver;
	gchar **package_ids;

	// refresh the repos before installing packages
	if (!zypp_refresh_cache (backend, FALSE)) {
		pk_backend_finished (backend);
		return FALSE;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	ZYpp::Ptr zypp;
	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}

	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (!pk_package_ids_check (package_ids)) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
	}
	/* FIXME: support only_trusted */

	try
	{
		ResPool pool = zypp_build_pool (backend, TRUE);
		pk_backend_set_percentage (backend, 10);
		vector<PoolItem> *items = new vector<PoolItem> ();

		guint to_install = 0;
		for (guint i = 0; package_ids[i]; i++) {
			gchar **id_parts = pk_package_id_split (package_ids[i]);

			// Iterate over the selectables and mark the one with the right name
			ui::Selectable::constPtr selectable;
			string name = id_parts[PK_PACKAGE_ID_NAME];

			// Do we have this installed ?
			gboolean system = false;
			for (ResPool::byName_iterator it = pool.byNameBegin (name);
			     it != pool.byNameEnd (name); it++) {

				g_debug ("PoolItem '%s'", it->satSolvable().asString().c_str());

				if (!it->satSolvable().isSystem())
					continue;

				if (zypp_ver_and_arch_equal (it->satSolvable(), id_parts[PK_PACKAGE_ID_VERSION],
							     id_parts[PK_PACKAGE_ID_ARCH])) {
					system = true;
					break;
				}
			}

			if (!system) {
				gboolean hit = false;

				// Choose the PoolItem with the right architecture and version
				for (ResPool::byName_iterator it = pool.byNameBegin (name);
				     it != pool.byNameEnd (name); it++) {

					if (zypp_ver_and_arch_equal (it->satSolvable(), id_parts[PK_PACKAGE_ID_VERSION],
								     id_parts[PK_PACKAGE_ID_ARCH])) {
						hit = true;
						to_install++;
						// set status to ToBeInstalled
						it->status ().setToBeInstalled (ResStatus::USER);
						items->push_back (*it);
						break;
					}
				}
				if (!hit) {
					g_strfreev (id_parts);
					return zypp_backend_finished_error (
						backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						"Couldn't find the package '%s'.", package_ids[i]);
				}

			}
			g_strfreev (id_parts);
		}

		pk_backend_set_percentage (backend, 40);

		if (!to_install) {
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_ALL_PACKAGES_ALREADY_INSTALLED,
				"The packages are already all installed");
		}

		// Todo: ideally we should call pk_backend_package (...
		// PK_INFO_ENUM_DOWNLOADING | INSTALLING) for each package.
		if (!zypp_perform_execution (backend, INSTALL, FALSE)) {
			// reset the status of the marked packages
			for (vector<PoolItem>::iterator it = items->begin (); it != items->end (); it++) {
				it->statusReset ();
			}
			delete (items);
			pk_backend_finished (backend);
			return FALSE;
		}
		delete (items);

		pk_backend_set_percentage (backend, 100);

	} catch (const Exception &ex) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * pk_backend_install_packages:
 */
void
pk_backend_install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	// For now, don't let the user cancel the install once it's started
	pk_backend_set_allow_cancel (backend, FALSE);
	pk_backend_thread_create (backend, backend_install_packages_thread);
}

/**
 * pk_backend_simulate_install_packages:
 */
void
pk_backend_simulate_install_packages (PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_install_packages_thread);
}

static gboolean
backend_install_signature_thread (PkBackend *backend)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_SIG_CHECK);
	const gchar *key_id = pk_backend_get_string (backend, "key_id");
	const gchar *package_id = pk_backend_get_string (backend, "package_id");
	priv->signatures[backend]->push_back ((string)(key_id));

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * pk_backend_install_signature:
 */
void
pk_backend_install_signature (PkBackend *backend, PkSigTypeEnum type, const gchar *key_id, const gchar *package_id)
{
	pk_backend_thread_create (backend, backend_install_signature_thread);
}

static gboolean
backend_remove_packages_thread (PkBackend *backend)
{
	MIL << endl;
	PoolStatusSaver saver;
	gboolean autoremove;
	gchar **package_ids;
	vector<PoolItem> *items = new vector<PoolItem> ();

	pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);
	pk_backend_set_percentage (backend, 0);

	Target_Ptr target;
	ZYpp::Ptr zypp;
	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}
	autoremove = pk_backend_get_bool (backend, "autoremove");
	zypp->resolver()->setCleandepsOnRemove(autoremove);

	target = zypp->target ();

	// Load all the local system "resolvables" (packages)
	target->load ();
	pk_backend_set_percentage (backend, 10);

	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (!pk_package_ids_check (package_ids)) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
	}
	for (guint i = 0; package_ids[i]; i++) {
		gchar **id_parts = pk_package_id_split (package_ids[i]);

		// Iterate over the resolvables and mark the ones we want to remove
		ResPool pool = ResPool::instance ();
		for (ResPool::byIdent_iterator it = pool.byIdentBegin (ResKind::package, id_parts[PK_PACKAGE_ID_NAME]);
				it != pool.byIdentEnd (ResKind::package, id_parts[PK_PACKAGE_ID_NAME]); it++) {
			if ((*it)->isSystem ()) {
				it->status ().setToBeUninstalled (ResStatus::USER);
				items->push_back (*it);
				break;
			} else {
				it->status ().resetTransact (ResStatus::USER);
			}
		}
		g_strfreev (id_parts);
	}

	pk_backend_set_percentage (backend, 40);

	try
	{
		if (!zypp_perform_execution (backend, REMOVE, TRUE)) {
			//reset the status of the marked packages
			for (vector<PoolItem>::iterator it = items->begin (); it != items->end (); it++) {
				it->statusReset();
			}
			delete (items);
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_TRANSACTION_ERROR,
				"Couldn't remove the package");
		}

		delete (items);
		pk_backend_set_percentage (backend, 100);

	} catch (const repo::RepoNotFoundException &ex) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str());
	} catch (const Exception &ex) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * pk_backend_remove_packages:
 */
void
pk_backend_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	pk_backend_thread_create (backend, backend_remove_packages_thread);
}

void
pk_backend_simulate_remove_packages (PkBackend *backend, gchar **packages, gboolean autoremove)
{
	pk_backend_thread_create (backend, backend_remove_packages_thread);
}

static gboolean
backend_resolve_thread (PkBackend *backend)
{
	MIL << endl;
	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");
	PkBitfield _filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}

	zypp->getTarget()->load();

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	for (uint i = 0; package_ids[i]; i++) {
		vector<sat::Solvable> v;

		/* build a list of packages with this name */
		zypp_get_packages_by_name (backend, package_ids[i], ResKind::package, v);

		/* add source packages */
		if (!pk_bitfield_contain (_filters, PK_FILTER_ENUM_NOT_SOURCE)) {
			vector<sat::Solvable> src;
			zypp_get_packages_by_name (backend, package_ids[i], ResKind::srcpackage, src);
			v.insert (v.end (), src.begin (), src.end ());
		}

		/* include patches too */
		vector<sat::Solvable> v2;
		zypp_get_packages_by_name (backend, package_ids[i], ResKind::patch, v2);
		v.insert (v.end (), v2.begin (), v2.end ());

		sat::Solvable newest;
		vector<sat::Solvable> pkgs;

		/* Filter the list of packages with this name to 'pkgs' */
		for (vector<sat::Solvable>::iterator it = v.begin (); it != v.end (); it++) {

			if (zypp_filter_solvable (_filters, *it) ||
			    *it == sat::Solvable::noSolvable)
				continue;

			if (newest == sat::Solvable::noSolvable) {
				newest = *it;
			} else if (it->edition().match (newest.edition()) > 0) {
				newest = *it;
			}
			pkgs.push_back (*it);
		}

		/* 'newest' filter support */
		if (pk_bitfield_contain (_filters, PK_FILTER_ENUM_NEWEST)) {
			pkgs.clear();
			pkgs.push_back (newest);
		} else if (pk_bitfield_contain (_filters, PK_FILTER_ENUM_NOT_NEWEST)) {
			pkgs.erase (find (pkgs.begin (), pkgs.end(), newest));
		}

		zypp_emit_filtered_packages_in_list (backend, pkgs);

		if (pkgs.size() < 1) {
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				"couldn't find package");
		}
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * pk_backend_resolve:
 */
void
pk_backend_resolve (PkBackend *backend, PkBitfield filters, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_resolve_thread);
}

static gboolean
backend_find_packages_thread (PkBackend *backend)
{
	MIL << endl;
	gchar **values;
	const gchar *search;
	guint mode;
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}

	// refresh the repos before searching
	if (!zypp_refresh_cache (backend, FALSE)) {
		pk_backend_finished (backend);
		return FALSE;
	}

	values = pk_backend_get_strv (backend, "search");
	search = values[0];  //Fixme - support the possible multiple values (logical OR search)
	mode = pk_backend_get_uint (backend, "mode");

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);

	vector<sat::Solvable> v;

	PoolQuery q;
	q.addString( search ); // may be called multiple times (OR'ed)
	q.setCaseSensitive( true );
	q.setMatchSubstring();

	switch (mode) {
	case SEARCH_TYPE_NAME:
		zypp_build_pool (backend, TRUE); // seems to be necessary?
		q.addKind( ResKind::package );
		q.addKind( ResKind::srcpackage );
		q.addAttribute( sat::SolvAttr::name );
		// Note: The query result is NOT sorted packages first, then srcpackage.
		// If that's necessary you need to sort the vector accordongly or use
		// two separate queries.
		break;
	case SEARCH_TYPE_DETAILS:
		zypp_build_pool (backend, TRUE); // seems to be necessary?
		q.addKind( ResKind::package );
		//q.addKind( ResKind::srcpackage );
		q.addAttribute( sat::SolvAttr::name );
		q.addAttribute( sat::SolvAttr::description );
		// Note: Don't know if zypp_get_packages_by_details intentionally
		// did not search in srcpackages.
		break;
	case SEARCH_TYPE_FILE: {
		zypp_build_pool (backend, TRUE);
		q.addKind( ResKind::package );
		q.addAttribute( sat::SolvAttr::name );
		q.addAttribute( sat::SolvAttr::description );
		q.addAttribute( sat::SolvAttr::filelist );
		q.setFilesMatchFullPath(true);
		q.setMatchExact();
		break;
	    }
	};

	if ( ! q.empty() ) {
		copy( q.begin(), q.end(), back_inserter( v ) );
	}
	zypp_emit_filtered_packages_in_list (backend, v);

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * pk_backend_search_name:
 */
void
pk_backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_uint (backend, "mode", SEARCH_TYPE_NAME);
	pk_backend_thread_create (backend, backend_find_packages_thread);
}

/**
 * pk_backend_search_details:
 */
void
pk_backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_uint (backend, "mode", SEARCH_TYPE_DETAILS);
	pk_backend_thread_create (backend, backend_find_packages_thread);
}

static gboolean
backend_search_group_thread (PkBackend *backend)
{
	MIL << endl;
	gchar **values;
	const gchar *group;
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}

	values = pk_backend_get_strv (backend, "search");
	group = values[0];  //Fixme - add support for possible multiple values.

	if (group == NULL) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_GROUP_NOT_FOUND, "Group is invalid.");
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	ResPool pool = zypp_build_pool (backend, true);

	pk_backend_set_percentage (backend, 30);

	vector<sat::Solvable> v;
	PkGroupEnum pkGroup = pk_group_enum_from_string (group);

	sat::LookupAttr look (sat::SolvAttr::group);

	for (sat::LookupAttr::iterator it = look.begin (); it != look.end (); it++) {
		PkGroupEnum rpmGroup = get_enum_group (it.asString ());
		if (pkGroup == rpmGroup)
			v.push_back (it.inSolvable ());
	}

	pk_backend_set_percentage (backend, 70);

	zypp_emit_filtered_packages_in_list (backend, v);

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * pk_backend_search_group:
 */
void
pk_backend_search_groups (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_thread_create (backend, backend_search_group_thread);
}

/**
 * pk_backend_search_file:
 */
void
pk_backend_search_files (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_uint (backend, "mode", SEARCH_TYPE_FILE);
	pk_backend_thread_create (backend, backend_find_packages_thread);
}

/**
 * backend_get_repo_list:
 */
void
pk_backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	MIL << endl;
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	RepoManager manager;
	list <RepoInfo> repos;
	try
	{
		repos = list<RepoInfo>(manager.repoBegin(),manager.repoEnd());
	} catch (const repo::RepoNotFoundException &ex) {
		zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str());
		return;
	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
		return;
	}

	for (list <RepoInfo>::iterator it = repos.begin(); it != repos.end(); it++) {
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT) && zypp_is_development_repo (backend, *it))
			continue;
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
 * pk_backend_repo_enable:
 */
void
pk_backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
	MIL << endl;
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return;
	}
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	RepoManager manager;
	RepoInfo repo;

	try {
		repo = manager.getRepositoryInfo (rid);
		if (!zypp_is_valid_repo (backend, repo)){
			pk_backend_finished (backend);
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
			backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str());
		return;
	} catch (const Exception &ex) {
		zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
		return;
	}

	pk_backend_finished (backend);
}

static gboolean
backend_get_files_thread (PkBackend *backend)
{
	MIL << endl;
	gchar **package_ids;
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}

	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (!pk_package_ids_check (package_ids)) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
	}

	for (uint i = 0; package_ids[i]; i++) {
		gchar **id_parts = pk_package_id_split (package_ids[i]);
		pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

		vector<sat::Solvable> v;
		vector<sat::Solvable> v2;
		zypp_get_packages_by_name (backend, (const gchar *)id_parts[PK_PACKAGE_ID_NAME], ResKind::package, v);
		zypp_get_packages_by_name (backend, (const gchar *)id_parts[PK_PACKAGE_ID_NAME], ResKind::srcpackage, v2);

		v.insert (v.end (), v2.begin (), v2.end ());

		sat::Solvable package;
		for (vector<sat::Solvable>::iterator it = v.begin ();
				it != v.end (); it++) {
			char *version = g_strdup (it->edition ().asString ().c_str ());
			if (strcmp (id_parts[PK_PACKAGE_ID_VERSION], version) == 0) {
				g_free (version);
				package = *it;
				break;
			}
			g_free (version);
		}
		g_strfreev (id_parts);

		if (package == NULL) {
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				"couldn't find package");
		}

		string temp;
		if (package.isSystem ()){
			try {
				target::rpm::RpmHeader::constPtr rpmHeader = zypp_get_rpmHeader (package.name (), package.edition ());
				list<string> files = rpmHeader->tag_filenames ();

				for (list<string>::iterator it = files.begin (); it != files.end (); it++) {
					temp.append (*it);
					temp.append (";");
				}

			} catch (const target::rpm::RpmException &ex) {
				return zypp_backend_finished_error (
					backend, PK_ERROR_ENUM_REPO_NOT_FOUND,
					 "Couldn't open rpm-database");
			}
		} else {
			temp = "Only available for installed packages";
		}

		pk_backend_files (backend, package_ids[i], temp.c_str ());	// file_list
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
  * pk_backend_get_files:
  */
void
pk_backend_get_files(PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_get_files_thread);
}

static gboolean
backend_get_packages_thread (PkBackend *backend)
{
	MIL << endl;
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	vector<sat::Solvable> v;

	zypp_build_pool (backend, TRUE);
	ResPool pool = ResPool::instance ();
	for (ResPool::byKind_iterator it = pool.byKindBegin (ResKind::package); it != pool.byKindEnd (ResKind::package); it++) {
		v.push_back (it->satSolvable ());
	}

	zypp_emit_filtered_packages_in_list (backend, v);

	pk_backend_finished (backend);
	return TRUE;
}
/**
  * pk_backend_get_packages:
  */
void
pk_backend_get_packages (PkBackend *backend, PkBitfield filter)
{
	pk_backend_thread_create (backend, backend_get_packages_thread);
}

static gboolean
backend_update_packages_thread (PkBackend *backend)
{
	MIL << endl;
	PoolStatusSaver saver;
	gboolean retval;
	gchar **package_ids;
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}
	ResPool pool = zypp_build_pool (backend, TRUE);
	/* FIXME: support only_trusted */
	package_ids = pk_backend_get_strv (backend, "package_ids");
	PkRestartEnum restart = PK_RESTART_ENUM_NONE;

	for (guint i = 0; package_ids[i]; i++) {
		gchar **id_parts = pk_package_id_split (package_ids[i]);
		string name = id_parts[PK_PACKAGE_ID_NAME];

		// Do we have already the latest version.
		gboolean system = false;
		for (ResPool::byName_iterator it = pool.byNameBegin (name);
				it != pool.byNameEnd (name); it++) {
			if (!it->satSolvable().isSystem())
				continue;
			if (zypp_ver_and_arch_equal (it->satSolvable(), id_parts[PK_PACKAGE_ID_VERSION],
						id_parts[PK_PACKAGE_ID_ARCH])) {
				system = true;
				break;
			}
		}
		if (system == true)
			continue;
		sat::Solvable solvable = zypp_get_package_by_id (backend, package_ids[i]);
		PoolItem item = ResPool::instance ().find (solvable);
		item.status ().setToBeInstalled (ResStatus::USER);
		Patch::constPtr patch = asKind<Patch>(item.resolvable ());
		zypp_check_restart (&restart, patch);
		if (restart != PK_RESTART_ENUM_NONE){
			pk_backend_require_restart (backend, restart, package_ids[i]);
			restart = PK_RESTART_ENUM_NONE;
		}
	}

	retval = zypp_perform_execution (backend, UPDATE, FALSE);

	pk_backend_finished (backend);
	return retval;
}

/**
  * pk_backend_update_packages
  */
void
pk_backend_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_update_packages_thread);
}

/**
  * pk_backend_simulate_update_packages
  */
void
pk_backend_simulate_update_packages (PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_update_packages_thread);
}

static gboolean
backend_repo_set_data_thread (PkBackend *backend)
{
	MIL << endl;
	const gchar *repo_id;
	const gchar *parameter;
	const gchar *value;
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}

	repo_id = pk_backend_get_string (backend, "repo_id");
	parameter = pk_backend_get_string (backend, "parameter");
	value = pk_backend_get_string (backend, "value");
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	RepoManager manager;
	RepoInfo repo;

	gboolean bReturn = TRUE;

	try {
		pk_backend_set_status(backend, PK_STATUS_ENUM_SETUP);
		if (g_ascii_strcasecmp (parameter, "add") != 0) {
			repo = manager.getRepositoryInfo (repo_id);
			if (!zypp_is_valid_repo (backend, repo)){
				pk_backend_finished (backend);
				return FALSE;
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
				pk_backend_message (backend, PK_MESSAGE_ENUM_PARAMETER_INVALID, "Autorefresh a repo: Enter true or false");
				bReturn = FALSE;
			}

			manager.modifyRepository (repo_id, repo);
		} else if (g_ascii_strcasecmp (parameter, "keep") == 0) {

			if (g_ascii_strcasecmp (value, "true") == 0) {
				repo.setKeepPackages (TRUE);
			} else if (g_ascii_strcasecmp (value, "false") == 0) {
				repo.setKeepPackages (FALSE);
			} else {
				pk_backend_message (backend, PK_MESSAGE_ENUM_PARAMETER_INVALID, "Keep downloaded packages: Enter true or false");
				bReturn = FALSE;
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
				pk_backend_message (backend, PK_MESSAGE_ENUM_PRIORITY_INVALID, "Priorities has to be between 1 (highest) and 99");
				bReturn = false;
			} else {
				for (gint i = 0; i < length; i++) {
					gint tmp = g_ascii_digit_value (value[i]);

					if (tmp == -1) {
						pk_backend_message (backend, PK_MESSAGE_ENUM_PRIORITY_INVALID, "Priorities has to be a number between 1 (highest) and 99");
						bReturn = FALSE;
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
			pk_backend_error_code (backend, PK_ERROR_ENUM_NOT_SUPPORTED, "Valid parameters for set_repo_data are remove/add/refresh/prio/keep/url/name");
			bReturn = FALSE;
		}

	} catch (const repo::RepoNotFoundException &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "Couldn't find the specified repository");
		bReturn = FALSE;
	} catch (const repo::RepoAlreadyExistsException &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "This repo already exists");
		bReturn = FALSE;
	} catch (const repo::RepoUnknownTypeException &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Type of the repo can't be determined");
		bReturn = FALSE;
	} catch (const repo::RepoException &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Can't access the given URL");
		bReturn = FALSE;
	} catch (const Exception &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asString ().c_str ());
		bReturn = FALSE;
	}

	pk_backend_finished (backend);
	return bReturn;
}

/**
  * pk_backend_repo_set_data
  */
void
pk_backend_repo_set_data (PkBackend *backend, const gchar *repo_id, const gchar *parameter, const gchar *value)
{
	pk_backend_thread_create (backend, backend_repo_set_data_thread);
}

static gboolean
backend_what_provides_thread (PkBackend *backend)
{
	MIL << endl;
	ZYpp::Ptr zypp;

	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	gchar **values = pk_backend_get_strv (backend, "search");
	const gchar *search = values[0]; //Fixme - support possible multiple search values (logical OR)
	PkProvidesEnum provides = (PkProvidesEnum) pk_backend_get_uint (backend, "provides");
	PkBitfield _filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	ResPool pool = zypp_build_pool (backend, true);

	if((provides == PK_PROVIDES_ENUM_HARDWARE_DRIVER) || g_ascii_strcasecmp("drivers_for_attached_hardware", search) == 0) {
		// solver run
		Resolver solver(pool);
		solver.setIgnoreAlreadyRecommended (TRUE);

		if (!solver.resolvePool ()) {
			list<ResolverProblem_Ptr> problems = solver.problems ();
			for (list<ResolverProblem_Ptr>::iterator it = problems.begin (); it != problems.end (); it++){
				g_warning("Solver problem (This should never happen): '%s'", (*it)->description ().c_str ());
			}
			solver.setIgnoreAlreadyRecommended (FALSE);
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "Resolution failed");
		}

		// look for packages which would be installed
		for (ResPool::byKind_iterator it = pool.byKindBegin (ResKind::package);
				it != pool.byKindEnd (ResKind::package); it++) {
			PkInfoEnum status = PK_INFO_ENUM_UNKNOWN;

			gboolean hit = FALSE;

			if (it->status ().isToBeInstalled ()) {
				status = PK_INFO_ENUM_AVAILABLE;
				hit = TRUE;
			}

			if (hit && !zypp_filter_solvable (_filters, it->resolvable()->satSolvable())) {
				zypp_backend_package (backend, status, it->resolvable()->satSolvable(),
						      it->resolvable ()->summary ().c_str ());
			}
			it->statusReset ();
		}
		solver.setIgnoreAlreadyRecommended (FALSE);
	} else {
		Capability cap (search);
		sat::WhatProvides prov (cap);

		for (sat::WhatProvides::const_iterator it = prov.begin (); it != prov.end (); it++) {
			if (zypp_filter_solvable (_filters, *it))
				continue;

			PkInfoEnum info = it->isSystem () ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;
			zypp_backend_package (backend, info, *it, it->lookupStrAttribute (sat::SolvAttr::summary).c_str ());
		}
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
  * pk_backend_what_provides
  */
void
pk_backend_what_provides (PkBackend *backend, PkBitfield filters, PkProvidesEnum provide, gchar **values)
{
	pk_backend_thread_create (backend, backend_what_provides_thread);
}

gchar *
pk_backend_get_mime_types (PkBackend *backend)
{
	return g_strdup ("application/x-rpm");
}

static gboolean
backend_download_packages_thread (PkBackend *backend)
{
	MIL << endl;
	gchar **package_ids;
	gulong size = 0;

	if (!zypp_refresh_cache (backend, FALSE)) {
		pk_backend_finished (backend);
		return FALSE;
	}

	ZYpp::Ptr zypp;
	zypp = get_zypp (backend);
	if (zypp == NULL){
		pk_backend_finished (backend);
		return FALSE;
	}

	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (!pk_package_ids_check (package_ids)) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
	}

	try
	{
		ResPool pool = zypp_build_pool (backend, FALSE);
		PoolItem item;

		pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);
		for (guint i = 0; package_ids[i]; i++) {
			gchar **id_parts = pk_package_id_split (package_ids[i]);
			string name = id_parts[PK_PACKAGE_ID_NAME];

			for (ResPool::byName_iterator it = pool.byNameBegin (name); it != pool.byNameEnd (name); it++) {
				if (zypp_ver_and_arch_equal (it->satSolvable(), id_parts[PK_PACKAGE_ID_VERSION],
							     id_parts[PK_PACKAGE_ID_ARCH])) {
					size += 2 * it->satSolvable().lookupNumAttribute (sat::SolvAttr::downloadsize);
					item = *it;
					break;
				}
			}

			struct statfs stat;
			statfs(pk_backend_get_root (backend), &stat);
			if (size > stat.f_bavail * 4) {
				g_strfreev (id_parts);
				pk_backend_error_code (backend, PK_ERROR_ENUM_NO_SPACE_ON_DEVICE,
					"Insufficient space in download directory '%s'.", pk_backend_get_root (backend));
				pk_backend_finished (backend);
				return FALSE;
			}

			sat::Solvable solvable = item.resolvable()->satSolvable();
			filesystem::Pathname tmp_file;
			repo::RepoMediaAccess access;
			repo::DeltaCandidates deltas;
			if (strcmp (id_parts[PK_PACKAGE_ID_ARCH], "source") == 0) {
				SrcPackage::constPtr package = asKind<SrcPackage>(item.resolvable());
				repo::SrcPackageProvider pkgProvider(access);
				pkgProvider.provideSrcPackage(package);
				tmp_file = solvable.repository().info().packagesPath()+ package->location().filename();

			} else {
				Package::constPtr package = asKind<Package>(item.resolvable());
				repo::PackageProvider pkgProvider(access, package, deltas);
				pkgProvider.providePackage();
				tmp_file = solvable.repository().info().packagesPath()+ package->location().filename();
			}
			pk_backend_files (backend, package_ids[i], tmp_file.c_str());
			zypp_backend_package (backend, PK_INFO_ENUM_DOWNLOADING, solvable, item->summary ().c_str());

			g_strfreev (id_parts);
		}
	} catch (const Exception &ex) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED, ex.asUserString().c_str());
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * pk_backend_download_packages:
 */
void
pk_backend_download_packages (PkBackend *backend, gchar **package_ids, const gchar *directory)
{
	pk_backend_thread_create (backend, backend_download_packages_thread);
}

/**
 * pk_backend_transaction_start:
 */
void
pk_backend_transaction_start (PkBackend *backend)
{
	gchar *locale;
	gchar *proxy_http;
	gchar *proxy_https;
	gchar *proxy_ftp;
	gchar *uri;
	gchar *proxy_socks;
	gchar *no_proxy;
	gchar *pac;

	locale = pk_backend_get_locale(backend);
	if (!pk_strzero (locale)) {
		setlocale(LC_ALL, locale);
	}

	/* http_proxy */
	proxy_http = pk_backend_get_proxy_http (backend);
	if (!pk_strzero (proxy_http)) {
		uri = pk_backend_spawn_convert_uri (proxy_http);
		g_setenv ("http_proxy", uri, TRUE);
		g_free (uri);
	}

	/* https_proxy */
	proxy_https = pk_backend_get_proxy_https (backend);
	if (!pk_strzero (proxy_https)) {
		uri = pk_backend_spawn_convert_uri (proxy_https);
		g_setenv ("https_proxy", uri, TRUE);
		g_free (uri);
	}

	/* ftp_proxy */
	proxy_ftp = pk_backend_get_proxy_ftp (backend);
	if (!pk_strzero (proxy_ftp)) {
		uri = pk_backend_spawn_convert_uri (proxy_ftp);
		g_setenv ("ftp_proxy", uri, TRUE);
		g_free (uri);
	}

	/* socks_proxy */
	proxy_socks = pk_backend_get_proxy_socks (backend);
	if (!pk_strzero (proxy_socks)) {
		uri = pk_backend_spawn_convert_uri (proxy_socks);
		g_setenv ("socks_proxy", uri, TRUE);
		g_free (uri);
	}

	/* no_proxy */
	no_proxy = pk_backend_get_no_proxy (backend);
	if (!pk_strzero (no_proxy)) {
		g_setenv ("no_proxy", no_proxy, TRUE);
	}

	/* pac */
	pac = pk_backend_get_pac (backend);
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
 * pk_backend_transaction_stop:
 */
void
pk_backend_transaction_stop (PkBackend *backend)
{
	/* unset proxy info for this transaction */
	g_unsetenv ("http_proxy");
	g_unsetenv ("ftp_proxy");
	g_unsetenv ("https_proxy");
	g_unsetenv ("no_proxy");
	g_unsetenv ("socks_proxy");
	g_unsetenv ("pac");
}


