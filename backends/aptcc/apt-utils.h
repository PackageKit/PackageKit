/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (c) 2001, 2005 Daniel Burrows (aptitude)
 * Copyright (c) 2009 Daniel Nicoletti <dantti12@gmail.com>
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

#include <apt-pkg/acquire.h>
#include <apt-pkg/pkgrecords.h>
#include <glib.h>
#include <pk-backend.h>

#include "AptCacheFile.h"

using namespace std;

/**
  * Return the PkEnumGroup of the give group string.
  */
PkGroupEnum get_enum_group(string group);

/**
  * Return the changelog and extract details about the changes.
  */
string fetchChangelogData(AptCacheFile &CacheFile,
                          pkgAcquire &Fetcher,
                          pkgCache::VerIterator Ver,
                          pkgCache::VerIterator currver,
                          string *update_text,
                          string *updated,
                          string *issued);

/**
  * Returns a list of links pairs url;description for CVEs
  */
GPtrArray* getCVEUrls(const string &changelog);

/**
  * Returns a list of links pairs url;description for Debian and Ubuntu bugs
  */
GPtrArray* getBugzillaUrls(const string &changelog);

/**
  * Return if the given string ends with the other
  */
bool ends_with(const string &str, const char *end);

/**
  * Return if the given string starts with the other
  */
bool starts_with(const string &str, const char *end);

/**
  * Return true if the given package name is on the list of packages that require a restart
  */
bool utilRestartRequired(const string &packageName);

/**
  * Build a package id from the given package version
  * The caller must g_free the returned value
  */
gchar* utilBuildPackageId(const pkgCache::VerIterator &ver);

/**
  * Return an utf8 string
  */
const char *utf8(const char *str);

#endif
