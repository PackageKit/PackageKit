/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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

#include "apt-utils.h"

#include "pkg_acqfile.h"

#include <glib/gstdio.h>

#include <fstream>

PkGroupEnum get_enum_group(string group)
{
    if (group.compare ("admin") == 0) {
        return PK_GROUP_ENUM_ADMIN_TOOLS;
    } else if (group.compare ("base") == 0) {
        return PK_GROUP_ENUM_SYSTEM;
    } else if (group.compare ("comm") == 0) {
        return PK_GROUP_ENUM_COMMUNICATION;
    } else if (group.compare ("devel") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("doc") == 0) {
        return PK_GROUP_ENUM_DOCUMENTATION;
    } else if (group.compare ("editors") == 0) {
        return PK_GROUP_ENUM_PUBLISHING;
    } else if (group.compare ("electronics") == 0) {
        return PK_GROUP_ENUM_ELECTRONICS;
    } else if (group.compare ("embedded") == 0) {
        return PK_GROUP_ENUM_SYSTEM;
    } else if (group.compare ("fonts") == 0) {
        return PK_GROUP_ENUM_FONTS;
    } else if (group.compare ("games") == 0) {
        return PK_GROUP_ENUM_GAMES;
    } else if (group.compare ("gnome") == 0) {
        return PK_GROUP_ENUM_DESKTOP_GNOME;
    } else if (group.compare ("graphics") == 0) {
        return PK_GROUP_ENUM_GRAPHICS;
    } else if (group.compare ("hamradio") == 0) {
        return PK_GROUP_ENUM_COMMUNICATION;
    } else if (group.compare ("interpreters") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("kde") == 0) {
        return PK_GROUP_ENUM_DESKTOP_KDE;
    } else if (group.compare ("libdevel") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("libs") == 0) {
        return PK_GROUP_ENUM_SYSTEM;
    } else if (group.compare ("mail") == 0) {
        return PK_GROUP_ENUM_INTERNET;
    } else if (group.compare ("math") == 0) {
        return PK_GROUP_ENUM_SCIENCE;
    } else if (group.compare ("misc") == 0) {
        return PK_GROUP_ENUM_OTHER;
    } else if (group.compare ("net") == 0) {
        return PK_GROUP_ENUM_NETWORK;
    } else if (group.compare ("news") == 0) {
        return PK_GROUP_ENUM_INTERNET;
    } else if (group.compare ("oldlibs") == 0) {
        return PK_GROUP_ENUM_LEGACY;
    } else if (group.compare ("otherosfs") == 0) {
        return PK_GROUP_ENUM_SYSTEM;
    } else if (group.compare ("perl") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("python") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("science") == 0) {
        return PK_GROUP_ENUM_SCIENCE;
    } else if (group.compare ("shells") == 0) {
        return PK_GROUP_ENUM_SYSTEM;
    } else if (group.compare ("sound") == 0) {
        return PK_GROUP_ENUM_MULTIMEDIA;
    } else if (group.compare ("tex") == 0) {
        return PK_GROUP_ENUM_PUBLISHING;
    } else if (group.compare ("text") == 0) {
        return PK_GROUP_ENUM_PUBLISHING;
    } else if (group.compare ("utils") == 0) {
        return PK_GROUP_ENUM_ACCESSORIES;
    } else if (group.compare ("web") == 0) {
        return PK_GROUP_ENUM_INTERNET;
    } else if (group.compare ("x11") == 0) {
        return PK_GROUP_ENUM_DESKTOP_OTHER;
    } else if (group.compare ("alien") == 0) {
        return PK_GROUP_ENUM_UNKNOWN;//FIXME alien is an unknown group?
    } else if (group.compare ("translations") == 0) {
        return PK_GROUP_ENUM_LOCALIZATION;
    } else if (group.compare ("metapackages") == 0) {
        return PK_GROUP_ENUM_COLLECTIONS;
    } else {
        return PK_GROUP_ENUM_UNKNOWN;
    }
}

bool strIsPrefix(string const& s1, string const&s2)
{
    const char*p = s1.c_str();
    const char*q = s2.c_str();
    while (*p&&*q)
        if (*p++!=*q++)
            return false;
    return true;
}

/*}}}*/
// GetChangelogPath - return a path pointing to a changelog file or dir /*{{{*/
// ---------------------------------------------------------------------
/* This returns a "path" string for the changelog url construction.
 * Please note that its not complete, it either needs a "/changelog"
 * appended (for the packages.debian.org/changelogs site) or a
 * ".changelog" (for third party sites that store the changelog in the
 * pool/ next to the deb itself)
 * Example return: "main/a/apt/apt_0.8.8ubuntu3"
 */
string GetChangelogPath(AptCacheFile &Cache,
                        pkgCache::PkgIterator Pkg,
                        pkgCache::VerIterator Ver)
{
   string path;

   pkgRecords Recs(Cache);
   pkgRecords::Parser &rec=Recs.Lookup(Ver.FileList());
   string srcpkg = rec.SourcePkg().empty() ? Pkg.Name() : rec.SourcePkg();
   string ver = Ver.VerStr();
   // if there is a source version it always wins
   if (rec.SourceVer() != "")
      ver = rec.SourceVer();
   path = flNotFile(rec.FileName());

   if (strIsPrefix(path, "pool/")) {
       // the returned string starts with pool/, remove it
       path.erase (0, 5);
   }

   path += srcpkg + "_" + StripEpoch(ver);
   return path;
}

/*}}}*/
// GuessThirdPartyChangelogUri - return url 			        /*{{{*/
// ---------------------------------------------------------------------
/* Contruct a changelog file path for third party sites that do not use
 * packages.debian.org/changelogs
 * This simply uses the ArchiveURI() of the source pkg and looks for
 * a .changelog file there, Example for "mediabuntu":
 * apt-get changelog mplayer-doc:
 *  http://packages.medibuntu.org/pool/non-free/m/mplayer/mplayer_1.0~rc4~try1.dsfg1-1ubuntu1+medibuntu1.changelog
 */
bool GuessThirdPartyChangelogUri(AptCacheFile &Cache,
                                 pkgCache::PkgIterator Pkg,
                                 pkgCache::VerIterator Ver,
                                 string &out_uri)
{
   // get the binary deb server path
   pkgCache::VerFileIterator Vf = Ver.FileList();
   if (Vf.end() == true)
      return false;
   pkgCache::PkgFileIterator F = Vf.File();
   pkgIndexFile *index;
   pkgSourceList *SrcList = Cache.GetSourceList();
   if(SrcList->FindIndex(F, index) == false)
      return false;

   // get archive uri for the binary deb
   string path_without_dot_changelog;
   strprintf(path_without_dot_changelog, "%s/%s", "pool", GetChangelogPath(Cache, Pkg, Ver).c_str());
   out_uri = index->ArchiveURI(path_without_dot_changelog + ".changelog");

   // now strip away the filename and add srcpkg_srcver.changelog
   return true;
}

bool downloadChangelog(AptCacheFile &CacheFile,
                       pkgAcquire &Fetcher,
                       pkgCache::VerIterator Ver,
                       string targetfile)
/* Download a changelog file for the given package version to
 * targetfile. This will first try the server from Apt::Changelogs::Server
 * (http://packages.debian.org/changelogs by default) and if that gives
 * a 404 tries to get it from the archive directly (see
 * GuessThirdPartyChangelogUri for details how)
 */
{
   string path;
   string descr;
   string server;
   string changelog_uri;
   string origin;

   if (!Ver.end()) {
        pkgCache::VerFileIterator vf = Ver.FileList();
        origin = vf.File().Origin() == NULL ? "" : vf.File().Origin();
    }

   // data structures we need
   pkgCache::PkgIterator Pkg = Ver.ParentPkg();

   // make the server root configurable
   server = _config->Find("Apt::Changelogs::Server",
                          "http://packages.debian.org/changelogs");
   path = GetChangelogPath(CacheFile, Pkg, Ver);

   if (origin.compare("Ubuntu") == 0)
       strprintf(changelog_uri, "%s/%s/%s/changelog", server.c_str(), "pool", path.c_str());
   else
       strprintf(changelog_uri, "%s/%s_changelog", server.c_str(), path.c_str());

   g_debug("Trying to fetch '%s'", changelog_uri.c_str());
   strprintf(descr, "Changelog for %s", Pkg.Name());
   // queue it
   new pkgAcqFile(&Fetcher, changelog_uri, "", 0, descr, Pkg.Name(), "ignored", targetfile);

   // try downloading it, if that fails, try third-party-changelogs location
   // FIXME: Fetcher.Run() is "Continue" even if I get a 404?!?
   Fetcher.Run();
   if (!FileExists(targetfile)) {
        string third_party_uri;
        if (GuessThirdPartyChangelogUri(CacheFile, Pkg, Ver, third_party_uri)) {
            g_debug("Trying to fetch '%s'", third_party_uri.c_str());
            strprintf(descr, "Changelog for %s", Pkg.Name());
            new pkgAcqFile(&Fetcher, third_party_uri, "", 0, descr, Pkg.Name(), "ignored", targetfile);
            Fetcher.Run();
        }
   }

   if (FileExists(targetfile)) {
        return true;
   }

   // error
   pkgRecords Recs(CacheFile);
   pkgRecords::Parser &rec=Recs.Lookup(Ver.FileList());
   string srcpkg = rec.SourcePkg().empty() ? Pkg.Name() : rec.SourcePkg();
   strprintf(descr, "Changelog for this version is not yet available");
   return false;
}

void getChangelogFile(const string &filename,
                      const string &name,
                      const string &origin,
                      const string &verstr,
                      const string &srcPkg,
                      const string &uri,
                      pkgAcquire *fetcher)
{
    string descr("Changelog for ");
    descr += name;

    new pkgAcqFileSane(fetcher, uri, descr, name, filename);

    ofstream out(filename.c_str());
    if (fetcher->Run() == pkgAcquire::Failed) {
        out << "Failed to download the list of changes. " << endl;
        out << "Please check your Internet connection." << endl;
        // FIXME: Need to dequeue the item
    } else {
        struct stat filestatus;
        stat(filename.c_str(), &filestatus );

        if (filestatus.st_size == 0) {
            // FIXME: Use supportedOrigins
            if (origin.compare("Ubuntu") == 0) {
                out << "The list of changes is not available yet.\n" << endl;
                out << "Please use http://launchpad.net/ubuntu/+source/"<< srcPkg <<
                       "/" << verstr << "/+changelog" << endl;
                out << "until the changes become available or try again later." << endl;
            } else {
                out << "This change is not coming from a source that supports changelogs.\n" << endl;
                out << "Failed to fetch the changelog for " << name << endl;
                out << "URI was: " << uri << endl;
            }
        }
    }
    out.close();
}

GPtrArray* getCVEUrls(const string &changelog)
{
    GPtrArray *cve_urls = g_ptr_array_new();

    // Regular expression to find cve references
    GRegex *regex;
    GMatchInfo *match_info;
    regex = g_regex_new("CVE-\\d{4}-\\d{4,}",
                        G_REGEX_CASELESS,
                        G_REGEX_MATCH_NEWLINE_ANY,
                        0);
    g_regex_match (regex, changelog.c_str(), G_REGEX_MATCH_NEWLINE_ANY, &match_info);
    while (g_match_info_matches(match_info)) {
        gchar *cve = g_match_info_fetch (match_info, 0);
        gchar *cveLink;

        cveLink = g_strdup_printf("http://web.nvd.nist.gov/view/vuln/detail?vulnId=%s", cve);
        g_ptr_array_add(cve_urls, (gpointer) cveLink);

        g_free(cve);
        g_match_info_next(match_info, NULL);
    }
    g_match_info_free(match_info);
    g_regex_unref(regex);

    // NULL terminate
    g_ptr_array_add(cve_urls, NULL);

    return cve_urls;
}

GPtrArray* getBugzillaUrls(const string &changelog)
{
    GPtrArray *bugzilla_urls = g_ptr_array_new();

    // Matches Ubuntu bugs
    GRegex *regex;
    GMatchInfo *match_info;
    regex = g_regex_new("LP:\\s+(?:[,\\s*]?#(?'bug'\\d+))*",
                        G_REGEX_CASELESS,
                        G_REGEX_MATCH_NEWLINE_ANY,
                        0);
    g_regex_match (regex, changelog.c_str(), G_REGEX_MATCH_NEWLINE_ANY, &match_info);
    while (g_match_info_matches(match_info)) {
        gchar *bug = g_match_info_fetch_named(match_info, "bug");
        gchar *bugLink;

        bugLink = g_strdup_printf("https://bugs.launchpad.net/bugs/%s", bug);
        g_ptr_array_add(bugzilla_urls, (gpointer) bugLink);

        g_free(bug);
        g_match_info_next(match_info, NULL);
    }
    g_match_info_free(match_info);
    g_regex_unref(regex);

    // Debian bugs
    // Regular expressions to detect bug numbers in changelogs according to the
    // Debian Policy Chapter 4.4. For details see the footnote 15:
    // http://www.debian.org/doc/debian-policy/footnotes.html#f15
    // /closes:\s*(?:bug)?\#?\s?\d+(?:,\s*(?:bug)?\#?\s?\d+)*/i
    regex = g_regex_new("closes:\\s*(?:bug)?\\#?\\s?(?'bug1'\\d+)(?:,\\s*(?:bug)?\\#?\\s?(?'bug2'\\d+))*",
                        G_REGEX_CASELESS,
                        G_REGEX_MATCH_NEWLINE_ANY,
                        0);
    g_regex_match (regex, changelog.c_str(), G_REGEX_MATCH_NEWLINE_ANY, &match_info);
    while (g_match_info_matches(match_info)) {
        gchar *bug1 = g_match_info_fetch_named(match_info, "bug1");
        gchar *bugLink1;

        bugLink1 = g_strdup_printf("http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=%s", bug1);
        g_ptr_array_add(bugzilla_urls, (gpointer) bugLink1);

        g_free(bug1);

        gchar *bug2 = g_match_info_fetch_named(match_info, "bug2");
        if (bug2 != NULL) {
            gchar *bugLink2;

            bugLink2 = g_strdup_printf("http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=%s", bug2);
            g_ptr_array_add(bugzilla_urls, (gpointer) bugLink2);

            g_free(bug2);
        }

        g_match_info_next(match_info, NULL);
    }
    g_match_info_free(match_info);
    g_regex_unref(regex);

    // NULL terminate
    g_ptr_array_add(bugzilla_urls, NULL);

    return bugzilla_urls;
}

bool ends_with(const string &str, const char *end)
{
    size_t endSize = strlen(end);
    return str.size() >= endSize && (memcmp(str.data() + str.size() - endSize, end, endSize) == 0);
}

bool starts_with(const string &str, const char *start)
{
    size_t startSize = strlen(start);
    return str.size() >= startSize && (strncmp(str.data(), start, startSize) == 0);
}

bool utilRestartRequired(const string &packageName)
{
    if (starts_with(packageName, "linux-image-") ||
            starts_with(packageName, "nvidia-") ||
            packageName == "libc6" ||
            packageName == "dbus") {
        return true;
    }
    return false;
}

gchar* utilBuildPackageId(const pkgCache::VerIterator &ver)
{
    gchar *package_id;
    pkgCache::VerFileIterator vf = ver.FileList();
 
    string data;
    const pkgCache::PkgIterator &pkg = ver.ParentPkg();
    if (pkg->CurrentState == pkgCache::State::Installed &&
            pkg.CurrentVer() == ver) {
        data = "installed:";
    }
    
    if (vf.File().Archive() != NULL) {
        data += vf.File().Archive();
    }
    
    package_id = pk_package_id_build(ver.ParentPkg().Name(),
                                     ver.VerStr(),
                                     ver.Arch(),
                                     data.c_str());
    return package_id;
}

const char *utf8(const char *str)
{
    static char *_str = NULL;
    if (str == NULL) {
        return NULL;
    }

    if (g_utf8_validate(str, -1, NULL) == true) {
        return str;
    }

    g_free(_str);
    _str = NULL;
    _str = g_locale_to_utf8(str, -1, NULL, NULL, NULL);
    return _str;
}
