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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <apt-pkg/init.h>
#include <apt-pkg/algorithms.h>

#include "apt.h"
#include "apt-utils.h"
#include "matcher.h"
#include "aptcc_show_broken.h"
#include "acqprogress.h"
#include "aptcc_show_error.h"
#include "pkg_acqfile.h"
#include "rsources.h"

#include <config.h>

/* static bodges */
static bool _cancel = false;

/**
 * backend_initialize:
 */
static void
backend_initialize (PkBackend *backend)
{
	egg_debug ("APTcc Initializing");

	// make sure we do not get a graphical debconf
	setenv("DEBIAN_FRONTEND", "noninteractive", 1);
	setenv("APT_LISTCHANGES_FRONTEND", "none", 1);

	if (pkgInitConfig(*_config) == false ||
	    pkgInitSystem(*_config, _system) == false)
	{
		egg_debug ("ERROR initializing backend");
	}
}

/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
	egg_debug ("APTcc being destroyed");
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
	aptcc *m_apt = (aptcc*) pk_backend_get_pointer(backend, "aptcc_obj");
	if (m_apt) {
		m_apt->cancel();
	}
}

static gboolean
backend_get_depends_or_requires_thread (PkBackend *backend)
{
	gchar **package_ids;
	PkBitfield filters;
	gchar *pi;
	bool recursive;

	package_ids = pk_backend_get_strv (backend, "package_ids");
	filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	recursive = pk_backend_get_bool (backend, "recursive");

	pk_backend_set_allow_cancel (backend, true);

	aptcc *m_apt = new aptcc(backend, _cancel);
	pk_backend_set_pointer(backend, "aptcc_obj", m_apt);
	if (m_apt->init()) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		pk_backend_finished (backend);
		return false;
	}

	bool depends = pk_backend_get_bool(backend, "get_depends");

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > output;
	for (uint i = 0; i < g_strv_length(package_ids); i++) {
		if (_cancel) {
			break;
		}
		pi = package_ids[i];
		if (pk_package_id_check(pi) == false) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_ID_INVALID,
					       pi);
			delete m_apt;
			pk_backend_finished (backend);
			return false;
		}

		pair<pkgCache::PkgIterator, pkgCache::VerIterator> pkg_ver;
		pkg_ver = m_apt->find_package_id(pi);
		if (pkg_ver.second.end() == true)
		{
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "Couldn't find package");
			delete m_apt;
			pk_backend_finished (backend);
			return false;
		}

		if (depends) {
			m_apt->get_depends(output, pkg_ver.first, recursive);
		} else {
			m_apt->get_requires(output, pkg_ver.first, recursive);
		}
	}

	// It's faster to emmit the packages here than in the matching part
	m_apt->emit_packages(output, filters);

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
backend_get_distro_upgrades_thread (PkBackend *backend)
{
	FILE *fpipe;
	char releaseName[256];
	char releaseVersion[256];

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	if (!(fpipe = (FILE*)popen("./get-distro-upgrade.py", "r"))) {
	    goto out;
	}

	if (fgets(releaseName, sizeof releaseName, fpipe)) {
	    goto out;
	}

	if (fgets(releaseVersion, sizeof releaseVersion, fpipe)) {
	    goto out;
	}
	pclose(fpipe);

	pk_backend_distro_upgrade (backend,
				   PK_DISTRO_UPGRADE_ENUM_STABLE,
				   g_strdup_printf("%s %s", releaseName, releaseVersion),
				   "The latest stable release");

	pk_backend_finished (backend);
	return true;

out:
	pk_backend_finished (backend);
	return false;
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
backend_get_files_thread (PkBackend *backend)
{
	gchar **package_ids;
	gchar *pi;

	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (package_ids == NULL) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_PACKAGE_ID_INVALID,
				       "Invalid package id");
		pk_backend_finished (backend);
		return false;
	}

	aptcc *m_apt = new aptcc(backend, _cancel);
	pk_backend_set_pointer(backend, "aptcc_obj", m_apt);
	if (m_apt->init()) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		pk_backend_finished (backend);
		return false;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	for (uint i = 0; i < g_strv_length(package_ids); i++) {
		pi = package_ids[i];
		if (pk_package_id_check(pi) == false) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_ID_INVALID,
					       pi);
			delete m_apt;
			pk_backend_finished (backend);
			return false;
		}

		pair<pkgCache::PkgIterator, pkgCache::VerIterator> pkg_ver;
		pkg_ver = m_apt->find_package_id(pi);
		if (pkg_ver.second.end() == true)
		{
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "Couldn't find package");
			delete m_apt;
			pk_backend_finished (backend);
			return false;
		}

		emit_files (backend, pi);
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
	gchar *pi;

	bool updateDetail = pk_backend_get_bool (backend, "updateDetail");
	package_ids = pk_backend_get_strv (backend, "package_ids");
	if (package_ids == NULL) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_PACKAGE_ID_INVALID,
				       "Invalid package id");
		pk_backend_finished (backend);
		return false;
	}

	aptcc *m_apt = new aptcc(backend, _cancel);
	pk_backend_set_pointer(backend, "aptcc_obj", m_apt);
	if (m_apt->init()) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		pk_backend_finished (backend);
		return false;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	for (uint i = 0; i < g_strv_length(package_ids); i++) {
		pi = package_ids[i];
		if (pk_package_id_check(pi) == false) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_ID_INVALID,
					       pi);
			delete m_apt;
			pk_backend_finished (backend);
			return false;
		}

		pair<pkgCache::PkgIterator, pkgCache::VerIterator> pkg_ver;
		pkg_ver = m_apt->find_package_id(pi);
		if (pkg_ver.second.end() == true)
		{
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "couldn't find package");

			delete m_apt;
			pk_backend_finished (backend);
			return false;
		}

		if (updateDetail) {
			m_apt->emit_update_detail(pkg_ver.first);
		} else {
			m_apt->emit_details(pkg_ver.first);
		}
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
backend_get_or_update_system_thread (PkBackend *backend)
{
	PkBitfield filters;
	bool getUpdates;
	filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	getUpdates = pk_backend_get_bool(backend, "getUpdates");
	pk_backend_set_allow_cancel (backend, true);

	aptcc *m_apt = new aptcc(backend, _cancel);
	pk_backend_set_pointer(backend, "aptcc_obj", m_apt);
	if (m_apt->init()) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		pk_backend_finished (backend);
		return false;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	pkgCacheFile Cache;
	OpTextProgress Prog(*_config);
	int timeout = 10;
	// TODO test this
	while (Cache.Open(Prog, !getUpdates) == false) {
		// failed to open cache, try checkDeps then..
		// || Cache.CheckDeps(CmdL.FileSize() != 1) == false
		if (getUpdates == true || (timeout <= 0)) {
			pk_backend_error_code(backend,
					      PK_ERROR_ENUM_NO_CACHE,
					      "Could not open package cache.");
			return false;
		} else {
			pk_backend_set_status (backend, PK_STATUS_ENUM_WAITING_FOR_LOCK);
			sleep(1);
			timeout--;
		}
	}
	pk_backend_set_status (backend, PK_STATUS_ENUM_RUNNING);

	if (pkgDistUpgrade(*Cache) == false)
	{
		show_broken(backend, Cache, false);
		egg_debug ("Internal error, DistUpgrade broke stuff");
		delete m_apt;
		pk_backend_finished (backend);
		return false;
	}

	bool res = true;
	if (getUpdates) {
		vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > update,
									    kept;

		for(pkgCache::PkgIterator pkg=m_apt->packageCache->PkgBegin();
		    !pkg.end();
		    ++pkg)
		{
			if((*Cache)[pkg].Upgrade()    == true &&
			(*Cache)[pkg].NewInstall() == false) {
				update.push_back(
					pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, m_apt->find_candidate_ver(pkg)));
			} else if ((*Cache)[pkg].Upgradable() == true &&
				pkg->CurrentVer != 0 &&
				(*Cache)[pkg].Delete() == false) {
				kept.push_back(
					pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, m_apt->find_candidate_ver(pkg)));
			}
		}

		m_apt->emitUpdates(update, filters);
		m_apt->emit_packages(kept, filters, PK_INFO_ENUM_BLOCKED);
	} else {
		res = m_apt->installPackages(Cache);
	}

	delete m_apt;
	pk_backend_finished (backend);
	return res;
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	pk_backend_set_bool (backend, "getUpdates", true);
	pk_backend_thread_create (backend, backend_get_or_update_system_thread);
}

/**
 * backend_update_system:
 */
static void
backend_update_system (PkBackend *backend, gboolean only_trusted)
{
	pk_backend_set_bool (backend, "getUpdates", false);
	pk_backend_thread_create (backend, backend_get_or_update_system_thread);
}

static gboolean
backend_what_provides_thread (PkBackend *backend)
{
	PkProvidesEnum provides;
	PkBitfield filters;
	const gchar *search;
	const gchar *provides_text;
	gchar **values;
	bool error = false;

	filters  = (PkBitfield)     pk_backend_get_uint (backend, "filters");
	provides = (PkProvidesEnum) pk_backend_get_uint (backend, "provides");
	search   = pk_backend_get_string (backend, "search");
	values   = g_strsplit (search, "&", 0);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	if (provides == PK_PROVIDES_ENUM_MIMETYPE ||
	    provides == PK_PROVIDES_ENUM_CODEC ||
	    provides == PK_PROVIDES_ENUM_ANY) {
		aptcc *m_apt = new aptcc(backend, _cancel);
		pk_backend_set_pointer(backend, "aptcc_obj", m_apt);
		if (m_apt->init()) {
			egg_debug ("Failed to create apt cache");
			g_strfreev (values);
			delete m_apt;
			pk_backend_finished (backend);
			return false;
		}


		pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
		vector<string> packages;
		vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > output;
		if (provides == PK_PROVIDES_ENUM_MIMETYPE) {
			packages = searchMimeType (backend, values, error, _cancel);
		} else if (provides == PK_PROVIDES_ENUM_CODEC) {
			m_apt->povidesCodec(output, values);
		} else {
			// any...
			packages = searchMimeType (backend, values, error, _cancel);
			m_apt->povidesCodec(output, values);
		}

		for(vector<string>::iterator i = packages.begin();
		    i != packages.end(); ++i)
		{
			if (_cancel) {
			    break;
			}
			pkgCache::PkgIterator pkg = m_apt->packageCache->FindPkg(i->c_str());
			pkgCache::VerIterator ver = m_apt->find_ver(pkg);
			if (ver.end() == true) {
				continue;
			}
			output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, ver));
		}

		if (error) {
			// check if app-install-data is installed
			pkgCache::PkgIterator pkg;
			pkg = m_apt->packageCache->FindPkg("app-install-data");
			if (pkg->CurrentState != pkgCache::State::Installed) {
				pk_backend_error_code (backend,
						       PK_ERROR_ENUM_INTERNAL_ERROR,
						       "You need the app-install-data "
						       "package to be able to look for "
						       "applications that can handle "
						       "this kind of file");
			}
		} else {
			// It's faster to emmit the packages here rather than in the matching part
			m_apt->emit_packages(output, filters);
		}

		delete m_apt;
	} else {
		provides_text = pk_provides_enum_to_string (provides);
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_NOT_SUPPORTED,
				       "Provides %s not supported",
				       provides_text);
	}

	g_strfreev (values);
	pk_backend_finished (backend);
	return true;
}

/**
  * backend_what_provides
  */
static void
backend_what_provides (PkBackend *backend,
		       PkBitfield filters,
		       PkProvidesEnum provide,
		       gchar **values)
{
	pk_backend_thread_create (backend, backend_what_provides_thread);
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
	pk_backend_set_allow_cancel (backend, true);

	aptcc *m_apt = new aptcc(backend, _cancel);
	pk_backend_set_pointer(backend, "aptcc_obj", m_apt);
	if (m_apt->init()) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		pk_backend_finished (backend);
		return false;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	// Create the progress
	AcqPackageKitStatus Stat(m_apt, backend, _cancel);

	// get a fetcher
	pkgAcquire fetcher(&Stat);
	string filelist;
	gchar *pi;

	// TODO this might be useful when the item is in the cache
// 	for (pkgAcquire::ItemIterator I = fetcher.ItemsBegin(); I < fetcher.ItemsEnd();)
// 	{
// 		if ((*I)->Local == true)
// 		{
// 			I++;
// 			continue;
// 		}
//
// 		// Close the item and check if it was found in cache
// 		(*I)->Finished();
// 		if ((*I)->Complete == false) {
// 			Transient = true;
// 		}
//
// 		// Clear it out of the fetch list
// 		delete *I;
// 		I = fetcher.ItemsBegin();
// 	}

	for (uint i = 0; i < g_strv_length(package_ids); i++) {
		pi = package_ids[i];
		if (pk_package_id_check(pi) == false) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_ID_INVALID,
					       pi);
			delete m_apt;
			pk_backend_finished (backend);
			return false;
		}

		if (_cancel) {
			break;
		}

		pair<pkgCache::PkgIterator, pkgCache::VerIterator> pkg_ver;
		pkg_ver = m_apt->find_package_id(pi);
		// Ignore packages that could not be found or that exist only due to dependencies.
		if (pkg_ver.second.end() == true)
		{
			_error->Error("Can't find this package id \"%s\".", pi);
			continue;
		} else {
			if(!pkg_ver.second.Downloadable()) {
				_error->Error("No downloadable files for %s,"
					      "perhaps it is a local or obsolete" "package?",
					      pi);
				continue;
			}

			string storeFileName;
			if (get_archive(&fetcher,
					m_apt->packageSourceList,
					m_apt->packageRecords,
					pkg_ver.second,
					directory,
					storeFileName))
			{
				Stat.addPackagePair(pkg_ver);
			}
			string destFile = directory + "/" + flNotDir(storeFileName);
			if (filelist.empty()) {
				filelist = destFile;
			} else {
				filelist.append(";" + destFile);
			}
		}
	}

	if (fetcher.Run() != pkgAcquire::Continue
	    && _cancel == false)
	// We failed and we did not cancel
	{
		show_errors(backend, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED);
		delete m_apt;
		pk_backend_finished (backend);
		return _cancel;
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
	pk_backend_set_allow_cancel (backend, true);

	aptcc *m_apt = new aptcc(backend, _cancel);
	pk_backend_set_pointer(backend, "aptcc_obj", m_apt);
	if (m_apt->init()) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		pk_backend_finished (backend);
		return false;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
	// Lock the list directory
	FileFd Lock;
	if (_config->FindB("Debug::NoLocking", false) == false)
	{
		Lock.Fd(GetLock(_config->FindDir("Dir::State::Lists") + "lock"));
		if (_error->PendingError() == true) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_CANNOT_GET_LOCK, "Unable to lock the list directory");
			delete m_apt;
			pk_backend_finished (backend);
			return false;
	// 	 return _error->Error(_("Unable to lock the list directory"));
		}
	}
	// Create the progress
	AcqPackageKitStatus Stat(m_apt, backend, _cancel);

	// do the work
	if (_config->FindB("APT::Get::Download",true) == true) {
		ListUpdate(Stat, *m_apt->packageSourceList);
	}

	// Rebuild the cache.
	pkgCacheFile Cache;
	OpTextProgress Prog(*_config);
	if (Cache.BuildCaches(Prog, true) == false) {
		if (_error->PendingError() == true) {
			show_errors(backend, PK_ERROR_ENUM_CANNOT_GET_LOCK);
		}
		delete m_apt;
		pk_backend_finished (backend);
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
	pk_backend_set_allow_cancel (backend, true);

	aptcc *m_apt = new aptcc(backend, _cancel);
	pk_backend_set_pointer(backend, "aptcc_obj", m_apt);
	if (m_apt->init()) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		pk_backend_finished (backend);
		return false;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	gchar *pi;
	vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > output;
	for (uint i = 0; i < g_strv_length(package_ids); i++) {
		if (_cancel) {
			break;
		}

		pair<pkgCache::PkgIterator, pkgCache::VerIterator> pkg_ver;
		pi = package_ids[i];
		if (pk_package_id_check(pi) == false) {
			pkg_ver.first = m_apt->packageCache->FindPkg(pi);
			// Ignore packages that could not be found or that exist only due to dependencies.
			if (pkg_ver.first.end() == true ||
			    (pkg_ver.first.VersionList().end() && pkg_ver.first.ProvidesList().end()))
			{
				continue;
			}

			pkg_ver.second = m_apt->find_ver(pkg_ver.first);
			// check to see if the provided package isn't virtual too
			if (pkg_ver.second.end() == false)
			{
				output.push_back(pkg_ver);
			}

			pkg_ver.second = m_apt->find_candidate_ver(pkg_ver.first);
			// check to see if the provided package isn't virtual too
			if (pkg_ver.second.end() == false)
			{
				output.push_back(pkg_ver);
			}
		} else {
			pkg_ver = m_apt->find_package_id(pi);
			// check to see if we found the package
			if (pkg_ver.second.end() == false)
			{
				output.push_back(pkg_ver);
			}
		}
	}
	// It's faster to emmit the packages here rather than in the matching part
	m_apt->emit_packages(output, filters);

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
backend_search_files_thread (PkBackend *backend)
{
	gchar **values;
	PkBitfield filters;

	values = pk_backend_get_strv (backend, "search");
	filters = (PkBitfield) pk_backend_get_uint (backend, "filters");

	pk_backend_set_allow_cancel (backend, true);

	// as we can only search for installed files lets avoid the opposite
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
		aptcc *m_apt = new aptcc(backend, _cancel);
		pk_backend_set_pointer(backend, "aptcc_obj", m_apt);
		if (m_apt->init()) {
			egg_debug ("Failed to create apt cache");
			delete m_apt;
			pk_backend_finished (backend);
			return false;
		}

		pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
		vector<string> packages = search_files (backend, values, _cancel);
		vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > output;
		for(vector<string>::iterator i = packages.begin();
		    i != packages.end(); ++i)
		{
			if (_cancel) {
			    break;
			}
			pkgCache::PkgIterator pkg = m_apt->packageCache->FindPkg(i->c_str());
			pkgCache::VerIterator ver = m_apt->find_ver(pkg);
			if (ver.end() == true)
			{
				continue;
			}
			output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, ver));
		}
		// It's faster to emmit the packages here rather than in the matching part
		m_apt->emit_packages(output, filters);

		delete m_apt;
	}

	pk_backend_finished (backend);
	return true;
}

/**
 * backend_search_files:
 */
static void
backend_search_files (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_thread_create (backend, backend_search_files_thread);
}

static gboolean
backend_search_groups_thread (PkBackend *backend)
{
	gchar **values;
	PkBitfield filters;
	vector<PkGroupEnum> groups;

	values = pk_backend_get_strv (backend, "search");
	filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	pk_backend_set_allow_cancel (backend, true);

	int len = g_strv_length(values);
	for (uint i = 0; i < len; i++) {
		if (values[i] == NULL) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_GROUP_NOT_FOUND,
					       values[i]);
			pk_backend_finished (backend);
			return false;
		} else {
			groups.push_back(pk_group_enum_from_string(values[i]));
		}
	}

	aptcc *m_apt = new aptcc(backend, _cancel);
	pk_backend_set_pointer(backend, "aptcc_obj", m_apt);
	if (m_apt->init()) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		pk_backend_finished (backend);
		return false;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > output;
	for (pkgCache::PkgIterator pkg = m_apt->packageCache->PkgBegin(); !pkg.end(); ++pkg) {
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
			for (vector<PkGroupEnum>::iterator i = groups.begin();
			     i != groups.end();
			     ++i) {
				if (*i == get_enum_group(section)) {
					output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, ver));
					break;
				}
			}
		}
	}

	// It's faster to emmit the packages here rather than in the matching part
	m_apt->emit_packages(output, filters);

	delete m_apt;

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
	return true;
}

/**
 * backend_search_groups:
 */
static void
backend_search_groups (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_thread_create (backend, backend_search_groups_thread);
}

static gboolean
backend_search_package_thread (PkBackend *backend)
{
	gchar **values;
	gchar *search;
	PkBitfield filters;

	values = pk_backend_get_strv (backend, "search");
	search = g_strjoinv("|", values);
	filters = (PkBitfield) pk_backend_get_uint (backend, "filters");

	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	pk_backend_set_allow_cancel (backend, true);

	matcher *m_matcher = new matcher(search);
	g_free(search);
	if (m_matcher->hasError()) {
		egg_debug("Regex compilation error");
		delete m_matcher;
		pk_backend_finished (backend);
		return false;
	}

	aptcc *m_apt = new aptcc(backend, _cancel);
	pk_backend_set_pointer(backend, "aptcc_obj", m_apt);
	if (m_apt->init()) {
		egg_debug ("Failed to create apt cache");
		delete m_matcher;
		delete m_apt;
		pk_backend_finished (backend);
		return false;
	}

	if (_error->PendingError() == true)
	{
		delete m_matcher;
		delete m_apt;
		pk_backend_finished (backend);
		return false;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pkgDepCache::Policy Plcy;
	vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > output;
	if (pk_backend_get_bool (backend, "search_details")) {
		for (pkgCache::PkgIterator pkg = m_apt->packageCache->PkgBegin(); !pkg.end(); ++pkg) {
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
		for (pkgCache::PkgIterator pkg = m_apt->packageCache->PkgBegin(); !pkg.end(); ++pkg) {
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

	// It's faster to emmit the packages here than in the matching part
	m_apt->emit_packages(output, filters);

	delete m_matcher;
	delete m_apt;

	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
	return true;
}

/**
 * backend_search_names:
 */
static void
backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_bool(backend, "search_details", false);
	pk_backend_thread_create(backend, backend_search_package_thread);
}

/**
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_bool(backend, "search_details", true);
	pk_backend_thread_create(backend, backend_search_package_thread);
}

static gboolean
backend_manage_packages_thread (PkBackend *backend)
{
	gchar **package_ids;
	gchar *pi;
	bool simulate;
	bool remove;

	package_ids = pk_backend_get_strv (backend, "package_ids");
	simulate = pk_backend_get_bool (backend, "simulate");
	remove = pk_backend_get_bool (backend, "remove");

	pk_backend_set_allow_cancel (backend, true);

	aptcc *m_apt = new aptcc(backend, _cancel);
	pk_backend_set_pointer(backend, "aptcc_obj", m_apt);
	if (m_apt->init()) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		pk_backend_finished (backend);
		return false;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > pkgs;
	for (uint i = 0; i < g_strv_length(package_ids); i++) {
		if (_cancel) {
			break;
		}

		pi = package_ids[i];
		if (pk_package_id_check(pi) == false) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_ID_INVALID,
					       pi);
			delete m_apt;
			pk_backend_finished (backend);
			return false;
		}

		pair<pkgCache::PkgIterator, pkgCache::VerIterator> pkg_ver;
		pkg_ver = m_apt->find_package_id(pi);
		// Ignore packages that could not be found or that exist only due to dependencies.
		if (pkg_ver.second.end() == true)
		{
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "couldn't find package");

			delete m_apt;
			pk_backend_finished (backend);
			return false;
		} else {
			pkgs.push_back(pkg_ver);
		}
	}

	if (!m_apt->runTransaction(pkgs, simulate, remove)) {
		// Print transaction errors
		cout << "runTransaction failed" << endl;
		delete m_apt;
		pk_backend_finished (backend);
		return false;
	}

	delete m_apt;
	pk_backend_finished (backend);
	return true;
}

/**
 * backend_install_update_packages:
 */
static void
backend_install_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	pk_backend_set_bool(backend, "simulate", false);
	pk_backend_set_bool(backend, "remove", false);
	pk_backend_thread_create (backend, backend_manage_packages_thread);
}

/**
 * backend_simulate_install_update_packages:
 */
static void
backend_simulate_install_update_packages (PkBackend *backend, gchar **packages)
{
	pk_backend_set_bool(backend, "simulate", true);
	pk_backend_set_bool(backend, "remove", false);
	pk_backend_thread_create (backend, backend_manage_packages_thread);
}

/**
 * backend_remove_packages:
 */
static void
backend_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	pk_backend_set_bool(backend, "simulate", false);
	pk_backend_set_bool(backend, "remove", true);
	pk_backend_thread_create (backend, backend_manage_packages_thread);
}

/**
 * backend_simulate_remove_packages:
 */
static void
backend_simulate_remove_packages (PkBackend *backend, gchar **packages, gboolean autoremove)
{
	pk_backend_set_bool(backend, "simulate", true);
	pk_backend_set_bool(backend, "remove", true);
	pk_backend_thread_create (backend, backend_manage_packages_thread);
}

static gboolean
backend_repo_manager_thread (PkBackend *backend)
{
	// list
	PkBitfield filters;
	bool notDevelopment;
	// enable
	const gchar *repo_id;
	bool enabled;
	bool found = false;
	// generic
	const char *const salt = "$1$/iSaq7rB$EoUw5jJPPvAPECNaaWzMK/";
	bool list = pk_backend_get_bool(backend, "list");

	if (list) {
		pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
		filters = (PkBitfield) pk_backend_get_uint(backend, "filters");
		notDevelopment = pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	} else {
		pk_backend_set_status (backend, PK_STATUS_ENUM_REQUEST);
		repo_id = pk_backend_get_string(backend, "repo_id");
		enabled = pk_backend_get_bool(backend, "enabled");
	}

	SourcesList _lst;
	if (_lst.ReadSources() == false) {
		_error->
		    Warning("Ignoring invalid record(s) in sources.list file!");
	    //return false;
	}

	if (_lst.ReadVendors() == false) {
		_error->Error("Cannot read vendors.list file");
		show_errors(backend, PK_ERROR_ENUM_FAILED_CONFIG_PARSING);
		pk_backend_finished (backend);
		return false;
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
		repo += " " + (*it)->VendorID;
		repo += " " + (*it)->URI;
		repo += " " + (*it)->Dist;
		repo += " " + Sections;
		gchar *hash;
		const gchar allowedChars[] =
		    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
		hash = crypt(repo.c_str(), salt);
		g_strcanon(hash, allowedChars, 'D');
		string repoId(hash);

		if (list) {
			pk_backend_repo_detail(backend,
					    repoId.c_str(),
					    repo.c_str(),
					    !((*it)->Type & SourcesList::Disabled));
		} else {
			if (repoId.compare(repo_id) == 0) {
				if (enabled) {
					(*it)->Type = (*it)->Type & ~SourcesList::Disabled;
				} else {
					(*it)->Type |= SourcesList::Disabled;
				}
				found = true;
				break;
			}
		}
	}

	if (!list) {
		if (!found) {
			_error->Error("Could not found the repositorie");
			show_errors(backend, PK_ERROR_ENUM_REPO_NOT_AVAILABLE);
		} else if (!_lst.UpdateSources()) {
			_error->Error("Could not update sources file");
			show_errors(backend, PK_ERROR_ENUM_CANNOT_WRITE_REPO_CONFIG);
		}
	}
	pk_backend_finished (backend);
	return true;
}

/**
 * backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	pk_backend_set_bool(backend, "list", true);
	pk_backend_thread_create(backend, backend_repo_manager_thread);
}

/**
 * backend_repo_enable:
 */
static void
backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
	pk_backend_set_bool(backend, "list", false);
	pk_backend_thread_create(backend, backend_repo_manager_thread);
}

static gboolean
backend_get_packages_thread (PkBackend *backend)
{
	PkBitfield filters;
	filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	pk_backend_set_allow_cancel (backend, true);

	aptcc *m_apt = new aptcc(backend, _cancel);
	pk_backend_set_pointer(backend, "aptcc_obj", m_apt);
	if (m_apt->init()) {
		egg_debug ("Failed to create apt cache");
		delete m_apt;
		pk_backend_finished (backend);
		return false;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > output;
	output.reserve(m_apt->packageCache->HeaderP->PackageCount);
	for(pkgCache::PkgIterator pkg = m_apt->packageCache->PkgBegin();
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

	// It's faster to emmit the packages rather here than in the matching part
	m_apt->emit_packages(output, filters);

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
	NULL,						/* get_roles */
	backend_get_mime_types,				/* get_mime_types */
	backend_cancel,					/* cancel */
	backend_download_packages,			/* download_packages */
	NULL,						/* get_categories */
	backend_get_depends,				/* get_depends */
	backend_get_details,				/* get_details */
	backend_get_distro_upgrades,			/* get_distro_upgrades */
	backend_get_files,				/* get_files */
	backend_get_packages,				/* get_packages */
	backend_get_repo_list,				/* get_repo_list */
	backend_get_requires,				/* get_requires */
	backend_get_update_detail,			/* get_update_detail */
	backend_get_updates,				/* get_updates */
	NULL,						/* install_files */
	backend_install_update_packages,		/* install_packages */
	NULL,						/* install_signature */
	backend_refresh_cache,				/* refresh_cache */
	backend_remove_packages,			/* remove_packages */
	backend_repo_enable,				/* repo_enable */
	NULL,						/* repo_set_data */
	backend_resolve,				/* resolve */
	NULL,						/* rollback */
	backend_search_details,				/* search_details */
	backend_search_files,				/* search_files */
	backend_search_groups,				/* search_groups */
	backend_search_names,				/* search_names */
	backend_install_update_packages,		/* update_packages */
	backend_update_system,				/* update_system */
	backend_what_provides,				/* what_provides */
	NULL,						/* simulate_install_files */
	backend_simulate_install_update_packages,	/* simulate_install_packages */
	backend_simulate_remove_packages,		/* simulate_remove_packages */
	backend_simulate_install_update_packages	/* simulate_update_packages */
);
