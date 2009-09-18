/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 200 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
 * Copyright (C) 2001, 2005 Daniel Burrows (aptitude)
 *
 * Licensed under the GNU General Public License Version 2
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef APT_UTILS_H
#define APT_UTILS_H

#include <apt-pkg/pkgrecords.h>

#include <packagekit-glib2/packagekit.h>
#include <pk-backend.h>

#include <string.h>
#include <set>

#include "apt.h"

typedef std::set<pkgCache::PkgIterator> pkgset;
typedef std::vector<pkgCache::PkgIterator> pkgvector;

using namespace std;

// compare...uses the candidate version of each package.
class compare
{
public:
	compare() {}

	bool operator()(const pair<pkgCache::PkgIterator, pkgCache::VerIterator> &a,
			const pair<pkgCache::PkgIterator, pkgCache::VerIterator> &b)
	{
		int ret = strcmp(a.first.Name(), b.first.Name());
		if (ret == 0) {
		    return strcmp(a.second.VerStr(), b.second.VerStr()) < 0;
		}
		return ret < 0;
	}
};

/** \brief operator== for match results. */
class result_equality
{
public:
	result_equality() {}

	bool operator()(const pair<pkgCache::PkgIterator, pkgCache::VerIterator> &a,
			const pair<pkgCache::PkgIterator, pkgCache::VerIterator> &b)
	{
		return strcmp(a.first.Name(), b.first.Name()) == 0 &&
		       strcmp(a.second.VerStr(), b.second.VerStr()) == 0;
	}
};

/** \return a short description string corresponding to the given
 *  version.
 */
string get_default_short_description(const pkgCache::VerIterator &ver,
				     pkgRecords *records);

/** \return a short description string corresponding to the given
 *  version.
 */
string get_short_description(const pkgCache::VerIterator &ver,
			     pkgRecords *records);


/** \return a short description string corresponding to the given
 *  version.
 */
string get_default_long_description(const pkgCache::VerIterator &ver,
				    pkgRecords *records);

/** \return a short description string corresponding to the given
 *  version.
 */
string get_long_description(const pkgCache::VerIterator &ver,
			    pkgRecords *records);

/** \return a short description string corresponding to the given
 *  version.
 */
string get_long_description_parsed(const pkgCache::VerIterator &ver,
				   pkgRecords *records);

/**
  * Return the PkEnumGroup of the give group string.
  */
PkGroupEnum get_enum_group(string group);

enum pkg_action_state {pkg_unchanged=-1,
		       pkg_broken,
		       pkg_unused_remove,
		       pkg_auto_hold,
		       pkg_auto_install,
		       pkg_auto_remove,
		       pkg_downgrade,
		       pkg_hold,
		       pkg_reinstall,
		       pkg_install,
		       pkg_remove,
		       pkg_upgrade,
                       pkg_unconfigured};
const int num_pkg_action_states=12;
pkg_action_state find_pkg_state(pkgCache::PkgIterator pkg,
				aptcc &cache);

/**
  * Return if the given vector contain a package
  */
bool contains(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > packages,
	      const pkgCache::PkgIterator pkg);

/**
  * Return if the given string ends with the other
  */
bool ends_with(const string &str, const char *end);

#endif
