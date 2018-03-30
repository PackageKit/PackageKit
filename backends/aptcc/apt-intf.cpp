/* apt-intf.cpp
 *
 * Copyright (c) 1999-2008 Daniel Burrows
 * Copyright (c) 2004 Michael Vogt <mvo@debian.org>
 *               2009-2016 Daniel Nicoletti <dantti12@gmail.com>
 *               2012-2015 Matthias Klumpp <matthias@tenstral.net>
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

#include "apt-intf.h"

#include <apt-pkg/init.h>
#include <apt-pkg/error.h>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/version.h>

#include <appstream.h>

#include <sys/statvfs.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <pty.h>

#include <iostream>
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

AptIntf::AptIntf(PkBackendJob *job) :
    m_job(job),
    m_cancel(false),
    m_terminalTimeout(120),
    m_lastSubProgress(0),
    m_cache(0)
{
    m_cancel = false;
}

bool AptIntf::init(gchar **localDebs)
{
    const gchar *locale;
    const gchar *http_proxy;
    const gchar *ftp_proxy;

    m_isMultiArch = APT::Configuration::getArchitectures(false).size() > 1;

    // set locale
    if (locale = pk_backend_job_get_locale(m_job)) {
        setlocale(LC_ALL, locale);
        // TODO why this cuts characters on ui?
        // 		string _locale(locale);
        // 		size_t found;
        // 		found = _locale.find('.');
        // 		_locale.erase(found);
        // 		_config->Set("APT::Acquire::Translation", _locale);
    }

    // set http proxy
    http_proxy = pk_backend_job_get_proxy_http(m_job);
    if (http_proxy != NULL)
        setenv("http_proxy", http_proxy, 1);

    // set ftp proxy
    ftp_proxy = pk_backend_job_get_proxy_ftp(m_job);
    if (ftp_proxy != NULL)
        setenv("ftp_proxy", ftp_proxy, 1);

    // Check if we should open the Cache with lock
    bool withLock;
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
        for (int i = 0; i < g_strv_length(localDebs); ++i) {
            markFileForInstall(localDebs[i]);
        }
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
        _config->Set("Dpkg::Options::", "--force-confdef");
        _config->Set("Dpkg::Options::", "--force-confold");
        // Ensure nothing interferes with questions
        setenv("APT_LISTCHANGES_FRONTEND", "none", 1);
        setenv("APT_LISTBUGS_FRONTEND", "none", 1);
    }

    // Check if there are half-installed packages and if we can fix them
    return m_cache->CheckDeps(AllowBroken);
}

AptIntf::~AptIntf()
{
    delete m_cache;
}

void AptIntf::cancel()
{
    if (!m_cancel) {
        m_cancel = true;
        pk_backend_job_set_status(m_job, PK_STATUS_ENUM_CANCEL);
    }

    if (m_child_pid > 0) {
        kill(m_child_pid, SIGTERM);
    }
}

bool AptIntf::cancelled() const
{
    return m_cancel;
}

bool AptIntf::matchPackage(const pkgCache::VerIterator &ver, PkBitfield filters)
{
    if (filters != 0) {
        const pkgCache::PkgIterator &pkg = ver.ParentPkg();
        bool installed = false;

        // Check if the package is installed
        if (pkg->CurrentState == pkgCache::State::Installed && pkg.CurrentVer() == ver) {
            installed = true;
        }

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

        if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED) && installed) {
            return false;
        } else if (pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED) && !installed) {
            return false;
        }

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

PkgList AptIntf::filterPackages(const PkgList &packages, PkBitfield filters)
{
    if (filters != 0) {
        PkgList ret;
        ret.reserve(packages.size());

        for (const pkgCache::VerIterator &ver : packages) {
            if (matchPackage(ver, filters)) {
                ret.push_back(ver);
            }
        }

        // This filter is more complex so we filter it after the list has shrunk
        if (pk_bitfield_contain(filters, PK_FILTER_ENUM_DOWNLOADED) && ret.size() > 0) {
            PkgList downloaded;

            pkgProblemResolver Fix(*m_cache);
            {
                pkgDepCache::ActionGroup group(*m_cache);
                for (auto autoInst : { true, false }) {
                    for (const pkgCache::VerIterator &ver : ret) {
                        if (m_cancel) {
                            break;
                        }

                        m_cache->tryToInstall(Fix, ver, false, autoInst, false);
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

            for (const pkgCache::VerIterator &verIt : ret) {
                bool found = false;
                for (pkgAcquire::ItemIterator it = fetcher.ItemsBegin(); it < fetcher.ItemsEnd(); ++it) {
                    pkgAcqArchiveSane *archive = static_cast<pkgAcqArchiveSane*>(dynamic_cast<pkgAcqArchive*>(*it));
                    if (archive == nullptr) {
                        continue;
                    }
                    const pkgCache::VerIterator ver = archive->version();
                    if ((*it)->Local && verIt == ver) {
                        found = true;
                        break;
                    }
                }

                if (found) {
                    downloaded.push_back(verIt);
                }
            }

            return downloaded;
        }

        return ret;
    } else {
        return packages;
    }
}

// used to emit packages it collects all the needed info
void AptIntf::emitPackage(const pkgCache::VerIterator &ver, PkInfoEnum state)
{
    // check the state enum to see if it was not set.
    if (state == PK_INFO_ENUM_UNKNOWN) {
        const pkgCache::PkgIterator &pkg = ver.ParentPkg();

        if (pkg->CurrentState == pkgCache::State::Installed &&
                pkg.CurrentVer() == ver) {
            state = PK_INFO_ENUM_INSTALLED;
        } else {
            state = PK_INFO_ENUM_AVAILABLE;
        }
    }

    gchar *package_id;
    package_id = utilBuildPackageId(ver);
    pk_backend_job_package(m_job,
                           state,
                           package_id,
                           m_cache->getShortDescription(ver).c_str());
    g_free(package_id);
}

void AptIntf::emitPackageProgress(const pkgCache::VerIterator &ver, PkStatusEnum status, uint percentage)
{
    gchar *package_id;
    package_id = utilBuildPackageId(ver);
    pk_backend_job_set_item_progress(m_job, package_id, status, percentage);
    g_free(package_id);
}

void AptIntf::emitPackages(PkgList &output, PkBitfield filters, PkInfoEnum state)
{
    // Sort so we can remove the duplicated entries
    output.sort();

    // Remove the duplicated entries
    output.removeDuplicates();

    output = filterPackages(output, filters);
    for (const pkgCache::VerIterator &verIt : output) {
        if (m_cancel) {
            break;
        }

        emitPackage(verIt, state);
    }
}

void AptIntf::emitRequireRestart(PkgList &output)
{
    // Sort so we can remove the duplicated entries
    output.sort();

    // Remove the duplicated entries
    output.removeDuplicates();

    for (const pkgCache::VerIterator &verIt : output) {
        gchar *package_id;
        package_id = utilBuildPackageId(verIt);
        pk_backend_job_require_restart(m_job, PK_RESTART_ENUM_SYSTEM, package_id);
        g_free(package_id);
    }
}

void AptIntf::emitUpdates(PkgList &output, PkBitfield filters)
{
    PkInfoEnum state;
    // Sort so we can remove the duplicated entries
    output.sort();

    // Remove the duplicated entries
    output.removeDuplicates();

    output = filterPackages(output, filters);
    for (const pkgCache::VerIterator &verIt : output) {
        if (m_cancel) {
            break;
        }

        // the default update info
        state = PK_INFO_ENUM_NORMAL;

        // let find what kind of upgrade this is
        pkgCache::VerFileIterator vf = verIt.FileList();
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
            } else if (ends_with(archive, "-updates")) {
                state = PK_INFO_ENUM_BUGFIX;
            }
        } else if (origin.compare("Backports.org archive") == 0 ||
                   ends_with(origin, "-backports")) {
            state = PK_INFO_ENUM_ENHANCEMENT;
        }

        emitPackage(verIt, state);
    }
}

// search packages which provide a codec (specified in "values")
void AptIntf::providesCodec(PkgList &output, gchar **values)
{
    string arch;
    GstMatcher *matcher = new GstMatcher(values);
    if (!matcher->hasMatches()) {
        return;
    }

    for (pkgCache::PkgIterator pkg = m_cache->GetPkgCache()->PkgBegin(); !pkg.end(); ++pkg) {
        if (m_cancel) {
            delete matcher;
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
        arch = string(ver.Arch());
        if (ver.end() == true) {
            ver = m_cache->findCandidateVer(pkg);
            if (ver.end() == true) {
                continue;
            }
        }

        pkgCache::VerFileIterator vf = ver.FileList();
        pkgRecords::Parser &rec = m_cache->GetPkgRecords()->Lookup(vf);
        const char *start, *stop;
        rec.GetRec(start, stop);
        string record(start, stop - start);
        if (matcher->matches(record, arch)) {
            output.push_back(ver);
        }
    }

    delete matcher;
}

// search packages which provide the libraries specified in "values"
void AptIntf::providesLibrary(PkgList &output, gchar **values)
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
            if ((pos != string::npos) && (pos > 0)) {
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
                    output.push_back(ver);
                }
            }
        } else {
            g_debug("libmatcher: Did not match: %s", value);
        }
    }
}

// Mostly copied from pkgAcqArchive.
bool AptIntf::getArchive(pkgAcquire *Owner,
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

AptCacheFile* AptIntf::aptCacheFile() const
{
    return m_cache;
}

// used to emit packages it collects all the needed info
void AptIntf::emitPackageDetail(const pkgCache::VerIterator &ver)
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

    gchar *package_id;
    package_id = utilBuildPackageId(ver);
    pk_backend_job_details(m_job,
                           package_id,
                           m_cache->getShortDescription(ver).c_str(),
                           "unknown",
                           get_enum_group(section),
                           m_cache->getLongDescriptionParsed(ver).c_str(),
                           rec.Homepage().c_str(),
                           size);

    g_free(package_id);
}

void AptIntf::emitDetails(PkgList &pkgs)
{
    // Sort so we can remove the duplicated entries
    pkgs.sort();

    // Remove the duplicated entries
    pkgs.removeDuplicates();

    for (const pkgCache::VerIterator &verIt : pkgs) {
        if (m_cancel) {
            break;
        }

        emitPackageDetail(verIt);
    }
}

// used to emit packages it collects all the needed info
void AptIntf::emitUpdateDetail(const pkgCache::VerIterator &candver)
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
    gchar *current_package_id = utilBuildPackageId(currver);

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
        AcqPackageKitStatus Stat(this, m_job);

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
    gchar *package_id;
    package_id = utilBuildPackageId(candver);

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

    gchar **updates;
    updates = (gchar **) g_malloc(2 * sizeof(gchar *));
    updates[0] = current_package_id;
    updates[1] = NULL;

    GPtrArray *bugzilla_urls;
    GPtrArray *cve_urls;
    bugzilla_urls = getBugzillaUrls(changelog);
    cve_urls = getCVEUrls(changelog);

    pk_backend_job_update_detail(m_job,
                                 package_id,
                                 updates,//const gchar *updates
                                 NULL,//const gchar *obsoletes
                                 NULL,//const gchar *vendor_url
                                 (gchar **) bugzilla_urls->pdata,// gchar **bugzilla_urls
                                 (gchar **) cve_urls->pdata,// gchar **cve_urls
                                 restart,//PkRestartEnum restart
                                 update_text.c_str(),//const gchar *update_text
                                 changelog.c_str(),//const gchar *changelog
                                 updateState,//PkUpdateStateEnum state
                                 issued.c_str(), //const gchar *issued_text
                                 updated.c_str() //const gchar *updated_text
                                 );

    g_free(package_id);
    g_strfreev(updates);
    g_ptr_array_unref(bugzilla_urls);
    g_ptr_array_unref(cve_urls);
}

void AptIntf::emitUpdateDetails(const PkgList &pkgs)
{
    for (const pkgCache::VerIterator &verIt : pkgs) {
        if (m_cancel) {
            break;
        }

        emitUpdateDetail(verIt);
    }
}

void AptIntf::getDepends(PkgList &output,
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
                    output.push_back(ver);
                    getDepends(output, ver, recursive);
                }
            } else {
                output.push_back(ver);
            }
        }
        dep++;
    }
}

void AptIntf::getRequires(PkgList &output,
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
            for (const pkgCache::VerIterator &depVer : deps) {
                if (depVer == ver) {
                    if (recursive) {
                        if (!output.contains(parentPkg)) {
                            output.push_back(parentVer);
                            getRequires(output, parentVer, recursive);
                        }
                    } else {
                        output.push_back(parentVer);
                    }
                    break;
                }
            }
        }
    }
}

PkgList AptIntf::getPackages()
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
        if (ver.end() == false) {
            output.push_back(ver);
        }
    }
    return output;
}

PkgList AptIntf::getPackagesFromRepo(SourcesList::SourceRecord *&rec)
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

        output.push_back(ver);
    }
    return output;
}

PkgList AptIntf::getPackagesFromGroup(gchar **values)
{
    pk_backend_job_set_status(m_job, PK_STATUS_ENUM_QUERY);

    PkgList output;
    vector<PkGroupEnum> groups;

    int len = g_strv_length(values);
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
                    output.push_back(ver);
                    break;
                }
            }
        }
    }
    return output;
}

bool AptIntf::matchesQueries(const vector<string> &queries, string s) {
    for (string query : queries) {
        // Case insensitive "string.contains"
        auto it = std::search(
            s.begin(), s.end(),
            query.begin(), query.end(),
            [](char ch1, char ch2) {
                return std::tolower(static_cast<unsigned char>(ch1)) == std::tolower(static_cast<unsigned char>(ch2));
            }
        );

        if (it != s.end()) {
            return true;
        }
    }
    return false;
}

PkgList AptIntf::searchPackageName(const vector<string> &queries)
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
                output.push_back(ver);
            } else {
                // iterate over the provides list
                for (pkgCache::PrvIterator Prv = pkg.ProvidesList(); Prv.end() == false; ++Prv) {
                    const pkgCache::VerIterator &ownerVer = m_cache->findVer(Prv.OwnerPkg());

                    // check to see if the provided package isn't virtual too
                    if (ownerVer.end() == false) {
                        // we add the package now because we will need to
                        // remove duplicates later anyway
                        output.push_back(ownerVer);
                    }
                }
            }
        }
    }
    return output;
}

PkgList AptIntf::searchPackageDetails(const vector<string> &queries)
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
                output.push_back(ver);
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
                    output.push_back(ownerVer);
                }
            }
        }
    }
    return output;
}

// used to return files it reads, using the info from the files in /var/lib/dpkg/info/
PkgList AptIntf::searchPackageFiles(gchar **values)
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
            search.append("|");
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
        output.push_back(ver);
    }

    return output;
}

PkgList AptIntf::getUpdates(PkgList &blocked, PkgList &downgrades)
{
    PkgList updates;

    if (m_cache->DistUpgrade() == false) {
        m_cache->ShowBroken(false);
        g_debug("Internal error, DistUpgrade broke stuff");
        cout << "Internal error, DistUpgrade broke stuff" << endl;
        return updates;
    }

    for (pkgCache::PkgIterator pkg = (*m_cache)->PkgBegin(); !pkg.end(); ++pkg) {
        const auto &state = (*m_cache)[pkg];
        if (pkg->SelectedState == pkgCache::State::Hold) {
            // We pretend held packages are not upgradable at all since we can't represent
            // the concept of holds in PackageKit.
            // https://github.com/hughsie/PackageKit/issues/120
            continue;
        } else if (state.Upgrade() == true && state.NewInstall() == false) {
            const pkgCache::VerIterator &ver = m_cache->findCandidateVer(pkg);
            if (!ver.end()) {
                updates.push_back(ver);
            }
        } else if (state.Downgrade() == true) {
            const pkgCache::VerIterator &ver = m_cache->findCandidateVer(pkg);
            if (!ver.end()) {
                downgrades.push_back(ver);
            }
        } else if (state.Upgradable() == true &&
                   pkg->CurrentVer != 0 &&
                   state.Delete() == false) {
            const pkgCache::VerIterator &ver = m_cache->findCandidateVer(pkg);
            if (!ver.end()) {
                blocked.push_back(ver);
            }
        }
    }

    return updates;
}

// used to return files it reads, using the info from the files in /var/lib/dpkg/info/
void AptIntf::providesMimeType(PkgList &output, gchar **values)
{
    g_autoptr(AsPool) pool = NULL;
    g_autoptr(GError) error = NULL;
    guint i;
    vector<string> packages;

    pool = as_pool_new ();
    as_pool_load (pool, NULL, &error);
    if (error != NULL) {
        /* we do not fail here because even with error we might still find metadata */
        g_warning ("Issue while loading the AppStream metadata pool: %s", error->message);
        g_error_free (error);
        error = NULL;
    }

    for (i = 0; values[i] != NULL; i++) {
        g_autoptr(GPtrArray) result = NULL;
        guint j;
        if (m_cancel)
            break;

        result = as_pool_get_components_by_provided_item (pool, AS_PROVIDED_KIND_MIMETYPE, values[i]);
        for (j = 0; j < result->len; j++) {
            AsComponent *cpt = AS_COMPONENT (g_ptr_array_index (result, j));
            /* we only select one package per component - on Debian systems, AppStream components never reference multiple packages */
            packages.push_back (as_component_get_pkgname (cpt));
        }
    }

    /* resolve the package names */
    for (const string &package : packages) {
        if (m_cancel)
            break;

        const pkgCache::PkgIterator &pkg = (*m_cache)->FindPkg(package);
        if (pkg.end() == true)
            continue;
        const pkgCache::VerIterator &ver = m_cache->findVer(pkg);
        if (ver.end() == true)
            continue;

        output.push_back(ver);
    }

    /* check if we found nothing because AppStream data is missing completely */
    if (output.empty()) {
        g_autoptr(GPtrArray) all_cpts = as_pool_get_components (pool);
        if (all_cpts->len <= 0) {
            pk_backend_job_error_code(m_job,
                                      PK_ERROR_ENUM_INTERNAL_ERROR,
                                      "No AppStream metadata was found. This means we are unable to find any information for your request.");
	}
    }
}

bool AptIntf::isApplication(const pkgCache::VerIterator &ver)
{
    bool ret = false;
    gchar *fileName;
    string line;

    if (m_isMultiArch) {
        fileName = g_strdup_printf("/var/lib/dpkg/info/%s:%s.list",
                                   ver.ParentPkg().Name(),
                                   ver.Arch());
        if (!FileExists(fileName)) {
            g_free(fileName);
            // if the file was not found try without the arch field
            fileName = g_strdup_printf("/var/lib/dpkg/info/%s.list",
                                       ver.ParentPkg().Name());
        }
    } else {
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
void AptIntf::emitPackageFiles(const gchar *pi)
{
    GPtrArray *files;
    string line;
    gchar **parts;

    parts = pk_package_id_split(pi);

    string fName;
    if (m_isMultiArch) {
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
    } else {
        fName = "/var/lib/dpkg/info/" +
                string(parts[PK_PACKAGE_ID_NAME]) +
                ".list";
    }
    g_strfreev (parts);

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

void AptIntf::emitPackageFilesLocal(const gchar *file)
{
    DebFile deb(file);
    if (!deb.isValid()){
        return;
    }

    gchar *package_id;
    package_id = pk_package_id_build(deb.packageName().c_str(),
                                     deb.version().c_str(),
                                     deb.architecture().c_str(),
                                     file);

    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
    for (auto file : deb.files()) {
        g_ptr_array_add(files, g_strdup(file.c_str()));
    }
    g_ptr_array_add(files, NULL);
    pk_backend_job_files(m_job, package_id, (gchar **) files->pdata);

    g_ptr_array_unref(files);
}

/**
  * Check if package is officially supported by the current distribution
  */
bool AptIntf::packageIsSupported(const pkgCache::VerIterator &verIter, string component)
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
    AcqPackageKitStatus Stat(this, m_job);
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

bool AptIntf::checkTrusted(pkgAcquire &fetcher, PkBitfield flags)
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
            untrusted.push_back(archive->version());

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
PkgList AptIntf::checkChangedPackages(bool emitChanged)
{
    PkgList ret;
    PkgList installing;
    PkgList removing;
    PkgList updating;
    PkgList downgrading;

    for (pkgCache::PkgIterator pkg = (*m_cache)->PkgBegin(); ! pkg.end(); ++pkg) {
        if ((*m_cache)[pkg].NewInstall() == true) {
            // installing;
            const pkgCache::VerIterator &ver = m_cache->findCandidateVer(pkg);
            if (!ver.end()) {
                ret.push_back(ver);
                installing.push_back(ver);

                // append to the restart required list
                if (utilRestartRequired(pkg.Name())) {
                    m_restartPackages.push_back(ver);
                }
            }
        } else if ((*m_cache)[pkg].Delete() == true) {
            // removing
            const pkgCache::VerIterator &ver = m_cache->findVer(pkg);
            if (!ver.end()) {
                ret.push_back(ver);
                removing.push_back(ver);

                // append to the restart required list
                if (utilRestartRequired(pkg.Name())) {
                    m_restartPackages.push_back(ver);
                }
            }
        } else if ((*m_cache)[pkg].Upgrade() == true) {
            // updating
            const pkgCache::VerIterator &ver = m_cache->findCandidateVer(pkg);
            if (!ver.end()) {
                ret.push_back(ver);
                updating.push_back(ver);

                // append to the restart required list
                if (utilRestartRequired(pkg.Name())) {
                    m_restartPackages.push_back(ver);
                }
            }
        } else if ((*m_cache)[pkg].Downgrade() == true) {
            // downgrading
            const pkgCache::VerIterator &ver = m_cache->findVer(pkg);
            if (!ver.end()) {
                ret.push_back(ver);
                downgrading.push_back(ver);

                // append to the restart required list
                if (utilRestartRequired(pkg.Name())) {
                    m_restartPackages.push_back(ver);
                }
            }
        }
    }

    if (emitChanged) {
        // emit packages that have changes
        emitPackages(removing,    PK_FILTER_ENUM_NONE, PK_INFO_ENUM_REMOVING);
        emitPackages(downgrading, PK_FILTER_ENUM_NONE, PK_INFO_ENUM_DOWNGRADING);
        emitPackages(installing,  PK_FILTER_ENUM_NONE, PK_INFO_ENUM_INSTALLING);
        emitPackages(updating,    PK_FILTER_ENUM_NONE, PK_INFO_ENUM_UPDATING);
    }

    return ret;
}

pkgCache::VerIterator AptIntf::findTransactionPackage(const std::string &name)
{
    for (const pkgCache::VerIterator &verIt : m_pkgs) {
        if (verIt.ParentPkg().Name() == name) {
            return verIt;
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

void AptIntf::updateInterface(int fd, int writeFd)
{
    char buf[2];
    static char line[1024] = "";

    while (1) {
        // This algorithm should be improved (it's the same as the rpm one ;)
        int len = read(fd, buf, 1);

        // nothing was read
        if(len < 1) {
            break;
        }

        // update the time we last saw some action
        m_lastTermAction = time(NULL);

        if( buf[0] == '\n') {
            if (m_cancel) {
                kill(m_child_pid, SIGTERM);
            }
            //cout << "got line: " << line << endl;

            gchar **split  = g_strsplit(line, ":",5);
            gchar *status  = g_strstrip(split[0]);
            gchar *pkg     = g_strstrip(split[1]);
            gchar *percent = g_strstrip(split[2]);
            gchar *str     = g_strdup(g_strstrip(split[3]));

            // major problem here, we got unexpected input. should _never_ happen
            if(!(pkg && status)) {
                continue;
            }

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
                                          str);
            } else if (strstr(status, "pmconffile") != NULL) {
                // conffile-request from dpkg, needs to be parsed different
                int i=0;
                int count=0;
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
                filename = g_build_filename(DATADIR, "PackageKit", "helpers", "aptcc", "pkconffile", NULL);
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
                    cout << ">>>Unmaped value<<< :" << line << endl;
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
            g_strfreev(split);
            g_free(str);
            line[0] = 0;
        } else {
            buf[1] = 0;
            strcat(line, buf);
        }
    }

    time_t now = time(NULL);

    if(!m_startCounting) {
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

    // sleep for a while to don't obcess over it
    usleep(5000);
}

PkgList AptIntf::resolvePackageIds(gchar **package_ids, PkBitfield filters)
{
    gchar *pi;
    PkgList ret;

    pk_backend_job_set_status (m_job, PK_STATUS_ENUM_QUERY);

    // Don't fail if package list is empty
    if (package_ids == NULL) {
        return ret;
    }

    for (uint i = 0; i < g_strv_length(package_ids); ++i) {
        if (m_cancel) {
            break;
        }

        pi = package_ids[i];

        // Check if it's a valid package id
        if (pk_package_id_check(pi) == false) {
            string name(pi);
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
                    if (!ver.end()) {
                        ret.push_back(ver);
                    }

                    const pkgCache::VerIterator &candidateVer = m_cache->findCandidateVer(pkg);
                    // check to see if the provided package isn't virtual too
                    if (!candidateVer.end()) {
                        ret.push_back(candidateVer);
                    }
                }
            } else {
                const pkgCache::PkgIterator &pkg = (*m_cache)->FindPkg(name);
                // Ignore packages that could not be found or that exist only due to dependencies.
                if (pkg.end() == true || (pkg.VersionList().end() && pkg.ProvidesList().end())) {
                    continue;
                }

                const pkgCache::VerIterator &ver = m_cache->findVer(pkg);
                // check to see if the provided package isn't virtual too
                if (ver.end() == false) {
                    ret.push_back(ver);
                }

                const pkgCache::VerIterator &candidateVer = m_cache->findCandidateVer(pkg);
                // check to see if the provided package isn't virtual too
                if (candidateVer.end() == false) {
                    ret.push_back(candidateVer);
                }
            }
        } else {
            const pkgCache::VerIterator &ver = m_cache->resolvePkgID(pi);
            // check to see if we found the package
            if (!ver.end()) {
                ret.push_back(ver);
            }
        }
    }

    return filterPackages(ret, filters);
}

void AptIntf::refreshCache()
{
    pk_backend_job_set_status(m_job, PK_STATUS_ENUM_REFRESH_CACHE);

    if (m_cache->BuildSourceList() == false) {
        return;
    }

    // Create the progress
    AcqPackageKitStatus Stat(this, m_job);

    // do the work
    ListUpdate(Stat, *m_cache->GetSourceList());

    // Rebuild the cache.
    pkgCacheFile::RemoveCaches();
    if (m_cache->BuildCaches() == false) {
        return;
    }

    // missing repo gpg signature would appear here
    if (_error->PendingError() == false && _error->empty() == false) {
        // TODO this shouldn't
        show_errors(m_job, PK_ERROR_ENUM_GPG_FAILURE);
    }
}

void AptIntf::markAutoInstalled(const PkgList &pkgs)
{
    for (const pkgCache::VerIterator &verIt : pkgs) {
        if (m_cancel) {
            break;
        }

        // Mark package as auto-installed
        (*m_cache)->MarkAuto(verIt.ParentPkg(), true);
    }
}

bool AptIntf::markFileForInstall(std::string const &file)
{
    return m_cache->GetSourceList()->AddVolatileFile(file);
}

PkgList AptIntf::resolveLocalFiles(gchar **localDebs)
{
    PkgList ret;
    for (int i = 0; i < g_strv_length(localDebs); ++i) {
        pkgCache::PkgIterator const P = (*m_cache)->FindPkg(localDebs[i]);
        if (P.end()) {
            continue;
        }

        // Set any version providing the .deb as the candidate.
        for (auto Prv = P.ProvidesList(); Prv.end() == false; Prv++) {
            ret.push_back(Prv.OwnerVer());
        }

        // TODO do we need this?
        // via cacheset to have our usual virtual handling
        //APT::VersionContainerInterface::FromPackage(&(verset[MOD_INSTALL]), Cache, P, APT::CacheSetHelper::CANDIDATE, helper);
    }
    return ret;
}

bool AptIntf::runTransaction(const PkgList &install, const PkgList &remove, const PkgList &update,
                             bool fixBroken, PkBitfield flags, bool autoremove)
{
    //cout << "runTransaction" << simulate << remove << endl;

    pk_backend_job_set_status (m_job, PK_STATUS_ENUM_RUNNING);

    bool simulate = pk_bitfield_contain(flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE);

    // Enter the special broken fixing mode if the user specified arguments
    // THIS mode will run if fixBroken is false and the cache has broken packages
    bool BrokenFix = false;
    if ((*m_cache)->BrokenCount() != 0) {
        BrokenFix = true;
    }

    pkgProblemResolver Fix(*m_cache);

    // TODO: could use std::bind an have a generic operation array iff toRemove had the same
    //       signature

    struct Operation {
        const PkgList &list;
        const bool preserveAuto;
    };

    // new scope for the ActionGroup
    {
        pkgDepCache::ActionGroup group(*m_cache);

        for (auto op : { Operation { install, false }, Operation { update, true } }) {
            for (auto autoInst : { false, true }) {
                for (const pkgCache::VerIterator &verIt : op.list) {
                    if (m_cancel) {
                        break;
                    }
                    if (!m_cache->tryToInstall(Fix, verIt, BrokenFix, autoInst, op.preserveAuto)) {
                        return false;
                    }
                }
            }
        }

        for (const pkgCache::VerIterator &verIt : remove) {
            if (m_cancel) {
                break;
            }

            m_cache->tryToRemove(Fix, verIt);
        }

        // Call the scored problem resolver
        if (Fix.Resolve(true) == false) {
            _error->Discard();
        }

        // Now we check the state of the packages,
        if ((*m_cache)->BrokenCount() != 0) {
            // if the problem resolver could not fix all broken things
            // suggest to run RepairSystem by saing that the last transaction
            // did not finish well
            m_cache->ShowBroken(false, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED);
            return false;
        }
    }

    // Prepare for the restart thing
    struct stat restartStatStart;
    if (g_file_test(REBOOT_REQUIRED, G_FILE_TEST_EXISTS)) {
        g_stat(REBOOT_REQUIRED, &restartStatStart);
    }

    // If we are simulating the install packages
    // will just calculate the trusted packages
    const auto ret = installPackages(flags, autoremove);

    if (g_file_test(REBOOT_REQUIRED, G_FILE_TEST_EXISTS)) {
        struct stat restartStat;
        g_stat(REBOOT_REQUIRED, &restartStat);

        if (restartStat.st_mtime > restartStatStart.st_mtime) {
            // Emit the packages that caused the restart
            if (!m_restartPackages.empty()) {
                emitRequireRestart(m_restartPackages);
            } else if (!m_pkgs.empty()) {
                // Assume all of them
                emitRequireRestart(m_pkgs);
            } else {
                // Emit a foo require restart
                pk_backend_job_require_restart(m_job, PK_RESTART_ENUM_SYSTEM, "aptcc;;;");
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
bool AptIntf::installPackages(PkBitfield flags, bool autoremove)
{
    bool simulate = pk_bitfield_contain(flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE);
    PkBackend *backend = PK_BACKEND(pk_backend_job_get_backend(m_job));

    //cout << "installPackages() called" << endl;
    // Try to auto-remove packages
    if (autoremove && !m_cache->doAutomaticRemove()) {
        // TODO
        return false;
    }

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
    AcqPackageKitStatus Stat(this, m_job);

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
        cout << DebBytes << ',' << (*m_cache)->DebSize() << endl;
        cout << "How odd.. The sizes didn't match, email apt@packages.debian.org";
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
        cout << "PendingError " << endl;
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
        cout << "PendingError download" << endl;
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
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
    _system->UnLock();

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
        cout << "Failed to create a pipe" << endl;
        return false;
    }

    int pty_master;
    m_child_pid = forkpty(&pty_master, NULL, NULL, NULL);
    if (m_child_pid == -1) {
        return false;
    }

    if (m_child_pid == 0) {
        //cout << "FORKED: installPackages(): DoInstall" << endl;

        // close pipe we don't need
        close(readFromChildFD[0]);

        // Change the locale to not get libapt localization
        setlocale(LC_ALL, "C");

        // Debconf handling
        const gchar *socket = pk_backend_job_get_frontend_socket(m_job);
        if ((m_interactive) && (socket != NULL)) {
            setenv("DEBIAN_FRONTEND", "passthrough", 1);
            setenv("DEBCONF_PIPE", socket, 1);
        } else {
            // we don't have a socket set or are not interactive, let's fallback to noninteractive
            setenv("DEBIAN_FRONTEND", "noninteractive", 1);
        }

        const gchar *locale;
        // Set the LANGUAGE so debconf messages get localization
        if (locale = pk_backend_job_get_locale(m_job)) {
            setenv("LANGUAGE", locale, 1);
            setenv("LANG", locale, 1);
            //setenv("LANG", "C", 1);
        }

        // apt will record this in its history.log
        guint uid = pk_backend_job_get_uid(m_job);
        if (uid > 0) {
            gchar buf[16];
            snprintf(buf, sizeof(buf), "%d", uid);
            setenv("PACKAGEKIT_CALLER_UID", buf, 1);
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

    cout << "PARENT process running..." << endl;
    // make it nonblocking, verry important otherwise
    // when the child finish we stay stuck.
    fcntl(readFromChildFD[0], F_SETFL, O_NONBLOCK);
    fcntl(pty_master, F_SETFL, O_NONBLOCK);

    // init the timer
    m_lastTermAction = time(NULL);
    m_startCounting = false;

    // Check if the child died
    int ret;
    char masterbuf[1024];
    while (waitpid(m_child_pid, &ret, WNOHANG) == 0) {
        // TODO: This is dpkg's raw output. Maybe save it for error-solving?
        while(read(pty_master, masterbuf, sizeof(masterbuf)) > 0);
        updateInterface(readFromChildFD[0], pty_master);
    }

    close(readFromChildFD[0]);
    close(readFromChildFD[1]);
    close(pty_master);

    cout << "Parent finished..." << endl;
    return true;
}
