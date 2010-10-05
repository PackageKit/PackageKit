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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef APT_UTILS_H
#define APT_UTILS_H

#include <apt-pkg/pkgrecords.h>

#include <pk-backend.h>

#include <string.h>
#include <set>

#include "apt.h"

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

/**
  * Return the changelog filename fetched
  */
string getChangelogFile(const string &name, const string &uri, pkgAcquire *fetcher);

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
bool contains(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > packages,
	      const pkgCache::PkgIterator pkg);

/**
  * Return if the given string ends with the other
  */
bool ends_with(const string &str, const char *end);

/**
  * Return if the given string starts with the other
  */
bool starts_with(const string &str, const char *end);

GDateTime* dateFromString(const gchar *tz,
                          const gchar *year,
                          const gchar *month,
                          const gchar *day,
                          const gchar *hour,
                          const gchar *minute,
                          const gchar *seconds);
/**
  * Return an utf8 string
  */
const char *utf8(const char *str);

#endif
