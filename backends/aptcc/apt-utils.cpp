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

#include <apt-pkg/fileutl.h>
#include <apt-pkg/error.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/version.h>
#include <apt-pkg/acquire-item.h>
#include <glib/gstdio.h>

#include <fstream>
#include <regex>

PkGroupEnum get_enum_group(string group)
{
    if (group.compare ("admin") == 0) {
        return PK_GROUP_ENUM_ADMIN_TOOLS;
    } else if (group.compare ("base") == 0) {
        return PK_GROUP_ENUM_SYSTEM;
    } else if (group.compare ("cli-mono") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("comm") == 0) {
        return PK_GROUP_ENUM_COMMUNICATION;
    } else if (group.compare ("database") == 0) {
        return PK_GROUP_ENUM_ADMIN_TOOLS;
    } else if (group.compare ("debug") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("devel") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("doc") == 0) {
        return PK_GROUP_ENUM_DOCUMENTATION;
    } else if (group.compare ("editors") == 0) {
        return PK_GROUP_ENUM_PUBLISHING;
    } else if (group.compare ("education") == 0) {
        return PK_GROUP_ENUM_EDUCATION;
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
    } else if (group.compare ("gnu-r") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("gnustep") == 0) {
        return PK_GROUP_ENUM_DESKTOP_OTHER;
    } else if (group.compare ("golang") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("graphics") == 0) {
        return PK_GROUP_ENUM_GRAPHICS;
    } else if (group.compare ("hamradio") == 0) {
        return PK_GROUP_ENUM_COMMUNICATION;
    } else if (group.compare ("haskell") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("httpd") == 0) {
        return PK_GROUP_ENUM_SERVERS;
    } else if (group.compare ("interpreters") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("introspection") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("java") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("javascript") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("kde") == 0) {
        return PK_GROUP_ENUM_DESKTOP_KDE;
    } else if (group.compare ("kernel") == 0) {
        return PK_GROUP_ENUM_SYSTEM;
    } else if (group.compare ("libdevel") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("libs") == 0) {
        return PK_GROUP_ENUM_SYSTEM;
    } else if (group.compare ("lisp") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
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
    } else if (group.compare ("ocaml") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("oldlibs") == 0) {
        return PK_GROUP_ENUM_LEGACY;
    } else if (group.compare ("otherosfs") == 0) {
        return PK_GROUP_ENUM_SYSTEM;
    } else if (group.compare ("perl") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("php") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("python") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("ruby") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("rust") == 0) {
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
    } else if (group.compare ("vcs") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
    } else if (group.compare ("video") == 0) {
        return PK_GROUP_ENUM_MULTIMEDIA;
    } else if (group.compare ("web") == 0) {
        return PK_GROUP_ENUM_INTERNET;
    } else if (group.compare ("x11") == 0) {
        return PK_GROUP_ENUM_DESKTOP_OTHER;
    } else if (group.compare ("xfce") == 0) {
        return PK_GROUP_ENUM_DESKTOP_XFCE;
    } else if (group.compare ("zope") == 0) {
        return PK_GROUP_ENUM_PROGRAMMING;
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
                g_autoptr(GDateTime) dateTime = NULL;
                g_autofree gchar *date = NULL;
                date = g_match_info_fetch_named(match_info, "date");
                time_t time;
                g_warn_if_fail(RFC1123StrToTime(date, time));
                dateTime = g_date_time_new_from_unix_local(time);

                *issued = g_date_time_format_iso8601(dateTime);
                if (updated->empty()) {
                    *updated = g_date_time_format_iso8601(dateTime);
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

string utilBuildPackageOriginId(pkgCache::VerFileIterator vf)
{
    if (vf.File().Origin() == nullptr)
        return string("local");
    if (vf.File().Archive() == nullptr)
        return string("local");
    if (vf.File().Component() == nullptr)
        return string("invalid");

    // https://wiki.debian.org/DebianRepository/Format
    // Optional field indicating the origin of the repository, a single line of free form text.
    // e.g. "Debian" or "Google Inc."
    auto origin = string(vf.File().Origin());
    // The Suite field may describe the suite. A suite is a single word.
    // e.g. "jessie" or "sid"
    auto suite = string(vf.File().Archive());
    // An area within the repository. May be prefixed by parts of the path
    // following the directory beneath dists.
    // e.g. "main" or "non-free"
    // NOTE: this may need the slash stripped, currently having a slash doesn't
    //    seem a problem though. we'll allow them until otherwise indicated
    auto component = string(vf.File().Component());

    // Origin is defined as 'a single line of free form text'.
    // Sanitize it!
    // All space characters, control characters and punctuation get replaced
    // with underscore.
    // In particular the punctuations ',' and ';' may be used as list separators
    // so we must not have them appear in our package_ids as that would break
    // any number of higher level features.
    std::transform(origin.begin(), origin.end(), origin.begin(), ::tolower);
    origin = std::regex_replace(origin, std::regex("[[:space:][:cntrl:][:punct:]]+"), "_");

    string res = origin + "-" + suite + "-" + component;
    return res;
}

gchar* utilBuildPackageId(AptCacheFile *cacheFile, const pkgCache::VerIterator &ver)
{
    pkgCache::VerFileIterator vf = ver.FileList();
    const pkgCache::PkgIterator &pkg = ver.ParentPkg();
    pkgDepCache::StateCache &State = (*cacheFile)[pkg];

    const bool isInstalled = (pkg->CurrentState == pkgCache::State::Installed && pkg.CurrentVer() == ver);
    const bool isAuto = (State.CandidateVer != 0) && (State.Flags & pkgCache::Flag::Auto);

    // when a package is installed manually, the data part of a package-id is "manual:<repo-id>",
    // otherwise it is "auto:<repo-id>". Available (not installed) packages have no prefix, unless
    // a pending installation is marked, in which case we prefix the desired new mode of the installed
    // package (auto/manual) with a plus sign (+).
    string data;
    if (isInstalled) {
        data = isAuto? "auto:" : "manual:";
        data += utilBuildPackageOriginId(vf);
    } else {
        if (State.NewInstall()) {
            data = isAuto? "+auto:" : "+manual:";
            data += utilBuildPackageOriginId(vf);
        } else {
            data = utilBuildPackageOriginId(vf);
        }
    }

    return pk_package_id_build(ver.ParentPkg().Name(),
                               ver.VerStr(),
                               ver.Arch(),
                               data.c_str());
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
