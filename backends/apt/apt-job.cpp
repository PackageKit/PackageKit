/* apt-job.cpp
 *
 * Copyright (c) 1999-2008 Daniel Burrows
 * Copyright (c) 2004 Michael Vogt <mvo@debian.org>
 *               2009-2018 Daniel Nicoletti <dantti12@gmail.com>
 *               2012-2022 Matthias Klumpp <matthias@tenstral.net>
 *               2016 Harald Sitter <sitter@kde.org>
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
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "apt-job.h"

#include <apt-pkg/aptconfiguration.h>
#include <apt-pkg/init.h>
#include <apt-pkg/error.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/install-progress.h>
#include <apt-pkg/sourcelist.h>
#include <apt-pkg/update.h>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/version.h>

#include <appstream.h>

#include <sys/prctl.h>
#include <sys/statvfs.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <pty.h>

#include <iostream>
#include <sstream>
#include <memory>
#include <fstream>
#include <dirent.h>

#include "apt-cache-file.h"
#include "apt-utils.h"
#include "gst-matcher.h"
#include "apt-messages.h"
#include "acqpkitstatus.h"
#include "deb-file.h"

using namespace APT;

#define RAMFS_MAGIC     0x858458f6

AptJob::AptJob(PkBackendJob *job) :
    m_cache(nullptr),
    m_job(job),
    m_cancel(false),
    m_lastSubProgress(0),
    m_terminalTimeout(120)
{
    const gchar *http_proxy;
    const gchar *ftp_proxy;

    // set locale
    setEnvLocaleFromJob();

    // set http proxy
    http_proxy = pk_backend_job_get_proxy_http(m_job);
    if (http_proxy != NULL) {
        g_autofree gchar *uri = pk_backend_convert_uri(http_proxy);
        g_setenv("http_proxy", uri, TRUE);
    }

    // set ftp proxy
    ftp_proxy = pk_backend_job_get_proxy_ftp(m_job);
    if (ftp_proxy != NULL) {
        g_autofree gchar *uri = pk_backend_convert_uri(ftp_proxy);
        g_setenv("ftp_proxy", uri, TRUE);
    }

    // default settings
    _config->CndSet("APT::Get::AutomaticRemove::Kernels", _config->FindB("APT::Get::AutomaticRemove", true));
}

AptJob::~AptJob()
{
    delete m_cache;
}

bool AptJob::init(gchar **localDebs)
{
    m_isMultiArch = APT::Configuration::getArchitectures(false).size() > 1;

    // Check if we should open the Cache with lock
    bool withLock = false;
    bool AllowBroken = false;
    PkRoleEnum role = pk_backend_job_get_role(m_job);
    switch (role) {
    case PK_ROLE_ENUM_INSTALL_PACKAGES:
    case PK_ROLE_ENUM_INSTALL_FILES:
    case PK_ROLE_ENUM_REMOVE_PACKAGES:
    case PK_ROLE_ENUM_UPDATE_PACKAGES:
        withLock = true;
        break;
    case PK_ROLE_ENUM_REPAIR_SYSTEM:
        AllowBroken = true;
        break;
    default:
        withLock = false;
    }

    bool simulate = false;
    if (withLock) {
        // Get the simulate value to see if the lock is valid
        PkBitfield transactionFlags = pk_backend_job_get_transaction_flags(m_job);
        simulate = pk_bitfield_contain(transactionFlags, PK_TRANSACTION_FLAG_ENUM_SIMULATE);

        // Disable the lock if we are simulating
        withLock = !simulate;
    }

    // Create the AptCacheFile class to search for packages
    m_cache = new AptCacheFile(m_job);
    if (localDebs) {
        PkBitfield flags = pk_backend_job_get_transaction_flags(m_job);
        if (pk_bitfield_contain(flags, PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED)) {
            // We are NOT simulating and have untrusted packages
            // fail the transaction.
            pk_backend_job_error_code(m_job,
                                  PK_ERROR_ENUM_CANNOT_INSTALL_REPO_UNSIGNED,
                                  "Local packages cannot be authenticated");
            return false;
        }

        for (guint i = 0; i < g_strv_length(localDebs); ++i)
            markFileForInstall(localDebs[i]);
    }

    int timeout = 10;
    // TODO test this
    while (m_cache->Open(withLock) == false) {
        if (withLock == false || (timeout <= 0)) {
            show_errors(m_job, PK_ERROR_ENUM_CANNOT_GET_LOCK);
            return false;
        } else {
            _error->Discard();
            pk_backend_job_set_status(m_job, PK_STATUS_ENUM_WAITING_FOR_LOCK);
            sleep(1);
            timeout--;
        }

        // Close the cache if we are going to try again
        m_cache->Close();
    }

    m_interactive = pk_backend_job_get_interactive(m_job);
    if (!m_interactive) {
        // Do not ask about config updates if we are not interactive
        if (!dpkgHasForceConfFileSet()) {
            _config->Set("Dpkg::Options::", "--force-confdef");
            _config->Set("Dpkg::Options::", "--force-confold");
        } else {
            // If any option is set we should not change anything
            g_debug("Using system settings for --force-conf*");
        }
        // Ensure nothing interferes with questions
        g_setenv("APT_LISTCHANGES_FRONTEND", "none", TRUE);
        g_setenv("APT_LISTBUGS_FRONTEND", "none", TRUE);
    }

    // Check if there are half-installed packages and if we can fix them
    return m_cache->CheckDeps(AllowBroken);
}

void AptJob::setEnvLocaleFromJob()
{
    const gchar *locale = pk_backend_job_get_locale(m_job);
    if (locale == NULL)
        return;

    // set daemon locale
    setlocale(LC_ALL, locale);

    // processes spawned by APT need to inherit the right locale as well
    g_setenv("LANG", locale, TRUE);
    g_setenv("LANGUAGE", locale, TRUE);
}

bool AptJob::dpkgHasForceConfFileSet() {
    std::vector<std::string> dpkg_options = _config->FindVector("Dpkg::Options");

    bool is_set = false;
    const std::string forced_options[]{"--force-confdef", "--force-confold", "--force-confnew"};

    for (auto setting : forced_options) {
        if (std::find(dpkg_options.begin(), dpkg_options.end(), setting) != dpkg_options.end()) {
            is_set = true;
            break;
        }
    }

    return is_set;
}

void AptJob::cancel()
{
    if (!m_cancel) {
        m_cancel = true;
        pk_backend_job_set_status(m_job, PK_STATUS_ENUM_CANCEL);
    }

    if (m_child_pid > 0) {
        kill(m_child_pid, SIGTERM);
    }
}

bool AptJob::cancelled() const
{
    return m_cancel;
}

PkBackendJob *AptJob::pkJob() const
{
    return m_job;
}

bool AptJob::matchPackage(const pkgCache::VerIterator &ver, PkBitfield filters)
{
    if (filters != 0) {
        const pkgCache::PkgIterator &pkg = ver.ParentPkg();
        bool installed = false;

        // Check if the package is installed
        if (pkg->CurrentState == pkgCache::State::Installed && pkg.CurrentVer() == ver)
            installed = true;

        // if we are on multiarch check also the arch filter
        if (m_isMultiArch && pk_bitfield_contain(filters, PK_FILTER_ENUM_ARCH)/* && !installed*/) {
            // don't emit the package if it does not match
            // the native architecture
            if (strcmp(ver.Arch(), "all") != 0 &&
                    strcmp(ver.Arch(), _config->Find("APT::Architecture").c_str()) != 0) {
                return false;
            }
        }

        std::string str = ver.Section() == NULL ? "" : ver.Section();
        std::string section, component;

        size_t found;
        found = str.find_last_of("/");
        section = str.substr(found + 1);
        if(found == str.npos) {
            component = "main";
        } else {
            component = str.substr(0, found);
        }

        if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED) && installed)
            return false;
        else if (pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED) && !installed)
            return false;

        if (pk_bitfield_contain(filters, PK_FILTER_ENUM_DEVELOPMENT)) {
            // if ver.end() means unknow
            // strcmp will be true when it's different than devel
            std::string pkgName = pkg.Name();
            if (!ends_with(pkgName, "-dev") &&
                    !ends_with(pkgName, "-dbg") &&
                    section.compare("devel") &&
                    section.compare("libdevel")) {
                return false;
            }
        } else if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_DEVELOPMENT)) {
            std::string pkgName = pkg.Name();
            if (ends_with(pkgName, "-dev") ||
                    ends_with(pkgName, "-dbg") ||
                    !section.compare("devel") ||
                    !section.compare("libdevel")) {
                return false;
            }
        }

        if (pk_bitfield_contain(filters, PK_FILTER_ENUM_GUI)) {
            // if ver.end() means unknow
            // strcmp will be true when it's different than x11
            if (section.compare("x11") && section.compare("gnome") &&
                    section.compare("kde") && section.compare("graphics")) {
                return false;
            }
        } else if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_GUI)) {
            if (!section.compare("x11") || !section.compare("gnome") ||
                    !section.compare("kde") || !section.compare("graphics")) {
                return false;
            }
        }

        if (pk_bitfield_contain(filters, PK_FILTER_ENUM_FREE)) {
            if (component.compare("main") != 0 &&
                    component.compare("universe") != 0) {
                // Must be in main and universe to be free
                return false;
            }
        } else if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_FREE)) {
            if (component.compare("main") == 0 ||
                    component.compare("universe") == 0) {
                // Must not be in main or universe to be free
                return false;
            }
        }

        // Check for supported packages
        if (pk_bitfield_contain(filters, PK_FILTER_ENUM_SUPPORTED)) {
            if (!packageIsSupported(ver, component)) {
                return false;
            }
        } else if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_SUPPORTED)) {
            if (packageIsSupported(ver, component)) {
                return false;
            }
        }

        // Check for applications, if they have files with .desktop
        if (pk_bitfield_contain(filters, PK_FILTER_ENUM_APPLICATION)) {
            // We do not support checking if it is an Application
            // if NOT installed
            if (!installed || !isApplication(ver)) {
                return false;
            }
        } else if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_APPLICATION)) {
            // We do not support checking if it is an Application
            // if NOT installed
            if (!installed || isApplication(ver)) {
                return false;
            }
        }

        // TODO test this one..
#if 0
        // I couldn'tfind any packages with the metapackages component, and I
        // think the check is the wrong way around; PK_FILTER_ENUM_COLLECTIONS
        // is for virtual group packages -- hughsie
        if (pk_bitfield_contain(filters, PK_FILTER_ENUM_COLLECTIONS)) {
            if (!component.compare("metapackages")) {
                return false;
            }
        } else if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_COLLECTIONS)) {
            if (component.compare("metapackages")) {
                return false;
            }
        }
#endif
    }
    return true;
}

PkgList AptJob::filterPackages(const PkgList &packages, PkBitfield filters)
{
    if (filters == 0)
        return packages;

    PkgList ret;
    ret.reserve(packages.size());

    for (const PkgInfo &info : packages) {
        if (matchPackage(info.ver, filters)) {
            ret.push_back(info);
        }
    }

    // This filter is more complex so we filter it after the list has shrunk
    if (pk_bitfield_contain(filters, PK_FILTER_ENUM_DOWNLOADED) && ret.size() > 0) {
        PkgList downloaded;

        pkgProblemResolver Fix(*m_cache);
        {
            pkgDepCache::ActionGroup group(*m_cache);
            for (auto autoInst : { true, false }) {
                for (const PkgInfo &pki : ret) {
                    if (m_cancel)
                        break;

                    m_cache->tryToInstall(Fix, pki, autoInst, false);
                }
            }
        }

        // get a fetcher
        pkgAcquire fetcher;

        // Read the source list
        if (m_cache->BuildSourceList() == false) {
            return downloaded;
        }

        // Create the package manager and prepare to download
        std::unique_ptr<pkgPackageManager> PM (_system->CreatePM(*m_cache));
        if (!PM->GetArchives(&fetcher, m_cache->GetSourceList(), m_cache->GetPkgRecords()) ||
                _error->PendingError() == true) {
            return downloaded;
        }

        for (const PkgInfo &info : ret) {
            bool found = false;
            for (pkgAcquire::ItemIterator it = fetcher.ItemsBegin(); it < fetcher.ItemsEnd(); ++it) {
                pkgAcqArchiveSane *archive = static_cast<pkgAcqArchiveSane*>(dynamic_cast<pkgAcqArchive*>(*it));
                if (archive == nullptr) {
                    continue;
                }
                const pkgCache::VerIterator ver = archive->version();
                if ((*it)->Local && info.ver == ver) {
                    found = true;
                    break;
                }
            }

            if (found)
                downloaded.append(info);
        }

        return downloaded;
    }

    return ret;
}

PkInfoEnum AptJob::packageStateFromVer(const pkgCache::VerIterator &ver) const
{
    const pkgCache::PkgIterator &pkg = ver.ParentPkg();
    if (pkg->CurrentState == pkgCache::State::Installed &&
            pkg.CurrentVer() == ver) {
        return PK_INFO_ENUM_INSTALLED;
    } else {
        return PK_INFO_ENUM_AVAILABLE;
    }
}

void AptJob::emitPackage(const pkgCache::VerIterator &ver, PkInfoEnum state)
{
    // get state from the cache if it was not set explicitly
    if (state == PK_INFO_ENUM_UNKNOWN)
        state = packageStateFromVer(ver);

    g_autofree gchar *package_id = m_cache->buildPackageId(ver);
    pk_backend_job_package(m_job,
                           state,
                           package_id,
                           m_cache->getShortDescription(ver).c_str());
}

void AptJob::emitPackageProgress(const pkgCache::VerIterator &ver, PkStatusEnum status, uint percentage)
{
    g_autofree gchar *package_id = m_cache->buildPackageId(ver);
    pk_backend_job_set_item_progress(m_job, package_id, status, percentage);
}

void AptJob::stagePackageForEmit(GPtrArray *array, const pkgCache::VerIterator &ver, PkInfoEnum state, PkInfoEnum updateSeverity) const
{
    g_autoptr(PkPackage) pk_package = pk_package_new ();
    g_autofree gchar *package_id = m_cache->buildPackageId(ver);
    g_autoptr(GError) local_error = NULL;

    if (!pk_package_set_id (pk_package, package_id, &local_error)) {
        g_warning ("package_id %s invalid and cannot be processed: %s",
               package_id, local_error->message);
        return;
    }

    // get state from the cache if it was not set explicitly
    if (state == PK_INFO_ENUM_UNKNOWN)
        state = packageStateFromVer(ver);
    pk_package_set_info (pk_package, state);

    if (updateSeverity != PK_INFO_ENUM_UNKNOWN)
        pk_package_set_update_severity (pk_package, updateSeverity);

    pk_package_set_summary (pk_package, m_cache->getShortDescription(ver).c_str());
    g_ptr_array_add (array, g_steal_pointer (&pk_package));
}

void AptJob::emitPackages(PkgList &output, PkBitfield filters, PkInfoEnum state, bool multiversion)
{
    // Sort so we can remove the duplicated entries
    output.sort();

    // Remove the duplicated entries
    output.removeDuplicates();

    // apply filter
    output = filterPackages(output, filters);

    // create array of PK package data to emit
    g_autoptr(GPtrArray) pkgArray = g_ptr_array_new_full (output.size(), (GDestroyNotify) g_object_unref);

    for (const PkgInfo &info : output) {
        if (m_cancel)
            break;

        auto ver = info.ver;
        // emit only the latest/chosen version if newest is requested
        if (!multiversion || pk_bitfield_contain(filters, PK_FILTER_ENUM_NEWEST)) {
            stagePackageForEmit(pkgArray, info.ver, state);
            continue;
        } else if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_NEWEST) && !ver.end()) {
            ver++;
        }

        for (; !ver.end(); ver++) {
            stagePackageForEmit(pkgArray, info.ver, state);
        }
    }

    // emit
    if (pkgArray->len > 0)
        pk_backend_job_packages(m_job, pkgArray);
}

void AptJob::emitRequireRestart(PkgList &output)
{
    // Sort so we can remove the duplicated entries
    output.sort();

    // Remove the duplicated entries
    output.removeDuplicates();

    for (const PkgInfo &info : output) {
        g_autofree gchar *package_id = m_cache->buildPackageId(info.ver);
        pk_backend_job_require_restart(m_job, PK_RESTART_ENUM_SYSTEM, package_id);
    }
}

void AptJob::emitUpdates(PkgList &output, PkBitfield filters)
{
    PkInfoEnum state;
    // Sort so we can remove the duplicated entries
    output.sort();

    // Remove the duplicated entries
    output.removeDuplicates();

    // filter
    output = filterPackages(output, filters);

    // create array of PK package data to emit
    g_autoptr(GPtrArray) pkgArray = g_ptr_array_new_full (output.size(), (GDestroyNotify) g_object_unref);

    for (const PkgInfo &pkgInfo : output) {
        if (m_cancel)
            break;

        // the default update info
        state = PK_INFO_ENUM_NORMAL;

        // let find what kind of upgrade this is
        pkgCache::VerFileIterator vf = pkgInfo.ver.FileList();
        std::string origin  = vf.File().Origin() == NULL ? "" : vf.File().Origin();
        std::string archive = vf.File().Archive() == NULL ? "" : vf.File().Archive();
        std::string label   = vf.File().Label() == NULL ? "" : vf.File().Label();
        if (origin.compare("Debian") == 0 ||
                origin.compare("Ubuntu") == 0) {
            if (ends_with(archive, "-security") ||
                    label.compare("Debian-Security") == 0) {
                state = PK_INFO_ENUM_SECURITY;
            } else if (ends_with(archive, "-backports")) {
                state = PK_INFO_ENUM_ENHANCEMENT;
            } else if (ends_with(archive, "-proposed-updates") || ends_with(archive, "-updates-proposed")) {
                state = PK_INFO_ENUM_LOW;
            } else if (ends_with(archive, "-updates")) {
                state = PK_INFO_ENUM_BUGFIX;
            }
        } else if (origin.compare("Backports.org archive") == 0 ||
                   ends_with(origin, "-backports")) {
            state = PK_INFO_ENUM_ENHANCEMENT;
        }

        // NOTE: Frontends expect us to pass the update urgency as both its state *and* actual urgency value here.
        stagePackageForEmit(pkgArray, pkgInfo.ver, state, state);
    }

    // emit
    if (pkgArray->len > 0)
        pk_backend_job_packages(m_job, pkgArray);
}

// search packages which provide a codec (specified in "values")
void AptJob::providesCodec(PkgList &output, gchar **values)
{
    string arch;
    GstMatcher matcher(values);
    if (!matcher.hasMatches()) {
        return;
    }

    for (pkgCache::PkgIterator pkg = m_cache->GetPkgCache()->PkgBegin(); !pkg.end(); ++pkg) {
        if (m_cancel) {
            break;
        }

        // Ignore packages that exist only due to dependencies.
        if (pkg.VersionList().end() && pkg.ProvidesList().end()) {
            continue;
        }

        // Ignore debug packages - these aren't interesting as codec providers,
        // but they do have apt GStreamer-* metadata.
        if (ends_with (pkg.Name(), "-dbg") || ends_with (pkg.Name(), "-dbgsym")) {
            continue;
        }

        // TODO search in updates packages
        // Ignore virtual packages
        pkgCache::VerIterator ver = m_cache->findVer(pkg);
        if (ver.end() == true) {
            ver = m_cache->findCandidateVer(pkg);
        }
        if (ver.end() == true) {
            continue;
        }

        arch = string(ver.Arch());

        pkgCache::VerFileIterator vf = ver.FileList();
        pkgRecords::Parser &rec = m_cache->GetPkgRecords()->Lookup(vf);
        const char *start, *stop;
        rec.GetRec(start, stop);
        string record(start, stop - start);
        if (matcher.matches(record, arch)) {
            output.append(ver);
        }
    }
}

// search packages which provide the libraries specified in "values"
void AptJob::providesLibrary(PkgList &output, gchar **values)
{
    bool ret = false;
    // Quick-check for library names
    for (uint i = 0; i < g_strv_length(values); i++) {
        if (g_str_has_prefix(values[i], "lib")) {
            ret = true;
            break;
        }
    }

    if (!ret) {
        return;
    }

    const char *libreg_str = "^\\(lib.*\\)\\.so\\.[0-9]*";
    g_debug("RegStr: %s", libreg_str);
    regex_t libreg;
    if(regcomp(&libreg, libreg_str, 0) != 0) {
        g_debug("Error compiling regular expression to match libraries.");
        return;
    }

    gchar *value;
    for (uint i = 0; i < g_strv_length(values); i++) {
        value = values[i];
        regmatch_t matches[2];
        if (regexec(&libreg, value, 2, matches, 0) != REG_NOMATCH) {
            string libPkgName = string(value, matches[1].rm_so, matches[1].rm_eo - matches[1].rm_so);

            string strvalue = string(value);
            ssize_t pos = strvalue.find (".so.");
            if ((pos > 0) && ((size_t) pos != string::npos)) {
                // If last char is a number, add a "-" (to be policy-compliant)
                if (g_ascii_isdigit (libPkgName.at (libPkgName.length () - 1))) {
                    libPkgName.append ("-");
                }

                libPkgName.append (strvalue.substr (pos + 4));
            }

            g_debug ("pkg-name: %s", libPkgName.c_str ());

            for (pkgCache::PkgIterator pkg = m_cache->GetPkgCache()->PkgBegin(); !pkg.end(); ++pkg) {
                // Ignore packages that exist only due to dependencies.
                if (pkg.VersionList().end() && pkg.ProvidesList().end()) {
                    continue;
                }

                // TODO: Ignore virtual packages
                pkgCache::VerIterator ver = m_cache->findVer(pkg);
                if (ver.end()) {
                    ver = m_cache->findCandidateVer(pkg);
                    if (ver.end()) {
                        continue;
                    }
                }

                // Make everything lower-case
                std::transform(libPkgName.begin(), libPkgName.end(), libPkgName.begin(), ::tolower);

                if (g_strcmp0 (pkg.Name (), libPkgName.c_str ()) == 0) {
                    output.append(ver);
                }
            }
        } else {
            g_debug("libmatcher: Did not match: %s", value);
        }
    }
}

// Mostly copied from pkgAcqArchive.
bool AptJob::getArchive(pkgAcquire *Owner,
                         const pkgCache::VerIterator &Version,
                         std::string directory,
                         std::string &StoreFilename)
{
    pkgCache::VerFileIterator Vf=Version.FileList();

    if (Version.Arch() == 0) {
        return _error->Error("I wasn't able to locate a file for the %s package. "
                             "This might mean you need to manually fix this package. (due to missing arch)",
                             Version.ParentPkg().Name());
    }

    /* We need to find a filename to determine the extension. We make the
        assumption here that all the available sources for this version share
        the same extension.. */
    // Skip not source sources, they do not have file fields.
    for (; Vf.end() == false; Vf++) {
        if ((Vf.File()->Flags & pkgCache::Flag::NotSource) != 0) {
            continue;
        }
        break;
    }

    // Does not really matter here.. we are going to fail out below
    if (Vf.end() != true) {
        // If this fails to get a file name we will bomb out below.
        pkgRecords::Parser &Parse = m_cache->GetPkgRecords()->Lookup(Vf);
        if (_error->PendingError() == true) {
            return false;
        }

        // Generate the final file name as: package_version_arch.foo
        StoreFilename = QuoteString(Version.ParentPkg().Name(),"_:") + '_' +
                QuoteString(Version.VerStr(),"_:") + '_' +
                QuoteString(Version.Arch(),"_:.") +
                "." + flExtension(Parse.FileName());
    }

    for (; Vf.end() == false; Vf++) {
        // Ignore not source sources
        if ((Vf.File()->Flags & pkgCache::Flag::NotSource) != 0) {
            continue;
        }

        // Try to cross match against the source list
        pkgIndexFile *Index;
        if (m_cache->GetSourceList()->FindIndex(Vf.File(),Index) == false) {
            continue;
        }

        // Grab the text package record
        pkgRecords::Parser &Parse = m_cache->GetPkgRecords()->Lookup(Vf);
        if (_error->PendingError() == true) {
            return false;
        }

        const string PkgFile = Parse.FileName();
        const HashStringList hashes = Parse.Hashes();
        if (PkgFile.empty() == true) {
            return _error->Error("The package index files are corrupted. No Filename: "
                                 "field for package %s.",
                                 Version.ParentPkg().Name());
        }

        string DestFile = directory + "/" + flNotDir(StoreFilename);

        // Create the item
        new pkgAcqFile(Owner,
                       Index->ArchiveURI(PkgFile),
                       hashes,
                       Version->Size,
                       Index->ArchiveInfo(Version),
                       Version.ParentPkg().Name(),
                       "",
                       DestFile);

        Vf++;
        return true;
    }
    return false;
}

AptCacheFile* AptJob::aptCacheFile() const
{
    return m_cache;
}

// used to emit packages it collects all the needed info
void AptJob::emitPackageDetail(const pkgCache::VerIterator &ver)
{
    if (ver.end() == true) {
        return;
    }

    const pkgCache::PkgIterator &pkg = ver.ParentPkg();
    std::string section = ver.Section() == NULL ? "" : ver.Section();

    size_t found;
    found = section.find_last_of("/");
    section = section.substr(found + 1);

    pkgCache::VerFileIterator vf = ver.FileList();
    pkgRecords::Parser &rec = m_cache->GetPkgRecords()->Lookup(vf);

    long size;
    if (pkg->CurrentState == pkgCache::State::Installed && pkg.CurrentVer() == ver) {
        // if the package is installed emit the installed size
        size = ver->InstalledSize;
    } else {
        size = ver->Size;
    }

    g_autofree gchar *package_id = m_cache->buildPackageId(ver);
    pk_backend_job_details(m_job,
                           package_id,
                           m_cache->getShortDescription(ver).c_str(),
                           "unknown",
                           get_enum_group(section),
                           m_cache->getLongDescriptionParsed(ver).c_str(),
                           rec.Homepage().c_str(),
                           size);
}

void AptJob::emitDetails(PkgList &pkgs)
{
    // Sort so we can remove the duplicated entries
    pkgs.sort();

    // Remove the duplicated entries
    pkgs.removeDuplicates();

    for (const PkgInfo &pkgInfo : pkgs) {
        if (m_cancel)
            break;

        emitPackageDetail(pkgInfo.ver);
    }
}

// helper for emitUpdateDetails() to create update items and add them to the final array for emission
void AptJob::stageUpdateDetail(GPtrArray *updateArray, const pkgCache::VerIterator &candver)
{
    // Verify if our update version is valid
    if (candver.end()) {
        // No candidate version was provided
        return;
    }

    const pkgCache::PkgIterator &pkg = candver.ParentPkg();

    // Get the version of the current package
    const pkgCache::VerIterator &currver = m_cache->findVer(pkg);

    // Build a package_id from the current version
    gchar *current_package_id = m_cache->buildPackageId(currver);

    pkgCache::VerFileIterator vf = candver.FileList();
    string origin = vf.File().Origin() == NULL ? "" : vf.File().Origin();
    pkgRecords::Parser &rec = m_cache->GetPkgRecords()->Lookup(candver.FileList());

    string changelog;
    string update_text;
    string updated;
    string issued;
    string srcpkg;
    if (rec.SourcePkg().empty()) {
        srcpkg = pkg.Name();
    } else {
        srcpkg = rec.SourcePkg();
    }

    PkBackend *backend = PK_BACKEND(pk_backend_job_get_backend(m_job));
    if (pk_backend_is_online(backend)) {
        // Create the download object
        AcqPackageKitStatus Stat(this);

        // get a fetcher
        pkgAcquire fetcher;
        fetcher.SetLog(&Stat);

        // fetch the changelog
        pk_backend_job_set_status(m_job, PK_STATUS_ENUM_DOWNLOAD_CHANGELOG);
        changelog = fetchChangelogData(*m_cache,
                                       fetcher,
                                       candver,
                                       currver,
                                       &update_text,
                                       &updated,
                                       &issued);
    }

    // Check if the update was updates since it was issued
    if (issued.compare(updated) == 0) {
        updated = "";
    }

    // Build a package_id from the update version
    string archive = vf.File().Archive() == NULL ? "" : vf.File().Archive();
    g_autofree gchar *package_id = m_cache->buildPackageId(candver);

    PkUpdateStateEnum updateState = PK_UPDATE_STATE_ENUM_UNKNOWN;
    if (archive.compare("stable") == 0) {
        updateState = PK_UPDATE_STATE_ENUM_STABLE;
    } else if (archive.compare("testing") == 0) {
        updateState = PK_UPDATE_STATE_ENUM_TESTING;
    } else if (archive.compare("unstable")  == 0 ||
               archive.compare("experimental") == 0) {
        updateState = PK_UPDATE_STATE_ENUM_UNSTABLE;
    }

    PkRestartEnum restart = PK_RESTART_ENUM_NONE;
    if (utilRestartRequired(pkg.Name())) {
        restart = PK_RESTART_ENUM_SYSTEM;
    }

    g_auto(GStrv) updates = (gchar **) g_malloc(2 * sizeof(gchar *));
    updates[0] = current_package_id;
    updates[1] = NULL;

    g_autoptr(GPtrArray) bugzilla_urls = getBugzillaUrls(changelog);
    g_autoptr(GPtrArray) cve_urls = getCVEUrls(changelog);
    g_autoptr(GPtrArray) obsoletes = g_ptr_array_new();

    for (auto deps = candver.DependsList(); not deps.end(); ++deps)
    {
        if (deps->Type == pkgCache::Dep::Obsoletes)
        {
            g_ptr_array_add(obsoletes, (void*) deps.TargetPkg().Name());
        }
    }

    // NULL terminate
    g_ptr_array_add(obsoletes, NULL);

    // construct the update item with out newly gathered data
    PkUpdateDetail *item = pk_update_detail_new ();
    g_object_set(item,
              "package-id", package_id,
              "updates", updates, //const gchar *updates
              "obsoletes", (gchar **) obsoletes->pdata, //const gchar *obsoletes
              "vendor-urls", NULL, //const gchar *vendor_url
              "bugzilla-urls", (gchar **) bugzilla_urls->pdata, // gchar **bugzilla_urls
              "cve-urls", (gchar **) cve_urls->pdata, // gchar **cve_urls
              "restart", restart, //PkRestartEnum restart
              "update-text", update_text.c_str(), //const gchar *update_text
              "changelog", changelog.c_str(), //const gchar *changelog
              "state", updateState, //PkUpdateStateEnum state
              "issued", issued.c_str(), //const gchar *issued_text
              "updated", updated.c_str(), //const gchar *updated_text
              NULL);
    g_ptr_array_add(updateArray, item);
}

void AptJob::emitUpdateDetails(const PkgList &pkgs)
{
    g_autoptr(GPtrArray) updateDetailsArray = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

    for (const PkgInfo &pi : pkgs) {
        if (m_cancel)
            break;
        stageUpdateDetail(updateDetailsArray, pi.ver);
    }

    // emit all data that we've just collected
    pk_backend_job_update_details(m_job, updateDetailsArray);
}

void AptJob::getDepends(PkgList &output,
                         const pkgCache::VerIterator &ver,
                         bool recursive)
{
    pkgCache::DepIterator dep = ver.DependsList();
    while (!dep.end()) {
        if (m_cancel) {
            break;
        }

        const pkgCache::VerIterator &ver = m_cache->findVer(dep.TargetPkg());
        // Ignore packages that exist only due to dependencies.
        if (ver.end()) {
            dep++;
            continue;
        } else if (dep->Type == pkgCache::Dep::Depends) {
            if (recursive) {
                if (!output.contains(dep.TargetPkg())) {
                    output.append(ver);
                    getDepends(output, ver, recursive);
                }
            } else {
                output.append(ver);
            }
        }
        dep++;
    }
}

void AptJob::getRequires(PkgList &output,
                          const pkgCache::VerIterator &ver,
                          bool recursive)
{
    for (pkgCache::PkgIterator parentPkg = m_cache->GetPkgCache()->PkgBegin(); !parentPkg.end(); ++parentPkg) {
        if (m_cancel) {
            break;
        }

        // Ignore packages that exist only due to dependencies.
        if (parentPkg.VersionList().end() && parentPkg.ProvidesList().end()) {
            continue;
        }

        // Don't insert virtual packages instead add what it provides
        const pkgCache::VerIterator &parentVer = m_cache->findVer(parentPkg);
        if (parentVer.end() == false) {
            PkgList deps;
            getDepends(deps, parentVer, false);
            for (const PkgInfo &depInfo : deps) {
                if (depInfo.ver == ver) {
                    if (recursive) {
                        if (!output.contains(parentPkg)) {
                            output.append(parentVer);
                            getRequires(output, parentVer, recursive);
                        }
                    } else {
                        output.append(parentVer);
                    }
                    break;
                }
            }
        }
    }
}

PkgList AptJob::getPackages()
{
    pk_backend_job_set_status(m_job, PK_STATUS_ENUM_QUERY);

    PkgList output;
    output.reserve(m_cache->GetPkgCache()->HeaderP->PackageCount);
    for (pkgCache::PkgIterator pkg = m_cache->GetPkgCache()->PkgBegin(); !pkg.end(); ++pkg) {
        if (m_cancel) {
            break;
        }

        // Ignore packages that exist only due to dependencies.
        if(pkg.VersionList().end() && pkg.ProvidesList().end()) {
            continue;
        }

        // Don't insert virtual packages as they don't have all kinds of info
        const pkgCache::VerIterator &ver = m_cache->findVer(pkg);
        if (ver.end() == false)
            output.append(ver);
    }
    return output;
}

PkgList AptJob::getPackagesFromRepo(SourcesList::SourceRecord *&rec)
{
    pk_backend_job_set_status(m_job, PK_STATUS_ENUM_QUERY);

    PkgList output;
    output.reserve(m_cache->GetPkgCache()->HeaderP->PackageCount);
    for (pkgCache::PkgIterator pkg = m_cache->GetPkgCache()->PkgBegin(); !pkg.end(); ++pkg) {
        if (m_cancel) {
            break;
        }

        // Ignore packages that exist only due to dependencies.
        if(pkg.VersionList().end() && pkg.ProvidesList().end()) {
            continue;
        }

        // Don't insert virtual packages as they don't have all kinds of info
        const pkgCache::VerIterator &ver = m_cache->findVer(pkg);
        if (ver.end()) {
            continue;
        }

        // only installed packages matters
        if (!(pkg->CurrentState == pkgCache::State::Installed && pkg.CurrentVer() == ver)) {
            continue;
        }

        // Distro name
        pkgCache::VerFileIterator vf = ver.FileList();
        if (vf.File().Archive() == NULL || rec->Dist.compare(vf.File().Archive()) != 0){
            continue;
        }

        // Section part
        if (vf.File().Component() == NULL || !rec->hasSection(vf.File().Component())) {
            continue;
        }

        // Check if the site the package comes from is include in the Repo uri
        if (vf.File().Site() == NULL || rec->URI.find(vf.File().Site()) == std::string::npos) {
            continue;
        }

        output.append(ver);
    }
    return output;
}

PkgList AptJob::getPackagesFromGroup(gchar **values)
{
    pk_backend_job_set_status(m_job, PK_STATUS_ENUM_QUERY);

    PkgList output;
    vector<PkGroupEnum> groups;

    uint len = g_strv_length(values);
    for (uint i = 0; i < len; i++) {
        if (values[i] == NULL) {
            pk_backend_job_error_code(m_job,
                                      PK_ERROR_ENUM_GROUP_NOT_FOUND,
                                      "An empty group was received");
            return output;
        } else {
            groups.push_back(pk_group_enum_from_string(values[i]));
        }
    }

    pk_backend_job_set_allow_cancel(m_job, true);

    for (pkgCache::PkgIterator pkg = m_cache->GetPkgCache()->PkgBegin(); !pkg.end(); ++pkg) {
        if (m_cancel) {
            break;
        }
        // Ignore packages that exist only due to dependencies.
        if (pkg.VersionList().end() && pkg.ProvidesList().end()) {
            continue;
        }

        // Ignore virtual packages
        const pkgCache::VerIterator &ver = m_cache->findVer(pkg);
        if (ver.end() == false) {
            string section = pkg.VersionList().Section() == NULL ? "" : pkg.VersionList().Section();

            size_t found;
            found = section.find_last_of("/");
            section = section.substr(found + 1);

            // Don't insert virtual packages instead add what it provides
            for (PkGroupEnum group : groups) {
                if (group == get_enum_group(section)) {
                    output.append(ver);
                    break;
                }
            }
        }
    }
    return output;
}

bool AptJob::matchesQueries(const vector<string> &queries, string s) {
    for (string query : queries) {
        // Case insensitive "string.contains"
        auto it = std::search(
            s.begin(), s.end(),
            query.begin(), query.end(),
            [](unsigned char ch1, unsigned char ch2) {
                return std::tolower(ch1) == std::tolower(ch2);
            }
        );

        if (it != s.end()) {
            return true;
        }
    }
    return false;
}

PkgList AptJob::searchPackageName(const vector<string> &queries)
{
    PkgList output;

    for (pkgCache::PkgIterator pkg = m_cache->GetPkgCache()->PkgBegin(); !pkg.end(); ++pkg) {
        if (m_cancel) {
            break;
        }
        // Ignore packages that exist only due to dependencies.
        if (pkg.VersionList().end() && pkg.ProvidesList().end()) {
            continue;
        }

        if (matchesQueries(queries, pkg.Name())) {
            // Don't insert virtual packages instead add what it provides
            const pkgCache::VerIterator &ver = m_cache->findVer(pkg);
            if (ver.end() == false) {
                output.append(ver);
            } else {
                // iterate over the provides list
                for (pkgCache::PrvIterator Prv = pkg.ProvidesList(); Prv.end() == false; ++Prv) {
                    const pkgCache::VerIterator &ownerVer = m_cache->findVer(Prv.OwnerPkg());

                    // check to see if the provided package isn't virtual too
                    if (ownerVer.end() == false) {
                        // we add the package now because we will need to
                        // remove duplicates later anyway
                        output.append(ownerVer);
                    }
                }
            }
        }
    }
    return output;
}

PkgList AptJob::searchPackageDetails(const vector<string> &queries)
{
    PkgList output;

    for (pkgCache::PkgIterator pkg = m_cache->GetPkgCache()->PkgBegin(); !pkg.end(); ++pkg) {
        if (m_cancel) {
            break;
        }
        // Ignore packages that exist only due to dependencies.
        if (pkg.VersionList().end() && pkg.ProvidesList().end()) {
            continue;
        }

        const pkgCache::VerIterator &ver = m_cache->findVer(pkg);
        if (ver.end() == false) {
            if (matchesQueries(queries, pkg.Name()) ||
                    matchesQueries(queries, (*m_cache).getLongDescription(ver))) {
                // The package matched
                output.append(ver);
            }
        } else if (matchesQueries(queries, pkg.Name())) {
            // The package is virtual and MATCHED the name
            // Don't insert virtual packages instead add what it provides

            // iterate over the provides list
            for (pkgCache::PrvIterator Prv = pkg.ProvidesList(); Prv.end() == false; ++Prv) {
                const pkgCache::VerIterator &ownerVer = m_cache->findVer(Prv.OwnerPkg());

                // check to see if the provided package isn't virtual too
                if (ownerVer.end() == false) {
                    // we add the package now because we will need to
                    // remove duplicates later anyway
                    output.append(ownerVer);
                }
            }
        }
    }
    return output;
}

// used to return files it reads, using the info from the files in /var/lib/dpkg/info/
PkgList AptJob::searchPackageFiles(gchar **values)
{
    PkgList output;
    vector<string> packages;
    string search;
    regex_t re;

    for (uint i = 0; i < g_strv_length(values); ++i) {
        gchar *value = values[i];
        if (strlen(value) < 1) {
            continue;
        }

        if (!search.empty()) {
            search.append("\\|");
        }

        if (value[0] == '/') {
            search.append("^");
            search.append(value);
            search.append("$");
        } else {
            search.append(value);
            search.append("$");
        }
    }

    if(regcomp(&re, search.c_str(), REG_NOSUB) != 0) {
        g_debug("Regex compilation error");
        return output;
    }

    DIR *dp;
    struct dirent *dirp;
    if (!(dp = opendir("/var/lib/dpkg/info/"))) {
        g_debug ("Error opening /var/lib/dpkg/info/\n");
        regfree(&re);
        return output;
    }

    string line;
    while ((dirp = readdir(dp)) != NULL) {
        if (m_cancel) {
            break;
        }

        if (ends_with(dirp->d_name, ".list")) {
            string file(dirp->d_name);
            string f = "/var/lib/dpkg/info/" + file;
            ifstream in(f.c_str());
            if (!in != 0) {
                continue;
            }

            while (!in.eof()) {
                getline(in, line);
                if (regexec(&re, line.c_str(), (size_t)0, NULL, 0) == 0) {
                    packages.push_back(file.erase(file.size() - 5, file.size()));
                    break;
                }
            }
        }
    }
    closedir(dp);
    regfree(&re);

    // Resolve the package names now
    for (const string &name : packages) {
        if (m_cancel) {
            break;
        }

        pkgCache::PkgIterator pkg;
        if (name.find(':') != std::string::npos) {
            pkg = (*m_cache)->FindPkg(name);
            if (pkg.end()) {
                continue;
            }
        } else {
            pkgCache::GrpIterator grp = (*m_cache)->FindGrp(name);
            for (pkg = grp.PackageList(); pkg.end() == false; pkg = grp.NextPkg(pkg)) {
                if (pkg->CurrentState == pkgCache::State::Installed) {
                    break;
                }
            }

            if (pkg->CurrentState != pkgCache::State::Installed) {
                 continue;
            }
        }

        const pkgCache::VerIterator &ver = m_cache->findVer(pkg);
        if (ver.end()) {
            continue;
        }
        output.append(ver);
    }

    return output;
}

PkgList AptJob::getUpdates(PkgList &blocked, PkgList &downgrades, PkgList &installs, PkgList &removals, PkgList &obsoleted)
{
    PkgList updates;

    if (m_cache->DistUpgrade() == false) {
        m_cache->ShowBroken(false);
        g_debug("Internal error, DistUpgrade broke stuff");
        return updates;
    }

    for (pkgCache::PkgIterator pkg = (*m_cache)->PkgBegin(); !pkg.end(); ++pkg) {
        const auto &state = (*m_cache)[pkg];
        if (pkg->SelectedState == pkgCache::State::Hold) {
            // We pretend held packages are not upgradable at all since we can't represent
            // the concept of holds in PackageKit.
            // https://github.com/PackageKit/PackageKit/issues/120
            continue;
        } else if (state.Upgrade() == true && state.NewInstall() == false) {
            const pkgCache::VerIterator &ver = m_cache->findCandidateVer(pkg);
            if (!ver.end()) {
                updates.append(ver);
            }
        } else if (state.Downgrade() == true) {
            const pkgCache::VerIterator &ver = m_cache->findCandidateVer(pkg);
            if (!ver.end()) {
                downgrades.append(ver);
            }
        } else if (state.Upgradable() == true &&
                   pkg->CurrentVer != 0 &&
                   state.Delete() == false) {
            const pkgCache::VerIterator &ver = m_cache->findCandidateVer(pkg);
            if (!ver.end()) {
                blocked.append(ver);
            }
        } else if (state.NewInstall()) {
            const pkgCache::VerIterator &ver = m_cache->findCandidateVer(pkg);
            if (!ver.end()) {
                installs.append(ver);
            }
        } else if (state.Delete()) {
            const pkgCache::VerIterator &ver = m_cache->findCandidateVer(pkg);
            if (!ver.end()) {
                bool is_obsoleted = false;

                for (pkgCache::DepIterator D = pkg.RevDependsList(); not D.end(); ++D)
                {
                    if ((D->Type == pkgCache::Dep::Obsoletes)
                        && ((*m_cache)[D.ParentPkg()].CandidateVer != nullptr)
                        && (*m_cache)[D.ParentPkg()].CandidateVerIter(*m_cache).Downloadable()
                        && ((pkgCache::Version*)D.ParentVer() == (*m_cache)[D.ParentPkg()].CandidateVer)
                        && (*m_cache)->VS().CheckDep(pkg.CurrentVer().VerStr(), D->CompareOp, D.TargetVer())
                        && ((*m_cache)->GetPolicy().GetPriority(D.ParentPkg()) >= (*m_cache)->GetPolicy().GetPriority(pkg)))
                    {
                        is_obsoleted = true;
                        break;
                    }
                }

                if (is_obsoleted ) {
                    /* Obsoleted packages */
                    obsoleted.append(ver);
                } else {
                    /* Removed packages */
                    removals.append(ver);
                }
            }
        }
    }

    return updates;
}

// used to return files it reads, using the info from the files in /var/lib/dpkg/info/
void AptJob::providesMimeType(PkgList &output, gchar **values)
{
    g_autoptr(AsPool) pool = NULL;
    g_autoptr(GError) error = NULL;
    std::vector<string> pkg_names;

    pool = as_pool_new ();

    /* don't monitor cache locations or load Flatpak data */
    as_pool_remove_flags (pool, AS_POOL_FLAG_MONITOR);
    as_pool_remove_flags (pool, AS_POOL_FLAG_LOAD_FLATPAK);

    /* try to load the metadata pool */
    if (!as_pool_load (pool, NULL, &error)) {
        pk_backend_job_error_code(m_job,
                                  PK_ERROR_ENUM_INTERNAL_ERROR,
                                  "Failed to load AppStream metadata: %s", error->message);
        return;
    }

    /* search for mimetypes for all values */
    for (guint i = 0; values[i] != NULL; i++) {
        g_autoptr(GPtrArray) result = NULL;

        if (m_cancel)
            break;

        result = as_pool_get_components_by_provided_item (pool, AS_PROVIDED_KIND_MEDIATYPE, values[i]);
        for (guint j = 0; j < result->len; j++) {
            const gchar *pkgname;
            AsComponent *cpt = AS_COMPONENT (g_ptr_array_index (result, j));

            /* sanity check */
            pkgname = as_component_get_pkgname (cpt);
            if (pkgname == NULL) {
                g_warning ("Component %s has no package name (it was ignored in the search).", as_component_get_data_id (cpt));
                continue;
            }

            pkg_names.push_back (pkgname);
        }
    }

    /* resolve the package names */
    for (const std::string &package : pkg_names) {
        if (m_cancel)
            break;

        const pkgCache::PkgIterator &pkg = (*m_cache)->FindPkg(package);
        if (pkg.end() == true)
            continue;
        const pkgCache::VerIterator &ver = m_cache->findVer(pkg);
        if (ver.end() == true)
            continue;

        output.append(ver);
    }
}

bool AptJob::isApplication(const pkgCache::VerIterator &ver)
{
    bool ret = false;
    gchar *fileName;
    string line;

    fileName = g_strdup_printf("/var/lib/dpkg/info/%s:%s.list",
                               ver.ParentPkg().Name(),
                               ver.Arch());
    if (!FileExists(fileName)) {
        g_free(fileName);
        // if the file was not found try without the arch field
        fileName = g_strdup_printf("/var/lib/dpkg/info/%s.list",
                                   ver.ParentPkg().Name());
    }

    if (FileExists(fileName)) {
        ifstream in(fileName);
        if (!in != 0) {
            g_free(fileName);
            return false;
        }

        while (in.eof() == false) {
            getline(in, line);
            if (ends_with(line, ".desktop")) {
                ret = true;
                break;
            }
        }
    }

    g_free(fileName);
    return ret;
}

// used to emit files it reads the info directly from the files
void AptJob::emitPackageFiles(const gchar *pi)
{
    GPtrArray *files;
    string line;

    g_auto(GStrv) parts = pk_package_id_split(pi);
    string fName;
    fName = "/var/lib/dpkg/info/" +
            string(parts[PK_PACKAGE_ID_NAME]) +
            ":" +
            string(parts[PK_PACKAGE_ID_ARCH]) +
            ".list";
    if (!FileExists(fName)) {
        // if the file was not found try without the arch field
        fName = "/var/lib/dpkg/info/" +
                string(parts[PK_PACKAGE_ID_NAME]) +
                ".list";
    }

    if (FileExists(fName)) {
        ifstream in(fName.c_str());
        if (!in != 0) {
            return;
        }

        files = g_ptr_array_new_with_free_func(g_free);
        while (in.eof() == false) {
            getline(in, line);
            if (!line.empty()) {
                g_ptr_array_add(files, g_strdup(line.c_str()));
            }
        }

        if (files->len) {
            g_ptr_array_add(files, NULL);
            pk_backend_job_files(m_job, pi, (gchar **) files->pdata);
        }
        g_ptr_array_unref(files);
    }
}

void AptJob::emitPackageFilesLocal(const gchar *file)
{
    DebFile deb(file);
    if (!deb.isValid()){
        return;
    }

    g_autofree gchar *package_id = pk_package_id_build(deb.packageName().c_str(),
                                                       deb.version().c_str(),
                                                       deb.architecture().c_str(),
                                                       file);

    g_autoptr(GPtrArray) files = g_ptr_array_new_with_free_func(g_free);
    for (auto file : deb.files()) {
        g_ptr_array_add(files, g_canonicalize_filename(file.c_str(), "/"));
    }
    g_ptr_array_add(files, NULL);
    pk_backend_job_files(m_job, package_id, (gchar **) files->pdata);
}

/**
  * Check if package is officially supported by the current distribution
  */
bool AptJob::packageIsSupported(const pkgCache::VerIterator &verIter, string component)
{
    string origin;
    if (!verIter.end()) {
        pkgCache::VerFileIterator vf = verIter.FileList();
        origin = vf.File().Origin() == NULL ? "" : vf.File().Origin();
    }

    if (component.empty()) {
        component = "main";
    }

    // Get a fetcher
    AcqPackageKitStatus Stat(this);
    pkgAcquire fetcher;
    fetcher.SetLog(&Stat);

    PkBitfield flags = pk_backend_job_get_transaction_flags(m_job);
    bool trusted = checkTrusted(fetcher, flags);

    if ((origin.compare("Debian") == 0) || (origin.compare("Ubuntu") == 0))  {
        if ((component.compare("main") == 0 ||
             component.compare("restricted") == 0 ||
             component.compare("unstable") == 0 ||
             component.compare("testing") == 0) && trusted) {
            return true;
        }
    }

    return false;
}

bool AptJob::checkTrusted(pkgAcquire &fetcher, PkBitfield flags)
{
    string UntrustedList;
    PkgList untrusted;
    for (pkgAcquire::ItemIterator I = fetcher.ItemsBegin(); I < fetcher.ItemsEnd(); ++I) {
        if (!(*I)->IsTrusted()) {
            // The pkgAcquire::Item had a version hiden on it's subclass
            // pkgAcqArchive but it was protected our subclass exposes that
            pkgAcqArchiveSane *archive = static_cast<pkgAcqArchiveSane*>(dynamic_cast<pkgAcqArchive*>(*I));
            if (archive == nullptr) {
                continue;
            }
            untrusted.append(archive->version());

            UntrustedList += string((*I)->ShortDesc()) + " ";
        }
    }

    if (untrusted.empty()) {
        return true;
    } else if (pk_bitfield_contain(flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
        // We are just simulating and have untrusted packages emit them
        // and return true to continue processing
        emitPackages(untrusted, PK_FILTER_ENUM_NONE, PK_INFO_ENUM_UNTRUSTED);

        return true;
    } else if (pk_bitfield_contain(flags, PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED)) {
        // We are NOT simulating and have untrusted packages
        // fail the transaction.
        pk_backend_job_error_code(m_job,
                                  PK_ERROR_ENUM_CANNOT_INSTALL_REPO_UNSIGNED,
                                  "The following packages cannot be authenticated:\n%s",
                                  UntrustedList.c_str());
        _error->Discard();

        return false;
    } else {
        // We are NOT simulating and have untrusted packages
        // But the user didn't set ONLY_TRUSTED flag
        g_debug ("Authentication warning overridden.\n");
        return true;
    }
}

/**
 * checkChangedPackages - Check whas is goind to happen to the packages
 */
PkgList AptJob::checkChangedPackages(bool emitChanged)
{
    PkgList ret;
    PkgList installing;
    PkgList removing;
    PkgList updating;
    PkgList downgrading;
    PkgList obsoleting;

    for (pkgCache::PkgIterator pkg = (*m_cache)->PkgBegin(); ! pkg.end(); ++pkg) {
        if ((*m_cache)[pkg].NewInstall() == true) {
            // installing;
            const pkgCache::VerIterator &ver = m_cache->findCandidateVer(pkg);
            if (!ver.end()) {
                ret.append(ver);
                installing.append(ver);

                // append to the restart required list
                if (utilRestartRequired(pkg.Name())) {
                    m_restartPackages.append(ver);
                }
            }
        } else if ((*m_cache)[pkg].Delete() == true) {
            // removing
            const pkgCache::VerIterator &ver = m_cache->findVer(pkg);
            if (!ver.end()) {
                ret.append(ver);

                bool is_obsoleted = false;

                for (pkgCache::DepIterator D = pkg.RevDependsList(); not D.end(); ++D)
                {
                    if ((D->Type == pkgCache::Dep::Obsoletes)
                            && ((*m_cache)[D.ParentPkg()].CandidateVer != nullptr)
                            && (*m_cache)[D.ParentPkg()].CandidateVerIter(*m_cache).Downloadable()
                            && ((pkgCache::Version*)D.ParentVer() == (*m_cache)[D.ParentPkg()].CandidateVer)
                            && (*m_cache)->VS().CheckDep(pkg.CurrentVer().VerStr(), D->CompareOp, D.TargetVer())
                            && ((*m_cache)->GetPolicy().GetPriority(D.ParentPkg()) >= (*m_cache)->GetPolicy().GetPriority(pkg)))
                    {
                        is_obsoleted = true;
                        break;
                    }
                }

                if (!is_obsoleted) {
                    removing.append(ver);
                } else {
                    obsoleting.append(ver);
                }

                // append to the restart required list
                if (utilRestartRequired(pkg.Name())) {
                    m_restartPackages.append(ver);
                }
            }
        } else if ((*m_cache)[pkg].Upgrade() == true) {
            // updating
            const pkgCache::VerIterator &ver = m_cache->findCandidateVer(pkg);
            if (!ver.end()) {
                ret.append(ver);
                updating.append(ver);

                // append to the restart required list
                if (utilRestartRequired(pkg.Name()))
                    m_restartPackages.append(ver);
            }
        } else if ((*m_cache)[pkg].Downgrade() == true) {
            // downgrading
            const pkgCache::VerIterator &ver = m_cache->findCandidateVer(pkg);
            if (!ver.end()) {
                ret.append(ver);
                downgrading.append(ver);

                // append to the restart required list
                if (utilRestartRequired(pkg.Name())) {
                    m_restartPackages.append(ver);
                }
            }
        }
    }

    if (emitChanged) {
        // emit packages that have changes
        emitPackages(obsoleting,  PK_FILTER_ENUM_NONE, PK_INFO_ENUM_OBSOLETING);
        emitPackages(removing,    PK_FILTER_ENUM_NONE, PK_INFO_ENUM_REMOVING);
        emitPackages(downgrading, PK_FILTER_ENUM_NONE, PK_INFO_ENUM_DOWNGRADING);
        emitPackages(installing,  PK_FILTER_ENUM_NONE, PK_INFO_ENUM_INSTALLING);
        emitPackages(updating,    PK_FILTER_ENUM_NONE, PK_INFO_ENUM_UPDATING);
    }

    return ret;
}

pkgCache::VerIterator AptJob::findTransactionPackage(const std::string &name)
{
    for (const PkgInfo &pkInfo : m_pkgs) {
        if (pkInfo.ver.ParentPkg().Name() == name) {
            return pkInfo.ver;
        }
    }

    const pkgCache::PkgIterator &pkg = (*m_cache)->FindPkg(name);
    // Ignore packages that could not be found or that exist only due to dependencies.
    if (pkg.end() == true ||
            (pkg.VersionList().end() && pkg.ProvidesList().end())) {
        return pkgCache::VerIterator();
    }

    const pkgCache::VerIterator &ver = m_cache->findVer(pkg);
    // check to see if the provided package isn't virtual too
    if (ver.end() == false) {
        return ver;
    }

    const pkgCache::VerIterator &candidateVer = m_cache->findCandidateVer(pkg);

    // Return the last try anyway
    return candidateVer;
}

void AptJob::updateInterface(int fd, int writeFd, bool *errorEmitted)
{
    char buf[2];
    static char line[1024] = "";

    while (1) {
        // This algorithm should be improved (it's the same as the rpm one ;)
        int len = read(fd, buf, 1);

        // nothing was read
        if(len < 1)
            break;

        // update the time we last saw some action
        m_lastTermAction = time(NULL);

        if (buf[0] == '\n') {
            if (m_cancel)
                kill(m_child_pid, SIGTERM);

            //cout << "got line: " << line << endl;

            g_auto(GStrv) split   = g_strsplit(line, ":",5);
            const gchar *status   = g_strstrip(split[0]);
            const gchar *pkg      = g_strstrip(split[1]);
            const gchar *percent  = g_strstrip(split[2]);
            const std::string str = g_strstrip(split[3]);

            // major problem here, we got unexpected input. should _never_ happen
            if(pkg == nullptr && status == nullptr)
                continue;

            // Since PackageKit doesn't emulate finished anymore
            // we need to manually do it here, as at this point
            // dpkg doesn't process two packages at the same time
            if (!m_lastPackage.empty() && m_lastPackage.compare(pkg) != 0) {
                const pkgCache::VerIterator &ver = findTransactionPackage(m_lastPackage);
                if (!ver.end()) {
                    emitPackage(ver, PK_INFO_ENUM_FINISHED);
                }
                m_lastSubProgress = 0;
            }

            // first check for errors and conf-file prompts
            if (strstr(status, "pmerror") != NULL) {
                // error from dpkg
                pk_backend_job_error_code(m_job,
                                          PK_ERROR_ENUM_PACKAGE_FAILED_TO_INSTALL,
                                          "Error while installing package: %s",
                                          str.c_str());
                if (errorEmitted != nullptr)
                    *errorEmitted = true;
            } else if (strstr(status, "pmconffile") != NULL) {
                // conffile-request from dpkg, needs to be parsed different
                int i = 0;
                string orig_file, new_file;

                // go to first ' and read until the end
                for(;str[i] != '\'' || str[i] == 0; i++)
                    /*nothing*/
                    ;
                i++;
                for(;str[i] != '\'' || str[i] == 0; i++)
                    orig_file.append(1, str[i]);
                i++;

                // same for second ' and read until the end
                for(;str[i] != '\'' || str[i] == 0; i++)
                    /*nothing*/
                    ;
                i++;
                for(;str[i] != '\'' || str[i] == 0; i++)
                    new_file.append(1, str[i]);
                i++;

                gchar *filename;
                filename = g_build_filename(DATADIR, "PackageKit", "helpers", "apt", "pkconffile", NULL);
                gchar **argv;
                gchar **envp;
                GError *error = NULL;
                argv = (gchar **) g_malloc(5 * sizeof(gchar *));
                argv[0] = filename;
                argv[1] = g_strdup(m_lastPackage.c_str());
                argv[2] = g_strdup(orig_file.c_str());
                argv[3] = g_strdup(new_file.c_str());
                argv[4] = NULL;

                const gchar *socket = pk_backend_job_get_frontend_socket(m_job);
                if ((m_interactive) && (socket != NULL)) {
                    envp = (gchar **) g_malloc(3 * sizeof(gchar *));
                    envp[0] = g_strdup("DEBIAN_FRONTEND=passthrough");
                    envp[1] = g_strdup_printf("DEBCONF_PIPE=%s", socket);
                    envp[2] = NULL;
                } else {
                    // we don't have a socket set or are non-interactive. Use the noninteractive frontend.
                    envp = (gchar **) g_malloc(2 * sizeof(gchar *));
                    envp[0] = g_strdup("DEBIAN_FRONTEND=noninteractive");
                    envp[1] = NULL;
                }

                gboolean ret;
                gint exitStatus;
                ret = g_spawn_sync(NULL, // working dir
                                   argv, // argv
                                   envp, // envp
                                   G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                                   NULL, // child_setup
                                   NULL, // user_data
                                   NULL, // standard_output
                                   NULL, // standard_error
                                   &exitStatus,
                                   &error);

                int exit_code = WEXITSTATUS(exitStatus);
                cout << filename << " " << exit_code << " ret: "<< ret << endl;

                g_strfreev(argv);
                g_strfreev(envp);

                if (exit_code == 10) {
                    // 1 means the user wants the package config
                    if (write(writeFd, "Y\n", 2) != 2) {
                        // TODO we need a DPKG patch to use debconf
                        g_debug("Failed to write");
                    }
                } else if (exit_code == 20) {
                    // 2 means the user wants to keep the current config
                    if (write(writeFd, "N\n", 2) != 2) {
                        // TODO we need a DPKG patch to use debconf
                        g_debug("Failed to write");
                    }
                } else {
                    // either the user didn't choose an option or the front end failed'
                    //                     pk_backend_job_message(m_job,
                    //                                            PK_MESSAGE_ENUM_CONFIG_FILES_CHANGED,
                    //                                            "The configuration file '%s' "
                    //                                            "(modified by you or a script) "
                    //                                            "has a newer version '%s'.\n"
                    //                                            "Please verify your changes and update it manually.",
                    //                                            orig_file.c_str(),
                    //                                            new_file.c_str());
                    // fall back to keep the current config file
                    if (write(writeFd, "N\n", 2) != 2) {
                        // TODO we need a DPKG patch to use debconf
                        g_debug("Failed to write");
                    }
                }
            } else if (strstr(status, "pmstatus") != NULL) {
                // INSTALL & UPDATE
                // - Running dpkg
                // loops ALL
                // -  0 Installing pkg (sometimes this is skiped)
                // - 25 Preparing pkg
                // - 50 Unpacking pkg
                // - 75 Preparing to configure pkg
                //   ** Some pkgs have
                //   - Running post-installation
                //   - Running dpkg
                // reloops all
                // -   0 Configuring pkg
                // - +25 Configuring pkg (SOMETIMES)
                // - 100 Installed pkg
                // after all
                // - Running post-installation

                // REMOVE
                // - Running dpkg
                // loops
                // - 25  Removing pkg
                // - 50  Preparing for removal of pkg
                // - 75  Removing pkg
                // - 100 Removed pkg
                // after all
                // - Running post-installation

                // Let's start parsing the status:
                if (starts_with(str, "Preparing to configure")) {
                    // Preparing to Install/configure
                    // cout << "Found Preparing to configure! " << line << endl;
                    // The next item might be Configuring so better it be 100
                    m_lastSubProgress = 100;
                    const pkgCache::VerIterator &ver = findTransactionPackage(pkg);
                    if (!ver.end()) {
                        emitPackage(ver, PK_INFO_ENUM_PREPARING);
                        emitPackageProgress(ver, PK_STATUS_ENUM_SETUP, 75);
                    }
                } else if (starts_with(str, "Preparing for removal")) {
                    // Preparing to Install/configure
                    // cout << "Found Preparing for removal! " << line << endl;
                    m_lastSubProgress = 50;
                    const pkgCache::VerIterator &ver = findTransactionPackage(pkg);
                    if (!ver.end()) {
                        emitPackage(ver, PK_INFO_ENUM_REMOVING);
                        emitPackageProgress(ver, PK_STATUS_ENUM_SETUP, m_lastSubProgress);
                    }
                } else if (starts_with(str, "Preparing")) {
                    // Preparing to Install/configure
                    // cout << "Found Preparing! " << line << endl;
                    const pkgCache::VerIterator &ver = findTransactionPackage(pkg);
                    if (!ver.end()) {
                        emitPackage(ver, PK_INFO_ENUM_PREPARING);
                        emitPackageProgress(ver, PK_STATUS_ENUM_SETUP, 25);
                    }
                } else if (starts_with(str, "Unpacking")) {
                    // cout << "Found Unpacking! " << line << endl;
                    const pkgCache::VerIterator &ver = findTransactionPackage(pkg);
                    if (!ver.end()) {
                        emitPackage(ver, PK_INFO_ENUM_DECOMPRESSING);
                        emitPackageProgress(ver, PK_STATUS_ENUM_INSTALL, 50);
                    }
                } else if (starts_with(str, "Configuring")) {
                    // Installing Package
                    // cout << "Found Configuring! " << line << endl;
                    if (m_lastSubProgress >= 100 && !m_lastPackage.empty()) {
                        // cout << "FINISH the last package: " << m_lastPackage << endl;
                        const pkgCache::VerIterator &ver = findTransactionPackage(m_lastPackage);
                        if (!ver.end()) {
                            emitPackage(ver, PK_INFO_ENUM_FINISHED);
                        }
                        m_lastSubProgress = 0;
                    }

                    const pkgCache::VerIterator &ver = findTransactionPackage(pkg);
                    if (!ver.end()) {
                        emitPackage(ver, PK_INFO_ENUM_INSTALLING);
                        emitPackageProgress(ver, PK_STATUS_ENUM_INSTALL, m_lastSubProgress);
                    }
                    m_lastSubProgress += 25;
                } else if (starts_with(str, "Running dpkg")) {
                    // cout << "Found Running dpkg! " << line << endl;
                } else if (starts_with(str, "Running")) {
                    // cout << "Found Running! " << line << endl;
                    pk_backend_job_set_status (m_job, PK_STATUS_ENUM_COMMIT);
                } else if (starts_with(str, "Installing")) {
                    // cout << "Found Installing! " << line << endl;
                    // FINISH the last package
                    if (!m_lastPackage.empty()) {
                        // cout << "FINISH the last package: " << m_lastPackage << endl;
                        const pkgCache::VerIterator &ver = findTransactionPackage(m_lastPackage);
                        if (!ver.end()) {
                            emitPackage(ver, PK_INFO_ENUM_FINISHED);
                        }
                    }
                    m_lastSubProgress = 0;
                    const pkgCache::VerIterator &ver = findTransactionPackage(pkg);
                    if (!ver.end()) {
                        emitPackage(ver, PK_INFO_ENUM_INSTALLING);
                        emitPackageProgress(ver, PK_STATUS_ENUM_INSTALL, m_lastSubProgress);
                    }
                } else if (starts_with(str, "Removing")) {
                    // cout << "Found Removing! " << line << endl;
                    if (m_lastSubProgress >= 100 && !m_lastPackage.empty()) {
                        // cout << "FINISH the last package: " << m_lastPackage << endl;
                        const pkgCache::VerIterator &ver = findTransactionPackage(m_lastPackage);
                        if (!ver.end()) {
                            emitPackage(ver, PK_INFO_ENUM_FINISHED);
                        }
                    }
                    m_lastSubProgress += 25;

                    const pkgCache::VerIterator &ver = findTransactionPackage(pkg);
                    if (!ver.end()) {
                        emitPackage(ver, PK_INFO_ENUM_REMOVING);
                        emitPackageProgress(ver, PK_STATUS_ENUM_REMOVE, m_lastSubProgress);
                    }
                } else if (starts_with(str, "Installed") ||
                           starts_with(str, "Removed")) {
                    // cout << "Found FINISHED! " << line << endl;
                    m_lastSubProgress = 100;
                    const pkgCache::VerIterator &ver = findTransactionPackage(pkg);
                    if (!ver.end()) {
                        emitPackage(ver, PK_INFO_ENUM_FINISHED);
                        //                         emitPackageProgress(ver, m_lastSubProgress);
                    }
                } else {
                    g_debug("apt-backend: >>>Unmaped dpkg status value: %s", line);
                }

                if (!starts_with(str, "Running")) {
                    m_lastPackage = pkg;
                }
                m_startCounting = true;
            } else {
                m_startCounting = true;
            }

            int val = atoi(percent);
            //cout << "progress: " << val << endl;
            pk_backend_job_set_percentage(m_job, val);

            // clean-up
            line[0] = 0;
        } else {
            buf[1] = 0;
            strcat(line, buf);
        }
    }

    time_t now = time(NULL);

    if (!m_startCounting) {
        usleep(100000);
        // wait until we get the first message from apt
        m_lastTermAction = now;
    }

    if ((now - m_lastTermAction) > m_terminalTimeout) {
        // get some debug info
        g_warning("no statusfd changes/content updates in terminal for %i"
                  " seconds",m_terminalTimeout);
        m_lastTermAction = time(NULL);
    }

    // sleep for a while to not obsess over it
    usleep(5000);
}

PkgList AptJob::resolvePackageIds(gchar **package_ids, PkBitfield filters)
{
    PkgList ret;

    pk_backend_job_set_status (m_job, PK_STATUS_ENUM_QUERY);

    // Don't fail if package list is empty
    if (package_ids == NULL)
        return ret;

    for (uint i = 0; i < g_strv_length(package_ids); ++i) {
        if (m_cancel)
            break;

        const gchar *pkgid = package_ids[i];

        // Check if it's a valid package id
        if (pk_package_id_check(pkgid) == false) {
            string name(pkgid);
            // Check if the package name didn't contains the arch field
            if (name.find(':') == std::string::npos) {
                // OK FindPkg is not suitable on muitarch without ":arch"
                // it can only return one package in this case we need to
                // search the whole package cache and match the package
                // name manually
                pkgCache::PkgIterator pkg;
                // Name can be supplied user input and may not be an actually valid id. In this
                // case FindGrp can come back with a bad group we shouldn't process any further
                // as results are undefined.
                pkgCache::GrpIterator grp = (*m_cache)->FindGrp(name);
                for (pkg = grp.PackageList(); grp.IsGood() && pkg.end() == false; pkg = grp.NextPkg(pkg)) {
                    if (m_cancel) {
                        break;
                    }

                    // Ignore packages that exist only due to dependencies.
                    if (pkg.VersionList().end() && pkg.ProvidesList().end()) {
                        continue;
                    }

                    const pkgCache::VerIterator &ver = m_cache->findVer(pkg);
                    // check to see if the provided package isn't virtual too
                    if (!ver.end())
                        ret.append(ver);

                    const pkgCache::VerIterator &candidateVer = m_cache->findCandidateVer(pkg);
                    // check to see if the provided package isn't virtual too
                    if (!candidateVer.end())
                        ret.append(candidateVer);
                }
            } else {
                const pkgCache::PkgIterator &pkg = (*m_cache)->FindPkg(name);
                // Ignore packages that could not be found or that exist only due to dependencies.
                if (pkg.end() == true || (pkg.VersionList().end() && pkg.ProvidesList().end())) {
                    continue;
                }

                const pkgCache::VerIterator &ver = m_cache->findVer(pkg);
                // check to see if the provided package isn't virtual too
                if (ver.end() == false)
                    ret.append(ver);

                const pkgCache::VerIterator &candidateVer = m_cache->findCandidateVer(pkg);
                // check to see if the provided package isn't virtual too
                if (candidateVer.end() == false)
                    ret.append(candidateVer);
            }
        } else {
            const PkgInfo &pkgi = m_cache->resolvePkgID(pkgid);
            // check to see if we found the package
            if (!pkgi.ver.end())
                ret.append(pkgi);
        }
    }

    return filterPackages(ret, filters);
}

void AptJob::refreshCache()
{
    pk_backend_job_set_status(m_job, PK_STATUS_ENUM_REFRESH_CACHE);

    if (m_cache->BuildSourceList() == false) {
        return;
    }

    // Create the progress
    AcqPackageKitStatus Stat(this);

    // do the work
    ListUpdate(Stat, *m_cache->GetSourceList());

    // Rebuild the cache.
    pkgCacheFile::RemoveCaches();
    if (m_cache->BuildCaches() == false) {
        return;
    }
}

void AptJob::markAutoInstalled(const PkgList &pkgs)
{
    for (const PkgInfo &pkInfo : pkgs) {
        if (m_cancel)
            break;

        // Mark package as auto-installed
        (*m_cache)->MarkAuto(pkInfo.ver.ParentPkg(), true);
    }
}

bool AptJob::markFileForInstall(std::string const &file)
{
    return m_cache->GetSourceList()->AddVolatileFile(file);
}

PkgList AptJob::resolveLocalFiles(gchar **localDebs)
{
    PkgList ret;
    for (guint i = 0; i < g_strv_length(localDebs); ++i) {
        pkgCache::PkgIterator const P = (*m_cache)->FindPkg(localDebs[i]);
        if (P.end()) {
            continue;
        }

        // Set any version providing the .deb as the candidate.
        for (auto Prv = P.ProvidesList(); Prv.end() == false; Prv++)
            ret.append(Prv.OwnerVer());

        // TODO do we need this?
        // via cacheset to have our usual virtual handling
        //APT::VersionContainerInterface::FromPackage(&(verset[MOD_INSTALL]), Cache, P, APT::CacheSetHelper::CANDIDATE, helper);
    }
    return ret;
}

bool AptJob::runTransaction(const PkgList &install, const PkgList &remove, const PkgList &update,
                             bool fixBroken, PkBitfield flags, bool autoremove)
{
    pk_backend_job_set_status (m_job, PK_STATUS_ENUM_RUNNING);

    // Enter the special broken fixing mode if the user specified arguments
    // THIS mode will run if fixBroken is false and the cache has broken packages
    bool attemptFixBroken = false;
    if ((*m_cache)->BrokenCount() != 0) {
        attemptFixBroken = true;
    }

    pkgProblemResolver Fix(*m_cache);

    // TODO: could use std::bind an have a generic operation array iff toRemove had the same
    //       signature

    struct Operation {
        const PkgList &list;
        const bool preserveAuto;
    };

    // Calculate existing garbage before the transaction
    PkgList initial_garbage;
    if (autoremove) {
        for (pkgCache::PkgIterator pkg = (*m_cache)->PkgBegin(); ! pkg.end(); ++pkg) {
            const pkgCache::VerIterator &ver = pkg.CurrentVer();
            if (!ver.end() && m_cache->isGarbage(pkg))
                initial_garbage.append(ver);
        }
    }

    // new scope for the ActionGroup
    {
        pkgDepCache::ActionGroup group(*m_cache);

        for (auto op : { Operation { install, false }, Operation { update, true } }) {
            // We first need to mark all manual selections with AutoInst=false, so they influence which packages
            // are chosen when resolving dependencies.
            // Consider A depends X|Y, with installation of A,Y requested.
            // With just one run and AutoInst=true, A would be marked for install, it would auto-install X;
            // then Y is marked for install, and we end up with both X and Y marked for install.
            // With two runs (one without AutoInst and one with AutoInst), we first mark A and Y for install.
            // In the 2nd run, when resolving X|Y APT notices that X is already marked for install, and does not install Y.
            for (auto autoInst : { false, true }) {
                for (const PkgInfo &pkInfo : op.list) {
                    if (m_cancel) {
                        break;
                    }
                    if (!m_cache->tryToInstall(Fix,
                                               pkInfo,
                                               autoInst,
                                               op.preserveAuto,
                                               attemptFixBroken)) {
                        return false;
                    }
                }
            }
        }

        for (const PkgInfo &pkInfo : remove) {
            if (m_cancel)
                break;

            m_cache->tryToRemove(Fix, pkInfo);
        }

        // Call the scored problem resolver
        if (Fix.Resolve(true) == false) {
            _error->Discard();
        }

        // Now we check the state of the packages,
        if ((*m_cache)->BrokenCount() != 0) {
            // if the problem resolver could not fix all broken things
            // suggest to run RepairSystem by saying that the last transaction
            // did not finish well
            m_cache->ShowBroken(false, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED);
            return false;
        }
    }

    // Remove new garbage that is created
    if (autoremove) {
        for (pkgCache::PkgIterator pkg = (*m_cache)->PkgBegin(); ! pkg.end(); ++pkg) {
            const pkgCache::VerIterator &ver = pkg.CurrentVer();
            if (!ver.end() && !initial_garbage.contains(pkg) && m_cache->isGarbage(pkg))
                m_cache->tryToRemove (Fix, PkgInfo(ver));
        }
    }

    // Prepare for the restart thing
    struct stat restartStatStart;
    if (g_file_test(REBOOT_REQUIRED_FILE, G_FILE_TEST_EXISTS)) {
        g_stat(REBOOT_REQUIRED_FILE, &restartStatStart);
    }

    // If we are simulating the install packages
    // will just calculate the trusted packages
    const auto ret = installPackages(flags);

    if (g_file_test(REBOOT_REQUIRED_FILE, G_FILE_TEST_EXISTS)) {
        struct stat restartStat;
        g_stat(REBOOT_REQUIRED_FILE, &restartStat);

        if (restartStat.st_mtime > restartStatStart.st_mtime) {
            // Emit the packages that caused the restart
            if (!m_restartPackages.empty()) {
                emitRequireRestart(m_restartPackages);
            } else if (!m_pkgs.empty()) {
                // Assume all of them
                emitRequireRestart(m_pkgs);
            } else {
                // Emit a foo require restart
                pk_backend_job_require_restart(m_job, PK_RESTART_ENUM_SYSTEM, "apt-backend;;;");
            }
        }
    }

    return ret;
}

/**
 * InstallPackages - Download and install the packages
 *
 * This displays the informative messages describing what is going to
 * happen and then calls the download routines
 */
bool AptJob::installPackages(PkBitfield flags)
{
    bool simulate = pk_bitfield_contain(flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE);
    PkBackend *backend = PK_BACKEND(pk_backend_job_get_backend(m_job));

    //cout << "installPackages() called" << endl;

    // check for essential packages!!!
    if (m_cache->isRemovingEssentialPackages()) {
        return false;
    }

    // Sanity check
    if ((*m_cache)->BrokenCount() != 0) {
        // TODO
        m_cache->ShowBroken(false);
        _error->Error("Internal error, InstallPackages was called with broken packages!");
        return false;
    }

    if ((*m_cache)->DelCount() == 0 && (*m_cache)->InstCount() == 0 &&
            (*m_cache)->BadCount() == 0) {
        return true;
    }

    // Create the download object
    AcqPackageKitStatus Stat(this);

    // get a fetcher
    pkgAcquire fetcher(&Stat);
    if (!simulate) {
        // Only lock the archive directory if we will download
        if (fetcher.GetLock(_config->FindDir("Dir::Cache::Archives")) == false) {
            return false;
        }
    }

    // Read the source list
    if (m_cache->BuildSourceList() == false) {
        return false;
    }

    // Create the package manager and prepare to download
    std::unique_ptr<pkgPackageManager> PM (_system->CreatePM(*m_cache));
    if (!PM->GetArchives(&fetcher, m_cache->GetSourceList(), m_cache->GetPkgRecords()) ||
            _error->PendingError() == true) {
        return false;
    }

    // Display statistics
    unsigned long long FetchBytes = fetcher.FetchNeeded();
    unsigned long long FetchPBytes = fetcher.PartialPresent();
    unsigned long long DebBytes = fetcher.TotalNeeded();
    if (DebBytes != (*m_cache)->DebSize()) {
        g_debug ("%lld, %lld: How odd.. The sizes didn't match, email apt@packages.debian.org",
                 DebBytes, (*m_cache)->DebSize());
    }

    // Number of bytes
    if (FetchBytes != 0) {
        // Emit the remainig download size
        pk_backend_job_set_download_size_remaining(m_job, FetchBytes);

        // check network state if we are going to download
        // something or if we are not simulating
        if (!simulate && !pk_backend_is_online(backend)) {
            pk_backend_job_error_code(m_job,
                                      PK_ERROR_ENUM_NO_NETWORK,
                                      "Cannot download packages whilst offline");
            return false;
        }
    }

    /* Check for enough free space */
    struct statvfs Buf;
    string OutputDir = _config->FindDir("Dir::Cache::Archives");
    if (statvfs(OutputDir.c_str(),&Buf) != 0) {
        return _error->Errno("statvfs",
                             "Couldn't determine free space in %s",
                             OutputDir.c_str());
    }
    if (unsigned(Buf.f_bfree) < (FetchBytes - FetchPBytes)/Buf.f_bsize) {
        struct statfs Stat;
        if (statfs(OutputDir.c_str(), &Stat) != 0 ||
                unsigned(Stat.f_type) != RAMFS_MAGIC) {
            pk_backend_job_error_code(m_job,
                                      PK_ERROR_ENUM_NO_SPACE_ON_DEVICE,
                                      "You don't have enough free space in %s",
                                      OutputDir.c_str());
            return false;
        }
    }

    if (_error->PendingError() == true) {
        g_debug("PendingError");
        return false;
    }

    // Make sure we are not installing any untrusted package is untrusted is not set
    if (!checkTrusted(fetcher, flags) && !simulate) {
        return false;
    }

    if (simulate) {
        // Print out a list of packages that are going to be installed extra
        checkChangedPackages(true);

        return true;
    } else {
        // Store the packages that are going to change
        // so we can emit them as we process it
        m_pkgs = checkChangedPackages(false);
    }

    // Download and check if we can continue
    if (fetcher.Run() != pkgAcquire::Continue
            && m_cancel == false) {
        // We failed and we did not cancel
        show_errors(m_job, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED);
        return false;
    }

    if (_error->PendingError() == true) {
        g_debug("PendingError download");
        return false;
    }

    // Download finished, check if we should proceed the install
    if (pk_bitfield_contain(flags, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD)) {
        return true;
    }

    // Check if the user canceled
    if (m_cancel) {
        return true;
    }

    // Right now it's not safe to cancel
    pk_backend_job_set_allow_cancel(m_job, false);

    // Download should be finished by now, changing it's status
    pk_backend_job_set_percentage(m_job, PK_BACKEND_PERCENTAGE_INVALID);

    // we could try to see if this is the case
    g_setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", TRUE);
    _system->UnLockInner();

    pkgPackageManager::OrderResult res;
    res = PM->DoInstallPreFork();
    if (res == pkgPackageManager::Failed) {
        g_warning ("Failed to prepare installation");
        show_errors(m_job, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED);
        return false;
    }

    // File descriptors for reading dpkg --status-fd
    int readFromChildFD[2];
    if (pipe(readFromChildFD) < 0) {
        g_warning("Failed to create a pipe");
        return false;
    }

    int pty_master;
    m_child_pid = forkpty(&pty_master, NULL, NULL, NULL);
    if (m_child_pid == -1) {
        return false;
    }

    if (m_child_pid == 0) {
        //cout << "FORKED: installPackages(): DoInstall" << endl;

        // ensure that this process dies with its parent
        prctl(PR_SET_PDEATHSIG, SIGKILL);

        // close pipe we don't need
        close(readFromChildFD[0]);

        // Change the locale to not get libapt localization
        setlocale(LC_ALL, "C.UTF-8");
        g_setenv("LANG", "C.UTF-8", TRUE);
        g_setenv("LANGUAGE", "C.UTF-8", TRUE);

        // Debconf handling
        const gchar *socket = pk_backend_job_get_frontend_socket(m_job);
        if ((m_interactive) && (socket != NULL)) {
            g_setenv("DEBIAN_FRONTEND", "passthrough", TRUE);
            g_setenv("DEBCONF_PIPE", socket, TRUE);

            // Set the LANGUAGE so debconf messages get localization
            // NOTE: This will cause dpkg messages to be localized and the APT backend's string matching
            // to fail, so progress information may no longer be accurate in these cases.
            setEnvLocaleFromJob();
        } else {
            // we don't have a socket set or are not interactive, let's fallback to noninteractive
            g_setenv("DEBIAN_FRONTEND", "noninteractive", TRUE);
        }

        // apt will record this in its history.log
        guint uid = pk_backend_job_get_uid(m_job);
        if (uid > 0) {
            gchar buf[16];
            snprintf(buf, sizeof(buf), "%d", uid);
            g_setenv("PACKAGEKIT_CALLER_UID", buf, TRUE);
        }

        PkRoleEnum role = pk_backend_job_get_role(m_job);
        gchar *cmd = g_strdup_printf("packagekit role='%s'", pk_role_enum_to_string(role));
        _config->Set("CommandLine::AsString", cmd);
        g_free(cmd);

        // Pass the write end of the pipe to the install function
        auto *progress = new Progress::PackageManagerProgressFd(readFromChildFD[1]);
        res = PM->DoInstallPostFork(progress);
        delete progress;

        // dump errors into cerr (pass it to the parent process)
        _error->DumpErrors();

        // finishes the child process, _exit is used to not
        // close some parent file descriptors
        _exit(res);
    }

    g_debug("apt-backend parent process running...");

    // make it nonblocking, very important otherwise
    // when the child finish we stay stuck.
    fcntl(readFromChildFD[0], F_SETFL, O_NONBLOCK);
    fcntl(pty_master, F_SETFL, O_NONBLOCK);

    // init the timer
    m_lastTermAction = time(NULL);
    m_startCounting = false;

    // process messages from child
    int ret = 0;
    char masterbuf[1024];
    std::string errorLogTail = "";
    bool errorEmitted = false;
    bool childTerminated = false;
    while (true) {
        while (true) {
            int bufLen = read(pty_master, masterbuf, sizeof(masterbuf));
            if (bufLen <= 0)
                break;
            masterbuf[bufLen] = '\0';
            errorLogTail.append(masterbuf);
            if (errorLogTail.length() > 2048)
                errorLogTail.erase(0, errorLogTail.length() - 2048);
        }

        // don't continue if the child terminated previously
        if (childTerminated)
            break;

        // try to parse dpkg status
        updateInterface(readFromChildFD[0], pty_master, &errorEmitted);

        // Check if the child died
        if (waitpid(m_child_pid, &ret, WNOHANG) != 0)
            childTerminated = true; // one last round to read remaining output
    }

    close(readFromChildFD[0]);
    close(readFromChildFD[1]);
    close(pty_master);
    _system->LockInner();

    g_debug("apt-backend parent process finished: %d", ret);

    if (ret != 0 && !m_cancel && !errorEmitted) {
        // If the child died with a non-zero exit code, and we didn't deliberately
        // kill it in a cancel operation and we didn't already emit an error,
        // we still need to find out what went wrong to present a message to the user.
        // Let's see if we can find any kind of not overlay verbose information to display.

        std::stringstream ss(errorLogTail);
        std::string line;
        std::string shortErrorLog = "";
        while(std::getline(ss, line, '\n')) {
            if (g_str_has_prefix (line.c_str(), "E:"))
                shortErrorLog.append("\n" + line);
        }

        if (shortErrorLog.empty()) {
            if (errorLogTail.length() > 1200)
                errorLogTail.erase(0, errorLogTail.length() - 1200);
            std::string logExcerpt = errorLogTail.substr(errorLogTail.find("\n") + 1, errorLogTail.length());
            logExcerpt = logExcerpt.empty()? "No log generated. Check `/var/log/apt/term.log`!" : "\n" + logExcerpt;
            pk_backend_job_error_code(m_job,
                                      PK_ERROR_ENUM_TRANSACTION_ERROR,
                                      "Error while running dpkg. Log excerpt: %s",
                                       logExcerpt.c_str());
        } else {
            pk_backend_job_error_code(m_job,
                                      PK_ERROR_ENUM_TRANSACTION_ERROR,
                                      "Error while running the transaction: %s",
                                      shortErrorLog.c_str());
        }
        return false;
    }

    return true;
}
