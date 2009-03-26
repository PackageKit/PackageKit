/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2009 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
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

#include <string>

#include <apt-pkg/error.h>
#include <apt-pkg/init.h>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/cachefile.h>

#include "apt.h"
#include "apt-utils.h"
#include "matcher.h"
#include "aptcc_show_broken.h"
#include "acqprogress.h"
#include "aptcc_show_error.h"
#include "pkg_acqfile.h"
#include "rsources.h"

#include <config.h>

#include <locale.h>
// #include <unistd.h>
#include <errno.h>
#include <stdio.h>

using namespace std;

/* static bodges */
static bool _cancel = false;
static pkgSourceList *apt_source_list = 0;

/**
 * backend_initialize:
 */
static void
backend_initialize (PkBackend *backend)
{
	egg_debug ("APTcc Initializing");

	if (pkgInitConfig(*_config) == false ||
	    pkgInitSystem(*_config, _system) == false)
	{
		egg_debug ("ERROR initializing backend");
	}

	// Open the cache file
	apt_source_list = new pkgSourceList;
	apt_source_list->ReadMainList();
}

/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
	egg_debug ("APTcc being destroyed");
	if (apt_source_list)
	{
		delete apt_source_list;
		apt_source_list = NULL;
	}
}

/**
 * backend_get_groups:
 */
static PkBitfield
backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_GROUP_ENUM_ACCESSORIES,
		PK_GROUP_ENUM_ADMIN_TOOLS,
		PK_GROUP_ENUM_COMMUNICATION,
		PK_GROUP_ENUM_DOCUMENTATION,
		PK_GROUP_ENUM_DESKTOP_GNOME,
		PK_GROUP_ENUM_DESKTOP_KDE,
		PK_GROUP_ENUM_DESKTOP_OTHER,
		PK_GROUP_ENUM_ELECTRONICS,
		PK_GROUP_ENUM_GAMES,
		PK_GROUP_ENUM_GRAPHICS,
		PK_GROUP_ENUM_INTERNET,
		PK_GROUP_ENUM_LEGACY,
		PK_GROUP_ENUM_LOCALIZATION,
		PK_GROUP_ENUM_MULTIMEDIA,
		PK_GROUP_ENUM_NETWORK,
		PK_GROUP_ENUM_OTHER,
		PK_GROUP_ENUM_PROGRAMMING,
		PK_GROUP_ENUM_PUBLISHING,
		PK_GROUP_ENUM_SCIENCE,
		PK_GROUP_ENUM_SYSTEM,
		PK_GROUP_ENUM_COLLECTIONS,
		-1);
}

/**
 * backend_get_filters:
 */
static PkBitfield
backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_GUI,
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_DEVELOPMENT,
		PK_FILTER_ENUM_FREE,
// 		PK_FILTER_ENUM_COLLECTIONS,//FIXME see if this apply
		-1);
}

/**
 * backend_get_mime_types:
 */
static gchar *
backend_get_mime_types (PkBackend *backend)
{
	return g_strdup ("application/x-deb");
}

/**
 * backend_cancel:
 */
static void
backend_cancel (PkBackend *backend)
{
	_cancel = true;
        pk_backend_set_status(backend, PK_STATUS_ENUM_CANCEL);
}

static gboolean
backend_get_depends_or_requires_thread (PkBackend *backend)
{
	gchar **package_ids;
	PkBitfield filters;
	bool recursive;

	package_ids = pk_backend_get_strv (backend, "package_ids");
	filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	recursive = pk_backend_get_bool (backend, "recursive");
	_cancel = false;
	pk_backend_set_allow_cancel (backend, true);
	PkPackageId *pi = pk_package_id_new_from_string (package_ids[0]);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_backend_finished (backend);
		return false;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	aptcc *m_apt = new aptcc();
	if (m_apt->init(pk_backend_get_locale (backend), *apt_source_list)) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		return false;
	}

	bool depends = pk_backend_get_bool(backend, "get_depends");

	vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > output;
	for (uint i = 0; i < g_strv_length(package_ids); i++) {
		if (_cancel) {
			break;
		}
		pi = pk_package_id_new_from_string (package_ids[i]);
		if (pi == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
			pk_backend_finished (backend);
			delete m_apt;
			return false;
		}

		pkgCache::PkgIterator pkg = m_apt->cacheFile->FindPkg(pi->name);
		if (pkg.end() == true)
		{
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "couldn't find package");
			pk_package_id_free (pi);
			pk_backend_finished (backend);
			delete m_apt;
			return false;
		}

		if (depends) {
			m_apt->get_depends(output, pkg, recursive, _cancel);
		} else {
			m_apt->get_requires(output, pkg, recursive, _cancel);
		}

		pk_package_id_free (pi);
	}

	sort(output.begin(), output.end(), compare());
	output.erase(unique(output.begin(), output.end(), result_equality()),
		     output.end());

	// It's faster to emmit the packages here than in the matching part
	for(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> >::iterator i=output.begin();
	    i != output.end(); ++i)
	{
		if (_cancel) {
			break;
		}
		m_apt->emit_package(backend, filters, i->first, i->second);
	}

	delete m_apt;

	pk_backend_finished (backend);
	return true;
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_set_bool (backend, "get_depends", true);
	pk_backend_set_bool (backend, "recursive", recursive);
	pk_backend_thread_create (backend, backend_get_depends_or_requires_thread);
}

/**
 * backend_get_requires:
 */
static void
backend_get_requires (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_set_bool (backend, "get_depends", false);
	pk_backend_set_bool (backend, "recursive", recursive);
	pk_backend_thread_create (backend, backend_get_depends_or_requires_thread);
}

static gboolean
backend_get_files_thread (PkBackend *backend)
{
	gchar **package_ids;
	PkPackageId *pi;

	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (package_ids == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_backend_finished (backend);
		return false;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	aptcc *m_apt = new aptcc();
	if (m_apt->init(pk_backend_get_locale (backend), *apt_source_list)) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		return false;
	}

	for (uint i = 0; i < g_strv_length(package_ids); i++) {
		pi = pk_package_id_new_from_string (package_ids[i]);
		if (pi == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
			pk_backend_finished (backend);
			delete m_apt;
			return false;
		}

		pkgCache::PkgIterator Pkg = m_apt->cacheFile->FindPkg(pi->name);
		if (Pkg.end() == true)
		{
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "couldn't find package");
			pk_package_id_free (pi);
			pk_backend_finished (backend);
			delete m_apt;
			return false;
		}

		emit_files (backend, pi);

		pk_package_id_free (pi);
	}

	delete m_apt;

	pk_backend_finished (backend);
	return true;
}

/**
 * backend_get_files:
 */
static void
backend_get_files (PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_get_files_thread);
}

static gboolean
backend_get_details_thread (PkBackend *backend)
{
	gchar **package_ids;
	PkPackageId *pi;

	bool updateDetail = pk_backend_get_bool (backend, "updateDetail");
	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (package_ids == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_backend_finished (backend);
		return false;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	aptcc *m_apt = new aptcc();
	if (m_apt->init(pk_backend_get_locale (backend), *apt_source_list)) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		return false;
	}

	
	for (uint i = 0; i < g_strv_length(package_ids); i++) {
		pi = pk_package_id_new_from_string (package_ids[i]);
		if (pi == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
			pk_backend_finished (backend);
			delete m_apt;
			return false;
		}

		pkgCache::PkgIterator pkg = m_apt->cacheFile->FindPkg(pi->name);
		if (pkg.end() == true)
		{
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "couldn't find package");
			pk_package_id_free (pi);
			pk_backend_finished (backend);
			delete m_apt;
			return false;
		}

		if (updateDetail) {
			m_apt->emit_update_detail(backend, pkg);
		} else {
			m_apt->emit_details(backend, pkg);
		}

		pk_package_id_free (pi);
	}

	delete m_apt;

	pk_backend_finished (backend);
	return true;
}

/**
 * backend_get_update_detail:
 */
static void
backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	pk_backend_set_bool (backend, "updateDetail", true);
	pk_backend_thread_create (backend, backend_get_details_thread);
}

/**
 * backend_get_details:
 */
static void
backend_get_details (PkBackend *backend, gchar **package_ids)
{
	pk_backend_set_bool (backend, "updateDetail", false);
	pk_backend_thread_create (backend, backend_get_details_thread);
}

static gboolean
backend_get_updates_thread (PkBackend *backend)
{
	PkBitfield filters;
	filters = (PkBitfield) pk_backend_get_uint (backend, "filters");

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	_cancel = false;
	pk_backend_set_allow_cancel (backend, true);

	aptcc *m_apt = new aptcc();
	if (m_apt->init(pk_backend_get_locale (backend), *apt_source_list)) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		return false;
	}

	pkgset to_install, to_hold, to_remove, to_purge;
	// Build to_install to avoid a big printout
	for(pkgCache::PkgIterator i=m_apt->cacheFile->PkgBegin(); !i.end(); ++i)
	{
		pkgDepCache::StateCache state= m_apt->get_state(i);

		if(!i.CurrentVer().end() &&
		    state.Upgradable() && !m_apt->is_held(i)) {
		    to_install.insert(i);
		}
	}

	// First try the built-in resolver; if it fails (which it shouldn't
	// if an upgrade is possible), cancel all changes and try apt's
	// resolver.
// 	m_apt->mark_all_upgradable(false, true);

	if (pkgAllUpgrade(*m_apt->DCache) == false)
	{
		show_broken(backend, m_apt);
		egg_debug ("Internal error, AllUpgrade broke stuff");
		delete m_apt;
		return false;
	}

// //   if(!aptitude::cmdline::safe_resolve_deps(verbose, no_new_installs, true))
// //     {
//       {
// 	aptcc::action_group action_group(*m_apt);
// 
// 	// Reset all the package states.
// 	for(pkgCache::PkgIterator i=m_apt->DCache->PkgBegin();
// 	    !i.end(); ++i)
// 	  m_apt->mark_keep(i, false, false, NULL);
//       }
// 
//       // Use the apt 'upgrade' algorithm as a fallback against, e.g.,
//       // bugs in the aptitude resolver.
//       if(!m_apt->all_upgrade(false, NULL))
// 	{
// // 	  show_broken();
// 
// 	  _error->DumpErrors();
// 	  return -1;
// 	}
// //     }

	pkgvector lists[num_pkg_action_states];
	pkgvector recommended, suggested;
	pkgvector extra_install, extra_remove;
	unsigned long Upgrade=0, Downgrade=0, Install=0, ReInstall=0;

	for(pkgCache::PkgIterator pkg=m_apt->cacheFile->PkgBegin();
	    !pkg.end(); ++pkg)
	{
		if((*m_apt->DCache)[pkg].NewInstall()) {
			++Install;
		} else if((*m_apt->DCache)[pkg].Upgrade()) {
			++Upgrade;
		} else if((*m_apt->DCache)[pkg].Downgrade()) {
			++Downgrade;
		} else if(!(*m_apt->DCache)[pkg].Delete() &&
			((*m_apt->DCache)[pkg].iFlags & pkgDepCache::ReInstall)) {
			++ReInstall;
		}

		pkg_action_state state=find_pkg_state(pkg, *m_apt);

		switch(state)
		    {
		    case pkg_auto_install:
		    case pkg_install:
		    case pkg_upgrade:
		    if(to_install.find(pkg)==to_install.end())
			extra_install.push_back(pkg);
		    break;
		    case pkg_auto_remove:
		    case pkg_unused_remove:
		    case pkg_remove:
		    if(to_remove.find(pkg)==to_remove.end())
			extra_remove.push_back(pkg);
		    break;
		    case pkg_unchanged:
		    if(pkg.CurrentVer().end())
			{
	    // 	      if(package_recommended(pkg))
	    // 		recommended.push_back(pkg);
	    // 	      else if(package_suggested(pkg))
	    // 		suggested.push_back(pkg);
			}
		    default:
		    break;
		    }

		switch(state)
		{
		case pkg_auto_install:
			lists[pkg_install].push_back(pkg);
			break;
		case pkg_unused_remove:
		case pkg_auto_remove:
			lists[pkg_remove].push_back(pkg);
			break;
		case pkg_auto_hold:
			if(to_install.find(pkg) != to_install.end()) {
			    lists[pkg_hold].push_back(pkg);
			}
			break;
		case pkg_hold:
			if(to_install.find(pkg) != to_install.end()) {
			    lists[pkg_hold].push_back(pkg);
			}
			break;
		case pkg_unchanged:
			break;
		default:
			lists[state].push_back(pkg);
		}
	}

// printf("upgrade: %lu\n", Upgrade);
// printf("install: %lu\n", Install);
// printf("to_install: %lu\n", to_install.size());
// printf("to_hold: %lu\n", lists[pkg_hold].size());

	for(int i=0; i < num_pkg_action_states; ++i)
	{
		PkInfoEnum state;
		switch(i)
		{
		case pkg_hold:
			state = PK_INFO_ENUM_BLOCKED;
			break;
		case pkg_upgrade:
			state = PK_INFO_ENUM_NORMAL;
			break;
		default:
			continue;
		}

		if(!lists[i].empty())
		{
			vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > output;
			for (pkgvector::iterator it = lists[i].begin(); it != lists[i].end(); ++it)
			{
				if (_cancel) {
				    break;
				}
				pkgCache::VerIterator ver = m_apt->find_candidate_ver(*it);
				if (ver.end() == false)
				{
					output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(*it, ver));
				}
			}

			sort(output.begin(), output.end(), compare());
			output.erase(unique(output.begin(), output.end(), result_equality()),
				    output.end());

			// It's faster to emmit the packages here than in the matching part
			for(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> >::iterator it = output.begin();
			    it != output.end(); ++it)
			{
				if (_cancel) {
					break;
				}
				m_apt->emit_package(backend, filters, it->first, it->second, state);
			}
		}
	}

	delete m_apt;
	pk_backend_finished (backend);
	return true;
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	pk_backend_thread_create (backend, backend_get_updates_thread);
}

/**
 * backend_download_packages_thread:
 */
static gboolean
backend_download_packages_thread (PkBackend *backend)
{
	gchar **package_ids;
	string directory;

	package_ids = pk_backend_get_strv(backend, "package_ids");
	directory = _config->FindDir("Dir::Cache::archives") + "partial/";

	_cancel = false;
	pk_backend_set_allow_cancel (backend, true);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	aptcc *m_apt = new aptcc();
	if (m_apt->init(pk_backend_get_locale (backend), *apt_source_list)) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		return false;
	}

	// Create the progress
	AcqPackageKitStatus Stat(backend, _cancel, _config->FindI("quiet",0));

	// get a fetcher
	pkgAcquire fetcher(&Stat);
	string filelist;
	PkPackageId *pi;

	for (uint i = 0; i < g_strv_length(package_ids); i++) {
		pi = pk_package_id_new_from_string (package_ids[i]);
		if (pi == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
			pk_backend_finished (backend);
			delete m_apt;
			return false;
		}

		if (_cancel) {
			break;
		}

		pkgCache::PkgIterator pkg = m_apt->cacheFile->FindPkg(pi->name);
		// Ignore packages that could not be found or that exist only due to dependencies.
		if (pkg.end() == true || (pkg.VersionList().end() && pkg.ProvidesList().end()))
		{
			_error->Error("Can't find a package named \"%s\"", pi->name);
			pk_package_id_free(pi);
			continue;
		}

		pkgCache::VerIterator ver;
		ver = m_apt->find_ver(pkg);
		// check to see if the provided package isn't virtual too
		if (ver.end())
		{
			pk_package_id_free(pi);
			continue;
		}

		if(!ver.Downloadable()) {
			_error->Error("No downloadable files for %s version %s; perhaps it is a local or obsolete package?",
				    pi->name, ver.VerStr());
		}

		string storeFileName;
		get_archive(&fetcher, apt_source_list, m_apt->packageRecords,
			    ver, directory, storeFileName);
		string destFile = directory + "/" + flNotDir(storeFileName);
		if (filelist.empty()) {
			filelist = destFile;
		} else {
			filelist.append(";" + destFile);
		}
		pk_package_id_free(pi);
	}

	pk_backend_set_status(backend, PK_STATUS_ENUM_DOWNLOAD);
	if(fetcher.Run() != pkgAcquire::Continue)
	// We failed or were cancelled
	{
		_error->DumpErrors();
		delete m_apt;
		if (_cancel) {
			pk_backend_finished(backend);
			return true;
		}
		return false;
	}

	// send the filelist
	pk_backend_files(backend, NULL, filelist.c_str());

	delete m_apt;
	pk_backend_finished (backend);
	return true;
}

/**
 * backend_download_packages:
 */
static void
backend_download_packages (PkBackend *backend, gchar **package_ids, const gchar *directory)
{
	pk_backend_thread_create (backend, backend_download_packages_thread);
}

/**
 * backend_refresh_cache_thread:
 */
static gboolean
backend_refresh_cache_thread (PkBackend *backend)
{
	_cancel = false;
	pk_backend_set_allow_cancel (backend, true);
	pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);

	aptcc *m_apt = new aptcc();
	if (m_apt->init(pk_backend_get_locale (backend), *apt_source_list)) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		return false;
	}

	// Lock the list directory
	FileFd Lock;
	if (_config->FindB("Debug::NoLocking", false) == false)
	{
		Lock.Fd(GetLock(_config->FindDir("Dir::State::Lists") + "lock"));
		if (_error->PendingError() == true) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_CANNOT_GET_LOCK, "Unable to lock the list directory");
			delete m_apt;
			return false;
	// 	 return _error->Error(_("Unable to lock the list directory"));
		}
	}
	// Create the progress
	AcqPackageKitStatus Stat(backend, _cancel, _config->FindI("quiet",0));

	// Just print out the uris an exit if the --print-uris flag was used
	if (_config->FindB("APT::Get::Print-URIs") == true)
	{
		// get a fetcher
		pkgAcquire Fetcher(&Stat);

		// Populate it with the source selection and get all Indexes
		// (GetAll=true)
		if (apt_source_list->GetIndexes(&Fetcher, true) == false) {
			delete m_apt;
			return false;
		}

		pkgAcquire::UriIterator I = Fetcher.UriBegin();
		for (; I != Fetcher.UriEnd(); I++) {
			cout << '\'' << I->URI << "' " << flNotDir(I->Owner->DestFile) << ' ' <<
			    I->Owner->FileSize << ' ' << I->Owner->HashSum() << endl;
		}
		delete m_apt;
		return true;
	}

	// do the work
	if (_config->FindB("APT::Get::Download",true) == true) {
		ListUpdate(Stat, *apt_source_list);
	}

	// Rebuild the cache.
	pkgCacheFile Cache;
	OpTextProgress Prog(*_config);
	if (Cache.BuildCaches(Prog, true) == false) {
		if (_error->PendingError() == true) {
			show_errors(backend, PK_ERROR_ENUM_CANNOT_GET_LOCK);
		}
		delete m_apt;
		return false;
	}

	// missing gpg signature would appear here
	// TODO we need a better enum
	if (_error->PendingError() == false && _error->empty() == false) {
		show_warnings(backend, PK_MESSAGE_ENUM_UNTRUSTED_PACKAGE);
	}

	pk_backend_finished (backend);
	delete m_apt;
	return true;
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	pk_backend_thread_create (backend, backend_refresh_cache_thread);
}


static gboolean
backend_resolve_thread (PkBackend *backend)
{
	gchar **package_ids;
	PkBitfield filters;

	filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	package_ids = pk_backend_get_strv (backend, "package_ids");
	_cancel = false;
	pk_backend_set_allow_cancel (backend, true);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	aptcc *m_apt = new aptcc();
	if (m_apt->init(pk_backend_get_locale (backend), *apt_source_list)) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		return false;
	}

	for (uint i = 0; i < g_strv_length(package_ids); i++) {
		if (_cancel) {
			break;
		}

		pkgCache::PkgIterator pkg = m_apt->cacheFile->FindPkg(package_ids[i]);
		// Ignore packages that could not be found or that exist only due to dependencies.
		if (pkg.end() == true || (pkg.VersionList().end() && pkg.ProvidesList().end()))
		{
			continue;
		}

		pkgCache::VerIterator ver;
		ver = m_apt->find_ver(pkg);
		// check to see if the provided package isn't virtual too
		if (ver.end() == false)
		{
			m_apt->emit_package(backend, filters, pkg, ver);
		}
	}

	delete m_apt;

	pk_backend_finished (backend);
	return true;
}

/**
 * backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, PkBitfield filters, gchar **packages)
{
	pk_backend_thread_create (backend, backend_resolve_thread);
}

static gboolean
backend_search_file_thread (PkBackend *backend)
{
	const gchar *search;
	PkBitfield filters;

	search = pk_backend_get_string (backend, "search");
	filters = (PkBitfield) pk_backend_get_uint (backend, "filters");

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	_cancel = false;
	pk_backend_set_allow_cancel (backend, true);

	// as we can only search for installed files lets avoid the opposite
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
		aptcc *m_apt = new aptcc();
		if (m_apt->init(pk_backend_get_locale (backend), *apt_source_list)) {
			egg_debug ("Failed to create apt cache");
			delete m_apt;
			return false;
		}

		vector<string> packages = search_file (backend, search, _cancel);
		for(vector<string>::iterator i = packages.begin();
		    i != packages.end(); ++i)
		{
			if (_cancel) {
			    break;
			}
			pkgCache::PkgIterator pkg = m_apt->cacheFile->FindPkg(i->c_str());
			pkgCache::VerIterator ver = m_apt->find_ver(pkg);
			if (ver.end() == true)
			{
				continue;
			}
			m_apt->emit_package(backend, filters, pkg, ver);
		}

		delete m_apt;
	}

	pk_backend_finished (backend);
	return true;
}

/**
 * backend_search_file:
 */
static void
backend_search_file (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	pk_backend_thread_create (backend, backend_search_file_thread);
}

static gboolean
backend_search_group_thread (PkBackend *backend)
{
	const gchar *group;
	PkBitfield filters;

	group = pk_backend_get_string (backend, "search");
	filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	_cancel = false;
	pk_backend_set_allow_cancel (backend, true);

	if (group == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_GROUP_NOT_FOUND, "Group is invalid.");
		pk_backend_finished (backend);
		return false;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	PkGroupEnum pkGroup = pk_group_enum_from_text (group);

	aptcc *m_apt = new aptcc();
	if (m_apt->init(pk_backend_get_locale (backend), *apt_source_list)) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		return false;
	}

	vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > output;
	for (pkgCache::PkgIterator pkg = m_apt->cacheFile->PkgBegin(); !pkg.end(); ++pkg) {
		if (_cancel) {
			break;
		}
		// Ignore packages that exist only due to dependencies.
		if (pkg.VersionList().end() && pkg.ProvidesList().end()) {
			continue;
		}

		// Ignore virtual packages
		pkgCache::VerIterator ver = m_apt->find_ver(pkg);
		if (ver.end() == false) {
			string section = pkg.VersionList().Section();

			size_t found;
			found = section.find_last_of("/");
			section = section.substr(found + 1);

			// Don't insert virtual packages instead add what it provides
			if (pkGroup == get_enum_group(section)) {
				output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, ver));
			}
		}
	}

	sort(output.begin(), output.end(), compare());

	// It's faster to emmit the packages here rather than in the matching part
	for(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> >::iterator i=output.begin();
	    i != output.end(); ++i)
	{
		if (_cancel) {
			break;
		}
		m_apt->emit_package(backend, filters, i->first, i->second);
	}

	delete m_apt;

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
	return true;
}

/**
 * backend_search_group:
 */
static void
backend_search_group (PkBackend *backend, PkBitfield filters, const gchar *pkGroup)
{
	pk_backend_thread_create (backend, backend_search_group_thread);
}

static gboolean
backend_search_package_thread (PkBackend *backend)
{
	const gchar *search;
	PkBitfield filters;

	search = pk_backend_get_string (backend, "search");
	filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	_cancel = false;
	pk_backend_set_allow_cancel (backend, true);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	matcher *m_matcher = new matcher(string(search));
	if (m_matcher->hasError()) {
		egg_debug("Regex compilation error");
		delete m_matcher;
		return false;
	}

	aptcc *m_apt = new aptcc();
	if (m_apt->init(pk_backend_get_locale (backend), *apt_source_list)) {
		egg_debug ("Failed to create apt cache");
		delete m_matcher;
		delete m_apt;
		return false;
	}

	if (_error->PendingError() == true)
	{
		delete m_matcher;
		delete m_apt;
		return false;
	}

	pkgDepCache::Policy Plcy;
	vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > output;
	if (pk_backend_get_bool (backend, "search_details")) {
		for (pkgCache::PkgIterator pkg = m_apt->cacheFile->PkgBegin(); !pkg.end(); ++pkg) {
			if (_cancel) {
				break;
			}
			// Ignore packages that exist only due to dependencies.
			if (pkg.VersionList().end() && pkg.ProvidesList().end()) {
				continue;
			}

			if (m_matcher->matches(pkg.Name())) {
				// Don't insert virtual packages instead add what it provides
				pkgCache::VerIterator ver = m_apt->find_ver(pkg);
				if (ver.end() == false) {
					output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, ver));
				} else {
					// iterate over the provides list
					for (pkgCache::PrvIterator Prv = pkg.ProvidesList(); Prv.end() == false; Prv++) {
						ver = m_apt->find_ver(Prv.OwnerPkg());

						// check to see if the provided package isn't virtual too
						if (ver.end() == false)
						{
							// we add the package now because we will need to
							// remove duplicates later anyway
							output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(Prv.OwnerPkg(), ver));
						}
					}
				}
			} else {
				// Don't insert virtual packages instead add what it provides
				pkgCache::VerIterator ver = m_apt->find_ver(pkg);
				if (ver.end() == false) {
					if (m_matcher->matches(get_default_short_description(ver, m_apt->packageRecords))
					    || m_matcher->matches(get_default_long_description(ver, m_apt->packageRecords))
					    || m_matcher->matches(get_short_description(ver, m_apt->packageRecords))
					    || m_matcher->matches(get_long_description(ver, m_apt->packageRecords)))
					{
						output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, ver));
					}
				} else {
					// iterate over the provides list
					for (pkgCache::PrvIterator Prv = pkg.ProvidesList(); Prv.end() == false; Prv++) {
						ver = m_apt->find_ver(Prv.OwnerPkg());

						// check to see if the provided package isn't virtual too
						if (ver.end() == false)
						{
							// we add the package now because we will need to
							// remove duplicates later anyway
							if (m_matcher->matches(Prv.OwnerPkg().Name())
							    || m_matcher->matches(get_default_short_description(ver, m_apt->packageRecords))
							    || m_matcher->matches(get_default_long_description(ver, m_apt->packageRecords))
							    || m_matcher->matches(get_short_description(ver, m_apt->packageRecords))
							    || m_matcher->matches(get_long_description(ver, m_apt->packageRecords)))
							{
								output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(Prv.OwnerPkg(), ver));
							}
						}
					}
				}
			}
		}
	} else {
		for (pkgCache::PkgIterator pkg = m_apt->cacheFile->PkgBegin(); !pkg.end(); ++pkg) {
			if (_cancel) {
				break;
			}
			// Ignore packages that exist only due to dependencies.
			if (pkg.VersionList().end() && pkg.ProvidesList().end()) {
				continue;
			}

			if (m_matcher->matches(pkg.Name())) {
				// Don't insert virtual packages instead add what it provides
				pkgCache::VerIterator ver = m_apt->find_ver(pkg);
				if (ver.end() == false) {
					output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, ver));
				} else {
					// iterate over the provides list
					for (pkgCache::PrvIterator Prv = pkg.ProvidesList(); Prv.end() == false; Prv++) {
						ver = m_apt->find_ver(Prv.OwnerPkg());

						// check to see if the provided package isn't virtual too
						if (ver.end() == false)
						{
							// we add the package now because we will need to
							// remove duplicates later anyway
							output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(Prv.OwnerPkg(), ver));
						}
					}
				}
			}
		}
	}

	sort(output.begin(), output.end(), compare());
	output.erase(unique(output.begin(), output.end(), result_equality()),
		       output.end());

	// It's faster to emmit the packages here than in the matching part
	for(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> >::iterator i=output.begin();
	    i != output.end(); ++i)
	{
		if (_cancel) {
			break;
		}
		m_apt->emit_package(backend, filters, i->first, i->second);
	}

	delete m_matcher;
	delete m_apt;

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
	return true;
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	pk_backend_set_bool (backend, "search_details", false);
	pk_backend_thread_create (backend, backend_search_package_thread);
}


/**
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	pk_backend_set_bool (backend, "search_details", true);
	pk_backend_thread_create (backend, backend_search_package_thread);
}

/**
 * backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	bool notDevelopment;
	notDevelopment = pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	SourcesList _lst;
	if (_lst.ReadSources() == false) {
		_error->
		    Warning("Ignoring invalid record(s) in sources.list file!");
	    //return false;
	}

	if (_lst.ReadVendors() == false) {
		_error->Error("Cannot read vendors.list file");
		show_errors(backend, PK_ERROR_ENUM_FAILED_CONFIG_PARSING);
		return;
	}

	for (SourcesListIter it = _lst.SourceRecords.begin();
        it != _lst.SourceRecords.end(); it++)
	{
		if ((*it)->Type & SourcesList::Comment) {
		    continue;
		}

		string Sections;
		for (unsigned int J = 0; J < (*it)->NumSections; J++) {
			Sections += (*it)->Sections[J];
			Sections += " ";
		}

		if (notDevelopment &&
			((*it)->Type & SourcesList::DebSrc ||
			 (*it)->Type & SourcesList::RpmSrc ||
			 (*it)->Type & SourcesList::RpmSrcDir ||
			 (*it)->Type & SourcesList::RepomdSrc))
		{
			continue;
		}

		string repo;
		repo = (*it)->GetType();
		repo.append(" " + (*it)->VendorID);
		repo.append(" " + (*it)->URI);
		repo.append(" " + (*it)->Dist);
		repo.append(" " + Sections);
		pk_backend_repo_detail(backend,
				       repo.c_str(),
				       repo.c_str(),
				       !((*it)->Type & SourcesList::Disabled));
	}
	pk_backend_finished (backend);
}

/**
 * backend_repo_enable:
 */
static void
backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_REQUEST);

	SourcesList _lst;
	if (_lst.ReadSources() == false) {
		_error->
		    Warning("Ignoring invalid record(s) in sources.list file!");
	}

	if (_lst.ReadVendors() == false) {
		_error->Error("Cannot read vendors.list file");
		show_errors(backend, PK_ERROR_ENUM_FAILED_CONFIG_PARSING);
		return;
	}

	bool found = false;
	for (SourcesListIter it = _lst.SourceRecords.begin();
        it != _lst.SourceRecords.end(); it++)
	{
		if ((*it)->Type & SourcesList::Comment) {
		    continue;
		}

		string Sections;
		for (unsigned int J = 0; J < (*it)->NumSections; J++) {
			Sections += (*it)->Sections[J];
			Sections += " ";
		}

		string repo;
		repo = (*it)->GetType();
		repo.append(" " + (*it)->VendorID);
		repo.append(" " + (*it)->URI);
		repo.append(" " + (*it)->Dist);
		repo.append(" " + Sections);
		if (repo.compare(rid) == 0) {
// 			printf("Found: %s, repo %s.\n", rid, repo.c_str());
			if (enabled) {
				(*it)->Type = (*it)->Type & ~SourcesList::Disabled;
			} else {
				(*it)->Type |= SourcesList::Disabled;
			}
			found = true;
			break;
		} else {
// 			printf("Not found: %s, repo %s.\n", rid, repo.c_str());
		}
	}

	if (!found) {
		_error->Error("Could not found the repositorie");
		show_errors(backend, PK_ERROR_ENUM_REPO_NOT_AVAILABLE);
	} else if (!_lst.UpdateSources()) {
		_error->Error("Could not update sources file");
		show_errors(backend, PK_ERROR_ENUM_CANNOT_WRITE_REPO_CONFIG);
	}
	pk_backend_finished (backend);
}

static gboolean
backend_get_packages_thread (PkBackend *backend)
{
	PkBitfield filters;
	filters = (PkBitfield) pk_backend_get_uint (backend, "filters");

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	_cancel = false;
	pk_backend_set_allow_cancel (backend, true);

	aptcc *m_apt = new aptcc();
	if (m_apt->init(pk_backend_get_locale (backend), *apt_source_list)) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		return false;
	}

	vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > output;
	output.reserve(m_apt->cacheFile->HeaderP->PackageCount);
	for(pkgCache::PkgIterator pkg = m_apt->cacheFile->PkgBegin();
	    !pkg.end(); ++pkg)
	{
		if (_cancel) {
			break;
		}
		// Ignore packages that exist only due to dependencies.
		if(pkg.VersionList().end() && pkg.ProvidesList().end())
			continue;

		// Don't insert virtual packages as they don't have all kinds of info
		pkgCache::VerIterator ver = m_apt->find_ver(pkg);
		if (ver.end() == false) {
			output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, ver));
		}
	}

	sort(output.begin(), output.end(), compare());

	// It's faster to emmit the packages rather here than in the matching part
	for(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> >::iterator i=output.begin();
	    i != output.end(); ++i)
	{
		if (_cancel) {
			break;
		}
		m_apt->emit_package(backend, filters, i->first, i->second);
	}

	delete m_apt;

	pk_backend_finished (backend);
	return true;
}

/**
  * backend_get_packages:
  */
static void
backend_get_packages (PkBackend *backend, PkBitfield filter)
{
	pk_backend_thread_create (backend, backend_get_packages_thread);
}

extern "C" PK_BACKEND_OPTIONS (
	"APTcc",					/* description */
	"Daniel Nicoletti <dantti85-pk@yahoo.com.br>",	/* author */
	backend_initialize,	        		/* initalize */
	backend_destroy,				/* destroy */
	backend_get_groups,				/* get_groups */
	backend_get_filters,				/* get_filters */
	backend_get_mime_types,				/* get_mime_types */
	backend_cancel,					/* cancel */
	backend_download_packages,			/* download_packages */
	NULL,						/* get_categories */
	backend_get_depends,				/* get_depends */
	backend_get_details,				/* get_details */
	NULL,						/* get_distro_upgrades */
	backend_get_files,				/* get_files */
	backend_get_packages,				/* get_packages */
	backend_get_repo_list,				/* get_repo_list */
	backend_get_requires,				/* get_requires */
	backend_get_update_detail,			/* get_update_detail */
	backend_get_updates,				/* get_updates */
	NULL,						/* install_files */
	NULL,						/* install_packages */
	NULL,						/* install_signature */
	backend_refresh_cache,				/* refresh_cache */
	NULL,						/* remove_packages */
	backend_repo_enable,				/* repo_enable */
	NULL,						/* repo_set_data */
	backend_resolve,				/* resolve */
	NULL,						/* rollback */
	backend_search_details,				/* search_details */
	backend_search_file,				/* search_file */
	backend_search_group,				/* search_group */
	backend_search_name,				/* search_name */
	NULL,						/* update_packages */
	NULL,						/* update_system */
	NULL						/* what_provides */
);
