/* pkg-list.h
 *
 * Copyright (c) 2012 Daniel Nicoletti <dantti12@gmail.com>
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
#ifndef PKG_LIST_H
#define PKG_LIST_H

#include <apt-pkg/pkgcache.h>

#include <vector>

using std::vector;

/**
 * This class is meant to show Operation Progress using PackageKit
 */
class PkgList : public vector<pkgCache::VerIterator>
{
public:
    /**
     * Return if the given vector contain a package
     */
    bool contains(const pkgCache::PkgIterator &pkg);

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
