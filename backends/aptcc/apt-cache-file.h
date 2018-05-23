/* apt-cache-file.h
 *
 * Copyright (c) 2012 Daniel Nicoletti <dantti12@gmail.com>
 * Copyright (c) 2012 Matthias Klumpp <matthias@tenstral.net>
 * Copyright (c) 2016 Harald Sitter <sitter@kde.org>
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
#ifndef APT_CACHE_FILE_H
#define APT_CACHE_FILE_H

#include <apt-pkg/cachefile.h>
#include <pk-backend.h>

class pkgProblemResolver;
class AptCacheFile : public pkgCacheFile
{
public:
    AptCacheFile(PkBackendJob *job);
    ~AptCacheFile();

    /**
      * Inits the package cache returning false if it can't open
      */
    bool Open(bool withLock = false);

    /**
      * Closes the package cache
      */
    void Close();

    /**
      * Build caches
      */
    bool BuildCaches(bool withLock = false);

    /**
      * This routine generates the caches and then opens the dependency cache
      * and verifies that the system is OK.
      * @param AllowBroken when true it will try to perform the instalation
      * even if we have broken packages, when false it will try to fix
      * the current situation
      */
    bool CheckDeps(bool AllowBroken = false);

    /**
     * Mark Cache for dist-upgrade
     */
    bool DistUpgrade();

    /** Shows a list of all broken packages together with their
     *  dependencies.  Similar to and based on the equivalent routine in
     *  apt-get.
     */
    void ShowBroken(bool Now, PkErrorEnum error = PK_ERROR_ENUM_DEP_RESOLUTION_FAILED);

    inline pkgRecords* GetPkgRecords() { buildPkgRecords(); return m_packageRecords; }

    /**
      * GetPolicy will build the policy object if needed and return it
      * @note This override if because the cache should be built before the policy
      */
    inline pkgPolicy* GetPolicy() { BuildCaches(); BuildPolicy(); return Policy; }

    /**
      * GetDepCache will build the dependency cache if needed and return it
      * @note This override if because the policy should be built before the dependency cache
      */
    inline pkgDepCache* GetDepCache() { BuildCaches(); BuildPolicy(); BuildDepCache(); return DCache; }

    /**
     * Checks if the package is garbage (not depended on)
     */
    bool isGarbage(const pkgCache::PkgIterator &pkg);

    /**
     * DoAutomaticRemove - Remove all automatic unused packages
     *
     * Remove unused automatic packages
     */
    bool doAutomaticRemove();

    /**
     * Checks if there are Essential packages marked to be removed
     */
    bool isRemovingEssentialPackages();

    /**
     * Tries to find a package with the given packageId
     * @returns pkgCache::VerIterator, if .end() is true the package could not be found
     */
    pkgCache::VerIterator resolvePkgID(const gchar *packageId);

    /**
     * Tries to find the candidate version of a package
     * @returns pkgCache::VerIterator, if .end() is true the version could not be found
     */
    pkgCache::VerIterator findCandidateVer(const pkgCache::PkgIterator &pkg);

    /**
     * Tries to find the current version of a package
     * if it can't find it will return the candidate
     * TODO check if we really need the candidate version
     * @returns pkgCache::VerIterator, if .end() is true the version could not be found
     */
    pkgCache::VerIterator findVer(const pkgCache::PkgIterator &pkg);

    /** \return a short description string corresponding to the given
     *  version.
     */
    std::string getShortDescription(const pkgCache::VerIterator &ver);

    /** \return a short description string corresponding to the given
     *  version.
     */
    std::string getLongDescription(const pkgCache::VerIterator &ver);

    /** \return a short description string corresponding to the given
     *  version.
     */
    std::string getLongDescriptionParsed(const pkgCache::VerIterator &ver);

    bool tryToInstall(pkgProblemResolver &Fix,
                      const pkgCache::VerIterator &ver,
                      bool BrokenFix, bool autoInst, bool preserveAuto);

    void tryToRemove(pkgProblemResolver &Fix,
                     const pkgCache::VerIterator &ver);

private:
    void buildPkgRecords();
    static std::string debParser(std::string descr);

    pkgRecords *m_packageRecords;
    PkBackendJob *m_job;
};

/**
 * This class is maent to show Operation Progress using PackageKit
 */
class OpPackageKitProgress : public OpProgress
{
public:
    OpPackageKitProgress(PkBackendJob *job);
    virtual ~OpPackageKitProgress();

    virtual void Done();

protected:
    virtual void Update();

private:
    PkBackendJob  *m_job;
};

#endif // APT_CACHE_FILE_H
