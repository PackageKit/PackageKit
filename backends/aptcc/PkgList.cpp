/* PkgList.cpp
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

#include "PkgList.h"

#include <algorithm>

// compare...uses the candidate version of each package.
class compare
{
public:
    compare() {}
    
    bool operator()(const pkgCache::VerIterator &a,
                    const pkgCache::VerIterator &b) {
        int ret = strcmp(a.ParentPkg().Name(), b.ParentPkg().Name());
        if (ret == 0) {
            return strcmp(a.VerStr(), b.VerStr()) < 0;
        }
        return ret < 0;
                    }
};

/** \brief operator== for match results. */
class result_equality
{
public:
    result_equality() {}
    
    bool operator() (const pkgCache::VerIterator &a, const pkgCache::VerIterator &b) {
        return strcmp(a.ParentPkg().Name(), b.ParentPkg().Name()) == 0 &&
        strcmp(a.VerStr(), b.VerStr()) == 0 &&
        strcmp(a.Arch(), b.Arch()) == 0;
    }
};

bool PkgList::contains(const pkgCache::PkgIterator &pkg)
{
    for (PkgList::const_iterator it = begin(); it != end(); ++it) {
        if (it->ParentPkg() == pkg) {
            return true;
        }
    }
    return false;
}

void PkgList::sort()
{
    // Sort so we can remove the duplicated entries
    std::sort(begin(), end(), compare());
}

void PkgList::removeDuplicates()
{
    // Remove the duplicated entries
    erase(unique(begin(), end(), result_equality()), end());
}
