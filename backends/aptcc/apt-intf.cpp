/* apt-intf.cpp
 *
 * Copyright (c) 1999-2008 Daniel Burrows
 * Copyright (c) 2004 Michael Vogt <mvo@debian.org>
 *               2009 Daniel Nicoletti <dantti12@gmail.com>
 *               2012 Matthias Klumpp <matthias@tenstral.net>
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

#include <apt-pkg/error.h>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/sptr.h>
#include <apt-pkg/version.h>

#include <sys/statvfs.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <pty.h>

#include <fstream>
#include <dirent.h>

#include "AptCacheFile.h"
#include "apt-utils.h"
#include "matcher.h"
#include "gstMatcher.h"
#include "apt-messages.h"
#include "acqpkitstatus.h"
#include "pkg_acqfile.h"
#include "deb-file.h"

#define RAMFS_MAGIC     0x858458f6

AptIntf::AptIntf(PkBackendJob *job) :
    m_job(job),
    m_cancel(false),
    m_terminalTimeout(120),
    m_lastSubProgress(0)
{
    m_cancel = false;

    // Make sure initial m_time is 0
    m_restartStat.st_mtime = 0;

    m_cache = new AptCacheFile(job);
}

bool AptIntf::init()
{
    gchar *locale;
    gchar *http_proxy;
    gchar *ftp_proxy;

    m_isMultiArch = APT::Configuration::getArchitectures(false).size() > 1;

    // set locale
    if (locale = pk_backend_job_get_locale(m_job)) {
        setlocale(LC_ALL, locale);
        // TODO why this cuts characthers on ui?
        // 		string _locale(locale);
        // 		size_t found;
        // 		found = _locale.find('.');
        // 		_locale.erase(found);
        // 		_config->Set("APT::Acquire::Translation", _locale);
    }
    g_free(locale);

    // set http proxy
    http_proxy = pk_backend_job_get_proxy_http(m_job);
    setenv("http_proxy", http_proxy, 1);
    g_free(http_proxy);

    // set ftp proxy
    ftp_proxy = pk_backend_job_get_proxy_ftp(m_job);
    setenv("ftp_proxy", ftp_proxy, 1);
    g_free(ftp_proxy);

    // Tries to open the cache
    bool ret;
    ret = m_cache->Open();

    // Prepare for the restart thing
    if (g_file_test(REBOOT_REQUIRED, G_FILE_TEST_EXISTS)) {
        g_stat(REBOOT_REQUIRED, &m_restartStat);
    }

    return !ret;
}

AptIntf::~AptIntf()
{
    // Check the restart thing
    if (g_file_test(REBOOT_REQUIRED, G_FILE_TEST_EXISTS)) {
        struct stat restartStat;
        g_stat(REBOOT_REQUIRED, &restartStat);

        if (restartStat.st_mtime > m_restartStat.st_mtime) {
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

void AptIntf::emitFinished()
{
    pk_backend_job_finished(m_job);
}

pkgCache::VerIterator AptIntf::findPackageId(const gchar *packageId)
{
    gchar **parts;
    pkgCache::PkgIterator pkg;

    parts = pk_package_id_split(packageId);
    pkg = (*m_cache)->FindPkg(parts[PK_PACKAGE_ID_NAME], parts[PK_PACKAGE_ID_ARCH]);

    // Ignore packages that could not be found or that exist only due to dependencies.
    if (pkg.end() || (pkg.VersionList().end() && pkg.ProvidesList().end())) {
        g_strfreev(parts);
        return pkgCache::VerIterator();
    }

    const pkgCache::VerIterator &ver = findVer(pkg);
    // check to see if the provided package isn't virtual too
    if (ver.end() == false &&
            strcmp(ver.VerStr(), parts[PK_PACKAGE_ID_VERSION]) == 0) {
        g_strfreev(parts);
        return ver;
    }

    const pkgCache::VerIterator &candidateVer = m_cache->findCandidateVer(pkg);
    // check to see if the provided package isn't virtual too
    if (candidateVer.end() == false &&
            strcmp(candidateVer.VerStr(), parts[PK_PACKAGE_ID_VERSION]) == 0) {
        g_strfreev(parts);
        return candidateVer;
    }

    g_strfreev (parts);

    return ver;
}

pkgCache::VerIterator AptIntf::findVer(const pkgCache::PkgIterator &pkg)
{
    // if the package is installed return the current version
    if (!pkg.CurrentVer().end()) {
        return pkg.CurrentVer();
    }

    // Else get the candidate version iterator
    const pkgCache::VerIterator &candidateVer = m_cache->findCandidateVer(pkg);
    if (!candidateVer.end()) {
        return candidateVer;
    }

    // return the version list as a last resource
    return pkg.VersionList();
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

        // TODO test this one..
        if (pk_bitfield_contain(filters, PK_FILTER_ENUM_COLLECTIONS)) {
            if (!component.compare("metapackages")) {
                return false;
            }
        } else if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_COLLECTIONS)) {
            if (component.compare("metapackages")) {
                return false;
            }
        }
    }
    return true;
}

PkgList AptIntf::filterPackages(const PkgList &packages, PkBitfield filters)
{
    if (filters != 0) {
        PkgList ret;
        for (PkgList::const_iterator i = packages.begin(); i != packages.end(); ++i) {
            if (matchPackage(*i, filters)) {
                ret.push_back(*i);
            }
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

void AptIntf::emitPackageProgress(const pkgCache::VerIterator &ver, uint percentage)
{
    gchar *package_id;
    package_id = utilBuildPackageId(ver);
    pk_backend_job_set_item_progress(m_job, package_id, PK_STATUS_ENUM_UNKNOWN, percentage);
    g_free(package_id);
}

void AptIntf::emitPackages(PkgList &output, PkBitfield filters, PkInfoEnum state)
{
    // Sort so we can remove the duplicated entries
    output.sort();

    // Remove the duplicated entries
    output.removeDuplicates();

    for (PkgList::const_iterator it = output.begin(); it != output.end(); ++it) {
        if (m_cancel) {
            break;
        }

        if (matchPackage(*it, filters)) {
            emitPackage(*it, state);
        }
    }
}

void AptIntf::emitRequireRestart(PkgList &output)
{
    // Sort so we can remove the duplicated entries
    output.sort();

    // Remove the duplicated entries
    output.removeDuplicates();

    for (PkgList::const_iterator it = output.begin(); it != output.end(); ++it) {
        gchar *package_id;
        package_id = utilBuildPackageId(*it);
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

    for (PkgList::const_iterator i = output.begin(); i != output.end(); ++i) {
        if (m_cancel) {
            break;
        }

        if (!matchPackage(*i, filters)) {
            continue;
        }

        // the default update info
        state = PK_INFO_ENUM_NORMAL;

        // let find what kind of upgrade this is
        pkgCache::VerFileIterator vf = i->FileList();
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

        emitPackage(*i, state);
    }
}

// search packages which provide a codec (specified in "values")
void AptIntf::providesCodec(PkgList &output, gchar **values)
{
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

        // TODO search in updates packages
        // Ignore virtual packages
        pkgCache::VerIterator ver = findVer(pkg);
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
        if (matcher->matches(record)) {
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
        g_debug("Regex compilation error: ", libreg);
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
                pkgCache::VerIterator ver = findVer (pkg);
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
        const string MD5     = Parse.MD5Hash();
        if (PkgFile.empty() == true) {
            return _error->Error("The package index files are corrupted. No Filename: "
                                 "field for package %s.",
                                 Version.ParentPkg().Name());
        }

        string DestFile = directory + "/" + flNotDir(StoreFilename);

        // Create the item
        new pkgAcqFile(Owner,
                       Index->ArchiveURI(PkgFile),
                       MD5,
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

    for (PkgList::const_iterator i = pkgs.begin(); i != pkgs.end(); ++i) {
        if (m_cancel) {
            break;
        }

        emitPackageDetail(*i);
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
    const pkgCache::VerIterator &currver = findVer(pkg);

    // Build a package_id from the current version
    gchar *current_package_id;
    current_package_id = utilBuildPackageId(currver);

    pkgCache::VerFileIterator vf = candver.FileList();
    string origin = vf.File().Origin() == NULL ? "" : vf.File().Origin();
    pkgRecords::Parser &rec = m_cache->GetPkgRecords()->Lookup(candver.FileList());

    // Build the changelogURI
    char uri[512];
    string srcpkg;
    string verstr;

    if (rec.SourcePkg().empty()) {
        srcpkg = pkg.Name();
    } else {
        srcpkg = rec.SourcePkg();
    }
    if (origin.compare("Debian") == 0 || origin.compare("Ubuntu") == 0) {
        string prefix;

        string src_section = candver.Section() == NULL ? "" : candver.Section();
        if(src_section.find('/') != src_section.npos) {
            src_section = string(src_section, 0, src_section.find('/'));
        } else {
            src_section = "main";
        }

        prefix += srcpkg[0];
        if(srcpkg.size() > 3 && srcpkg[0] == 'l' && srcpkg[1] == 'i' && srcpkg[2] == 'b') {
            prefix = string("lib") + srcpkg[3];
        }

        if(candver.VerStr() != NULL) {
            verstr = candver.VerStr();
        }

        if(verstr.find(':') != verstr.npos) {
            verstr = string(verstr, verstr.find(':') + 1);
        }

        if (origin.compare("Debian") == 0) {
            snprintf(uri,
                     512,
                     "http://packages.debian.org/changelogs/pool/%s/%s/%s/%s_%s/changelog",                                    src_section.c_str(),
                     prefix.c_str(),
                     srcpkg.c_str(),
                     srcpkg.c_str(),
                     verstr.c_str());
        } else {
            snprintf(uri,
                     512,
                     "http://changelogs.ubuntu.com/changelogs/pool/%s/%s/%s/%s_%s/changelog",                                    src_section.c_str(),
                     prefix.c_str(),
                     srcpkg.c_str(),
                     srcpkg.c_str(),
                     verstr.c_str());
        }
    } else {
        string pkgfilename;
        const char *start, *stop;
        pkgTagSection sec;
        unsigned long len;

        rec.GetRec(start, stop);
        len = stop - start;
        // add +1 to ensure we have the double lineline in the buffer
        if (start && sec.Scan(start, len + 1)) {
            pkgfilename = sec.FindS("Filename");
        }

        string cadidateOriginSiteUrl;
        if(!vf.end() && vf.File() && vf.File().Site()) {
            cadidateOriginSiteUrl = vf.File().Site();
        }

        pkgfilename = pkgfilename.substr(0, pkgfilename.find_last_of('.')) + ".changelog";
        snprintf(uri,512,"http://%s/%s",
                 cadidateOriginSiteUrl.c_str(),
                 pkgfilename.c_str());
    }
    // Create the download object
    AcqPackageKitStatus Stat(this, m_job);

    // get a fetcher
    pkgAcquire fetcher;
    fetcher.Setup(&Stat);

    // fetch the changelog
    pk_backend_job_set_status(m_job, PK_STATUS_ENUM_DOWNLOAD_CHANGELOG);

    // Create a random temp dir
    char dirName[] = "/tmp/aptccXXXXXXXX";
    char *tempDir = mkdtemp(dirName);
    string filename = tempDir;
    filename.append("/");
    filename.append(pkg.Name());
    getChangelogFile(filename, pkg.Name(), origin, verstr, srcpkg, uri, &fetcher);

    string changelog;
    string update_text;
    ifstream in(filename.c_str());
    string line;
    GRegex *regexVer;
    regexVer = g_regex_new("(?'source'.+) \\((?'version'.*)\\) "
                           "(?'dist'.+); urgency=(?'urgency'.+)",
                           G_REGEX_CASELESS,
                           G_REGEX_MATCH_ANCHORED,
                           0);
    GRegex *regexDate;
    regexDate = g_regex_new("^ -- (?'maintainer'.+) (?'mail'<.+>)  (?'date'.+)$",
                            G_REGEX_CASELESS,
                            G_REGEX_MATCH_ANCHORED,
                            0);
    string updated;
    string issued;
    while (getline(in, line)) {
        // no need to free str later, it is allocated in a static buffer
        const char *str = utf8(line.c_str());
        if (strcmp(str, "") == 0) {
            changelog.append("\n");
            continue;
        } else {
            changelog.append(str);
            changelog.append("\n");
        }

        if (starts_with(str, srcpkg.c_str())) {
            // Check to see if the the text isn't about the current package,
            // otherwise add a == version ==
            GMatchInfo *match_info;
            if (g_regex_match(regexVer, str, G_REGEX_MATCH_ANCHORED, &match_info)) {
                gchar *version;
                version = g_match_info_fetch_named(match_info, "version");

                // Compare if the current version is shown in the changelog, to not
                // display old changelog information
                if (_system != 0  &&
                        _system->VS->DoCmpVersion(version, version + strlen(version),
                                                  currver.VerStr(), currver.VerStr() + strlen(currver.VerStr())) <= 0) {
                    g_free (version);
                    break;
                } else {
                    if (!update_text.empty()) {
                        update_text.append("\n\n");
                    }
                    update_text.append(" == ");
                    update_text.append(version);
                    update_text.append(" ==");
                    g_free (version);
                }
            }
            g_match_info_free (match_info);
        } else if (starts_with(str, "  ")) {
            // update descritption
            update_text.append("\n");
            update_text.append(str);
        } else if (starts_with(str, " --")) {
            // Parse the text to know when the update was issued,
            // and when it got updated
            GMatchInfo *match_info;
            if (g_regex_match(regexDate, str, G_REGEX_MATCH_ANCHORED, &match_info)) {
                GTimeVal dateTime = {0, 0};
                gchar *date;
                date = g_match_info_fetch_named(match_info, "date");
                g_warn_if_fail(RFC1123StrToTime(date, dateTime.tv_sec));
                g_free(date);

                issued = g_time_val_to_iso8601(&dateTime);
                if (updated.empty()) {
                    updated = g_time_val_to_iso8601(&dateTime);
                }
            }
            g_match_info_free(match_info);
        }
    }
    // Clean structures
    g_regex_unref(regexVer);
    g_regex_unref(regexDate);
    unlink(filename.c_str());
    rmdir(tempDir);

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
    for (PkgList::const_iterator it = pkgs.begin(); it != pkgs.end(); ++it) {
        if (m_cancel) {
            break;
        }

        emitUpdateDetail(*it);
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

        const pkgCache::VerIterator &ver = findVer(dep.TargetPkg());
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
        const pkgCache::VerIterator &parentVer = findVer(parentPkg);
        if (parentVer.end() == false) {
            PkgList deps;
            getDepends(deps, parentVer, false);
            for (PkgList::const_iterator it = deps.begin(); it != deps.end(); ++it) {
                if (*it == ver) {
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
        const pkgCache::VerIterator &ver = findVer(pkg);
        if (ver.end() == false) {
            output.push_back(ver);
        }
    }
    return output;
}

PkgList AptIntf::getPackagesFromGroup(gchar **values)
{
    PkgList output;
    vector<PkGroupEnum> groups;

    int len = g_strv_length(values);
    for (uint i = 0; i < len; i++) {
        if (values[i] == NULL) {
            pk_backend_job_error_code(m_job,
                                  PK_ERROR_ENUM_GROUP_NOT_FOUND,
                                  values[i]);
            pk_backend_job_finished(m_job);
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
        const pkgCache::VerIterator &ver = findVer(pkg);
        if (ver.end() == false) {
            string section = pkg.VersionList().Section() == NULL ? "" : pkg.VersionList().Section();

            size_t found;
            found = section.find_last_of("/");
            section = section.substr(found + 1);

            // Don't insert virtual packages instead add what it provides
            for (vector<PkGroupEnum>::const_iterator it = groups.begin();
                 it != groups.end();
                 ++it) {
                if (*it == get_enum_group(section)) {
                    output.push_back(ver);
                    break;
                }
            }
        }
    }
    return output;
}

PkgList AptIntf::searchPackageName(gchar *search)
{
    PkgList output;

    Matcher *matcher = new Matcher(search);
    if (matcher->hasError()) {
        g_debug("Regex compilation error");
        delete matcher;
        return output;
    }

    for (pkgCache::PkgIterator pkg = m_cache->GetPkgCache()->PkgBegin(); !pkg.end(); ++pkg) {
        if (m_cancel) {
            break;
        }
        // Ignore packages that exist only due to dependencies.
        if (pkg.VersionList().end() && pkg.ProvidesList().end()) {
            continue;
        }

        if (matcher->matches(pkg.Name())) {
            // Don't insert virtual packages instead add what it provides
            const pkgCache::VerIterator &ver = findVer(pkg);
            if (ver.end() == false) {
                output.push_back(ver);
            } else {
                // iterate over the provides list
                for (pkgCache::PrvIterator Prv = pkg.ProvidesList(); Prv.end() == false; ++Prv) {
                    const pkgCache::VerIterator &ownerVer = findVer(Prv.OwnerPkg());

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

PkgList AptIntf::searchPackageDetails(gchar *search)
{
    PkgList output;

    Matcher *matcher = new Matcher(search);
    if (matcher->hasError()) {
        g_debug("Regex compilation error");
        delete matcher;
        return output;
    }

    for (pkgCache::PkgIterator pkg = m_cache->GetPkgCache()->PkgBegin(); !pkg.end(); ++pkg) {
        if (m_cancel) {
            break;
        }
        // Ignore packages that exist only due to dependencies.
        if (pkg.VersionList().end() && pkg.ProvidesList().end()) {
            continue;
        }

        const pkgCache::VerIterator &ver = findVer(pkg);
        if (ver.end() == false) {
            if (matcher->matches(pkg.Name()) ||
                    matcher->matches((*m_cache).getLongDescription(ver))) {
                // The package matched
                output.push_back(ver);
            }
        } else if (matcher->matches(pkg.Name())) {
            // The package is virtual and MATCHED the name
            // Don't insert virtual packages instead add what it provides

            // iterate over the provides list
            for (pkgCache::PrvIterator Prv = pkg.ProvidesList(); Prv.end() == false; ++Prv) {
                const pkgCache::VerIterator &ownerVer = findVer(Prv.OwnerPkg());

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
    regex_t re;
    gchar *search;
    gchar *values_str;

    values_str = g_strjoinv("$|^", values);
    search = g_strdup_printf("^%s$",
                             values_str);
    g_free(values_str);
    if(regcomp(&re, search, REG_NOSUB) != 0) {
        g_debug("Regex compilation error");
        g_free(search);
        return output;
    }
    g_free(search);

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
            string f = "/var/lib/dpkg/info/" + string(dirp->d_name);
            ifstream in(f.c_str());
            if (!in != 0) {
                continue;
            }
            while (!in.eof()) {
                getline(in, line);
                if (regexec(&re, line.c_str(), (size_t)0, NULL, 0) == 0) {
                    string file(dirp->d_name);
                    packages.push_back(file.erase(file.size() - 5, file.size()));
                    break;
                }
            }
        }
    }
    closedir(dp);
    regfree(&re);

    // Resolve the package names now
    for (vector<string>::const_iterator it = packages.begin();
        it != packages.end(); ++it) {
        if (m_cancel) {
            break;
        }
        const pkgCache::PkgIterator &pkg = (*m_cache)->FindPkg(*it);
        if (pkg.end() == true) {
            continue;
        }
        const pkgCache::VerIterator &ver = findVer(pkg);
        if (ver.end() == true) {
            continue;
        }
        output.push_back(ver);
    }

    return output;
}

// used to return files it reads, using the info from the files in /var/lib/dpkg/info/
void AptIntf::providesMimeType(PkgList &output, gchar **values)
{
    regex_t re;
    gchar *value;
    gchar *values_str;

    values_str = g_strjoinv("|", values);
    value = g_strdup_printf("^MimeType=\\(.*;\\)\\?\\(%s\\)\\(;.*\\)\\?$",
                            values_str);
    g_free(values_str);

    if(regcomp(&re, value, REG_NOSUB) != 0) {
        g_debug("Regex compilation error");
        g_free(value);
        return;
    }
    g_free(value);

    DIR *dp;
    struct dirent *dirp;
    if (!(dp = opendir("/usr/share/app-install/desktop/"))) {
        g_debug ("Error opening /usr/share/app-install/desktop/\n");
        regfree(&re);
        return;
    }

    vector<string> packages;
    string line;
    while ((dirp = readdir(dp)) != NULL) {
        if (m_cancel) {
            break;
        }
        if (ends_with(dirp->d_name, ".desktop")) {
            string f = "/usr/share/app-install/desktop/" + string(dirp->d_name);
            ifstream in(f.c_str());
            if (!in != 0) {
                continue;
            }
            bool getName = false;
            while (!in.eof()) {
                getline(in, line);
                if (getName) {
                    if (starts_with(line, "X-AppInstall-Package=")) {
                        // Remove the X-AppInstall-Package=
                        packages.push_back(line.substr(21));
                        break;
                    }
                } else {
                    if (regexec(&re, line.c_str(), (size_t)0, NULL, 0) == 0) {
                        in.seekg(ios_base::beg);
                        getName = true;
                    }
                }
            }
        }
    }

    closedir(dp);
    regfree(&re);

    // resolve the package names
    for (vector<string>::const_iterator it = packages.begin();
         it != packages.end(); ++it) {
        if (m_cancel) {
            break;
        }
        const pkgCache::PkgIterator &pkg = (*m_cache)->FindPkg(*it);
        if (pkg.end() == true) {
            continue;
        }
        const pkgCache::VerIterator &ver = findVer(pkg);
        if (ver.end() == true) {
            continue;
        }
        output.push_back(ver);
    }

    // Check if app-install-data is installed
    if (output.empty()) {
        // check if app-install-data is installed
        pkgCache::PkgIterator pkg;
        pkg = (*m_cache)->FindPkg("app-install-data");
        if (pkg->CurrentState != pkgCache::State::Installed) {
            pk_backend_job_error_code(m_job,
                                  PK_ERROR_ENUM_INTERNAL_ERROR,
                                  "You need the app-install-data "
                                  "package to be able to look for "
                                  "applications that can handle "
                                  "this kind of file");
        }
    }
}

// used to emit files it reads the info directly from the files
void AptIntf::emitPackageFiles(const gchar *pi)
{
    static string filelist;
    string line;
    gchar **parts;

    parts = pk_package_id_split(pi);
    filelist.erase(filelist.begin(), filelist.end());

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
        while (in.eof() == false && filelist.empty()) {
            getline(in, line);
            filelist += line;
        }
        while (in.eof() == false) {
            getline(in, line);
            if (!line.empty()) {
                filelist += ";" + line;
            }
        }

        if (!filelist.empty()) {
            pk_backend_job_files(m_job, pi, filelist.c_str());
        }
    }
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
    fetcher.Setup(&Stat);

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
            pkgAcqArchiveSane *archive = static_cast<pkgAcqArchiveSane*>(*I);
            untrusted.push_back(archive->version());

            UntrustedList += string((*I)->ShortDesc()) + " ";
        }
    }

    if (untrusted.empty()) {
        return true;
    } else if (pk_bitfield_contain(flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
        emitPackages(untrusted, PK_FILTER_ENUM_NONE, PK_INFO_ENUM_UNTRUSTED);
    }

    if (!pk_bitfield_contain(flags, PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED)) {
        g_debug ("Authentication warning overridden.\n");
        return true;
    }

    string warning("The following packages cannot be authenticated:\n");
    warning += UntrustedList;
    pk_backend_job_error_code(m_job,
                              PK_ERROR_ENUM_CANNOT_INSTALL_REPO_UNSIGNED,
                              warning.c_str());
    _error->Discard();
    return false;
}

void AptIntf::tryToRemove(const pkgCache::VerIterator &ver,
                          pkgDepCache &Cache,
                          pkgProblemResolver &Fix)
{
    pkgCache::PkgIterator Pkg = ver.ParentPkg();

    // The package is not installed
    if (Pkg->CurrentVer == 0) {
        Fix.Clear(Pkg);
        Fix.Protect(Pkg);
        Fix.Remove(Pkg);

        return;
    }

    Fix.Clear(Pkg);
    Fix.Protect(Pkg);
    Fix.Remove(Pkg);
    // TODO this is false since PackageKit can't
    // tell it want's o purge
    Cache.MarkDelete(Pkg, false);
}


bool AptIntf::tryToInstall(const pkgCache::VerIterator &ver,
                           pkgDepCache &Cache,
                           pkgProblemResolver &Fix,
                           bool BrokenFix,
                           unsigned int &ExpectedInst)
{
    pkgCache::PkgIterator Pkg = ver.ParentPkg();

    // Check if there is something at all to install
    pkgDepCache::StateCache &State = Cache[Pkg];

    if (State.CandidateVer == 0) {
        _error->Error("Package %s is virtual and has no installation candidate", Pkg.Name());

        pk_backend_job_error_code(m_job,
                              PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
                              g_strdup_printf("Package %s is virtual and has no "
                                              "installation candidate",
                                              Pkg.Name()));
        return false;
    }

    Fix.Clear(Pkg);
    Fix.Protect(Pkg);

    // Install it
    Cache.MarkInstall(Pkg, false);
    if (State.Install() == true) {
        ExpectedInst++;
    }

    // 	cout << "trytoinstall ExpectedInst " << ExpectedInst << endl;
    // Install it with autoinstalling enabled (if we not respect the minial
    // required deps or the policy)
    if ((State.InstBroken() == true || State.InstPolicyBroken() == true) &&
            BrokenFix == false) {
        Cache.MarkInstall(Pkg,true);
    }

    return true;
}

// checks if there are Essential packages being removed
bool AptIntf::removingEssentialPackages(AptCacheFile &cache)
{
    string List;
    bool *Added = new bool[cache->Head().PackageCount];
    for (unsigned int I = 0; I != cache->Head().PackageCount; ++I) {
        Added[I] = false;
    }

    for (pkgCache::PkgIterator I = cache->PkgBegin(); ! I.end(); ++I) {
        if ((I->Flags & pkgCache::Flag::Essential) != pkgCache::Flag::Essential &&
                (I->Flags & pkgCache::Flag::Important) != pkgCache::Flag::Important) {
            continue;
        }

        if (cache[I].Delete() == true) {
            if (Added[I->ID] == false) {
                Added[I->ID] = true;
                List += string(I.Name()) + " ";
            }
        }

        if (I->CurrentVer == 0) {
            continue;
        }

        // Print out any essential package depenendents that are to be removed
        for (pkgCache::DepIterator D = I.CurrentVer().DependsList(); D.end() == false; ++D) {
            // Skip everything but depends
            if (D->Type != pkgCache::Dep::PreDepends &&
                    D->Type != pkgCache::Dep::Depends){
                continue;
            }

            pkgCache::PkgIterator P = D.SmartTargetPkg();
            if (cache[P].Delete() == true)
            {
                if (Added[P->ID] == true){
                    continue;
                }
                Added[P->ID] = true;

                char S[300];
                snprintf(S, sizeof(S), "%s (due to %s) ", P.Name(), I.Name());
                List += S;
            }
        }
    }

    delete [] Added;
    if (!List.empty()) {
        pk_backend_job_error_code(m_job,
                              PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE,
                              g_strdup_printf("WARNING: You are trying to remove the "
                                              "following essential packages: %s",
                                              List.c_str()));
        return true;
    }

    return false;
}

/**
 * checkChangedPackages - Check whas is goind to happen to the packages
 */
PkgList AptIntf::checkChangedPackages(AptCacheFile &cache, bool emitChanged)
{
    PkgList ret;
    PkgList installing;
    PkgList removing;
    PkgList updating;
    PkgList downgrading;

    for (pkgCache::PkgIterator pkg = cache->PkgBegin(); ! pkg.end(); ++pkg) {
        if (cache[pkg].NewInstall() == true) {
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
        } else if (cache[pkg].Delete() == true) {
            // removing
            const pkgCache::VerIterator &ver = findVer(pkg);
            if (!ver.end()) {
                ret.push_back(ver);
                removing.push_back(ver);

                // append to the restart required list
                if (utilRestartRequired(pkg.Name())) {
                    m_restartPackages.push_back(ver);
                }
            }
        } else if (cache[pkg].Upgrade() == true) {
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
        } else if (cache[pkg].Downgrade() == true) {
            // downgrading
            const pkgCache::VerIterator &ver = findVer(pkg);
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
    for (PkgList::const_iterator it = m_pkgs.begin(); it != m_pkgs.end(); ++it) {
        if (it->ParentPkg().Name() == name) {
            return *it;
        }
    }
    
    const pkgCache::PkgIterator &pkg = (*m_cache)->FindPkg(name);
    // Ignore packages that could not be found or that exist only due to dependencies.
    if (pkg.end() == true ||
        (pkg.VersionList().end() && pkg.ProvidesList().end())) {
        return pkgCache::VerIterator();
    }

    const pkgCache::VerIterator &ver = findVer(pkg);
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

            // first check for errors and conf-file prompts
            if (strstr(status, "pmerror") != NULL) {
                // error from dpkg
                pk_backend_job_error_code(m_job,
                                      PK_ERROR_ENUM_PACKAGE_FAILED_TO_INSTALL,
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

                gchar *socket;
                if (socket = pk_backend_job_get_frontend_socket(m_job)) {
                    envp = (gchar **) g_malloc(3 * sizeof(gchar *));
                    envp[0] = g_strdup("DEBIAN_FRONTEND=passthrough");
                    envp[1] = g_strdup_printf("DEBCONF_PIPE=%s", socket);
                    envp[2] = NULL;
                } else {
                    // we don't have a socket set, let's fallback to noninteractive
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

                g_free(filename);
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
                    gchar *confmsg;
                    confmsg = g_strdup_printf("The configuration file '%s' "
                                              "(modified by you or a script) "
                                              "has a newer version '%s'.\n"
                                              "Please verify your changes and update it manually.",
                                              orig_file.c_str(),
                                              new_file.c_str());
                    pk_backend_job_message(m_job,
                                       PK_MESSAGE_ENUM_CONFIG_FILES_CHANGED,
                                       confmsg);
                    // fall back to keep the current config file
                    if (write(writeFd, "N\n", 2) != 2) {
                        // TODO we need a DPKG patch to use debconf
                        g_debug("Failed to write");
                    }
                    g_free(confmsg);
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
                        emitPackageProgress(ver, 75);
                    }
                } else if (starts_with(str, "Preparing for removal")) {
                    // Preparing to Install/configure
                    // cout << "Found Preparing for removal! " << line << endl;
                    m_lastSubProgress = 50;
                    const pkgCache::VerIterator &ver = findTransactionPackage(pkg);
                    if (!ver.end()) {
                        emitPackage(ver, PK_INFO_ENUM_REMOVING);
                        emitPackageProgress(ver, m_lastSubProgress);
                    }
                } else if (starts_with(str, "Preparing")) {
                    // Preparing to Install/configure
                    // cout << "Found Preparing! " << line << endl;
                    // if last package is different then finish it
                    if (!m_lastPackage.empty() && m_lastPackage.compare(pkg) != 0) {
                        // cout << "FINISH the last package: " << m_lastPackage << endl;
                        const pkgCache::VerIterator &ver = findTransactionPackage(m_lastPackage);
                        if (!ver.end()) {
                            emitPackage(ver, PK_INFO_ENUM_FINISHED);
                        }
                    }
                    const pkgCache::VerIterator &ver = findTransactionPackage(pkg);
                    if (!ver.end()) {
                        emitPackage(ver, PK_INFO_ENUM_PREPARING);
                        emitPackageProgress(ver, 25);
                    }
                } else if (starts_with(str, "Unpacking")) {
                    // cout << "Found Unpacking! " << line << endl;
                    const pkgCache::VerIterator &ver = findTransactionPackage(pkg);
                    if (!ver.end()) {
                        emitPackage(ver, PK_INFO_ENUM_DECOMPRESSING);
                        emitPackageProgress(ver, 50);
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
                        emitPackageProgress(ver, m_lastSubProgress);
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
                        emitPackageProgress(ver, m_lastSubProgress);
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
                        emitPackageProgress(ver, m_lastSubProgress);
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

/**
 * DoAutomaticRemove - Remove all automatic unused packages
 *
 * Remove unused automatic packages
 */
bool AptIntf::doAutomaticRemove(AptCacheFile &cache)
{
    pkgDepCache::ActionGroup group(*cache);

    // look over the cache to see what can be removed
    for (pkgCache::PkgIterator Pkg = cache->PkgBegin(); ! Pkg.end(); ++Pkg) {
        if (cache[Pkg].Garbage) {
            if (Pkg.CurrentVer() != 0 &&
                    Pkg->CurrentState != pkgCache::State::ConfigFiles) {
                // TODO, packagekit could provide a way to purge
                cache->MarkDelete(Pkg, false);
            } else {
                cache->MarkKeep(Pkg, false, false);
            }
        }
    }

    // Now see if we destroyed anything
    if (cache->BrokenCount() != 0) {
        cout << "Hmm, seems like the AutoRemover destroyed something which really\n"
                "shouldn't happen. Please file a bug report against apt." << endl;
        // TODO call show_broken
        //       ShowBroken(c1out,cache,false);
        return _error->Error("Internal Error, AutoRemover broke stuff");
    }

    return true;
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
            // Check if we are on multiarch AND if the package name didn't contains the arch field (GDEBI for instance)
            if (m_isMultiArch && strstr(pi, ":") == NULL) {
                // OK FindPkg is not suitable on muitarch without ":arch"
                // it can only return one package in this case we need to
                // search the whole package cache and match the package
                // name manually
                for (pkgCache::PkgIterator pkg = m_cache->GetPkgCache()->PkgBegin(); !pkg.end(); ++pkg) {
                    if (m_cancel) {
                        break;
                    }

                    // check if this is the package we want
                    if (strcmp(pkg.Name(), pi) != 0) {
                        continue;
                    }

                    // Ignore packages that could not be found or that exist only due to dependencies.
                    if ((pkg.end() == true || (pkg.VersionList().end() && pkg.ProvidesList().end()))) {
                        continue;
                    }

                    const pkgCache::VerIterator &ver = findVer(pkg);
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
                const pkgCache::PkgIterator &pkg = (*m_cache)->FindPkg(pi);
                // Ignore packages that could not be found or that exist only due to dependencies.
                if (pkg.end() == true || (pkg.VersionList().end() && pkg.ProvidesList().end())) {
                    continue;
                }

                const pkgCache::VerIterator &ver = findVer(pkg);
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
            const pkgCache::VerIterator &ver = findPackageId(pi);
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
    // Create the progress
    AcqPackageKitStatus Stat(this, m_job);

    // do the work
    ListUpdate(Stat, *m_cache->GetSourceList());
}

void AptIntf::markAutoInstalled(AptCacheFile &cache, const PkgList &pkgs)
{
    for (PkgList::const_iterator it = pkgs.begin(); it != pkgs.end(); ++it) {
        if (m_cancel) {
            break;
        }

        // Mark package as auto-installed
        cache->MarkAuto(it->ParentPkg(), true);
    }
}

bool AptIntf::markFileForInstall(const gchar *file, PkgList &install, PkgList &remove)
{
    // We call gdebi to tell us what do we need to install/remove
    // in order to be able to install this package
    gint status;
    gchar **argv;
    gchar *std_out;
    gchar *std_err;
    GError *gerror = NULL;
    argv = (gchar **) g_malloc(5 * sizeof(gchar *));
    argv[0] = g_strdup(GDEBI_BINARY);
    argv[1] = g_strdup("-q");
    argv[2] = g_strdup("--apt-line");
    argv[3] = g_strdup(file);
    argv[4] = NULL;

    gboolean ret;
    ret = g_spawn_sync(NULL, // working dir
                       argv, // argv
                       NULL, // envp
                       G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                       NULL, // child_setup
                       NULL, // user_data
                       &std_out, // standard_output
                       &std_err, // standard_error
                       &status,
                       &gerror);
    int exit_code = WEXITSTATUS(status);
    //     cout << "DebStatus " << exit_code << " WEXITSTATUS " << WEXITSTATUS(status) << " ret: "<< ret << endl;
    cout << "std_out " << strlen(std_out) << std_out << endl;
    cout << "std_err " << strlen(std_err) << std_err << endl;

    PkgList pkgs;
    if (exit_code == 1) {
        if (strlen(std_out) == 0) {
            pk_backend_job_error_code(m_job, PK_ERROR_ENUM_TRANSACTION_ERROR, std_err);
        } else {
            pk_backend_job_error_code(m_job, PK_ERROR_ENUM_TRANSACTION_ERROR, std_out);
        }
        return false;
    } else {
        // GDebi outputs two lines
        gchar **lines = g_strsplit(std_out, "\n", 3);

        // The first line contains the packages to install
        gchar **installPkgs = g_strsplit(lines[0], " ", 0);

        // The second line contains the packages to remove with '-' appended to
        // the end of the package name
        gchar **removePkgs = NULL;
        if (strlen(lines[1]) > 0) {
            gchar *removeStr = g_strndup(lines[1], strlen(lines[1]) - 1);
            removePkgs = g_strsplit(removeStr, "- ", 0);
            g_free(removeStr);
        }

        // Resolve the packages to install
        PkBitfield intallFilters;
        intallFilters = pk_bitfield_from_enums (
                    PK_FILTER_ENUM_NOT_INSTALLED,
                    PK_FILTER_ENUM_ARCH,
                    -1);
        install = resolvePackageIds(installPkgs, intallFilters);

        // Resolve the packages to remove
        PkBitfield removeFilters;
        removeFilters = pk_bitfield_from_enums (
                    PK_FILTER_ENUM_INSTALLED,
                    PK_FILTER_ENUM_ARCH,
                    -1);
        remove = resolvePackageIds(removePkgs, removeFilters);

        g_strfreev(lines);
        g_strfreev(installPkgs);
        g_strfreev(removePkgs);
    }

    return true;
}

bool AptIntf::installFile(const gchar *path, bool simulate)
{
    if (path == NULL) {
        g_error ("installFile() path was NULL!");
        return false;
    }

    DebFile deb(path);
    if (!deb.isValid()) {
        pk_backend_job_error_code(m_job, PK_ERROR_ENUM_TRANSACTION_ERROR, "DEB package is invalid!");
        return false;
    }

    if (simulate) {
        // TODO: Emit signal for to-be-installed package
        //emit_package("",  PK_FILTER_ENUM_NONE, PK_INFO_ENUM_INSTALLING);
        return true;
    }

    string arch = deb.architecture();
    string aptArch = _config->Find("APT::Architecture");

    // TODO: Perform this check _before_ installing all dependencies. (The whole thing needs
    //       some rethinking anyway)
    if ((arch != "all") &&
            (arch != aptArch)) {
        cout << arch << " vs. " << aptArch << endl;
        gchar *msg = g_strdup_printf ("Package has wrong architecture, it is %s, but we need %s",
                                      arch.c_str(), aptArch.c_str());
        pk_backend_job_error_code(m_job, PK_ERROR_ENUM_INCOMPATIBLE_ARCHITECTURE, msg);
        g_free (msg);
        return false;
    }

    // Build package-id for the new package
    gchar *deb_package_id = pk_package_id_build(deb.packageName ().c_str (),
                                                deb.version ().c_str (),
                                                deb.architecture ().c_str (),
                                                "local");
    const gchar *deb_summary = deb.summary ().c_str ();

    gint status;
    gchar **argv;
    gchar **envp;
    gchar *std_out;
    gchar *std_err;
    GError *error = NULL;

    argv = (gchar **) g_malloc(4 * sizeof(gchar *));
    argv[0] = g_strdup("/usr/bin/dpkg");
    argv[1] = g_strdup("-i");
    argv[2] = g_strdup(path); //g_strdup_printf("\'%s\'", path);
    argv[3] = NULL;

    envp = (gchar **) g_malloc(4 * sizeof(gchar *));
    envp[0] = g_strdup("PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin");
    envp[1] = g_strdup("DEBIAN_FRONTEND=passthrough");
    envp[2] = g_strdup_printf("DEBCONF_PIPE=%s", pk_backend_job_get_frontend_socket(m_job));
    envp[3] = NULL;

    // We're installing the package now...
    pk_backend_job_package (m_job, PK_INFO_ENUM_INSTALLING, deb_package_id, deb_summary);

    g_spawn_sync(NULL, // working dir
                 argv,
                 envp,
                 G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                 NULL, // child_setup
                 NULL, // user_data
                 &std_out, // standard_output
                 &std_err, // standard_error
                 &status,
                 &error);
    int exit_code = WEXITSTATUS(status);

    cout << "DpkgOut: " << std_out << endl;
    cout << "DpkgErr: " << std_err << endl;

    if (error != NULL) {
        // We couldn't run dpkg for some reason...
        pk_backend_job_error_code(m_job, PK_ERROR_ENUM_TRANSACTION_ERROR, error->message);
        return false;
    }

    // If installation has failed...
    if (exit_code != 0) {
        if ((std_out == NULL) || (strlen(std_out) == 0)) {
            pk_backend_job_error_code(m_job, PK_ERROR_ENUM_TRANSACTION_ERROR, std_err);
        } else {
            pk_backend_job_error_code(m_job, PK_ERROR_ENUM_TRANSACTION_ERROR, std_out);
        }
        return false;
    }

    // Emit data of the now-installed DEB package
    pk_backend_job_package (m_job, PK_INFO_ENUM_INSTALLED, deb_package_id, deb_summary);
    g_free (deb_package_id);

    return true;
}

bool AptIntf::runTransaction(const PkgList &install, const PkgList &remove, bool simulate, bool markAuto, bool fixBroken, PkBitfield flags, bool autoremove)
{
    //cout << "runTransaction" << simulate << remove << endl;
    bool withLock = !simulate; // Check to see if we are just simulating,
    //since for that no lock is needed

    AptCacheFile cache(m_job);
    int timeout = 10;
    // TODO test this
    while (cache.Open(withLock) == false) {
        if (withLock == false || (timeout <= 0)) {
            show_errors(m_job, PK_ERROR_ENUM_CANNOT_GET_LOCK);
            return false;
        } else {
            _error->Discard();
            pk_backend_job_set_status (m_job, PK_STATUS_ENUM_WAITING_FOR_LOCK);
            sleep(1);
            timeout--;
        }
        // Close the cache if we are going to try again
        cache.Close();
    }

    // Check if there are half-installed packages and if we can fix them
    if (cache.CheckDeps(fixBroken) == false) {
        show_errors(m_job, PK_ERROR_ENUM_INTERNAL_ERROR);
        return false;
    }

    pk_backend_job_set_status (m_job, PK_STATUS_ENUM_RUNNING);

    // Enter the special broken fixing mode if the user specified arguments
    // THIS mode will run if fixBroken is false and the cache has broken packages
    bool BrokenFix = false;
    if (cache->BrokenCount() != 0) {
        BrokenFix = true;
    }

    unsigned int ExpectedInst = 0;
    pkgProblemResolver Fix(cache);

    // new scope for the ActionGroup
    {
        // Mark packages on hold
        for (pkgCache::PkgIterator pkg = cache->PkgBegin(); !pkg.end(); ++pkg) {
            // Check if the package is Held
            pkgDepCache::StateCache &State = cache[pkg];
            if (State.Upgradable() && State.Keep()) {
                cache->MarkKeep(pkg, false);
            }
        }

        pkgDepCache::ActionGroup group(cache);
        for (PkgList::const_iterator it = install.begin(); it != install.end(); ++it) {
            if (m_cancel) {
                break;
            }

            if (tryToInstall(*it,
                             cache,
                             Fix,
                             BrokenFix,
                             ExpectedInst) == false) {
                return false;
            }
        }

        // Mark package dependencies of a local file as auto-installed
        if (!simulate && markAuto) {
            markAutoInstalled(cache, install);
        }

        for (PkgList::const_iterator it = remove.begin(); it != remove.end(); ++it) {
            if (m_cancel) {
                break;
            }

            tryToRemove(*it, cache, Fix);
        }

        // Call the scored problem resolver
        Fix.InstallProtect();
        if (Fix.Resolve(true) == false) {
            _error->Discard();
        }

        // Now we check the state of the packages,
        if (cache->BrokenCount() != 0) {
            // if the problem resolver could not fix all broken things
            // suggest to run RepairSystem by saing that the last transaction
            // did not finish well
            cache.ShowBroken(false, PK_ERROR_ENUM_UNFINISHED_TRANSACTION);
            return false;
        }
    }

    // If we are simulating the install packages
    // will just calculate the trusted packages
    return installPackages(cache, flags, autoremove);
}

/**
 * InstallPackages - Download and install the packages
 *
 * This displays the informative messages describing what is going to
 * happen and then calls the download routines
 */
bool AptIntf::installPackages(AptCacheFile &cache, PkBitfield flags, bool autoremove)
{
    //cout << "installPackages() called" << endl;
    // Try to auto-remove packages
    if (autoremove && !doAutomaticRemove(cache)) {
        // TODO
        return false;
    }

    // check for essential packages!!!
    if (removingEssentialPackages(cache)) {
        return false;
    }

    // Sanity check
    if (cache->BrokenCount() != 0) {
        // TODO
        cache.ShowBroken(false);
        _error->Error("Internal error, InstallPackages was called with broken packages!");
        return false;
    }

    if (cache->DelCount() == 0 && cache->InstCount() == 0 &&
            cache->BadCount() == 0) {
        return true;
    }

    // Create the text record parser
    pkgRecords Recs(cache);
    if (_error->PendingError() == true) {
        return false;
    }

    // Lock the archive directory
    FileFd Lock;
    if (_config->FindB("Debug::NoLocking", false) == false) {
        Lock.Fd(GetLock(_config->FindDir("Dir::Cache::Archives") + "lock"));
        if (_error->PendingError() == true) {
            return _error->Error("Unable to lock the download directory");
        }
    }

    // Create the download object
    AcqPackageKitStatus Stat(this, m_job);

    // get a fetcher
    pkgAcquire fetcher;
    fetcher.Setup(&Stat);
    
    // Read the source list
    if (m_cache->BuildSourceList() == false) {
        return false;
    }
    pkgSourceList *List = m_cache->GetSourceList();

    // Create the package manager and prepare to download
    SPtr<pkgPackageManager> PM = _system->CreatePM(cache);
    if (PM->GetArchives(&fetcher, List, &Recs) == false ||
            _error->PendingError() == true) {
        return false;
    }
    
    // Display statistics
    double FetchBytes = fetcher.FetchNeeded();
    double FetchPBytes = fetcher.PartialPresent();
    double DebBytes = fetcher.TotalNeeded();
    if (DebBytes != cache->DebSize()) {
        cout << DebBytes << ',' << cache->DebSize() << endl;
        cout << "How odd.. The sizes didn't match, email apt@packages.debian.org";
    }

    // Number of bytes
    if (FetchBytes != 0) {
        // Emit the remainig download size
        pk_backend_job_set_download_size_remaining(m_job, FetchBytes);
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
                unsigned(Stat.f_type)            != RAMFS_MAGIC) {
            pk_backend_job_error_code(m_job,
                                  PK_ERROR_ENUM_NO_SPACE_ON_DEVICE,
                                  string("You don't have enough free space in ").append(OutputDir).c_str());
            return _error->Error("You don't have enough free space in %s.",
                                 OutputDir.c_str());
        }
    }

    if (_error->PendingError() == true) {
        cout << "PendingError " << endl;
        return false;
    }

    // Make sure we are not installing any untrusted package is untrusted is not set
    if (!checkTrusted(fetcher, flags) && !pk_bitfield_contain(flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
        return false;
    }

    if (pk_bitfield_contain(flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
        // Print out a list of packages that are going to be installed extra
        checkChangedPackages(cache, true);

        return true;
    } else {
        // Store the packages that are going to change
        // so we can emit them as we process it
        m_pkgs = checkChangedPackages(cache, false);
    }

    PkBackend *backend = PK_BACKEND(pk_backend_job_get_backend(m_job));
    pk_backend_set_simultaneous_mode(backend, true);
    // Download and check if we can continue
    if (fetcher.Run() != pkgAcquire::Continue
            && m_cancel == false) {
        // We failed and we did not cancel
        show_errors(m_job, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED);
        return false;
    }
    pk_backend_set_simultaneous_mode(backend, false);
    
    
    
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
    pk_backend_job_set_allow_cancel (m_job, false);

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

        // Debconf handlying
        gchar *socket;
        if (socket = pk_backend_job_get_frontend_socket(m_job)) {
            setenv("DEBIAN_FRONTEND", "passthrough", 1);
            setenv("DEBCONF_PIPE", socket, 1);
        } else {
            // we don't have a socket set, let's fallback to noninteractive
            setenv("DEBIAN_FRONTEND", "noninteractive", 1);
        }
        g_free(socket);

        gchar *locale;
        // Set the LANGUAGE so debconf messages get localization
        if (locale = pk_backend_job_get_locale(m_job)) {
            setenv("LANGUAGE", locale, 1);
            setenv("LANG", locale, 1);
            //setenv("LANG", "C", 1);
        }
        g_free(locale);

        // Pass the write end of the pipe to the install function
        res = PM->DoInstallPostFork(readFromChildFD[1]);

        // dump errors into cerr (pass it to the parent process)
        _error->DumpErrors();

        // finishes the child process, _exit is used to not
        // close some parent file descriptors
        _exit(res);
    }

    cout << "PARENT proccess running..." << endl;
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
