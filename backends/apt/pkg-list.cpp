/* pkg-list.cpp
 *
 * Copyright (c) 2012-2016 Daniel Nicoletti <dantti12@gmail.com>
 * Copyright (c) 2015-2022 Matthias Klumpp <matthias@tenstral.net>
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

#include "pkg-list.h"

#include <algorithm>

// compare...uses the candidate version of each package.
class compare
{
public:
    compare() {}

    bool operator() (const PkgInfo &a, const PkgInfo &b)
    {
        const pkgCache::VerIterator &viA = a.ver;
        const pkgCache::VerIterator &viB = b.ver;
        int ret = strcmp(viA.ParentPkg().Name(), viB.ParentPkg().Name());
        if (ret == 0) {
            ret = strcmp(viA.VerStr(), viB.VerStr());
            if (ret == 0) {
                ret = strcmp(viA.Arch(), viB.Arch());
                if (ret == 0) {
                    pkgCache::VerFileIterator aVF = viA.FileList();
                    pkgCache::VerFileIterator bVF = viB.FileList();
                    ret = strcmp(aVF.File().Archive() == NULL ? "" : aVF.File().Archive(),
                                 bVF.File().Archive() == NULL ? "" : bVF.File().Archive());
                }
            }
        }
        return ret < 0;
    }
};

/** \brief operator== for match results. */
class result_equality
{
public:
    result_equality() {}

    bool operator() (const PkgInfo &a, const PkgInfo &b)
    {
        const pkgCache::VerIterator &viA = a.ver;
        const pkgCache::VerIterator &viB = b.ver;

        bool ret;
        ret = strcmp(viA.ParentPkg().Name(), viB.ParentPkg().Name()) == 0 &&
                strcmp(viA.VerStr(), viB.VerStr()) == 0 &&
                strcmp(viA.Arch(), viB.Arch()) == 0;
        if (ret) {
            pkgCache::VerFileIterator aVF = viA.FileList();
            pkgCache::VerFileIterator bVF = viB.FileList();
            ret = strcmp(aVF.File().Archive() == NULL ? "" : aVF.File().Archive(),
                         bVF.File().Archive() == NULL ? "" : bVF.File().Archive()) == 0;
        }
        return ret;
    }
};

void PkgList::append(const pkgCache::VerIterator &verIter, PkgAction action)
{
    this->push_back(PkgInfo(verIter, action));
}

bool PkgList::contains(const pkgCache::PkgIterator &pkg)
{
    for (const PkgInfo &info : *this) {
        if (info.ver.ParentPkg() == pkg) {
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
