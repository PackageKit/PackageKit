/* pkg-list.h
 *
 * Copyright (c) 2012-2016 Daniel Nicoletti <dantti12@gmail.com>
 * Copyright (c) 2015-2026 Matthias Klumpp <matthias@tenstral.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef PKG_LIST_H
#define PKG_LIST_H

#include <apt-pkg/pkgcache.h>

#include <vector>

using std::vector;

/**
 * How a package is to be flagged (automatically or manually installed)
 * when a transaction installs it.
 */
enum class InstallMode {
    KeepCurrent, /// Keep the flag the package already has (a not-yet-installed package becomes manually installed)
    /**  */
    Auto,  /// Mark as automatically installed, e.g. a new dependency
    Manual /// Mark as manually installed, e.g. explicitly requested by the user.
};

/**
 * Information about a package, mainly containing its VerIterator
 * and the install mode to apply if the package gets installed.
 */
class PkgInfo
{
public:
    explicit PkgInfo(const pkgCache::VerIterator &verIter, InstallMode m = InstallMode::KeepCurrent)
        : ver(verIter),
          mode(m) {};
    pkgCache::VerIterator ver;
    InstallMode mode;
};

/**
 * This class is meant to show Operation Progress using PackageKit
 */
class PkgList : public vector<PkgInfo>
{
public:
    /**
     * Add a new package to the list
     * @param verIter The pkgCache::VerIterator assoicated with this package.
     * @param mode The install mode to apply if this package gets installed.
     */
    void append(const pkgCache::VerIterator &verIter, InstallMode mode = InstallMode::KeepCurrent);

    void append(const PkgInfo &pi)
    {
        this->push_back(pi);
    };

    /**
     * Return if the given vector contain a package
     */
    bool contains(const pkgCache::PkgIterator &pkg) const;

    /**
     * Sort the package list
     */
    void sort();

    /**
     * Remove duplicated packages (it's recommended to sort() first)
     */
    void removeDuplicates();
};

#endif // PKG_LIST_H
