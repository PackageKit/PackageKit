/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (c) 2001, 2005 Daniel Burrows (aptitude)
 * Copyright (c) 2009 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
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

#ifndef APT_UTILS_H
#define APT_UTILS_H

#include <apt-pkg/pkgrecords.h>

#include <string.h>
#include <set>
#include <pk-backend.h>

#include "apt-intf.h"
#include "pkg_acqfile.h"

using namespace std;

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

/**
  * Return the PkEnumGroup of the give group string.
  */
PkGroupEnum get_enum_group(string group);

/**
  * Return the changelog filename fetched
  */
string getChangelogFile(const string &name,
                        const string &origin,
                        const string &verstr,
                        const string &srcPkg,
                        const string &uri,
                        pkgAcquire *fetcher);

/**
  * Returns a list of links pairs url;description for CVEs
  */
string getCVEUrls(const string &changelog);

/**
  * Returns a list of links pairs url;description for Debian and Ubuntu bugs
  */
string getBugzillaUrls(const string &changelog);

/**
  * Return if the given vector contain a package
  */
bool contains(PkgList packages, const pkgCache::PkgIterator &pkg);

/**
  * Return if the given string ends with the other
  */
bool ends_with(const string &str, const char *end);

/**
  * Return if the given string starts with the other
  */
bool starts_with(const string &str, const char *end);

/**
  * Return an utf8 string
  */
const char *utf8(const char *str);

#endif
