/*
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2009-2012 Daniel Nicoletti <dantti12@gmail.com>
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

#include <config.h>
#include <pk-backend.h>
#include <pk-backend-spawn.h>

#include "apt-intf.h"
#include "AptCacheFile.h"
#include "apt-messages.h"
#include "acqpkitstatus.h"
#include "pkg_acqfile.h"
#include "apt-sourceslist.h"

/* static bodges */
static bool _cancel = false;
static PkBackendSpawn *spawn;

/**
 * pk_backend_get_description:
 */
const gchar* pk_backend_get_description(PkBackend *backend)
{
    return "APTcc";
}

/**
 * pk_backend_get_author:
 */
const gchar* pk_backend_get_author(PkBackend *backend)
{
    return "Daniel Nicoletti <dantti12@gmail.com>";
}

/**
 * pk_backend_initialize:
 */
void pk_backend_initialize(PkBackend *backend)
{
    g_debug("APTcc Initializing");

    if (pkgInitConfig(*_config) == false ||
            pkgInitSystem(*_config, _system) == false) {
        g_debug("ERROR initializing backend");
    }

    // Disable apt-listbugs as it freezes PK
    setenv("APT_LISTBUGS_FRONTEND", "none", 1);

    // Set apt-listchanges frontend to "debconf" to make it's output visible
    // (without using the debconf frontend, PK will freeze)
    setenv("APT_LISTCHANGES_FRONTEND", "debconf", 1);

    spawn = pk_backend_spawn_new();
    pk_backend_spawn_set_backend(spawn, backend);
    pk_backend_spawn_set_name(spawn, "aptcc");
}

/**
 * pk_backend_destroy:
 */
void pk_backend_destroy(PkBackend *backend)
{
    g_debug("APTcc being destroyed");
}

/**
 * pk_backend_get_groups:
 */
PkBitfield pk_backend_get_groups(PkBackend *backend)
{
    return pk_bitfield_from_enums(
                PK_GROUP_ENUM_ACCESSORIES,
                PK_GROUP_ENUM_ADMIN_TOOLS,
                PK_GROUP_ENUM_COMMUNICATION,
                PK_GROUP_ENUM_DOCUMENTATION,
                PK_GROUP_ENUM_DESKTOP_GNOME,
                PK_GROUP_ENUM_DESKTOP_KDE,
                PK_GROUP_ENUM_DESKTOP_OTHER,
                PK_GROUP_ENUM_ELECTRONICS,
                PK_GROUP_ENUM_FONTS,
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
 * pk_backend_get_filters:
 */
PkBitfield pk_backend_get_filters(PkBackend *backend)
{
    PkBitfield filters;
    filters = pk_bitfield_from_enums(
                PK_FILTER_ENUM_GUI,
                PK_FILTER_ENUM_INSTALLED,
                PK_FILTER_ENUM_DEVELOPMENT,
                PK_FILTER_ENUM_SUPPORTED,
                PK_FILTER_ENUM_FREE,
                -1);

    // if we have multiArch support we add the native filter
    if (APT::Configuration::getArchitectures(false).size() > 1) {
        pk_bitfield_add(filters, PK_FILTER_ENUM_ARCH);
    }

    return filters;
}

/**
 * pk_backend_get_mime_types:
 */
gchar **
pk_backend_get_mime_types(PkBackend *backend)
{
    const gchar *mime_types[] = {
				"application/x-deb", NULL };
    return g_strdupv ((gchar **) mime_types);
}

/**
 * pk_backend_cancel:
 */
void pk_backend_cancel(PkBackend *backend)
{
    AptIntf *apt = (AptIntf*) pk_backend_get_pointer(backend, "aptcc_obj");
    if (apt) {
        apt->cancel();
    }
}

static void backend_get_depends_or_requires_thread(PkBackend *backend, gpointer user_data)
{
    gchar **package_ids;
    PkBitfield filters;
    gchar *pi;
    bool recursive;

    package_ids = pk_backend_get_strv(backend, "package_ids");
    filters = (PkBitfield) pk_backend_get_uint(backend, "filters");
    recursive = pk_backend_get_bool(backend, "recursive");

    pk_backend_set_allow_cancel(backend, true);

    AptIntf *apt = new AptIntf(backend, _cancel);
    pk_backend_set_pointer(backend, "aptcc_obj", apt);
    if (apt->init()) {
        g_debug("Failed to create apt cache");
        delete apt;
        return;
    }

    bool depends = pk_backend_get_bool(backend, "get_depends");

    pk_backend_set_status(backend, PK_STATUS_ENUM_QUERY);
    PkgList output;
    for (uint i = 0; i < g_strv_length(package_ids); ++i) {
        if (_cancel) {
            break;
        }
        pi = package_ids[i];
        if (pk_package_id_check(pi) == false) {
            pk_backend_error_code(backend,
                                  PK_ERROR_ENUM_PACKAGE_ID_INVALID,
                                  pi);
            delete apt;
            return;
        }

        const pkgCache::VerIterator &ver = apt->findPackageId(pi);
        if (ver.end()) {
            pk_backend_error_code(backend,
                                  PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
                                  "Couldn't find package");
            delete apt;
            return;
        }

        if (depends) {
            apt->getDepends(output, ver, recursive);
        } else {
            apt->getRequires(output, ver, recursive);
        }
    }

    // It's faster to emmit the packages here than in the matching part
    apt->emitPackages(output, filters);

    delete apt;
}

/**
 * pk_backend_get_depends:
 */
void pk_backend_get_depends(PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
    pk_backend_set_bool(backend, "get_depends", true);
    pk_backend_set_bool(backend, "recursive", recursive);
    pk_backend_thread_create(backend, backend_get_depends_or_requires_thread, NULL, NULL);
}

/**
 * pk_backend_get_requires:
 */
void pk_backend_get_requires(PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
    pk_backend_set_bool(backend, "get_depends", false);
    pk_backend_set_bool(backend, "recursive", recursive);
    pk_backend_thread_create(backend, backend_get_depends_or_requires_thread, NULL, NULL);
}

/**
 * pk_backend_get_distro_upgrades:
 */
void pk_backend_get_distro_upgrades(PkBackend *backend)
{
    pk_backend_spawn_helper(spawn, "get-distro-upgrade.py", "get-distro-upgrades", NULL);
}

static void backend_get_files_thread(PkBackend *backend, gpointer user_data)
{
    gchar **package_ids;
    gchar *pi;

    package_ids = pk_backend_get_strv(backend, "package_ids");
    if (package_ids == NULL) {
        pk_backend_error_code(backend,
                              PK_ERROR_ENUM_PACKAGE_ID_INVALID,
                              "Invalid package id");
        pk_backend_finished(backend);
        return;
    }

    AptIntf *apt = new AptIntf(backend, _cancel);
    pk_backend_set_pointer(backend, "aptcc_obj", apt);
    if (apt->init()) {
        g_debug("Failed to create apt cache");
        delete apt;
        return;
    }

    pk_backend_set_status(backend, PK_STATUS_ENUM_QUERY);
    for (uint i = 0; i < g_strv_length(package_ids); ++i) {
        pi = package_ids[i];
        if (pk_package_id_check(pi) == false) {
            pk_backend_error_code(backend,
                                  PK_ERROR_ENUM_PACKAGE_ID_INVALID,
                                  pi);
            delete apt;
            return;
        }

        const pkgCache::VerIterator &ver = apt->findPackageId(pi);
        if (ver.end()) {
            pk_backend_error_code(backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "Couldn't find package");
            delete apt;
            return;
        }

        apt->emitPackageFiles(pi);
    }

    delete apt;
}

/**
 * pk_backend_get_files:
 */
void pk_backend_get_files(PkBackend *backend, gchar **package_ids)
{
    pk_backend_thread_create(backend, backend_get_files_thread, NULL, NULL);
}

static void backend_get_details_thread(PkBackend *backend, gpointer user_data)
{
    gchar **package_ids;

    PkRoleEnum role = pk_backend_get_role(backend);
    bool updateDetail = role == PK_ROLE_ENUM_GET_UPDATE_DETAIL ? true : false;
    package_ids = pk_backend_get_strv(backend, "package_ids");
    if (package_ids == NULL) {
        pk_backend_error_code(backend,
                              PK_ERROR_ENUM_PACKAGE_ID_INVALID,
                              "Invalid package id");
        pk_backend_finished(backend);
        return;
    }

    AptIntf *apt = new AptIntf(backend, _cancel);
    pk_backend_set_pointer(backend, "aptcc_obj", apt);
    if (apt->init()) {
        g_debug ("Failed to create apt cache");
        delete apt;
        return;
    }

    if (updateDetail) {
        // this is needed to compare the changelog verstion to
        // current package using DoCmpVersion()
        pkgInitSystem(*_config, _system);
    }

    pk_backend_set_status(backend, PK_STATUS_ENUM_QUERY);
    PkgList pkgs = apt->resolvePackageIds(package_ids);

    if (updateDetail) {
        apt->emitUpdateDetails(pkgs);
    } else {
        apt->emitDetails(pkgs);
    }

    delete apt;
}

/**
 * pk_backend_get_update_detail:
 */
void pk_backend_get_update_detail(PkBackend *backend, gchar **package_ids)
{
    pk_backend_thread_create(backend, backend_get_details_thread, NULL, NULL);
}

/**
 * pk_backend_get_details:
 */
void pk_backend_get_details(PkBackend *backend, gchar **package_ids)
{
    pk_backend_thread_create(backend, backend_get_details_thread, NULL, NULL);
}

static void backend_get_or_update_system_thread(PkBackend *backend, gpointer user_data)
{
    PkBitfield filters;
    bool getUpdates;
    filters = (PkBitfield) pk_backend_get_uint(backend, "filters");
    getUpdates = pk_backend_get_bool(backend, "getUpdates");
    pk_backend_set_allow_cancel(backend, true);

    AptIntf *apt = new AptIntf(backend, _cancel);
    pk_backend_set_pointer(backend, "aptcc_obj", apt);
    if (apt->init()) {
        g_debug("Failed to create apt cache");
        delete apt;
        return;
    }

    pk_backend_set_status(backend, PK_STATUS_ENUM_QUERY);

    AptCacheFile cache(backend);
    int timeout = 10;
    // TODO test this
    while (cache.Open(!getUpdates) == false || cache.CheckDeps() == false) {
        if (getUpdates == true || (timeout <= 0)) {
            pk_backend_error_code(backend,
                                  PK_ERROR_ENUM_NO_CACHE,
                                  "Could not open package cache.");
            delete apt;
            return;
        } else {
            pk_backend_set_status(backend, PK_STATUS_ENUM_WAITING_FOR_LOCK);
            sleep(1);
            timeout--;
        }
    }
    pk_backend_set_status(backend, PK_STATUS_ENUM_RUNNING);

    if (cache.DistUpgrade() == false) {
        cache.ShowBroken(false);
        g_debug("Internal error, DistUpgrade broke stuff");
        delete apt;
        return;
    }

    bool res = true;
    if (getUpdates) {
        PkgList updates;
        PkgList kept;
        for (pkgCache::PkgIterator pkg = cache->PkgBegin();
             !pkg.end();
             ++pkg) {
            if (cache[pkg].Upgrade() == true &&
                    cache[pkg].NewInstall() == false) {
                const pkgCache::VerIterator &ver = cache.findCandidateVer(pkg);
                if (!ver.end()) {
                    updates.push_back(ver);
                }
            } else if (cache[pkg].Upgradable() == true &&
                       pkg->CurrentVer != 0 &&
                       cache[pkg].Delete() == false) {
                const pkgCache::VerIterator &ver = cache.findCandidateVer(pkg);
                if (!ver.end()) {
                    kept.push_back(ver);
                }
            }
        }

        apt->emitUpdates(updates, filters);
        apt->emitPackages(kept, filters, PK_INFO_ENUM_BLOCKED);
    } else {
        PkBitfield transaction_flags;
        bool downloadOnly;
        transaction_flags = pk_backend_get_uint(backend, "transaction_flags");
        downloadOnly = pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD);

        // TODO there should be a simulate upgrade system,
        // tho afaik Apper and GPK don't use this
        res = apt->installPackages(cache, false, downloadOnly);
    }

    delete apt;
}

/**
 * pk_backend_get_updates:
 */
void pk_backend_get_updates(PkBackend *backend, PkBitfield filters)
{
    pk_backend_set_bool(backend, "getUpdates", true);
    pk_backend_thread_create(backend, backend_get_or_update_system_thread, NULL, NULL);
}

/**
 * pk_backend_update_system:
 */
void pk_backend_update_system(PkBackend *backend, PkBitfield transaction_flags)
{
    pk_backend_set_bool(backend, "getUpdates", false);
    pk_backend_thread_create(backend, backend_get_or_update_system_thread, NULL, NULL);
}

static void backend_what_provides_thread(PkBackend *backend, gpointer user_data)
{
    PkProvidesEnum provides;
    PkBitfield filters;
    const gchar *provides_text;
    gchar **values;
    bool error = false;

    filters  = (PkBitfield)     pk_backend_get_uint(backend, "filters");
    provides = (PkProvidesEnum) pk_backend_get_uint(backend, "provides");
    values   = pk_backend_get_strv(backend, "search");

    pk_backend_set_status(backend, PK_STATUS_ENUM_QUERY);

    // We can handle libraries, mimetypes and codecs
    if (provides == PK_PROVIDES_ENUM_SHARED_LIB ||
            provides == PK_PROVIDES_ENUM_MIMETYPE ||
            provides == PK_PROVIDES_ENUM_CODEC ||
            provides == PK_PROVIDES_ENUM_ANY) {
        AptIntf *apt = new AptIntf(backend, _cancel);
        pk_backend_set_pointer(backend, "aptcc_obj", apt);
        if (apt->init()) {
            g_debug("Failed to create apt cache");
            g_strfreev(values);
            delete apt;
            return;
        }

        pk_backend_set_status(backend, PK_STATUS_ENUM_QUERY);

        PkgList output;
        if (provides == PK_PROVIDES_ENUM_SHARED_LIB) {
            apt->providesLibrary(output, values);
        } else if (provides == PK_PROVIDES_ENUM_MIMETYPE) {
            apt->providesMimeType(output, values);
        } else if (provides == PK_PROVIDES_ENUM_CODEC) {
            apt->providesCodec(output, values);
        } else {
            // PK_PROVIDES_ENUM_ANY, just search for everything a package can provide
            apt->providesLibrary(output, values);
            apt->providesCodec(output, values);
            apt->providesMimeType(output, values);
        }

        // It's faster to emit the packages here rather than in the matching part
        apt->emitPackages(output, filters);

        delete apt;
    } else {
        provides_text = pk_provides_enum_to_string(provides);
        pk_backend_error_code(backend,
                              PK_ERROR_ENUM_NOT_SUPPORTED,
                              "Provides %s not supported",
                              provides_text);
        pk_backend_finished(backend);
    }
}

/**
  * pk_backend_what_provides
  */
void pk_backend_what_provides(PkBackend *backend,
                              PkBitfield filters,
                              PkProvidesEnum provide,
                              gchar **values)
{
    pk_backend_thread_create(backend, backend_what_provides_thread, NULL, NULL);
}

/**
 * pk_backend_download_packages_thread:
 */
static void pk_backend_download_packages_thread(PkBackend *backend, gpointer user_data)
{
    gchar **package_ids;
    string directory;

    package_ids = pk_backend_get_strv(backend, "package_ids");
    directory = _config->FindDir("Dir::Cache::archives") + "partial/";
    pk_backend_set_allow_cancel(backend, true);

    AptIntf *apt = new AptIntf(backend, _cancel);
    pk_backend_set_pointer(backend, "aptcc_obj", apt);
    if (apt->init()) {
        g_debug("Failed to create apt cache");
        delete apt;
        return;
    }

    pk_backend_set_status(backend, PK_STATUS_ENUM_QUERY);
    // Create the progress
    AcqPackageKitStatus Stat(apt, backend, _cancel);

    // get a fetcher
    pkgAcquire fetcher;
    fetcher.Setup(&Stat);
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

    for (uint i = 0; i < g_strv_length(package_ids); ++i) {
        pi = package_ids[i];
        if (pk_package_id_check(pi) == false) {
            pk_backend_error_code(backend,
                                  PK_ERROR_ENUM_PACKAGE_ID_INVALID,
                                  pi);
            delete apt;
            return;
        }

        if (_cancel) {
            break;
        }

        const pkgCache::VerIterator &ver = apt->findPackageId(pi);
        // Ignore packages that could not be found or that exist only due to dependencies.
        if (ver.end()) {
            _error->Error("Can't find this package id \"%s\".", pi);
            continue;
        } else {
            if(!ver.Downloadable()) {
                _error->Error("No downloadable files for %s,"
                              "perhaps it is a local or obsolete" "package?",
                              pi);
                continue;
            }

            string storeFileName;
            if (apt->getArchive(&fetcher,
                                  ver,
                                  directory,
                                  storeFileName)) {
                Stat.addPackage(ver);
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
            && _cancel == false) {
        // We failed and we did not cancel
        show_errors(backend, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED);
        delete apt;
        return;
    }

    // send the filelist
    pk_backend_files(backend, NULL, filelist.c_str());

    delete apt;
}

/**
 * pk_backend_download_packages:
 */
void pk_backend_download_packages(PkBackend *backend, gchar **package_ids, const gchar *directory)
{
    pk_backend_thread_create(backend, pk_backend_download_packages_thread, NULL, NULL);
}

/**
 * pk_backend_refresh_cache_thread:
 */
static void pk_backend_refresh_cache_thread(PkBackend *backend, gpointer user_data)
{
    pk_backend_set_allow_cancel(backend, true);

    AptIntf *apt = new AptIntf(backend, _cancel);
    pk_backend_set_pointer(backend, "aptcc_obj", apt);
    if (apt->init()) {
        g_debug("Failed to create apt cache");
        delete apt;
        return;
    }

    pk_backend_set_status(backend, PK_STATUS_ENUM_REFRESH_CACHE);
    // Lock the list directory
    FileFd Lock;
    if (_config->FindB("Debug::NoLocking", false) == false) {
        Lock.Fd(GetLock(_config->FindDir("Dir::State::Lists") + "lock"));
        if (_error->PendingError() == true) {
            pk_backend_error_code(backend, PK_ERROR_ENUM_CANNOT_GET_LOCK, "Unable to lock the list directory");
            delete apt;
            return;
            // 	 return _error->Error(_("Unable to lock the list directory"));
        }
    }

    apt->refreshCache();

    // Rebuild the cache.
    AptCacheFile cache(backend);
    if (cache.BuildCaches(true) == false) {
        if (_error->PendingError() == true) {
            show_errors(backend, PK_ERROR_ENUM_CANNOT_FETCH_SOURCES, true);
        }
        delete apt;
        return;
    }

    // missing repo gpg signature would appear here
    if (_error->PendingError() == false && _error->empty() == false) {
        // TODO we need a repo warning
        show_warnings(backend, PK_MESSAGE_ENUM_BROKEN_MIRROR);
    }

    delete apt;
}

/**
 * pk_backend_refresh_cache:
 */
void pk_backend_refresh_cache(PkBackend *backend, gboolean force)
{
    pk_backend_thread_create(backend, pk_backend_refresh_cache_thread, NULL, NULL);
}

static void pk_backend_resolve_thread(PkBackend *backend, gpointer user_data)
{
    gchar **package_ids;
    PkBitfield filters;

    filters = (PkBitfield) pk_backend_get_uint(backend, "filters");
    package_ids = pk_backend_get_strv(backend, "package_ids");
    pk_backend_set_allow_cancel(backend, true);

    AptIntf *apt = new AptIntf(backend, _cancel);
    pk_backend_set_pointer(backend, "aptcc_obj", apt);
    if (apt->init()) {
        g_debug("Failed to create apt cache");
        delete apt;
        return;
    }

    PkgList pkgs = apt->resolvePackageIds(package_ids);

    // It's faster to emmit the packages here rather than in the matching part
    apt->emitPackages(pkgs, filters);

    delete apt;
}

/**
 * pk_backend_resolve:
 */
void pk_backend_resolve(PkBackend *backend, PkBitfield filters, gchar **packages)
{
    pk_backend_thread_create(backend, pk_backend_resolve_thread, NULL, NULL);
}

static void pk_backend_search_files_thread(PkBackend *backend, gpointer user_data)
{
    gchar **values;
    PkBitfield filters;

    values = pk_backend_get_strv(backend, "search");
    filters = (PkBitfield) pk_backend_get_uint(backend, "filters");

    pk_backend_set_allow_cancel(backend, true);

    // as we can only search for installed files lets avoid the opposite
    if (!pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
        AptIntf *apt = new AptIntf(backend, _cancel);
        pk_backend_set_pointer(backend, "aptcc_obj", apt);
        if (apt->init()) {
            g_debug("Failed to create apt cache");
            delete apt;
            return;
        }

        pk_backend_set_status(backend, PK_STATUS_ENUM_QUERY);
        PkgList output;
        output = apt->searchPackageFiles(values);

        // It's faster to emit the packages here rather than in the matching part
        apt->emitPackages(output, filters);

        delete apt;
    } else {
        pk_backend_finished(backend);
    }
}

/**
 * pk_backend_search_files:
 */
void pk_backend_search_files(PkBackend *backend, PkBitfield filters, gchar **values)
{
    pk_backend_thread_create(backend, pk_backend_search_files_thread, NULL, NULL);
}

static void backend_search_groups_thread(PkBackend *backend, gpointer user_data)
{
    gchar **values;
    PkBitfield filters;

    values = pk_backend_get_strv(backend, "search");
    filters = (PkBitfield) pk_backend_get_uint(backend, "filters");

    AptIntf *apt = new AptIntf(backend, _cancel);
    pk_backend_set_pointer(backend, "aptcc_obj", apt);
    if (apt->init()) {
        g_debug("Failed to create apt cache");
        delete apt;
        return;
    }

    pk_backend_set_status(backend, PK_STATUS_ENUM_QUERY);

    // It's faster to emmit the packages here rather than in the matching part
    PkgList output;
    output = apt->getPackagesFromGroup(values);
    apt->emitPackages(output, filters);

    pk_backend_set_percentage(backend, 100);
    delete apt;
}

/**
 * pk_backend_search_groups:
 */
void pk_backend_search_groups(PkBackend *backend, PkBitfield filters, gchar **values)
{
    pk_backend_thread_create(backend, backend_search_groups_thread, NULL, NULL);
}

static void backend_search_package_thread(PkBackend *backend, gpointer user_data)
{
    gchar **values;
    gchar *search;
    PkBitfield filters;

    values = pk_backend_get_strv(backend, "search");
    search = g_strjoinv("|", values);
    filters = (PkBitfield) pk_backend_get_uint(backend, "filters");

    AptIntf *apt = new AptIntf(backend, _cancel);
    pk_backend_set_pointer(backend, "aptcc_obj", apt);
    if (apt->init()) {
        g_debug("Failed to create apt cache");
        g_free(search);
        delete apt;
        return;
    }

    if (_error->PendingError() == true) {
        g_free(search);
        delete apt;
        return;
    }

    pk_backend_set_status(backend, PK_STATUS_ENUM_QUERY);
    pk_backend_set_percentage(backend, PK_BACKEND_PERCENTAGE_INVALID);
    pk_backend_set_allow_cancel(backend, true);

    PkgList output;
    if (pk_backend_get_bool(backend, "search_details")) {
        output = apt->searchPackageDetails(search);
    } else {
        output = apt->searchPackageName(search);
    }
    g_free(search);

    // It's faster to emmit the packages here than in the matching part
    apt->emitPackages(output, filters);

    pk_backend_set_percentage(backend, 100);
    delete apt;
}

/**
 * pk_backend_search_names:
 */
void pk_backend_search_names(PkBackend *backend, PkBitfield filters, gchar **values)
{
    pk_backend_set_bool(backend, "search_details", false);
    pk_backend_thread_create(backend, backend_search_package_thread, NULL, NULL);
}

/**
 * pk_backend_search_details:
 */
void pk_backend_search_details(PkBackend *backend, PkBitfield filters, gchar **values)
{
    pk_backend_set_bool(backend, "search_details", true);
    pk_backend_thread_create(backend, backend_search_package_thread, NULL, NULL);
}

static void backend_manage_packages_thread(PkBackend *backend, gpointer user_data)
{
    // Get the transaction flags
    PkBitfield transaction_flags = pk_backend_get_uint(backend, "transaction_flags");

    // Check if we should only simulate the install (calculate dependencies)
    bool simulate;
    simulate = pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE);

    // Check if we should only download all the required packages for this transaction
    bool downloadOnly;
    downloadOnly = pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD);

    // Get the transaction role since this method is called by install/remove/update
    PkRoleEnum role = pk_backend_get_role(backend);

    // Check if we are removing packages
    bool remove = (role == PK_ROLE_ENUM_REMOVE_PACKAGES);

    // Check if we are installing local files
    bool fileInstall = false;
    gchar **full_paths = NULL;
    if (role == PK_ROLE_ENUM_INSTALL_FILES) {
        full_paths = pk_backend_get_strv(backend, "full_paths");
        fileInstall = true;
    }

    // Check if we should fix broken packages
    bool fixBroken = false;
    if (role == PK_ROLE_ENUM_REPAIR_SYSTEM) {
        // On fix broken mode no package to remove/install is allowed
        fixBroken = true;
    }
    g_debug("FILE INSTALL: %i", fileInstall);
    pk_backend_set_allow_cancel(backend, true);

    AptIntf *apt = new AptIntf(backend, _cancel);
    pk_backend_set_pointer(backend, "aptcc_obj", apt);
    if (apt->init()) {
        g_debug("Failed to create apt cache");
        delete apt;
        return;
    }

    pk_backend_set_status(backend, PK_STATUS_ENUM_QUERY);
    PkgList installPkgs, removePkgs;

    if (fileInstall) {
        // File installation EXPERIMENTAL

        // GDebi can not install more than one package at time
        if (g_strv_length(full_paths) > 1) {
            pk_backend_error_code(backend,
                                  PK_ERROR_ENUM_NOT_SUPPORTED,
                                  "The backend can only proccess one file at time.");
            delete apt;
            return;
        }

        // get the list of packages to install
        if (!apt->markFileForInstall(full_paths[0], installPkgs, removePkgs)) {
            delete apt;
            return;
        }

        cout << "installPkgs.size: " << installPkgs.size() << endl;
        cout << "removePkgs.size: " << removePkgs.size() << endl;

    } else if (!fixBroken) {
        // Resolve the given packages
        gchar **package_ids = pk_backend_get_strv(backend, "package_ids");
        if (remove) {
            removePkgs = apt->resolvePackageIds(package_ids);
        } else {
            installPkgs = apt->resolvePackageIds(package_ids);
        }

        if (removePkgs.size() == 0 && installPkgs.size() == 0) {
            pk_backend_error_code(backend,
                                  PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
                                  "Could not find package(s)");
            delete apt;
            return;
        }
    }

    // Install/Update/Remove packages, or just simulate
    bool ret;
    ret = apt->runTransaction(installPkgs,
                              removePkgs,
                              simulate,
                              fileInstall, // Mark newly installed packages as auto-installed
                                           // (they're dependencies of the new local package)
                              fixBroken,
                              downloadOnly);
    if (!ret) {
        // Print transaction errors
        g_debug("AptIntf::runTransaction() failed: ", _error->PendingError());
        delete apt;
        return;
    }

    if (fileInstall) {
        // Now perform the installation!
        gchar *path;
        for (uint i = 0; i < g_strv_length(full_paths); ++i) {
            if (_cancel) {
                break;
            }

            path = full_paths[i];
            if (!apt->installFile(path, simulate)) {
                cout << "Installation of DEB file " << path << " failed." << endl;
                delete apt;
                return;
            }
        }
    }

    delete apt;
}

/**
 * pk_backend_install_packages:
 */
void pk_backend_install_packages(PkBackend *backend, PkBitfield transaction_flags, gchar **package_ids)
{
    pk_backend_thread_create(backend, backend_manage_packages_thread, NULL, NULL);
}

/**
 * pk_backend_update_packages:
 */
void pk_backend_update_packages(PkBackend *backend, PkBitfield transaction_flags, gchar **package_ids)
{
    pk_backend_thread_create(backend, backend_manage_packages_thread, NULL, NULL);
}

/**
 * pk_backend_install_files:
 */
void pk_backend_install_files(PkBackend *backend, PkBitfield transaction_flags, gchar **full_paths)
{
    pk_backend_thread_create(backend, backend_manage_packages_thread, NULL, NULL);
}

/**
 * pk_backend_remove_packages:
 */
void pk_backend_remove_packages(PkBackend *backend,
                                PkBitfield transaction_flags,
                                gchar **package_ids,
                                gboolean allow_deps,
                                gboolean autoremove)
{
    pk_backend_thread_create(backend, backend_manage_packages_thread, NULL, NULL);
}

/**
 * pk_backend_repair_system:
 */
void pk_backend_repair_system(PkBackend *backend, PkBitfield transaction_flags)
{
    pk_backend_thread_create(backend, backend_manage_packages_thread, NULL, NULL);
}

static void backend_repo_manager_thread(PkBackend *backend, gpointer user_data)
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
        pk_backend_set_status(backend, PK_STATUS_ENUM_QUERY);
        filters = (PkBitfield) pk_backend_get_uint(backend, "filters");
        notDevelopment = pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_DEVELOPMENT);
    } else {
        pk_backend_set_status(backend, PK_STATUS_ENUM_REQUEST);
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
        pk_backend_finished(backend);
        return;
    }

    for (SourcesListIter it = _lst.SourceRecords.begin();
         it != _lst.SourceRecords.end(); ++it) {
        if ((*it)->Type & SourcesList::Comment) {
            continue;
        }

        string Sections;
        for (unsigned int j = 0; j < (*it)->NumSections; ++j) {
            Sections += (*it)->Sections[j];
            Sections += " ";
        }

        if (notDevelopment &&
                ((*it)->Type & SourcesList::DebSrc ||
                 (*it)->Type & SourcesList::RpmSrc ||
                 (*it)->Type & SourcesList::RpmSrcDir ||
                 (*it)->Type & SourcesList::RepomdSrc)) {
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
    pk_backend_finished(backend);
}

/**
 * pk_backend_get_repo_list:
 */
void pk_backend_get_repo_list(PkBackend *backend, PkBitfield filters)
{
    pk_backend_set_bool(backend, "list", true);
    pk_backend_thread_create(backend, backend_repo_manager_thread, NULL, NULL);
}

/**
 * pk_backend_repo_enable:
 */
void pk_backend_repo_enable(PkBackend *backend, const gchar *rid, gboolean enabled)
{
    pk_backend_set_bool(backend, "list", false);
    pk_backend_thread_create(backend, backend_repo_manager_thread, NULL, NULL);
}

static void backend_get_packages_thread(PkBackend *backend, gpointer user_data)
{
    PkBitfield filters;
    filters = (PkBitfield) pk_backend_get_uint(backend, "filters");
    pk_backend_set_allow_cancel(backend, true);

    AptIntf *apt = new AptIntf(backend, _cancel);
    pk_backend_set_pointer(backend, "aptcc_obj", apt);
    if (apt->init()) {
        g_debug("Failed to create apt cache");
        delete apt;
        return;
    }

    pk_backend_set_status(backend, PK_STATUS_ENUM_QUERY);
    PkgList output;
    output = apt->getPackages();

    // It's faster to emmit the packages rather here than in the matching part
    apt->emitPackages(output, filters);

    delete apt;
}

/**
 * pk_backend_get_packages:
 */
void pk_backend_get_packages(PkBackend *backend, PkBitfield filter)
{
    pk_backend_thread_create(backend, backend_get_packages_thread, NULL, NULL);
}


/**
 * pk_backend_get_categories:
 */
/* TODO
void
pk_backend_get_categories (PkBackend *backend)
{
    pk_backend_thread_create (backend, pk_backend_get_categories_thread, NULL, NULL);
}
*/

/**
 * pk_backend_get_roles:
 */
PkBitfield pk_backend_get_roles(PkBackend *backend)
{
    PkBitfield roles;
    roles = pk_bitfield_from_enums(
                PK_ROLE_ENUM_CANCEL,
                PK_ROLE_ENUM_GET_DEPENDS,
                PK_ROLE_ENUM_GET_DETAILS,
                PK_ROLE_ENUM_GET_FILES,
                PK_ROLE_ENUM_GET_REQUIRES,
                PK_ROLE_ENUM_GET_PACKAGES,
                PK_ROLE_ENUM_WHAT_PROVIDES,
                PK_ROLE_ENUM_GET_UPDATES,
                PK_ROLE_ENUM_GET_UPDATE_DETAIL,
                PK_ROLE_ENUM_INSTALL_PACKAGES,
                PK_ROLE_ENUM_INSTALL_SIGNATURE,
                PK_ROLE_ENUM_REFRESH_CACHE,
                PK_ROLE_ENUM_REMOVE_PACKAGES,
                PK_ROLE_ENUM_DOWNLOAD_PACKAGES,
                PK_ROLE_ENUM_RESOLVE,
                PK_ROLE_ENUM_SEARCH_DETAILS,
                PK_ROLE_ENUM_SEARCH_FILE,
                PK_ROLE_ENUM_SEARCH_GROUP,
                PK_ROLE_ENUM_SEARCH_NAME,
                PK_ROLE_ENUM_UPDATE_PACKAGES,
                PK_ROLE_ENUM_UPDATE_SYSTEM,
                PK_ROLE_ENUM_GET_REPO_LIST,
                PK_ROLE_ENUM_REPO_ENABLE,
                PK_ROLE_ENUM_REPAIR_SYSTEM,
                -1);

    // only add GetDistroUpgrades if the binary is present
    if (g_file_test(PREUPGRADE_BINARY, G_FILE_TEST_EXISTS)) {
        pk_bitfield_add(roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES);
    }

    // only add GetDistroUpgrades if the binary is present
    if (g_file_test(GDEBI_BINARY, G_FILE_TEST_EXISTS)) {
        pk_bitfield_add(roles, PK_ROLE_ENUM_INSTALL_FILES);
    }

    return roles;
}
