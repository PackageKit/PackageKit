/* apt-utils.h
 *
 * Copyright (c) 2001, 2005 Daniel Burrows (aptitude)
 * Copyright (c) 2009-2016 Daniel Nicoletti <dantti12@gmail.com>
 * Copyright (c) 2016-2026 Matthias Klumpp <mak@debian.org>
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
#include <pk-backend.h>

#include "apt-cache-file.h"

/**
 * Return the PkEnumGroup of the give group string.
 */
PkGroupEnum get_enum_group(std::string group);

/**
 * Return the changelog and extract details about the changes.
 */
std::string fetchChangelogData(
    AptCacheFile &CacheFile,
    pkgAcquire &Fetcher,
    pkgCache::VerIterator Ver,
    pkgCache::VerIterator currver,
    std::string *update_text,
    std::string *updated,
    std::string *issued);

/**
 * Returns a list of links pairs url;description for CVEs
 */
GPtrArray *getCVEUrls(const std::string &changelog);

/**
 * Returns a list of links pairs url;description for Debian and Ubuntu bugs
 */
GPtrArray *getBugzillaUrls(const std::string &changelog);

/**
 * Return if the given string ends with the other
 */
bool ends_with(const std::string &str, const char *end);

/**
 * Return if the given string starts with the other
 */
bool starts_with(const std::string &str, const char *end);

/**
 * Return true if the given package name is on the list of packages that require a restart
 */
bool utilRestartRequired(const std::string &packageName);

/**
 * Build a unique repository origin, in the form of
 * {distro}-{suite}-{component}
 */
std::string utilBuildPackageOriginId(pkgCache::VerFileIterator vf);

/**
 * Return an utf8 string
 */
const char *toUtf8(const char *str);

/**
 * Wrap a possibly-null C string in a std::string, treating null as empty.
 * Convenient for libapt-pkg accessors that may return nullptr.
 */
inline std::string safeStr(const char *s)
{
    return s ? std::string{s} : std::string{};
}

/**
 * Changelog dates are in format RFC2822/RFC5322 compatible:
 * "day-of-week, dd month yyyy hh:mm:ss +zzzz"
 * Parses and converts to ISO8601 respecting the original timezone
 */
std::string changelogDateToIso8601(const std::string &date_str);

#endif
