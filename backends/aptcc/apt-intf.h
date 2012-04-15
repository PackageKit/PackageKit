/* apt-intf.h - Interface to APT
 *
 * Copyright (c) 1999-2002, 2004-2005, 2007-2008 Daniel Burrows
 * Copyright (c) 2009 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
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

#ifndef APTINTF_H
#define APTINTF_H

#include <apt-pkg/pkgrecords.h>
#include <apt-pkg/pkgcachegen.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/policy.h>

#include <pk-backend.h>

#include "AptCacheFile.h"

#define PREUPGRADE_BINARY    "/usr/bin/do-release-upgrade"
#define GDEBI_BINARY         "/usr/bin/gdebi"

using namespace std;

/**
 *  returns a list of packages names
 */
vector<string> search_files (PkBackend *backend, gchar **values, bool &_cancel);

/**
 *  returns a list of packages names
 */
vector<string> searchMimeType (PkBackend *backend, gchar **values, bool &error, bool &_cancel);

typedef vector<pkgCache::VerIterator> PkgList;

class pkgProblemResolver;
class Matcher;
class AptIntf
{

public:
    AptIntf(PkBackend *backend, bool &cancel);
    ~AptIntf();

    bool init();
    void cancel();

    /**
     * Tries to find a package with the given packageId
     * @returns pkgCache::VerIterator that if .end() is true the package could not be found
     */
    pkgCache::PkgIterator findPackage(const std::string &name);

    /**
     * Tries to find a package with the given packageId
     * @returns pkgCache::VerIterator that if .end() is true the package could not be found
     */
    pkgCache::PkgIterator findPackageArch(const std::string &name, const std::string &arch = std::string());

    /**
     * Tries to find a package with the given packageId
     * @returns pkgCache::VerIterator that if .end() is true the package could not be found
     */
    pkgCache::VerIterator findPackageId(const gchar *packageId);

    /**
     * Tries to find the current version of a package
     * if it can't find it will return the candidate
     * TODO check if we really need the candidate version
     * @returns pkgCache::VerIterator that if .end() is true the version could not be found
     */
    pkgCache::VerIterator findVer(const pkgCache::PkgIterator &pkg);

    /**
     * Tries to find the candidate version of a package
     * @returns pkgCache::VerIterator that if .end() is true the version could not be found
     */
    pkgCache::VerIterator findCandidateVer(const pkgCache::PkgIterator &pkg);

    PkgList resolvePI(gchar **package_ids, PkBitfield filters = PK_FILTER_ENUM_NONE);

    void refreshCache();

    bool markFileForInstall(const gchar *file, PkgList &install, PkgList &remove);

    bool markAutoInstalled(AptCacheFile &cache, PkgList &pkgs, bool flag);

    /**
     *  runs a transaction to install/remove/update packages
     *  - for install and update, \p remove should be set to false
     *  - if you are going to remove, \p remove should be true
     *  - if you don't want to actually install/update/remove
     *    \p simulate should be true, in this case packages with
     *    what's going to happen will be emitted.
     */
    bool runTransaction(PkgList &install, PkgList &remove, bool simulate, bool markAuto = false);

    /**
     *  Get depends
     */
    void getDepends(PkgList &output,
                    const pkgCache::VerIterator &ver,
                    bool recursive);

    /**
     *  Get requires
     */
    void getRequires(PkgList &output,
                     const pkgCache::VerIterator &ver,
                     bool recursive);

    PkgList getPackages();
    PkgList getPackagesFromGroup(vector<PkGroupEnum> &groups);
    PkgList searchPackageName(Matcher *matcher);
    PkgList searchPackageDetails(Matcher *matcher);

    /**
      * Get the long description of a package
      */
    string getPackageLongDescription(const pkgCache::VerIterator &ver);

    /**
     *  Emits a package if it match the filters
     */
    void emitPackage(const pkgCache::VerIterator &ver,
                     PkBitfield filters = PK_FILTER_ENUM_NONE,
                     PkInfoEnum state = PK_INFO_ENUM_UNKNOWN);

    bool matchPackage(const pkgCache::VerIterator &ver, PkBitfield filters);
    PkgList filterPackages(PkgList &packages, PkBitfield filters);

    void emit_packages(PkgList &output,
                       PkBitfield filters = PK_FILTER_ENUM_NONE,
                       PkInfoEnum state = PK_INFO_ENUM_UNKNOWN);

    void emitUpdates(PkgList &output, PkBitfield filters = PK_FILTER_ENUM_NONE);

    /**
     *  Emits details
     */
    void emitDetails(const pkgCache::VerIterator &ver);
    void emitDetails(PkgList &pkgs);

    /**
     *  Emits update detail
     */
    void emitUpdateDetails(const pkgCache::VerIterator &version);
    void emitUpdateDetails(PkgList &pkgs);

    /**
    *  Emits files of packages
    */
    void emitFiles(PkBackend *backend, const gchar *pi);

    /**
     *  Download and install packages
     */
    bool installPackages(AptCacheFile &cache, bool simulating);

    /**
     *  Install a DEB file
     *
     *  If you don't want to actually install/update/remove
     *    \p simulate should be true, in this case packages with
     *    what's going to happen will be emitted.
     */
    bool installFile(const gchar *path, bool simulate);

    /**
     *  Check which package provides the codec
     */
    void providesCodec(PkgList &output, gchar **values);

    /**
     *  Check which package provides a shared library
     */
    void providesLibrary(PkgList &output, gchar **values);

    /** Like pkgAcqArchive, but uses generic File objects to download to
     *  the cwd (and copies from file:/ URLs).
     */
    bool getArchive(pkgAcquire *Owner, pkgCache::VerIterator const &Version,
                    std::string directory, std::string &StoreFilename);

private:
    AptCacheFile m_cache;
    PkBackend  *m_backend;
    bool       &m_cancel;

    bool checkTrusted(pkgAcquire &fetcher, bool simulating);
    bool packageIsSupported(const pkgCache::PkgIterator &pkgIter, string component);
    bool tryToInstall(const pkgCache::PkgIterator &constPkg,
                      pkgDepCache &Cache,
                      pkgProblemResolver &Fix,
                      bool Remove,
                      bool BrokenFix,
                      unsigned int &ExpectedInst);

    /**
     *  interprets dpkg status fd
    */
    void updateInterface(int readFd, int writeFd);
    bool doAutomaticRemove(AptCacheFile &cache);
    void emitChangedPackages(AptCacheFile &cache);
    bool removingEssentialPackages(AptCacheFile &cache);

    bool m_isMultiArch;
    PkgList m_pkgs;
    string m_localDebFile;
    void populateInternalPackages(AptCacheFile &cache);
    void emitTransactionPackage(string name, PkInfoEnum state);
    time_t     m_lastTermAction;
    string     m_lastPackage;
    uint       m_lastSubProgress;
    PkInfoEnum m_state;
    bool       m_startCounting;

    // when the internal terminal timesout after no activity
    int m_terminalTimeout;
    pid_t m_child_pid;
};

#endif
