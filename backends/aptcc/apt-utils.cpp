/* apt-utils.cpp
 *
 * Copyright (c) 2009 Daniel Nicoletti <dantti12@gmail.com>
 * Copyright (c) 2014 Matthias Klumpp <mak@debian.org>
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

#include <apt-pkg/version.h>
#include <apt-pkg/acquire-item.h>
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

string fetchChangelogData(AptCacheFile &CacheFile,
                          pkgAcquire &Fetcher,
                          pkgCache::VerIterator Ver,
                          pkgCache::VerIterator currver,
                          string *update_text,
                          string *updated,
                          string *issued)
{
    string changelog;

    pkgAcqChangelog *c = new pkgAcqChangelog(&Fetcher, Ver);

    // try downloading it, if that fails, try third-party-changelogs location
    // FIXME: Fetcher.Run() is "Continue" even if I get a 404?!?
    Fetcher.Run();

    // error
    pkgRecords Recs(CacheFile);
    pkgCache::PkgIterator Pkg = Ver.ParentPkg();
    pkgRecords::Parser &rec=Recs.Lookup(Ver.FileList());
    string srcpkg = rec.SourcePkg().empty() ? Pkg.Name() : rec.SourcePkg();
    changelog = "Changelog for this version is not yet available";

    // return empty string if we don't have a file to read
    if (!FileExists(c->DestFile)) {
        return changelog;
    }

    if (_error->PendingError()) {
        return changelog;
    }

    ifstream in(c->DestFile.c_str());
    string line;
    g_autoptr(GRegex) regexVer = NULL;
    regexVer = g_regex_new("(?'source'.+) \\((?'version'.*)\\) "
                           "(?'dist'.+); urgency=(?'urgency'.+)",
                           G_REGEX_CASELESS,
                           G_REGEX_MATCH_ANCHORED,
                           0);
    g_autoptr(GRegex) regexDate = NULL;
    regexDate = g_regex_new("^ -- (?'maintainer'.+) (?'mail'<.+>)  (?'date'.+)$",
                            G_REGEX_CASELESS,
                            G_REGEX_MATCH_ANCHORED,
                            0);

    changelog = "";
    while (getline(in, line)) {
        // we don't want the additional whitespace, because it can confuse
        // some markdown parsers used by client tools
        if (starts_with(line, "  "))
            line.erase(0,1);
        // no need to free str later, it is allocated in a static buffer
        const char *str = utf8(line.c_str());
        if (strcmp(str, "") == 0) {
            changelog.append("\n");
            continue;
        } else {
            changelog.append(str);
            changelog.append("\n");
        }

        if (starts_with(str, srcpkg.c_str())) {
            // Check to see if the the text isn't about the current package,
            // otherwise add a == version ==
            GMatchInfo *match_info;
            if (g_regex_match(regexVer, str, G_REGEX_MATCH_ANCHORED, &match_info)) {
                gchar *version;
                version = g_match_info_fetch_named(match_info, "version");

                // Compare if the current version is shown in the changelog, to not
                // display old changelog information
                if (_system != 0  &&
                        _system->VS->DoCmpVersion(version, version + strlen(version),
                                                  currver.VerStr(), currver.VerStr() + strlen(currver.VerStr())) <= 0) {
                    g_free (version);
                    break;
                } else {
                    if (!update_text->empty()) {
                        update_text->append("\n\n");
                    }
                    update_text->append(" == ");
                    update_text->append(version);
                    update_text->append(" ==");
                    g_free (version);
                }
            }
            g_match_info_free (match_info);
        } else if (starts_with(str, " ")) {
            // update descritption
            update_text->append("\n");
            update_text->append(str);
        } else if (starts_with(str, " --")) {
            // Parse the text to know when the update was issued,
            // and when it got updated
            GMatchInfo *match_info;
            if (g_regex_match(regexDate, str, G_REGEX_MATCH_ANCHORED, &match_info)) {
                GTimeVal dateTime = {0, 0};
                gchar *date;
                date = g_match_info_fetch_named(match_info, "date");
                time_t time;
                g_warn_if_fail(RFC1123StrToTime(date, time));
                dateTime.tv_sec = time;
                g_free(date);

                *issued = g_time_val_to_iso8601(&dateTime);
                if (updated->empty()) {
                    *updated = g_time_val_to_iso8601(&dateTime);
                }
            }
            g_match_info_free(match_info);
        }
    }

    return changelog;
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
        if (vf.File().Archive() == NULL) {
            data = "installed";
        } else {
            data += vf.File().Archive();
        }
    } else if (vf.File().Archive() != NULL) {
        data = vf.File().Archive();
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
