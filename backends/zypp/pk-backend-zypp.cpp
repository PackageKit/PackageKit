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
#include <unistd.h>
#include <egg-debug.h>
#include <string>
#include <set>
#include <glib/gi18n.h>

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
#include <zypp/base/Functional.h>
#include <zypp/parser/ProductFileReader.h>
#include <zypp/TmpPath.h>

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

/**
 * A map to keep track of the EventDirector objects for
 * each zypp backend that is created.
 */
static std::map<PkBackend *, EventDirector *> _eventDirectors;

std::map<PkBackend *, std::vector<std::string> *> _signatures;

/**
 * backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_initialize (PkBackend *backend)
{
	zypp_logging ();
	get_zypp ();
	egg_debug ("zypp_backend_initialize");
	EventDirector *eventDirector = new EventDirector (backend);
	_eventDirectors [backend] = eventDirector;
	std::vector<std::string> *signature = new std::vector<std::string> ();
	_signatures [backend] = signature;
	_updating_self = FALSE;
}

/**
 * backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_destroy (PkBackend *backend)
{
	egg_debug ("zypp_backend_destroy");
	EventDirector *eventDirector = _eventDirectors [backend];
	if (eventDirector != NULL) {
		delete (eventDirector);
		_eventDirectors.erase (backend);
	}

	delete (_signatures[backend]);
	g_free (_repoName);
}

/**
  * backend_get_requires_thread:
  */
static gboolean
backend_get_requires_thread (PkBackend *backend)
{
	gchar **package_ids;
	PkBitfield _filters = (PkBitfield) pk_backend_get_uint (backend, "filters");

	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (!pk_package_ids_check (package_ids)) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	//TODO repair percentages
	//pk_backend_set_percentage (backend, 0);

	for (uint i = 0; package_ids[i]; i++) {
		zypp::sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);
		zypp::PoolItem package;

		if (solvable.isSystem ()) {
			zypp::ResPool pool = zypp_build_pool (true);

			gboolean found = FALSE;
			gchar **id_parts = pk_package_id_split (package_ids[i]);

			for (zypp::ResPool::byIdent_iterator it = pool.byIdentBegin (zypp::ResKind::package, id_parts[PK_PACKAGE_ID_NAME]);
					it != pool.byIdentEnd (zypp::ResKind::package, id_parts[PK_PACKAGE_ID_NAME]); it++) {
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
			package.status ().setToBeUninstalled (zypp::ResStatus::USER);
		} else {
			if (solvable == zypp::sat::Solvable::noSolvable) {
				return zypp_backend_finished_error (
					backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					"Package couldn't be found");
			}

			zypp::ResPool pool = zypp::ResPool::instance ();
			package = pool.find (solvable);
			//set Package as to be installed
			package.status ().setToBeInstalled (zypp::ResStatus::USER);
		}

		// solver run
		zypp::ResPool pool = zypp::ResPool::instance ();
		zypp::Resolver solver(pool);

		solver.setForceResolve (true);

		if (!solver.resolvePool ()) {
			std::list<zypp::ResolverProblem_Ptr> problems = solver.problems ();
			for (std::list<zypp::ResolverProblem_Ptr>::iterator it = problems.begin (); it != problems.end (); it++){
				egg_warning("Solver problem (This should never happen): '%s'", (*it)->description ().c_str ());
			}
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
				"Resolution failed");
		}

		// look for packages which would be uninstalled
		for (zypp::ResPool::byKind_iterator it = pool.byKindBegin (zypp::ResKind::package);
				it != pool.byKindEnd (zypp::ResKind::package); it++) {

			if (!zypp_filter_solvable (_filters, it->resolvable()->satSolvable())) {
				zypp_backend_pool_item_notify (backend, *it);
			}

			it->statusReset ();
		}

		// undo the status-change of the package and disable forceResolve
		package.statusReset ();
		solver.setForceResolve (false);
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
  * backend_get_requires:
  */
static void
backend_get_requires(PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_thread_create (backend, backend_get_requires_thread);
}

/**
 * backend_get_groups:
 */
static PkBitfield
backend_get_groups (PkBackend *backend)
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
 * backend_get_filters:
 */
static PkBitfield
backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_ARCH,
		PK_FILTER_ENUM_NEWEST,
		PK_FILTER_ENUM_SOURCE,
		-1);
}

static bool
zypp_is_no_solvable (const zypp::sat::Solvable &solv)
{
	return solv.id() == zypp::sat::detail::noSolvableId;
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

	zypp::ZYpp::Ptr zypp;
	zypp = get_zypp ();

	egg_debug ("get_depends with filter '%s'", pk_filter_bitfield_to_string (_filters));

	try
	{
		gchar **id_parts = pk_package_id_split (package_ids[0]);
		pk_backend_set_percentage (backend, 20);
		// Load resolvables from all the enabled repositories
		zypp::ResPool pool = zypp_build_pool(true);

		zypp::PoolItem pool_item;
		gboolean pool_item_found = FALSE;
		// Iterate over the resolvables and mark the one we want to check its dependencies
		for (zypp::ResPool::byIdent_iterator it = pool.byIdentBegin (zypp::ResKind::package, id_parts[PK_PACKAGE_ID_NAME]);
				it != pool.byIdentEnd (zypp::ResKind::package, id_parts[PK_PACKAGE_ID_NAME]); it++) {
			zypp::PoolItem selectable = *it;
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

		zypp::sat::Solvable solvable = pool_item.satSolvable ();
		zypp::Capabilities req = solvable[zypp::Dep::REQUIRES];

		// which package each capability
		std::map<std::string, zypp::sat::Solvable> caps;
		// packages already providing a capability
		std::vector<std::string> pkg_names;

		for (zypp::Capabilities::const_iterator cap = req.begin (); cap != req.end (); ++cap) {
			egg_debug ("get_depends - capability '%s'", cap->asString().c_str());

			if (caps.find (cap->asString ()) != caps.end()) {
				egg_debug ("Interesting ! already have capability '%s'", cap->asString().c_str());
				continue;
			}

			// Look for packages providing each capability
			bool have_preference = false;
			zypp::sat::Solvable preferred;

			zypp::sat::WhatProvides prov_list (*cap);
			for (zypp::sat::WhatProvides::const_iterator provider = prov_list.begin ();
			     provider != prov_list.end (); provider++) {

				egg_debug ("provider: '%s'", provider->asString().c_str());

				// filter out caps like "rpmlib(PayloadFilesHavePrefix) <= 4.0-1" (bnc#372429)
				if (zypp_is_no_solvable (*provider))
					continue;


				// Is this capability provided by a package we already have listed ?
				if (std::find (pkg_names.begin (), pkg_names.end(),
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
			    std::find (pkg_names.begin (), pkg_names.end(),
				       preferred.name ()) == pkg_names.end()) {
				caps[cap->asString()] = preferred;
				pkg_names.push_back (preferred.name ());
			}
		}

		// print dependencies
		for (std::map<std::string, zypp::sat::Solvable>::iterator it = caps.begin ();
				it != caps.end();
				it++) {

			// backup sanity check for no-solvables
			if (! it->second.name ().c_str() ||
			    it->second.name ().c_str()[0] == '\0')
				continue;

			zypp::PoolItem item = zypp::ResPool::instance ().find (it->second);
			PkInfoEnum info = it->second.isSystem () ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;

			egg_debug ("add dep - '%s' '%s' %d [%s]", it->second.name().c_str(),
				   info == PK_INFO_ENUM_INSTALLED ? "installed" : "available",
				   it->second.isSystem(), 
				   zypp_filter_solvable (_filters, it->second) ? "don't add" : "add" );

			if (!zypp_filter_solvable (_filters, it->second)) {
				zypp_backend_package (backend, info, it->second,
						      item->summary ().c_str());
			}
		}

		pk_backend_set_percentage (backend, 100);
	} catch (const zypp::repo::RepoNotFoundException &ex) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str());
	} catch (const zypp::Exception &ex) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_thread_create (backend, backend_get_depends_thread);
}

static gboolean
backend_get_details_thread (PkBackend *backend)
{
	gchar **package_ids;

	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (!pk_package_ids_check (package_ids)) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
	}
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	for (uint i = 0; package_ids[i]; i++) {
		gchar **id_parts = pk_package_id_split (package_ids[i]);

		std::vector<zypp::sat::Solvable> *v;
		std::vector<zypp::sat::Solvable> *v2;
		std::vector<zypp::sat::Solvable> *v3;
		v = zypp_get_packages_by_name ((const gchar *)id_parts[PK_PACKAGE_ID_NAME], zypp::ResKind::package, TRUE);
		v2 = zypp_get_packages_by_name ((const gchar *)id_parts[PK_PACKAGE_ID_NAME], zypp::ResKind::patch, TRUE);
		v3 = zypp_get_packages_by_name ((const gchar *)id_parts[PK_PACKAGE_ID_NAME], zypp::ResKind::srcpackage, TRUE);

		v->insert (v->end (), v2->begin (), v2->end ());
		v->insert (v->end (), v3->begin (), v3->end ());

		zypp::sat::Solvable package;
		for (std::vector<zypp::sat::Solvable>::iterator it = v->begin ();
				it != v->end (); it++) {
			if (zypp_ver_and_arch_equal (*it, id_parts[PK_PACKAGE_ID_VERSION],
						     id_parts[PK_PACKAGE_ID_ARCH])) {
				package = *it;
				break;
			}
		}
		delete (v);
		delete (v2);
		delete (v3);
		g_strfreev (id_parts);

		if (package == NULL) {
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "couldn't find package");
		}

		try {
			PkGroupEnum group = get_enum_group (zypp_get_group (package));

			if (package.isSystem ()){
				zypp::target::rpm::RpmHeader::constPtr rpmHeader = zypp_get_rpmHeader (package.name (), package.edition ());

				pk_backend_details (backend,
					package_ids[i],			  // package_id
					rpmHeader->tag_license ().c_str (),     // const gchar *license
					group,				  // PkGroupEnum group
					package.lookupStrAttribute (zypp::sat::SolvAttr::description).c_str (), //pkg->description ().c_str (),
					rpmHeader->tag_url (). c_str (),	// const gchar *url
					(gulong)rpmHeader->tag_archivesize ());	// gulong size

			} else {
				pk_backend_details (backend,
					package_ids[i],
					package.lookupStrAttribute (zypp::sat::SolvAttr::license).c_str (), //pkg->license ().c_str (),
					group,
					package.lookupStrAttribute (zypp::sat::SolvAttr::description).c_str (), //pkg->description ().c_str (),
					"TODO", //pkg->url ().c_str (),
					((gulong)package.lookupNumAttribute (zypp::sat::SolvAttr::downloadsize) * 1024)); //pkg->size ());
			}

		} catch (const zypp::target::rpm::RpmException &ex) {
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "Couldn't open rpm-database");
		} catch (const zypp::Exception &ex) {
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString ().c_str ());
		}
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_get_details:
 */
static void
backend_get_details (PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_get_details_thread);
}

static gboolean
backend_get_distro_upgrades_thread(PkBackend *backend)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	std::vector<zypp::parser::ProductFileData> result;
	if (!zypp::parser::ProductFileReader::scanDir (zypp::functor::getAll (std::back_inserter (result)), "/etc/products.d")) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Could not parse /etc/products.d");
	}

	for (std::vector<zypp::parser::ProductFileData>::iterator it = result.begin (); it != result.end (); it++) {
		std::vector<zypp::parser::ProductFileData::Upgrade> upgrades = it->upgrades();
		for (std::vector<zypp::parser::ProductFileData::Upgrade>::iterator it2 = upgrades.begin (); it2 != upgrades.end (); it2++) {
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
 * backend_get_distro_upgrades:
 */
static void
backend_get_distro_upgrades (PkBackend *backend)
{
	pk_backend_thread_create (backend, backend_get_distro_upgrades_thread);
}

static gboolean
backend_refresh_cache_thread (PkBackend *backend)
{
	gboolean force = pk_backend_get_bool(backend, "force");
	zypp_refresh_cache (backend, force);
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_refresh_cache
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	pk_backend_thread_create (backend, backend_refresh_cache_thread);
}

/* If a critical self update (see qualifying steps below) is available then only show/install that update first.
 1. there is a patch available with the <restart_suggested> tag set
 2. The patch contains the package "PackageKit" or "gnome-packagekit
*/
/*static gboolean
check_for_self_update (PkBackend *backend, std::set<zypp::PoolItem> *candidates)
{
	std::set<zypp::PoolItem>::iterator cb = candidates->begin (), ce = candidates->end (), ci;
	for (ci = cb; ci != ce; ++ci) {
		zypp::ResObject::constPtr res = ci->resolvable();
		if (zypp::isKind<zypp::Patch>(res)) {
			zypp::Patch::constPtr patch = zypp::asKind<zypp::Patch>(res);
			//egg_debug ("restart_suggested is %d",(int)patch->restartSuggested());
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
	PkBitfield _filters = (PkBitfield) pk_backend_get_uint (backend, "filters");

	typedef std::set<zypp::PoolItem>::iterator pi_it_t;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	// refresh the repos before checking for updates
	if (!zypp_refresh_cache (backend, FALSE)) {
		pk_backend_finished (backend);
		return FALSE;
	}

	zypp::ResPool pool = zypp_build_pool (TRUE);
	pk_backend_set_percentage (backend, 40);

	// check if the repositories may be dead (feature #301904)
	warn_outdated_repos (backend, pool);

	std::set<zypp::PoolItem> *candidates = zypp_get_updates ();

	pk_backend_set_percentage (backend, 80);

	pi_it_t cb = candidates->begin (), ce = candidates->end (), ci;
	for (ci = cb; ci != ce; ++ci) {
		zypp::ResObject::constPtr res = ci->resolvable();

		// Emit the package
		PkInfoEnum infoEnum = PK_INFO_ENUM_ENHANCEMENT;
		if (zypp::isKind<zypp::Patch>(res)) {
			zypp::Patch::constPtr patch = zypp::asKind<zypp::Patch>(res);
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
	delete (candidates);

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_get_updates
 */
static void
backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	pk_backend_thread_create (backend, backend_get_updates_thread);
}

static gboolean
backend_install_files_thread (PkBackend *backend)
{
	gchar **full_paths;

	full_paths = pk_backend_get_strv (backend, "full_paths");

	// create a temporary directory
	zypp::filesystem::TmpDir tmpDir;
	if (tmpDir == NULL) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_LOCAL_INSTALL_FAILED,
			"Could not create a temporary directory");
	}

	for (guint i = 0; full_paths[i]; i++) {

		// check if file is really a rpm
		zypp::Pathname rpmPath (full_paths[i]);
		zypp::target::rpm::RpmHeader::constPtr rpmHeader = zypp::target::rpm::RpmHeader::readPackage (rpmPath, zypp::target::rpm::RpmHeader::NOSIGNATURE);

		if (rpmHeader == NULL) {
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_LOCAL_INSTALL_FAILED,
				"%s is not valid rpm-File", full_paths[i]);
		}

		// copy the rpm into tmpdir
		std::string tempDest = tmpDir.path ().asString () + "/" + rpmHeader->tag_name () + ".rpm";
		if (zypp::filesystem::copy (full_paths[i], tempDest) != 0) {
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_LOCAL_INSTALL_FAILED,
				"Could not copy the rpm-file into the temp-dir");
		}
	}

	// create a plaindir-repo and cache it
	zypp::RepoInfo tmpRepo;

	try {
		tmpRepo.setType(zypp::repo::RepoType::RPMPLAINDIR);
		std::string url = "dir://" + tmpDir.path ().asString ();
		tmpRepo.addBaseUrl(zypp::Url::parseUrl(url));
		tmpRepo.setEnabled (true);
		tmpRepo.setAutorefresh (true);
		tmpRepo.setAlias ("PK_TMP_DIR");
		tmpRepo.setName ("PK_TMP_DIR");
		zypp_build_pool(true);

		// add Repo to pool

		zypp::RepoManager manager;
		manager.addRepository (tmpRepo);

		manager.refreshMetadata (tmpRepo);
		manager.buildCache (tmpRepo);

	} catch (const zypp::url::UrlException &ex) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString ().c_str ());
	} catch (const zypp::Exception &ex) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString ().c_str ());
	}

	for (guint i = 0; full_paths[i]; i++) {

		zypp::Pathname rpmPath (full_paths[i]);
		zypp::target::rpm::RpmHeader::constPtr rpmHeader = zypp::target::rpm::RpmHeader::readPackage (rpmPath, zypp::target::rpm::RpmHeader::NOSIGNATURE);

		// look for the packages and set them to toBeInstalled
		std::vector<zypp::sat::Solvable> *solvables = 0;
		solvables = zypp_get_packages_by_name (rpmHeader->tag_name ().c_str (), zypp::ResKind::package, FALSE);
		zypp::PoolItem *item = NULL;
		gboolean found = FALSE;

		for (std::vector<zypp::sat::Solvable>::iterator it = solvables->begin (); it != solvables->end (); it ++) {
		       if (it->repository ().alias () == "PK_TMP_DIR") {
			       item = new zypp::PoolItem(*it);
			       found = TRUE;
			       break;
		       }
		}

		if (!found) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Could not find the rpm-Package in Pool");
		} else {
			zypp::ResStatus status = item->status ().setToBeInstalled (zypp::ResStatus::USER);
		}
		if (!zypp_perform_execution (backend, INSTALL, FALSE)) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_LOCAL_INSTALL_FAILED, "Could not install the rpm-file.");
		}

		item->statusReset ();
		delete (solvables);
		delete (item);
	}

	//remove tmp-dir and the tmp-repo
	try {
		zypp::RepoManager manager;
		manager.removeRepository (tmpRepo);
	} catch (const zypp::repo::RepoNotFoundException &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str() );
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
  * backend_install_files
  */
static void
backend_install_files (PkBackend *backend, gboolean only_trusted, gchar **full_paths)
{
	pk_backend_thread_create (backend, backend_install_files_thread);
}

/**
  * backend_simulate_install_files
  */
static void
backend_simulate_install_files (PkBackend *backend, gchar **full_paths)
{
	pk_backend_thread_create (backend, backend_install_files_thread);
}

static gboolean
backend_get_update_detail_thread (PkBackend *backend)
{
	gchar **package_ids;

	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (package_ids == NULL) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
	}
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	for (uint i = 0; package_ids[i]; i++) {
		zypp::sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);

		zypp::Capabilities obs = solvable.obsoletes ();

		gchar *obsoletes = zypp_build_package_id_capabilities (obs);

		PkRestartEnum restart = PK_RESTART_ENUM_NONE;

		zypp::PoolItem item = zypp::ResPool::instance ().find (solvable);

		gchar *bugzilla = new gchar ();
		gchar *cve = new gchar ();

		if (zypp::isKind<zypp::Patch>(solvable)) {
			zypp::Patch::constPtr patch = zypp::asKind<zypp::Patch>(item);
			zypp_get_restart (restart, patch);

			// Building links like "http://www.distro-update.org/page?moo;Bugfix release for kernel;http://www.test.de/bgz;test domain"
			for (zypp::Patch::ReferenceIterator it = patch->referencesBegin (); it != patch->referencesEnd (); it ++) {
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

			zypp::sat::SolvableSet content = patch->contents ();

			for (zypp::sat::SolvableSet::const_iterator it = content.begin (); it != content.end (); it++) {
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
					  solvable.lookupStrAttribute (zypp::sat::SolvAttr::description).c_str (),	// update-text
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
  * backend_get_update_detail
  */
static void
backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_get_update_detail_thread);
}

static gboolean
backend_update_system_thread (PkBackend *backend)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* FIXME: support only_trusted */
	zypp::ResPool pool = zypp_build_pool (TRUE);
	pk_backend_set_percentage (backend, 40);
	PkRestartEnum restart = PK_RESTART_ENUM_NONE;

	std::set<zypp::PoolItem> *candidates = zypp_get_updates ();

	if (_updating_self) {
		_updating_self = FALSE;
	}

	pk_backend_set_percentage (backend, 80);
	std::set<zypp::PoolItem>::iterator cb = candidates->begin (), ce = candidates->end (), ci;
	for (ci = cb; ci != ce; ++ci) {
		// set the status of the update to ToBeInstalled
		zypp::ResStatus &status = ci->status ();
		status.setToBeInstalled (zypp::ResStatus::USER);
		if (zypp::isKind<zypp::Patch>(ci->resolvable ())) {
			zypp_get_restart (restart, zypp::asKind<zypp::Patch>(ci->resolvable ()));
		}
	}

	if (!zypp_perform_execution (backend, UPDATE, FALSE)) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_TRANSACTION_ERROR,
			"Couldn't perform the installation.");
	}

	if (restart != PK_RESTART_ENUM_NONE)
		pk_backend_require_restart (backend, restart, "A restart is needed");

	delete (candidates);
	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_update_system
 */
static void
backend_update_system (PkBackend *backend, gboolean only_trusted)
{
	pk_backend_thread_create (backend, backend_update_system_thread);
}

static gboolean
backend_install_packages_thread (PkBackend *backend)
{
	gchar **package_ids;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	zypp::ZYpp::Ptr zypp;
	zypp = get_zypp ();

	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (!pk_package_ids_check (package_ids)) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
	}
	/* FIXME: support only_trusted */

	try
	{
		zypp::ResPool pool = zypp_build_pool (TRUE);
		pk_backend_set_percentage (backend, 10);
		std::vector<zypp::PoolItem> *items = new std::vector<zypp::PoolItem> ();

		guint to_install = 0;
		for (guint i = 0; package_ids[i]; i++) {
			gchar **id_parts = pk_package_id_split (package_ids[i]);

			// Iterate over the selectables and mark the one with the right name
			zypp::ui::Selectable::constPtr selectable;
			std::string name = id_parts[PK_PACKAGE_ID_NAME];

			// Do we have this installed ?
			gboolean system = false;
			for (zypp::ResPool::byName_iterator it = pool.byNameBegin (name);
			     it != pool.byNameEnd (name); it++) {

				egg_debug ("PoolItem '%s'", it->satSolvable().asString().c_str());

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
				for (zypp::ResPool::byName_iterator it = pool.byNameBegin (name);
				     it != pool.byNameEnd (name); it++) {

					if (zypp_ver_and_arch_equal (it->satSolvable(), id_parts[PK_PACKAGE_ID_VERSION],
								     id_parts[PK_PACKAGE_ID_ARCH])) {
						hit = true;
						to_install++;
						// set status to ToBeInstalled
						it->status ().setToBeInstalled (zypp::ResStatus::USER);
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
			for (std::vector<zypp::PoolItem>::iterator it = items->begin (); it != items->end (); it++) {
				it->statusReset ();
			}
			delete (items);
			pk_backend_finished (backend);
			return FALSE;
		}
		delete (items);

		pk_backend_set_percentage (backend, 100);

	} catch (const zypp::Exception &ex) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_install_packages:
 */
static void
backend_install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	// For now, don't let the user cancel the install once it's started
	pk_backend_set_allow_cancel (backend, FALSE);
	pk_backend_thread_create (backend, backend_install_packages_thread);
}

/**
 * backend_simulate_install_packages:
 */
static void
backend_simulate_install_packages (PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_install_packages_thread);
}

static gboolean
backend_install_signature_thread (PkBackend *backend)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_SIG_CHECK);
	const gchar *key_id = pk_backend_get_string (backend, "key_id");
	_signatures[backend]->push_back ((std::string)(key_id));

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_install_signature:
 */
static void
backend_install_signature (PkBackend *backend, PkSigTypeEnum type, const gchar *key_id, const gchar *package_id)
{
	pk_backend_thread_create (backend, backend_install_signature_thread);
}

static gboolean
backend_remove_packages_thread (PkBackend *backend)
{
	gchar **package_ids;
	std::vector<zypp::PoolItem> *items = new std::vector<zypp::PoolItem> ();

	pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);
	pk_backend_set_percentage (backend, 0);

	zypp::Target_Ptr target;
	zypp::ZYpp::Ptr zypp;
	zypp = get_zypp ();

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
		zypp::ResPool pool = zypp::ResPool::instance ();
		for (zypp::ResPool::byIdent_iterator it = pool.byIdentBegin (zypp::ResKind::package, id_parts[PK_PACKAGE_ID_NAME]);
				it != pool.byIdentEnd (zypp::ResKind::package, id_parts[PK_PACKAGE_ID_NAME]); it++) {
			if ((*it)->isSystem ()) {
				it->status ().setToBeUninstalled (zypp::ResStatus::USER);
				items->push_back (*it);
				break;
			}
		}
		g_strfreev (id_parts);
	}

	pk_backend_set_percentage (backend, 40);

	try
	{
		if (!zypp_perform_execution (backend, REMOVE, TRUE)) {
			//reset the status of the marked packages
			for (std::vector<zypp::PoolItem>::iterator it = items->begin (); it != items->end (); it++) {
				it->statusReset();
			}
			delete (items);
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_TRANSACTION_ERROR,
				"Couldn't remove the package");
		}

		delete (items);
		pk_backend_set_percentage (backend, 100);

	} catch (const zypp::repo::RepoNotFoundException &ex) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str());
	} catch (const zypp::Exception &ex) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_remove_packages:
 */
static void
backend_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	pk_backend_thread_create (backend, backend_remove_packages_thread);
}

static void
backend_simulate_remove_packages (PkBackend *backend, gchar **packages, gboolean autoremove)
{
	pk_backend_thread_create (backend, backend_remove_packages_thread);
}

static gboolean
backend_resolve_thread (PkBackend *backend)
{
	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");
	PkBitfield _filters = (PkBitfield) pk_backend_get_uint (backend, "filters");

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	for (uint i = 0; package_ids[i]; i++) {
		std::vector<zypp::sat::Solvable> *v;

		/* Build a list of packages with this name */
		v = zypp_get_packages_by_name (package_ids[i], zypp::ResKind::package, TRUE);

		if (!pk_bitfield_contain (_filters, PK_FILTER_ENUM_NOT_SOURCE)) {
			std::vector<zypp::sat::Solvable> *src;
			src = zypp_get_packages_by_name (package_ids[i], zypp::ResKind::srcpackage, TRUE);
			v->insert (v->end (), src->begin (), src->end ());
			delete (src);
		}

		zypp::sat::Solvable newest;
		std::vector<zypp::sat::Solvable> pkgs;

		/* Filter the list of packages with this name to 'pkgs' */
		for (std::vector<zypp::sat::Solvable>::iterator it = v->begin (); it != v->end (); it++) {

			if (zypp_filter_solvable (_filters, *it) ||
			    *it == zypp::sat::Solvable::noSolvable)
				continue;

			if (newest == zypp::sat::Solvable::noSolvable) {
				newest = *it;
			} else if (it->edition().match (newest.edition()) > 0) {
				newest = *it;
			}
			pkgs.push_back (*it);
		}
		delete (v);

		/* 'newest' filter support */
		if (pk_bitfield_contain (_filters, PK_FILTER_ENUM_NEWEST)) {
			pkgs.clear();
			pkgs.push_back (newest);
		} else if (pk_bitfield_contain (_filters, PK_FILTER_ENUM_NOT_NEWEST)) {
			pkgs.erase (std::find (pkgs.begin (), pkgs.end(), newest));
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
 * backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, PkBitfield filters, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_resolve_thread);
}

static gboolean
backend_find_packages_thread (PkBackend *backend)
{
	gchar **values;
	const gchar *search;
	guint mode;
	//GList *list = NULL;

	values = pk_backend_get_strv (backend, "search");
	search = values[0];  //Fixme - support the possible multiple values (logical OR search)
	mode = pk_backend_get_uint (backend, "mode");

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);

	std::vector<zypp::sat::Solvable> v;

	zypp::PoolQuery q;
	q.addString( search ); // may be called multiple times (OR'ed)
	q.setCaseSensitive( true );
	q.setMatchSubstring();

	switch (mode) {
		case SEARCH_TYPE_NAME:
			zypp_build_pool (TRUE); // seems to be necessary?
			q.addKind( zypp::ResKind::package );
			q.addKind( zypp::ResKind::srcpackage );
			q.addAttribute( zypp::sat::SolvAttr::name );
			// Note: The query result is NOT sorted packages first, then srcpackage.
			// If that's necessary you need to sort the vector accordongly or use
			// two separate queries.
			break;
		case SEARCH_TYPE_DETAILS:
			zypp_build_pool (TRUE); // seems to be necessary?
			q.addKind( zypp::ResKind::package );
			//q.addKind( zypp::ResKind::srcpackage );
			q.addAttribute( zypp::sat::SolvAttr::name );
			q.addAttribute( zypp::sat::SolvAttr::description );
			// Note: Don't know if zypp_get_packages_by_details intentionally
			// did not search in srcpackages.
			break;
		case SEARCH_TYPE_FILE:
			{
			  // zypp_build_pool (TRUE); called by zypp_get_packages_by_file
			  std::vector<zypp::sat::Solvable> * r = zypp_get_packages_by_file (search);
			  v.swap( *r );
			  delete r;
			  // zypp_get_packages_by_file does strange things :)
			  // Maybe it would be sufficient to simply query
			  // zypp::sat::SolvAttr::filelist instead?
			}
			break;
	};

	if ( ! q.empty() ) {
		std::copy( q.begin(), q.end(), std::back_inserter( v ) );
	}
	zypp_emit_filtered_packages_in_list (backend, v);

	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_search_name:
 */
static void
backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_uint (backend, "mode", SEARCH_TYPE_NAME);
	pk_backend_thread_create (backend, backend_find_packages_thread);
}

/**
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_uint (backend, "mode", SEARCH_TYPE_DETAILS);
	pk_backend_thread_create (backend, backend_find_packages_thread);
}

static gboolean
backend_search_group_thread (PkBackend *backend)
{
	gchar **values;
	const gchar *group;

	values = pk_backend_get_strv (backend, "search");
	group = values[0];  //Fixme - add support for possible multiple values.

	if (group == NULL) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_GROUP_NOT_FOUND, "Group is invalid.");
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	zypp::ResPool pool = zypp_build_pool(true);

	pk_backend_set_percentage (backend, 30);

	std::vector<zypp::sat::Solvable> v;
	PkGroupEnum pkGroup = pk_group_enum_from_string (group);

	zypp::sat::LookupAttr look (zypp::sat::SolvAttr::group);

	for (zypp::sat::LookupAttr::iterator it = look.begin (); it != look.end (); it++) {
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
 * backend_search_group:
 */
static void
backend_search_groups (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_thread_create (backend, backend_search_group_thread);
}

/**
 * backend_search_file:
 */
static void
backend_search_files (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_uint (backend, "mode", SEARCH_TYPE_FILE);
	pk_backend_thread_create (backend, backend_find_packages_thread);
}

/**
 * backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	//FIXME - use the new param - filter

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	zypp::RepoManager manager;
	std::list <zypp::RepoInfo> repos;
	try
	{
		repos = std::list<zypp::RepoInfo>(manager.repoBegin(),manager.repoEnd());
	} catch (const zypp::repo::RepoNotFoundException &ex) {
		zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str());
		return;
	} catch (const zypp::Exception &ex) {
		zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
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
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	zypp::RepoManager manager;
	zypp::RepoInfo repo;

	try {
		repo = manager.getRepositoryInfo (rid);
		repo.setEnabled (enabled);
		manager.modifyRepository (rid, repo);
		if (!enabled) {
			zypp::Repository repository = zypp::sat::Pool::instance ().reposFind (repo.alias ());
			repository.eraseFromPool ();
		}

	} catch (const zypp::repo::RepoNotFoundException &ex) {
		zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_REPO_NOT_FOUND, ex.asUserString().c_str());
		return;
	} catch (const zypp::Exception &ex) {
		zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asUserString().c_str());
		return;
	}

	pk_backend_finished (backend);
}

static gboolean
backend_get_files_thread (PkBackend *backend)
{
	gchar **package_ids;

	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (!pk_package_ids_check (package_ids)) {
		return zypp_backend_finished_error (
			backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
	}

	for (uint i = 0; package_ids[i]; i++) {
		gchar **id_parts = pk_package_id_split (package_ids[i]);
		pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

		std::vector<zypp::sat::Solvable> *v;
		std::vector<zypp::sat::Solvable> *v2;
		v = zypp_get_packages_by_name ((const gchar *)id_parts[PK_PACKAGE_ID_NAME], zypp::ResKind::package, TRUE);
		v2 = zypp_get_packages_by_name ((const gchar *)id_parts[PK_PACKAGE_ID_NAME], zypp::ResKind::srcpackage, TRUE);

		v->insert (v->end (), v2->begin (), v2->end ());

		zypp::sat::Solvable package;
		for (std::vector<zypp::sat::Solvable>::iterator it = v->begin ();
				it != v->end (); it++) {
			char *version = g_strdup (it->edition ().asString ().c_str ());
			if (strcmp (id_parts[PK_PACKAGE_ID_VERSION], version) == 0) {
				g_free (version);
				package = *it;
				break;
			}
			g_free (version);
		}

		delete (v);
		delete (v2);
		g_strfreev (id_parts);

		if (package == NULL) {
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				"couldn't find package");
		}

		std::string temp;
		if (package.isSystem ()){
			try {
				zypp::target::rpm::RpmHeader::constPtr rpmHeader = zypp_get_rpmHeader (package.name (), package.edition ());
				std::list<std::string> files = rpmHeader->tag_filenames ();

				for (std::list<std::string>::iterator it = files.begin (); it != files.end (); it++) {
					temp.append (*it);
					temp.append (";");
				}

			} catch (const zypp::target::rpm::RpmException &ex) {
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
  * backend_get_files:
  */
static void
backend_get_files(PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_get_files_thread);
}

static gboolean
backend_get_packages_thread (PkBackend *backend)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	std::vector<zypp::sat::Solvable> v;

	zypp_build_pool (TRUE);
	zypp::ResPool pool = zypp::ResPool::instance ();
	for (zypp::ResPool::byKind_iterator it = pool.byKindBegin (zypp::ResKind::package); it != pool.byKindEnd (zypp::ResKind::package); it++) {
		v.push_back (it->satSolvable ());
	}

	zypp_emit_filtered_packages_in_list (backend, v);

	pk_backend_finished (backend);
	return TRUE;
}
/**
  * backend_get_packages:
  */
static void
backend_get_packages (PkBackend *backend, PkBitfield filter)
{
	pk_backend_thread_create (backend, backend_get_packages_thread);
}

static gboolean
backend_update_packages_thread (PkBackend *backend)
{
	gboolean retval;
	gchar **package_ids;
	/* FIXME: support only_trusted */
	package_ids = pk_backend_get_strv (backend, "package_ids");
	PkRestartEnum restart = PK_RESTART_ENUM_NONE;

	delete zypp_get_updates (); // make sure _updating_self is set

	if (_updating_self) {
		egg_debug ("updating self and setting restart");
		pk_backend_require_restart (backend, PK_RESTART_ENUM_SESSION, "Package Management System updated - restart needed");
		_updating_self = FALSE;
	}
	for (guint i = 0; package_ids[i]; i++) {
		zypp::sat::Solvable solvable = zypp_get_package_by_id (package_ids[i]);
		zypp::PoolItem item = zypp::ResPool::instance ().find (solvable);
		item.status ().setToBeInstalled (zypp::ResStatus::USER);
		zypp::Patch::constPtr patch = zypp::asKind<zypp::Patch>(item.resolvable ());
		zypp_get_restart (restart, patch);
	}

	retval = zypp_perform_execution (backend, UPDATE, FALSE);
	pk_backend_finished (backend);

	if (restart != PK_RESTART_ENUM_NONE)
		pk_backend_require_restart (backend, restart, "A restart is needed");

	return retval;
}

/**
  * backend_update_packages
  */
static void
backend_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_update_packages_thread);
}

/**
  * backend_simulate_update_packages
  */
static void
backend_simulate_update_packages (PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_update_packages_thread);
}

static gboolean
backend_repo_set_data_thread (PkBackend *backend)
{
	const gchar *repo_id;
	const gchar *parameter;
	const gchar *value;

	repo_id = pk_backend_get_string (backend, "repo_id");
	parameter = pk_backend_get_string (backend, "parameter");
	value = pk_backend_get_string (backend, "value");
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	zypp::RepoManager manager;
	zypp::RepoInfo repo;

	gboolean bReturn = TRUE;

	try {
		pk_backend_set_status(backend, PK_STATUS_ENUM_SETUP);
		// add a new repo
		if (g_ascii_strcasecmp (parameter, "add") == 0) {
			repo.setAlias (repo_id);
			repo.setBaseUrl (zypp::Url(value));
			repo.setAutorefresh (TRUE);
			repo.setEnabled (TRUE);

			manager.addRepository (repo);

		// remove a repo
		} else if (g_ascii_strcasecmp (parameter, "remove") == 0) {
			repo = manager.getRepositoryInfo (repo_id);
			manager.removeRepository (repo);
		// set autorefresh of a repo true/false
		} else if (g_ascii_strcasecmp (parameter, "refresh") == 0) {
			repo = manager.getRepositoryInfo (repo_id);

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
			repo = manager.getRepositoryInfo (repo_id);

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
			repo = manager.getRepositoryInfo (repo_id);
			repo.setBaseUrl (zypp::Url(value));
			manager.modifyRepository (repo_id, repo);
		} else if (g_ascii_strcasecmp (parameter, "name") == 0) {
			repo = manager.getRepositoryInfo (repo_id);
			repo.setName(value);
			manager.modifyRepository (repo_id, repo);
		} else if (g_ascii_strcasecmp (parameter, "prio") == 0) {
			repo = manager.getRepositoryInfo (repo_id);
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

	} catch (const zypp::repo::RepoNotFoundException &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "Couldn't find the specified repository");
		bReturn = FALSE;
	} catch (const zypp::repo::RepoAlreadyExistsException &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "This repo already exists");
		bReturn = FALSE;
	} catch (const zypp::repo::RepoUnknownTypeException &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Type of the repo can't be determined");
		bReturn = FALSE;
	} catch (const zypp::repo::RepoException &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "Can't access the given URL");
		bReturn = FALSE;
	} catch (const zypp::Exception &ex) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, ex.asString ().c_str ());
		bReturn = FALSE;
	}

	pk_backend_finished (backend);
	return bReturn;
}

/**
  * backend_repo_set_data
  */
static void
backend_repo_set_data (PkBackend *backend, const gchar *repo_id, const gchar *parameter, const gchar *value)
{
	pk_backend_thread_create (backend, backend_repo_set_data_thread);
}

static gboolean
backend_what_provides_thread (PkBackend *backend)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	gchar **values = pk_backend_get_strv (backend, "search");
	const gchar *search = values[0]; //Fixme - support possible multiple search values (logical OR)
	PkProvidesEnum provides = (PkProvidesEnum) pk_backend_get_uint (backend, "provides");
	PkBitfield _filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	zypp::Capability cap (search);
	zypp::sat::WhatProvides prov (cap);

	if((provides == PK_PROVIDES_ENUM_HARDWARE_DRIVER) || g_ascii_strcasecmp("drivers_for_attached_hardware", search) == 0) {
		// solver run
		zypp::ResPool pool = zypp_build_pool(true);
		zypp::Resolver solver(pool);
		solver.setIgnoreAlreadyRecommended (TRUE);

		if (!solver.resolvePool ()) {
			std::list<zypp::ResolverProblem_Ptr> problems = solver.problems ();
			for (std::list<zypp::ResolverProblem_Ptr>::iterator it = problems.begin (); it != problems.end (); it++){
				egg_warning("Solver problem (This should never happen): '%s'", (*it)->description ().c_str ());
			}
			solver.setIgnoreAlreadyRecommended (FALSE);
			return zypp_backend_finished_error (
				backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "Resolution failed");
		}

		// look for packages which would be installed
		for (zypp::ResPool::byKind_iterator it = pool.byKindBegin (zypp::ResKind::package);
				it != pool.byKindEnd (zypp::ResKind::package); it++) {
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
		for (zypp::sat::WhatProvides::const_iterator it = prov.begin (); it != prov.end (); it++) {
			if (zypp_filter_solvable (_filters, *it))
				continue;
			
			PkInfoEnum info = it->isSystem () ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;
			zypp_backend_package (backend, info, *it, it->lookupStrAttribute (zypp::sat::SolvAttr::summary).c_str ());
		}
	}

	pk_backend_finished (backend);
	return TRUE;
}

/**
  * backend_what_provides
  */
static void
backend_what_provides (PkBackend *backend, PkBitfield filters, PkProvidesEnum provide, gchar **values)
{
	pk_backend_thread_create (backend, backend_what_provides_thread);
}

static gchar *
backend_get_mime_types (PkBackend *backend)
{
	return g_strdup ("application/x-rpm");
}

extern "C" PK_BACKEND_OPTIONS (
	"Zypp",					/* description */
	"Boyd Timothy <btimothy@gmail.com>, "
	"Scott Reeves <sreeves@novell.com>, "
	"Stefan Haas <shaas@suse.de>",		/* author */
	backend_initialize,			/* initalize */
	backend_destroy,			/* destroy */
	backend_get_groups,			/* get_groups */
	backend_get_filters,			/* get_filters */
	NULL,					/* get_roles */
	backend_get_mime_types,			/* get_mime_types */
	NULL,					/* cancel */
	NULL,					/* download_packages */
	NULL,					/* get_categories */
	backend_get_depends,			/* get_depends */
	backend_get_details,			/* get_details */
	backend_get_distro_upgrades,		/* get_distro_upgrades */
	backend_get_files,			/* get_files */
	backend_get_packages,			/* get_packages */
	backend_get_repo_list,			/* get_repo_list */
	backend_get_requires,			/* get_requires */
	backend_get_update_detail,		/* get_update_detail */
	backend_get_updates,			/* get_updates */
	backend_install_files,			/* install_files */
	backend_install_packages,		/* install_packages */
	backend_install_signature,		/* install_signature */
	backend_refresh_cache,			/* refresh_cache */
	backend_remove_packages,		/* remove_packages */
	backend_repo_enable,			/* repo_enable */
	backend_repo_set_data,			/* repo_set_data */
	backend_resolve,			/* resolve */
	NULL,					/* rollback */
	backend_search_details,			/* search_details */
	backend_search_files,			/* search_files */
	backend_search_groups,			/* search_groups */
	backend_search_names,			/* search_names */
	backend_update_packages,		/* update_packages */
	backend_update_system,			/* update_system */
	backend_what_provides,			/* what_provides */
	backend_simulate_install_files,		/* simulate_install_files */
	backend_simulate_install_packages,	/* simulate_install_packages */
	backend_simulate_remove_packages,	/* simulate_remove_packages */
	backend_simulate_update_packages	/* simulate_update_packages */
);

