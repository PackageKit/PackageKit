// apt.cc
//
//  Copyright 1999-2008 Daniel Burrows
//  Copyright (C) 2009 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
//  Copyright (c) 2004 Michael Vogt <mvo@debian.org>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; see the file COPYING.  If not, write to
//  the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
//  Boston, MA 02111-1307, USA.

#include "apt.h"
#include "apt-utils.h"
#include "matcher.h"
#include "gstMatcher.h"
#include "aptcc_show_broken.h"
#include "acqprogress.h"
#include "pkg_acqfile.h"
#include "aptcc_show_error.h"

#include <apt-pkg/error.h>
#include <apt-pkg/tagfile.h>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/init.h>
#include <apt-pkg/sptr.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/version.h>

#include <sys/statvfs.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <pty.h>

#define RAMFS_MAGIC     0x858458f6

#include <fstream>
#include <dirent.h>
#include <assert.h>

aptcc::aptcc(PkBackend *backend, bool &cancel)
	:
	packageRecords(0),
	packageCache(0),
	packageDepCache(0),
	packageSourceList(0),
	Map(0),
	Policy(0),
	m_backend(backend),
	_cancel(cancel),
	m_terminalTimeout(120),
	m_lastSubProgress(0)
{
	_cancel = false;
}

bool aptcc::init()
{
	gchar *locale;
	gchar *http_proxy;
	gchar *ftp_proxy;

	// Set PackageKit status
	pk_backend_set_status(m_backend, PK_STATUS_ENUM_LOADING_CACHE);

	// set locale
	if (locale = pk_backend_get_locale(m_backend)) {
		setlocale(LC_ALL, locale);
// TODO why this cuts characthers on ui?
// 		string _locale(locale);
// 		size_t found;
// 		found = _locale.find('.');
// 		_locale.erase(found);
// 		_config->Set("APT::Acquire::Translation", _locale);
	}

    // Disable apt-listbugs as it freezes PK
    setenv("APT_LISTBUGS_FRONTEND", "none", 1);

	// set http proxy
	http_proxy = pk_backend_get_proxy_http(m_backend);
	setenv("http_proxy", http_proxy, 1);

	// set ftp proxy
	ftp_proxy = pk_backend_get_proxy_ftp(m_backend);
	setenv("ftp_proxy", ftp_proxy, 1);

	packageSourceList = new pkgSourceList;
	// Read the source list
	packageSourceList->ReadMainList();

	// Generate it and map it
	bool Res = pkgMakeStatusCache(*packageSourceList, Progress, &Map, true);
	Progress.Done();
	if(!Res) {
		return false;
		//"The package lists or status file could not be parsed or opened."
	}

	packageCache = new pkgCache(Map);
	if (_error->PendingError()) {
		return false;
	}

	// create depcache
	Policy = new pkgPolicy(packageCache);
	if (_error->PendingError()) {
		return false;
	}

	if (!ReadPinFile(*Policy)) {
		return false;
	}

	packageDepCache = new pkgDepCache(packageCache, Policy);
	if (_error->PendingError()) {
		return false;
	}

	packageDepCache->Init(&Progress);
	Progress.Done();
	if (_error->PendingError()) {
		return false;
	}

	// Create the text record parser
	packageRecords = new pkgRecords(*packageDepCache);
}

aptcc::~aptcc()
{
	if (packageRecords)
	{
		g_debug ("~apt_init packageRecords");
		delete packageRecords;
	}

	if (packageCache)
	{
		g_debug ("~apt_init packageCache");
		delete packageCache;
	}

	if (packageDepCache)
	{
		g_debug ("~apt_init packageDepCache");
		delete packageDepCache;
	}

	if (Policy)
	{
		g_debug ("~apt_init Policy");
		delete Policy;
	}

	if (packageSourceList)
	{
		delete packageSourceList;
	}

	delete Map;
}

void aptcc::cancel()
{
	if (!_cancel) {
		_cancel = true;
	        pk_backend_set_status(m_backend, PK_STATUS_ENUM_CANCEL);
	}
	if (m_child_pid > 0) {
		kill(m_child_pid, SIGTERM);
	}
}

pair<pkgCache::PkgIterator, pkgCache::VerIterator>
		      aptcc::find_package_id(const gchar *package_id, bool &found)
{
	gchar **parts;
	pkgCache::VerIterator ver;
	pair<pkgCache::PkgIterator, pkgCache::VerIterator> pkg_ver;

    found = true;

	parts = pk_package_id_split (package_id);
	pkg_ver.first = packageCache->FindPkg(parts[PK_PACKAGE_ID_NAME]);

	// Ignore packages that could not be found or that exist only due to dependencies.
	if (pkg_ver.first.end() == true ||
	    (pkg_ver.first.VersionList().end() && pkg_ver.first.ProvidesList().end()))
	{
		g_strfreev (parts);
		return pkg_ver;
	}

	ver = find_ver(pkg_ver.first);
	// check to see if the provided package isn't virtual too
	if (ver.end() == false &&
	    strcmp(ver.VerStr(), parts[PK_PACKAGE_ID_VERSION]) == 0)
	{
		g_strfreev (parts);
		pkg_ver.second = ver;
		return pkg_ver;
	}

	ver = find_candidate_ver(pkg_ver.first);
	// check to see if the provided package isn't virtual too
	if (ver.end() == false &&
	    strcmp(ver.VerStr(), parts[PK_PACKAGE_ID_VERSION]) == 0)
	{
		g_strfreev (parts);
		pkg_ver.second = ver;
		return pkg_ver;
	}

    found = false;
	g_strfreev (parts);
	return pkg_ver;
}

pkgCache::VerIterator aptcc::find_candidate_ver(const pkgCache::PkgIterator &pkg)
{
	// get the candidate version iterator
	return (*packageDepCache)[pkg].CandidateVerIter(*packageDepCache);
}

pkgCache::VerIterator aptcc::find_ver(const pkgCache::PkgIterator &pkg)
{
	// if the package is installed return the current version
	if(!pkg.CurrentVer().end()) {
		return pkg.CurrentVer();
	}

	// Else get the candidate version iterator
	pkgCache::VerIterator candver = find_candidate_ver(pkg);
	if(!candver.end())
	{
		return candver;
	}

	// return the version list as a last resource
	return pkg.VersionList();
}

// used to emit packages it collects all the needed info
void aptcc::emit_package(const pkgCache::PkgIterator &pkg,
			 const pkgCache::VerIterator &ver,
			 PkBitfield filters,
			 PkInfoEnum state)
{
	// check the state enum to see if it was not set.
	if (state == PK_INFO_ENUM_UNKNOWN) {
        if (pkg->CurrentState == pkgCache::State::Installed &&
            pkg.CurrentVer() == ver) {
			state = PK_INFO_ENUM_INSTALLED;
		} else {
			state = PK_INFO_ENUM_AVAILABLE;
		}
	}

	if (filters != 0) {
		std::string str = ver.Section() == NULL ? "" : ver.Section();
		std::string section, repo_section;

		size_t found;
		found = str.find_last_of("/");
		section = str.substr(found + 1);
		repo_section = str.substr(0, found);

		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)
		    && state == PK_INFO_ENUM_INSTALLED) {
			return;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)
		    && state == PK_INFO_ENUM_AVAILABLE) {
			return;
		}

		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT)) {
			// if ver.end() means unknow
			// strcmp will be true when it's different than devel
			std::string pkgName = pkg.Name();
			if (!ends_with(pkgName, "-dev") &&
			    !ends_with(pkgName, "-dbg") &&
			    section.compare("devel") &&
			    section.compare("libdevel")) {
				return;
			}
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT)) {
			std::string pkgName = pkg.Name();
			if (ends_with(pkgName, "-dev") ||
			    ends_with(pkgName, "-dbg") ||
			    !section.compare("devel") ||
			    !section.compare("libdevel")) {
				return;
			}
		}

		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_GUI)) {
			// if ver.end() means unknow
			// strcmp will be true when it's different than x11
			if (section.compare("x11") && section.compare("gnome") &&
			    section.compare("kde") && section.compare("graphics")) {
				return;
			}
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_GUI)) {
			if (!section.compare("x11") || !section.compare("gnome") ||
			    !section.compare("kde") || !section.compare("graphics")) {
				return;
			}
		}

		// TODO add Ubuntu handling
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_FREE)) {
			if (!repo_section.compare("contrib") ||
			    !repo_section.compare("non-free")) {
				return;
			}
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_FREE)) {
			if (repo_section.compare("contrib") &&
			    repo_section.compare("non-free")) {
				return;
			}
		}

		// TODO test this one..
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_COLLECTIONS)) {
			if (!repo_section.compare("metapackages")) {
				return;
			}
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_COLLECTIONS)) {
			if (repo_section.compare("metapackages")) {
				return;
			}
		}

	}
	pkgCache::VerFileIterator vf = ver.FileList();

	gchar *package_id;
	package_id = pk_package_id_build(pkg.Name(),
					 ver.VerStr(),
					 ver.Arch(),
					 vf.File().Archive() == NULL ? "" : vf.File().Archive());
	pk_backend_package(m_backend,
			   state,
			   package_id,
			   get_short_description(ver, packageRecords).c_str());
}

void aptcc::emit_packages(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > &output,
			  PkBitfield filters,
			  PkInfoEnum state)
{
	// Sort so we can remove the duplicated entries
	sort(output.begin(), output.end(), compare());
	// Remove the duplicated entries
	output.erase(unique(output.begin(),
			    output.end(),
			    result_equality()),
		     output.end());

	for(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> >::iterator i=output.begin();
	    i != output.end(); ++i)
	{
		if (_cancel) {
			break;
		}
		emit_package(i->first, i->second, filters, state);
	}
}

void aptcc::emitUpdates(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > &output,
			 PkBitfield filters)
{
	PkInfoEnum state;
	// Sort so we can remove the duplicated entries
	sort(output.begin(), output.end(), compare());
	// Remove the duplicated entries
	output.erase(unique(output.begin(),
			    output.end(),
			    result_equality()),
		     output.end());

	for(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> >::iterator i=output.begin();
	    i != output.end(); ++i)
	{
		if (_cancel) {
			break;
		}

		// the default update info
		state = PK_INFO_ENUM_NORMAL;

		// let find what kind of upgrade this is
		pkgCache::VerFileIterator vf = i->second.FileList();
		std::string origin  = vf.File().Origin() == NULL ? "" : vf.File().Origin();
		std::string archive = vf.File().Archive() == NULL ? "" : vf.File().Archive();
		std::string label   = vf.File().Label() == NULL ? "" : vf.File().Label();
		if (origin.compare("Debian") == 0 ||
		    origin.compare("Ubuntu") == 0) {
			if (ends_with(archive, "-security") ||
			    label.compare("Debian-Security") == 0) {
				state = PK_INFO_ENUM_SECURITY;
			} else if (ends_with(archive, "-backports")) {
				state = PK_INFO_ENUM_ENHANCEMENT;
			} else if (ends_with(archive, "-updates")) {
				state = PK_INFO_ENUM_BUGFIX;
			}
		} else if (origin.compare("Backports.org archive") == 0 ||
				   ends_with(origin, "-backports")) {
			state = PK_INFO_ENUM_ENHANCEMENT;
		}

		emit_package(i->first, i->second, filters, state);
	}
}

void aptcc::povidesCodec(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > &output,
                         gchar **values)
{
    GstMatcher *matcher = new GstMatcher(values);
    if (!matcher->hasMatches()) {
        return;
    }

    for (pkgCache::PkgIterator pkg = packageCache->PkgBegin(); !pkg.end(); ++pkg)
    {
        if (_cancel) {
            delete matcher;
            break;
        }
        // Ignore packages that exist only due to dependencies.
        if (pkg.VersionList().end() && pkg.ProvidesList().end()) {
            continue;
        }

        // TODO search in updates packages
        // Ignore virtual packages
        pkgCache::VerIterator ver = find_ver(pkg);
        if (ver.end() == true) {
            ver = find_candidate_ver(pkg);
            if (ver.end() == true) {
                continue;
            }
        }

        pkgCache::VerFileIterator vf = ver.FileList();
        pkgRecords::Parser &rec = packageRecords->Lookup(vf);
        const char *start, *stop;
        rec.GetRec(start, stop);
        string record(start, stop - start);
        if (matcher->matches(record)) {
            output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, ver));
        }
    }

    delete matcher;
}

// used to emit packages it collects all the needed info
void aptcc::emit_details(const pkgCache::PkgIterator &pkg, const pkgCache::VerIterator &version)
{
	pkgCache::VerIterator ver;
    if (version.end() == false) {
        ver = version;
    } else {
        ver = find_ver(pkg);
    }
	std::string section = ver.Section() == NULL ? "" : ver.Section();

	size_t found;
	found = section.find_last_of("/");
	section = section.substr(found + 1);

	pkgCache::VerFileIterator vf = ver.FileList();
	pkgRecords::Parser &rec = packageRecords->Lookup(vf);

	gchar *package_id;
	package_id = pk_package_id_build(pkg.Name(),
					 ver.VerStr(),
					 ver.Arch(),
					 vf.File().Archive() == NULL ? "" : vf.File().Archive());
	pk_backend_details(m_backend,
			   package_id,
			   "unknown",
			   get_enum_group(section),
			   get_long_description_parsed(ver, packageRecords).c_str(),
			   rec.Homepage().c_str(),
			   ver->Size);
}

// used to emit packages it collects all the needed info
void aptcc::emit_update_detail(const pkgCache::PkgIterator &pkg, const pkgCache::VerIterator &version)
{
    // Get the version of the current package
    pkgCache::VerIterator currver = find_ver(pkg);
    pkgCache::VerFileIterator currvf  = currver.FileList();
    // Build a package_id from the current version
    gchar *current_package_id;
    current_package_id = pk_package_id_build(pkg.Name(),
                                             currver.VerStr(),
                                             currver.Arch(),
                                             currvf.File().Archive() == NULL ? "" : currvf.File().Archive());

    // Get the update version
    pkgCache::VerIterator candver;
    if (version.end() == false) {
        candver = version;
    } else {
        candver = find_candidate_ver(pkg);
    }

    pkgCache::VerFileIterator vf = candver.FileList();
    string origin = vf.File().Origin() == NULL ? "" : vf.File().Origin();
    pkgRecords::Parser &rec = packageRecords->Lookup(candver.FileList());

    // Build the changelogURI
    char uri[512];
    string srcpkg;
    string verstr;

    if (rec.SourcePkg().empty()) {
        srcpkg = pkg.Name();
    } else {
        srcpkg = rec.SourcePkg();
    }
    if (origin.compare("Debian") == 0 || origin.compare("Ubuntu") == 0) {
        string prefix;

        string src_section = candver.Section() == NULL ? "" : candver.Section();
        if(src_section.find('/') != src_section.npos) {
            src_section = string(src_section, 0, src_section.find('/'));
        } else {
            src_section = "main";
        }

        prefix += srcpkg[0];
        if(srcpkg.size() > 3 && srcpkg[0] == 'l' && srcpkg[1] == 'i' && srcpkg[2] == 'b') {
            prefix = string("lib") + srcpkg[3];
        }

        if(candver.VerStr() != NULL) {
            verstr = candver.VerStr();
        }

        if(verstr.find(':') != verstr.npos) {
            verstr = string(verstr, verstr.find(':') + 1);
        }

        if (origin.compare("Debian") == 0) {
            snprintf(uri,
                        512,
                        "http://packages.debian.org/changelogs/pool/%s/%s/%s/%s_%s/changelog",                                    src_section.c_str(),
                        prefix.c_str(),
                        srcpkg.c_str(),
                        srcpkg.c_str(),
                        verstr.c_str());
        } else {
            snprintf(uri,
                        512,
                        "http://changelogs.ubuntu.com/changelogs/pool/%s/%s/%s/%s_%s/changelog",                                    src_section.c_str(),
                        prefix.c_str(),
                        srcpkg.c_str(),
                        srcpkg.c_str(),
                        verstr.c_str());
        }
    } else {
        string pkgfilename;
        const char *start, *stop;
        pkgTagSection sec;
        unsigned long len;

        rec.GetRec(start, stop);
        len = stop - start;
        // add +1 to ensure we have the double lineline in the buffer
        if (start && sec.Scan(start, len + 1)) {
            pkgfilename = sec.FindS("Filename");
        }

        string cadidateOriginSiteUrl;
        if(!vf.end() && vf.File() && vf.File().Site()) {
            cadidateOriginSiteUrl = vf.File().Site();
        }

        pkgfilename = pkgfilename.substr(0, pkgfilename.find_last_of('.')) + ".changelog";
        snprintf(uri,512,"http://%s/%s",
                cadidateOriginSiteUrl.c_str(),
                pkgfilename.c_str());
    }
    // Create the download object
    AcqPackageKitStatus Stat(this, m_backend, _cancel);

    // get a fetcher
    pkgAcquire fetcher;
    fetcher.Setup(&Stat);

    // fetch the changelog
    pk_backend_set_status(m_backend, PK_STATUS_ENUM_DOWNLOAD_CHANGELOG);
    string filename = getChangelogFile(pkg.Name(), origin, verstr, srcpkg, uri, &fetcher);

    string changelog;
    string update_text;
    ifstream in(filename.c_str());
    string line;
    GRegex *regexVer;
    regexVer = g_regex_new("(?'source'.+) \\((?'version'.*)\\) "
                           "(?'dist'.+); urgency=(?'urgency'.+)",
                           G_REGEX_CASELESS,
                           G_REGEX_MATCH_ANCHORED,
                           0);
    GRegex *regexDate;
    regexDate = g_regex_new("^ -- (?'maintainer'.+) (?'mail'<.+>)  (?'date'.+)$",
                            G_REGEX_CASELESS,
                            G_REGEX_MATCH_ANCHORED,
                            0);
    string updated;
    string issued;
    while (getline(in, line)) {
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
                    if (!update_text.empty()) {
                        update_text.append("\n\n");
                    }
                    update_text.append(" == ");
                    update_text.append(version);
                    update_text.append(" ==");
                    g_free (version);
                }
            }
            g_match_info_free (match_info);
        } else if (starts_with(str, "  ")) {
            // update descritption
            update_text.append("\n");
            update_text.append(str);
        } else if (starts_with(str, " --")) {
            // Parse the text to know when the update was issued,
            // and when it got updated
            GMatchInfo *match_info;
            if (g_regex_match(regexDate, str, G_REGEX_MATCH_ANCHORED, &match_info)) {
                GTimeVal dateTime = {0, 0};
                gchar *date;
                date = g_match_info_fetch_named(match_info, "date");
                g_warn_if_fail(RFC1123StrToTime(date, dateTime.tv_sec));
                g_free(date);

                issued = g_time_val_to_iso8601(&dateTime);
                if (updated.empty()) {
                    updated = g_time_val_to_iso8601(&dateTime);
                }
            }
            g_match_info_free(match_info);
        }
    }
    // Clean structures
    g_regex_unref(regexVer);
    g_regex_unref(regexDate);
    unlink(filename.c_str());

    // Check if the update was updates since it was issued
    if (issued.compare(updated) == 0) {
        updated = "";
    }

    // Build a package_id from the update version
    string archive = vf.File().Archive() == NULL ? "" : vf.File().Archive();
    gchar *package_id;
    package_id = pk_package_id_build(pkg.Name(),
                    candver.VerStr(),
                    candver.Arch(),
                    archive.c_str());

    PkUpdateStateEnum updateState = PK_UPDATE_STATE_ENUM_UNKNOWN;
    if (archive.compare("stable") == 0) {
        updateState = PK_UPDATE_STATE_ENUM_STABLE;
    } else if (archive.compare("testing") == 0) {
        updateState = PK_UPDATE_STATE_ENUM_TESTING;
    } else if (archive.compare("unstable")  == 0 ||
        archive.compare("experimental") == 0)
    {
        updateState = PK_UPDATE_STATE_ENUM_UNSTABLE;
    }

    PkRestartEnum restart = PK_RESTART_ENUM_NONE;
    if (starts_with(pkg.Name(), "linux-image-") ||
        starts_with(pkg.Name(), "nvidia-") ||
        strcmp(pkg.Name(), "libc6") == 0 ||
        strcmp(pkg.Name(), "dbus") == 0) {
        restart = PK_RESTART_ENUM_SYSTEM;
    }

    pk_backend_update_detail(m_backend,
                             package_id,
                             current_package_id,//const gchar *updates
                             "",//const gchar *obsoletes
                             "",//const gchar *vendor_url
                             getBugzillaUrls(changelog).c_str(),//const gchar *bugzilla_url
                             getCVEUrls(changelog).c_str(),//const gchar *cve_url
                             restart,//PkRestartEnum restart
                             update_text.c_str(),//const gchar *update_text
                             changelog.c_str(),//const gchar *changelog
                             updateState,//PkUpdateStateEnum state
                             issued.c_str(), //const gchar *issued_text
                             updated.c_str() //const gchar *updated_text
                             );
}

void aptcc::get_depends(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > &output,
			pkgCache::PkgIterator pkg,
			bool recursive)
{
	pkgCache::DepIterator dep = find_ver(pkg).DependsList();
	while (!dep.end()) {
		if (_cancel) {
			break;
		}
		pkgCache::VerIterator ver = find_ver(dep.TargetPkg());
		// Ignore packages that exist only due to dependencies.
		if (ver.end()) {
			dep++;
			continue;
		} else if (dep->Type == pkgCache::Dep::Depends) {
			if (recursive) {
				if (!contains(output, dep.TargetPkg())) {
					output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(dep.TargetPkg(), ver));
					get_depends(output, dep.TargetPkg(), recursive);
				}
			} else {
				output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(dep.TargetPkg(), ver));
			}
		}
		dep++;
	}
}

void aptcc::get_requires(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > &output,
			pkgCache::PkgIterator pkg,
			bool recursive)
{
	for (pkgCache::PkgIterator parentPkg = packageCache->PkgBegin(); !parentPkg.end(); ++parentPkg) {
		if (_cancel) {
			break;
		}
		// Ignore packages that exist only due to dependencies.
		if (parentPkg.VersionList().end() && parentPkg.ProvidesList().end()) {
			continue;
		}

		// Don't insert virtual packages instead add what it provides
		pkgCache::VerIterator ver = find_ver(parentPkg);
		if (ver.end() == false) {
			vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > deps;
			get_depends(deps, parentPkg, false);
			for (vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> >::iterator i=deps.begin();
			    i != deps.end();
			    ++i)
			{
				if (i->first == pkg) {
					if (recursive) {
						if (!contains(output, parentPkg)) {
							output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(parentPkg, ver));
							get_requires(output, parentPkg, recursive);
						}
					} else {
						output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(parentPkg, ver));
					}
					break;
				}
			}
		}
	}
}

// used to return files it reads, using the info from the files in /var/lib/dpkg/info/
vector<string> search_files (PkBackend *backend, gchar **values, bool &_cancel)
{
	vector<string> packageList;
	regex_t re;
	gchar *search;
	gchar *values_str;

	values_str = g_strjoinv("$|^", values);
	search = g_strdup_printf("^%s$",
				 values_str);
	g_free(values_str);
	if(regcomp(&re, search, REG_NOSUB) != 0) {
		g_debug("Regex compilation error");
		g_free(search);
		return vector<string>();
	}
	g_free(search);

	DIR *dp;
	struct dirent *dirp;
	if (!(dp = opendir("/var/lib/dpkg/info/"))) {
		g_debug ("Error opening /var/lib/dpkg/info/\n");
		regfree(&re);
		return vector<string>();
	}

	string line;
	while ((dirp = readdir(dp)) != NULL) {
		if (_cancel) {
			break;
		}
		if (ends_with(dirp->d_name, ".list")) {
			string f = "/var/lib/dpkg/info/" + string(dirp->d_name);
			ifstream in(f.c_str());
			if (!in != 0) {
				continue;
			}
			while (!in.eof()) {
				getline(in, line);
				if (regexec(&re, line.c_str(), (size_t)0, NULL, 0) == 0) {
					string file(dirp->d_name);
					packageList.push_back(file.erase(file.size() - 5, file.size()));
					break;
				}
			}
		}
	}
	closedir(dp);
	regfree(&re);
	return packageList;
}

// used to return files it reads, using the info from the files in /var/lib/dpkg/info/
vector<string> searchMimeType (PkBackend *backend, gchar **values, bool &error, bool &_cancel)
{
	vector<string> packageList;
	regex_t re;
	gchar *value;
	gchar *values_str;

	values_str = g_strjoinv("|", values);
	value = g_strdup_printf("^MimeType=\\(.*;\\)\\?\\(%s\\)\\(;.*\\)\\?$",
				values_str);
	g_free(values_str);

	if(regcomp(&re, value, REG_NOSUB) != 0) {
		g_debug("Regex compilation error");
		g_free(value);
		return vector<string>();
	}
	g_free(value);

	DIR *dp;
	struct dirent *dirp;
	if (!(dp = opendir("/usr/share/app-install/desktop/"))) {
		g_debug ("Error opening /usr/share/app-install/desktop/\n");
		regfree(&re);
		error = true;
		return vector<string>();
	}

	string line;
	while ((dirp = readdir(dp)) != NULL) {
		if (_cancel) {
			break;
		}
		if (ends_with(dirp->d_name, ".desktop")) {
			string f = "/usr/share/app-install/desktop/" + string(dirp->d_name);
			ifstream in(f.c_str());
			if (!in != 0) {
				continue;
			}
			bool getName = false;
			while (!in.eof()) {
				getline(in, line);
				if (getName) {
					if (starts_with(line, "X-AppInstall-Package=")) {
						// Remove the X-AppInstall-Package=
						packageList.push_back(line.substr(21));
						break;
					}
				} else {
				    if (regexec(&re, line.c_str(), (size_t)0, NULL, 0) == 0) {
						in.seekg(ios_base::beg);
						getName = true;
					}
				}
			}
		}
	}

	closedir(dp);
	regfree(&re);
	return packageList;
}

// used to emit files it reads the info directly from the files
void emit_files (PkBackend *backend, const gchar *pi)
{
	static string filelist;
	string line;
	gchar **parts;

	parts = pk_package_id_split (pi);
	filelist.erase(filelist.begin(), filelist.end());

	string f = "/var/lib/dpkg/info/" +
		   string(parts[PK_PACKAGE_ID_NAME]) +
		   ".list";
	g_strfreev (parts);

	if (FileExists(f)) {
		ifstream in(f.c_str());
		if (!in != 0) {
			return;
		}
		while (in.eof() == false && filelist.empty()) {
			getline(in, line);
			filelist += line;
		}
		while (in.eof() == false) {
			getline(in, line);
			if (!line.empty()) {
				filelist += ";" + line;
			}
		}

		if (!filelist.empty()) {
			pk_backend_files (backend, pi, filelist.c_str());
		}
	}
}

static bool checkTrusted(pkgAcquire &fetcher, PkBackend *backend)
{
	string UntrustedList;
	for (pkgAcquire::ItemIterator I = fetcher.ItemsBegin(); I < fetcher.ItemsEnd(); ++I)
	{
		if (!(*I)->IsTrusted())
		{
			UntrustedList += string((*I)->ShortDesc()) + " ";
		}
	}

	if (UntrustedList == "")
	{
		return true;
	}

	if (pk_backend_get_bool(backend, "only_trusted") == false ||
	    _config->FindB("APT::Get::AllowUnauthenticated", false) == true)
	{
		g_debug ("Authentication warning overridden.\n");
		return true;
	}

	string warning("The following packages cannot be authenticated:\n");
	warning += UntrustedList;
	pk_backend_error_code(backend,
			      PK_ERROR_ENUM_CANNOT_INSTALL_REPO_UNSIGNED,
			      warning.c_str());
	_error->Discard();
	return false;
}

bool aptcc::TryToInstall(pkgCache::PkgIterator Pkg,
			 pkgDepCache &Cache,
			 pkgProblemResolver &Fix,
			 bool Remove,
			 bool BrokenFix,
			 unsigned int &ExpectedInst)
{
	// This is a pure virtual package and there is a single available provides
	if (Cache[Pkg].CandidateVer == 0 && Pkg->ProvidesList != 0 &&
	    Pkg.ProvidesList()->NextProvides == 0)
	{
		pkgCache::PkgIterator Tmp = Pkg.ProvidesList().OwnerPkg();
		Pkg = Tmp;
	}

	// Check if there is something at all to install
	pkgDepCache::StateCache &State = Cache[Pkg];
	if (Remove == true && Pkg->CurrentVer == 0)
	{
		Fix.Clear(Pkg);
		Fix.Protect(Pkg);
		Fix.Remove(Pkg);

		return true;
	}

	if (State.CandidateVer == 0 && Remove == false)
	{
		_error->Error("Package %s is virtual and has no installation candidate", Pkg.Name());

		pk_backend_error_code(m_backend,
				      PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
				      g_strdup_printf("Package %s is virtual and has no "
						      "installation candidate",
						      Pkg.Name()));
		return false;
	}

	Fix.Clear(Pkg);
	Fix.Protect(Pkg);
	if (Remove == true)
	{
		Fix.Remove(Pkg);
		Cache.MarkDelete(Pkg,_config->FindB("APT::Get::Purge", false));
		return true;
	}

	// Install it
	Cache.MarkInstall(Pkg,false);
	if (State.Install() == false)
	{
		if (_config->FindB("APT::Get::ReInstall",false) == true) {
			if (Pkg->CurrentVer == 0 || Pkg.CurrentVer().Downloadable() == false) {
		// 	    ioprintf(c1out,_("Reinstallation of %s is not possible, it cannot be downloaded.\n"),
		// 		     Pkg.Name());
		;
			} else {
				Cache.SetReInstall(Pkg,true);
			}
		} else {
	    // 	 if (AllowFail == true)
	    // 	    ioprintf(c1out,_("%s is already the newest version.\n"),
	    // 		     Pkg.Name());
		}
	} else {
		ExpectedInst++;
	}

// 	cout << "trytoinstall ExpectedInst " << ExpectedInst << endl;
	// Install it with autoinstalling enabled (if we not respect the minial
	// required deps or the policy)
	if ((State.InstBroken() == true || State.InstPolicyBroken() == true) &&
	    BrokenFix == false) {
	    Cache.MarkInstall(Pkg,true);
	}

	return true;
}

// checks if there are Essential packages being removed
bool aptcc::removingEssentialPackages(pkgCacheFile &Cache)
{
	string List;
	bool *Added = new bool[Cache->Head().PackageCount];
	for (unsigned int I = 0; I != Cache->Head().PackageCount; I++){
		Added[I] = false;
	}

	for (pkgCache::PkgIterator I = Cache->PkgBegin(); ! I.end(); ++I) {
		if ((I->Flags & pkgCache::Flag::Essential) != pkgCache::Flag::Essential &&
		    (I->Flags & pkgCache::Flag::Important) != pkgCache::Flag::Important) {
			continue;
		}

		if (Cache[I].Delete() == true) {
			if (Added[I->ID] == false) {
				Added[I->ID] = true;
				List += string(I.Name()) + " ";
			}
		}

		if (I->CurrentVer == 0) {
			continue;
		}

		// Print out any essential package depenendents that are to be removed
		for (pkgCache::DepIterator D = I.CurrentVer().DependsList(); D.end() == false; D++) {
			// Skip everything but depends
			if (D->Type != pkgCache::Dep::PreDepends &&
			    D->Type != pkgCache::Dep::Depends){
				continue;
			}

			pkgCache::PkgIterator P = D.SmartTargetPkg();
			if (Cache[P].Delete() == true)
			{
				if (Added[P->ID] == true){
				    continue;
				}
				Added[P->ID] = true;

				char S[300];
				snprintf(S, sizeof(S), "%s (due to %s) ", P.Name(), I.Name());
				List += S;
			}
		}
	}

	delete [] Added;
	if (!List.empty()) {
		pk_backend_error_code(m_backend,
				      PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE,
				      g_strdup_printf("WARNING: You are trying to remove the "
						      "following essential packages: %s",
						      List.c_str()));
		return true;
	}

	return false;
}

// emitChangedPackages - Show packages to newly install				/*{{{*/
// ---------------------------------------------------------------------
/* */
void aptcc::emitChangedPackages(pkgCacheFile &Cache)
{
	vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > installing,
								    removing,
								    updating,
								    downgrading;

	string VersionsList;
	for (pkgCache::PkgIterator pkg = Cache->PkgBegin(); ! pkg.end(); ++pkg)
	{
		if (Cache[pkg].NewInstall() == true) {
			// installing
			installing.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, find_candidate_ver(pkg)));
		} else if (Cache[pkg].Delete() == true) {
			// removing
			removing.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, find_ver(pkg)));
		} else if (Cache[pkg].Upgrade() == true) {
			// updating
			updating.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, find_candidate_ver(pkg)));
		} else if (Cache[pkg].Downgrade() == true) {
			// downgrading
			downgrading.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, find_candidate_ver(pkg)));
		}
	}

	// emit packages that have changes
	emit_packages(removing,    PK_FILTER_ENUM_NONE, PK_INFO_ENUM_REMOVING);
	emit_packages(downgrading, PK_FILTER_ENUM_NONE, PK_INFO_ENUM_DOWNGRADING);
	emit_packages(installing,  PK_FILTER_ENUM_NONE, PK_INFO_ENUM_INSTALLING);
	emit_packages(updating,    PK_FILTER_ENUM_NONE, PK_INFO_ENUM_UPDATING);
}

void aptcc::populateInternalPackages(pkgCacheFile &Cache)
{
	for (pkgCache::PkgIterator pkg = Cache->PkgBegin(); ! pkg.end(); ++pkg) {
		if (Cache[pkg].NewInstall() == true) {
			// installing
			m_pkgs.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, find_candidate_ver(pkg)));
		} else if (Cache[pkg].Delete() == true) {
			// removing
			m_pkgs.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, find_ver(pkg)));
		} else if (Cache[pkg].Upgrade() == true) {
			// updating
			m_pkgs.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, find_candidate_ver(pkg)));
		} else if (Cache[pkg].Downgrade() == true) {
			// downgrading
			m_pkgs.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(pkg, find_candidate_ver(pkg)));
		}
	}
}

void aptcc::emitTransactionPackage(string name, PkInfoEnum state)
{
	for (vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> >::iterator i=m_pkgs.begin();
	     i != m_pkgs.end();
	     ++i)
	{
		if (i->first.Name() == name) {
			emit_package(i->first, i->second, PK_FILTER_ENUM_NONE, state);
			return;
		}
	}

	pair<pkgCache::PkgIterator, pkgCache::VerIterator> pkg_ver;
	pkg_ver.first = packageCache->FindPkg(name);
	// Ignore packages that could not be found or that exist only due to dependencies.
	if (pkg_ver.first.end() == true ||
	    (pkg_ver.first.VersionList().end() && pkg_ver.first.ProvidesList().end()))
	{
		return;
	}

	pkg_ver.second = find_ver(pkg_ver.first);
	// check to see if the provided package isn't virtual too
	if (pkg_ver.second.end() == false)
	{
		emit_package(pkg_ver.first, pkg_ver.second, PK_FILTER_ENUM_NONE, state);
	}

	pkg_ver.second = find_candidate_ver(pkg_ver.first);
	// check to see if we found the package
	if (pkg_ver.second.end() == false)
	{
		emit_package(pkg_ver.first, pkg_ver.second, PK_FILTER_ENUM_NONE, state);
	}
}

void aptcc::updateInterface(int fd, int writeFd)
{
	char buf[2];
	static char line[1024] = "";

	while (1) {
		// This algorithm should be improved (it's the same as the rpm one ;)
		int len = read(fd, buf, 1);

		// nothing was read
		if(len < 1) {
			break;
		}

		// update the time we last saw some action
		m_lastTermAction = time(NULL);

		if( buf[0] == '\n') {
			if (_cancel) {
				kill(m_child_pid, SIGTERM);
			}
			//cout << "got line: " << line << endl;

			gchar **split  = g_strsplit(line, ":",5);
			gchar *status  = g_strstrip(split[0]);
			gchar *pkg     = g_strstrip(split[1]);
			gchar *percent = g_strstrip(split[2]);
			gchar *str     = g_strdup(g_strstrip(split[3]));

			// major problem here, we got unexpected input. should _never_ happen
			if(!(pkg && status)) {
				continue;
			}

			// first check for errors and conf-file prompts
			if (strstr(status, "pmerror") != NULL) {
				// error from dpkg
				pk_backend_error_code(m_backend,
						      PK_ERROR_ENUM_PACKAGE_FAILED_TO_INSTALL,
						      str);
			} else if (strstr(status, "pmconffile") != NULL) {
				// conffile-request from dpkg, needs to be parsed different
				int i=0;
				int count=0;
				string orig_file, new_file;

				// go to first ' and read until the end
				for(;str[i] != '\'' || str[i] == 0; i++)
					/*nothing*/
					;
				i++;
				for(;str[i] != '\'' || str[i] == 0; i++)
					orig_file.append(1, str[i]);
				i++;

				// same for second ' and read until the end
				for(;str[i] != '\'' || str[i] == 0; i++)
					/*nothing*/
				;
				i++;
				for(;str[i] != '\'' || str[i] == 0; i++)
					new_file.append(1, str[i]);
				i++;

                gchar *filename;
                filename = g_build_filename(DATADIR, "PackageKit", "helpers", "aptcc", "pkconffile", NULL);
                gint exit_code;
                gchar **argv;
                gchar **envp;
                GError *error = NULL;
                argv = (gchar **) g_malloc(5 * sizeof(gchar *));
                argv[0] = filename;
                argv[1] = g_strdup(m_lastPackage.c_str());
                argv[2] = g_strdup(orig_file.c_str());
                argv[3] = g_strdup(new_file.c_str());
                argv[4] = NULL;

                gchar *socket;
                if (socket = pk_backend_get_frontend_socket(m_backend)) {
                    envp = (gchar **) g_malloc(3 * sizeof(gchar *));
                    envp[0] = g_strdup("DEBIAN_FRONTEND=passthrough");
                    envp[1] = g_strdup_printf("DEBCONF_PIPE=%s", socket);
                    envp[2] = NULL;
                } else {
                    // we don't have a socket set, let's fallback to noninteractive
                    envp = (gchar **) g_malloc(2 * sizeof(gchar *));
                    envp[0] = g_strdup("DEBIAN_FRONTEND=noninteractive");
                    envp[1] = NULL;
                }

                gboolean ret;
                ret = g_spawn_sync(NULL, // working dir
                             argv, // argv
                             envp, // envp
                             G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                             NULL, // child_setup
                             NULL, // user_data
                             NULL, // standard_output
                             NULL, // standard_error
                             &exit_code,
                             &error);

                cout << filename << " " << exit_code << " ret: "<< ret << endl;

                if (exit_code == 10) {
                    // 1 means the user wants the package config
                    if (write(writeFd, "Y\n", 2) != 2) {
                        // TODO we need a DPKG patch to use debconf
                        g_debug("Failed to write");
                    }
                } else if (exit_code == 20) {
                    // 2 means the user wants to keep the current config
                    if (write(writeFd, "N\n", 2) != 2) {
                        // TODO we need a DPKG patch to use debconf
                        g_debug("Failed to write");
                    }
                } else {
                    // either the user didn't choose an option or the front end failed'
                    gchar *confmsg;
                    confmsg = g_strdup_printf("The configuration file '%s' "
                                "(modified by you or a script) "
                                "has a newer version '%s'.\n"
                                "Please verify your changes and update it manually.",
                                orig_file.c_str(),
                                new_file.c_str());
                    pk_backend_message(m_backend,
                                       PK_MESSAGE_ENUM_CONFIG_FILES_CHANGED,
                                       confmsg);
                    // fall back to keep the current config file
                    if (write(writeFd, "N\n", 2) != 2) {
                        // TODO we need a DPKG patch to use debconf
                        g_debug("Failed to write");
                    }
                }
			} else if (strstr(status, "pmstatus") != NULL) {
				// INSTALL & UPDATE
				// - Running dpkg
				// loops ALL
				// -  0 Installing pkg (sometimes this is skiped)
				// - 25 Preparing pkg
				// - 50 Unpacking pkg
				// - 75 Preparing to configure pkg
				//   ** Some pkgs have
				//   - Running post-installation
				//   - Running dpkg
				// reloops all
				// -   0 Configuring pkg
				// - +25 Configuring pkg (SOMETIMES)
				// - 100 Installed pkg
				// after all
				// - Running post-installation

				// REMOVE
				// - Running dpkg
				// loops
				// - 25  Removing pkg
				// - 50  Preparing for removal of pkg
				// - 75  Removing pkg
				// - 100 Removed pkg
				// after all
				// - Running post-installation

				// Let's start parsing the status:
				if (starts_with(str, "Preparing to configure")) {
					// Preparing to Install/configure
// 					cout << "Found Preparing to configure! " << line << endl;
					// The next item might be Configuring so better it be 100
					m_lastSubProgress = 100;
					emitTransactionPackage(pkg, PK_INFO_ENUM_PREPARING);
					pk_backend_set_sub_percentage(m_backend, 75);
				} else if (starts_with(str, "Preparing for removal")) {
					// Preparing to Install/configure
// 					cout << "Found Preparing for removal! " << line << endl;
					m_lastSubProgress = 50;
					emitTransactionPackage(pkg, PK_INFO_ENUM_REMOVING);
					pk_backend_set_sub_percentage(m_backend, m_lastSubProgress);
				} else if (starts_with(str, "Preparing")) {
					// Preparing to Install/configure
// 					cout << "Found Preparing! " << line << endl;
					// if last package is different then finish it
					if (!m_lastPackage.empty() && m_lastPackage.compare(pkg) != 0) {
// 						cout << "FINISH the last package: " << m_lastPackage << endl;
						emitTransactionPackage(m_lastPackage, PK_INFO_ENUM_FINISHED);
					}
					emitTransactionPackage(pkg, PK_INFO_ENUM_PREPARING);
					pk_backend_set_sub_percentage(m_backend, 25);
				} else if (starts_with(str, "Unpacking")) {
// 					cout << "Found Unpacking! " << line << endl;
					emitTransactionPackage(pkg, PK_INFO_ENUM_DECOMPRESSING);
					pk_backend_set_sub_percentage(m_backend, 50);
				} else if (starts_with(str, "Configuring")) {
					// Installing Package
// 					cout << "Found Configuring! " << line << endl;
					if (m_lastSubProgress >= 100 && !m_lastPackage.empty()) {
						cout << "FINISH the last package: " << m_lastPackage << endl;
						emitTransactionPackage(m_lastPackage, PK_INFO_ENUM_FINISHED);
						m_lastSubProgress = 0;
					}
					emitTransactionPackage(pkg, PK_INFO_ENUM_INSTALLING);
					pk_backend_set_sub_percentage(m_backend, m_lastSubProgress);
					m_lastSubProgress += 25;
				} else if (starts_with(str, "Running dpkg")) {
// 					cout << "Found Running dpkg! " << line << endl;
				} else if (starts_with(str, "Running")) {
// 					cout << "Found Running! " << line << endl;
					pk_backend_set_status (m_backend, PK_STATUS_ENUM_COMMIT);
				} else if (starts_with(str, "Installing")) {
// 					cout << "Found Installing! " << line << endl;
					// FINISH the last package
					if (!m_lastPackage.empty()) {
// 						cout << "FINISH the last package: " << m_lastPackage << endl;
						emitTransactionPackage(m_lastPackage, PK_INFO_ENUM_FINISHED);
					}
					m_lastSubProgress = 0;
					emitTransactionPackage(pkg, PK_INFO_ENUM_INSTALLING);
					pk_backend_set_sub_percentage(m_backend, 0);
				} else if (starts_with(str, "Removing")) {
// 					cout << "Found Removing! " << line << endl;
					if (m_lastSubProgress >= 100 && !m_lastPackage.empty()) {
// 						cout << "FINISH the last package: " << m_lastPackage << endl;
						emitTransactionPackage(m_lastPackage, PK_INFO_ENUM_FINISHED);
					}
					m_lastSubProgress += 25;
					emitTransactionPackage(pkg, PK_INFO_ENUM_REMOVING);
					pk_backend_set_sub_percentage(m_backend, m_lastSubProgress);
				} else if (starts_with(str, "Installed") ||
					       starts_with(str, "Removed")) {
// 					cout << "Found FINISHED! " << line << endl;
					m_lastSubProgress = 100;
					emitTransactionPackage(pkg, PK_INFO_ENUM_FINISHED);
				} else {
					cout << ">>>Unmaped value<<< :" << line << endl;
				}

				if (!starts_with(str, "Running")) {
					m_lastPackage = pkg;
				}
				m_startCounting = true;
			} else {
				m_startCounting = true;
			}

			int val = atoi(percent);
			//cout << "progress: " << val << endl;
			pk_backend_set_percentage(m_backend, val);

			// clean-up
			g_strfreev(split);
			g_free(str);
			line[0] = 0;
		} else {
			buf[1] = 0;
			strcat(line, buf);
		}
	}

	time_t now = time(NULL);

	if(!m_startCounting) {
		usleep(100000);
		// wait until we get the first message from apt
		m_lastTermAction = now;
	}

	if ((now - m_lastTermAction) > m_terminalTimeout) {
		// get some debug info
		g_warning("no statusfd changes/content updates in terminal for %i"
			  " seconds",m_terminalTimeout);
		m_lastTermAction = time(NULL);
	}

	// sleep for a while to don't obcess over it
	usleep(5000);
}

// DoAutomaticRemove - Remove all automatic unused packages		/*{{{*/
// ---------------------------------------------------------------------
/* Remove unused automatic packages */
bool aptcc::DoAutomaticRemove(pkgCacheFile &Cache)
{
	bool doAutoRemove = _config->FindB("APT::Get::AutomaticRemove", false);
	pkgDepCache::ActionGroup group(*Cache);

	if (_config->FindB("APT::Get::Remove",true) == false &&
	    doAutoRemove == true)
	{
		cout << "We are not supposed to delete stuff, can't start "
			"AutoRemover" << endl;
		doAutoRemove = false;
	}

	// look over the cache to see what can be removed
	for (pkgCache::PkgIterator Pkg = Cache->PkgBegin(); ! Pkg.end(); ++Pkg)
	{
		if (Cache[Pkg].Garbage && doAutoRemove)
		{
			if (Pkg.CurrentVer() != 0 &&
			    Pkg->CurrentState != pkgCache::State::ConfigFiles) {
				Cache->MarkDelete(Pkg, _config->FindB("APT::Get::Purge", false));
			} else {
				Cache->MarkKeep(Pkg, false, false);
			}
		}
	}

	// Now see if we destroyed anything
	if (Cache->BrokenCount() != 0)
	{
		cout << "Hmm, seems like the AutoRemover destroyed something which really\n"
			    "shouldn't happen. Please file a bug report against apt." << endl;
		// TODO call show_broken
	    //       ShowBroken(c1out,Cache,false);
		return _error->Error("Internal Error, AutoRemover broke stuff");
	}
	return true;
}

bool aptcc::runTransaction(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > &pkgs,
			   bool simulate,
			   bool remove)
{
	//cout << "runTransaction" << simulate << remove << endl;
	bool WithLock = !simulate; // Check to see if we are just simulating,
				   //since for that no lock is needed

	pkgCacheFile Cache;
	OpTextProgress Prog(*_config);
	int timeout = 10;
	// TODO test this
	while (Cache.Open(&Prog, WithLock) == false) {
		// failed to open cache, try checkDeps then..
		// || Cache.CheckDeps(CmdL.FileSize() != 1) == false
		if (WithLock == false || (timeout <= 0)) {
			show_errors(m_backend, PK_ERROR_ENUM_CANNOT_GET_LOCK);
			return false;
		} else {
			_error->Discard();
			pk_backend_set_status (m_backend, PK_STATUS_ENUM_WAITING_FOR_LOCK);
			sleep(1);
			timeout--;
		}
	}
	pk_backend_set_status (m_backend, PK_STATUS_ENUM_RUNNING);

	// Enter the special broken fixing mode if the user specified arguments
	bool BrokenFix = false;
	if (Cache->BrokenCount() != 0) {
		BrokenFix = true;
	}

	unsigned int ExpectedInst = 0;
	pkgProblemResolver Fix(Cache);

	// new scope for the ActionGroup
	{
		pkgDepCache::ActionGroup group(Cache);
		for(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> >::iterator i=pkgs.begin();
		    i != pkgs.end();
		    ++i)
		{
			pkgCache::PkgIterator Pkg = i->first;
			if (_cancel) {
				break;
			}

			if (TryToInstall(Pkg,
					 Cache,
					 Fix,
					 remove,
					 BrokenFix,
					 ExpectedInst) == false) {
				return false;
			}
		}

		// Call the scored problem resolver
		Fix.InstallProtect();
		if (Fix.Resolve(true) == false) {
			_error->Discard();
		}

		// Now we check the state of the packages,
		if (Cache->BrokenCount() != 0)
		{
			// if the problem resolver could not fix all broken things
			// show what is broken
			show_broken(m_backend, Cache, false);
			return false;
		}
	}
	// Try to auto-remove packages
	if (!DoAutomaticRemove(Cache)) {
		// TODO
		return false;
	}

	// check for essential packages!!!
	if (removingEssentialPackages(Cache)) {
		return false;
	}

	if (simulate) {
		// Print out a list of packages that are going to be installed extra
		emitChangedPackages(Cache);
		return true;
	} else {
		// Store the packages that are going to change
		// so we can emit them as we process it.
		populateInternalPackages(Cache);
	        return installPackages(Cache);
	}
}

									/*}}}*/

// InstallPackages - Actually download and install the packages		/*{{{*/
// ---------------------------------------------------------------------
/* This displays the informative messages describing what is going to
   happen and then calls the download routines */
bool aptcc::installPackages(pkgCacheFile &Cache)
{
	//cout << "installPackages() called" << endl;
	if (_config->FindB("APT::Get::Purge",false) == true)
	{
		pkgCache::PkgIterator I = Cache->PkgBegin();
		for (; I.end() == false; I++)
		{
			if (I.Purge() == false && Cache[I].Mode == pkgDepCache::ModeDelete) {
				Cache->MarkDelete(I,true);
			}
		}
	}

	// check for essential packages!!!
	if (removingEssentialPackages(Cache)) {
		return false;
	}

	// Sanity check
	if (Cache->BrokenCount() != 0)
	{
		// TODO
		show_broken(m_backend, Cache, false);
		_error->Error("Internal error, InstallPackages was called with broken packages!");
		return false;
	}

	if (Cache->DelCount() == 0 && Cache->InstCount() == 0 &&
	    Cache->BadCount() == 0) {
		return true;
	}

	// No remove flag
	if (Cache->DelCount() != 0 && _config->FindB("APT::Get::Remove", true) == false) {
		pk_backend_error_code(m_backend,
				      PK_ERROR_ENUM_PACKAGE_FAILED_TO_REMOVE,
				      "Packages need to be removed but remove is disabled.");
		return false;
	}

	// Create the text record parser
	pkgRecords Recs(Cache);
	if (_error->PendingError() == true) {
		return false;
	}

	// Lock the archive directory
	FileFd Lock;
	if (_config->FindB("Debug::NoLocking", false) == false)
	{
		Lock.Fd(GetLock(_config->FindDir("Dir::Cache::Archives") + "lock"));
		if (_error->PendingError() == true) {
			return _error->Error("Unable to lock the download directory");
		}
	}

	// Create the download object
	AcqPackageKitStatus Stat(this, m_backend, _cancel);

	// get a fetcher
	pkgAcquire fetcher;
	fetcher.Setup(&Stat);

	// Create the package manager and prepare to download
	SPtr<pkgPackageManager> PM= _system->CreatePM(Cache);
	if (PM->GetArchives(&fetcher, packageSourceList, &Recs) == false ||
	    _error->PendingError() == true) {
		return false;
	}

	// Generate the list of affected packages and sort it
	for (pkgCache::PkgIterator I = Cache->PkgBegin(); I.end() == false; I++)
	{
		// Ignore no-version packages
		if (I->VersionList == 0) {
			continue;
		}

		// Not interesting
		if ((Cache[I].Keep() == true ||
		    Cache[I].InstVerIter(Cache) == I.CurrentVer()) &&
		    I.State() == pkgCache::PkgIterator::NeedsNothing &&
		    (Cache[I].iFlags & pkgDepCache::ReInstall) != pkgDepCache::ReInstall &&
		    (I.Purge() != false || Cache[I].Mode != pkgDepCache::ModeDelete ||
		    (Cache[I].iFlags & pkgDepCache::Purge) != pkgDepCache::Purge))
		{
			continue;
		}

		// Append it to the list
		Stat.addPackagePair(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(I, Cache[I].InstVerIter(Cache)));
	}

	// Display statistics
	double FetchBytes = fetcher.FetchNeeded();
	double FetchPBytes = fetcher.PartialPresent();
	double DebBytes = fetcher.TotalNeeded();
	if (DebBytes != Cache->DebSize())
	{
 	    cout << DebBytes << ',' << Cache->DebSize() << endl;
cout << "How odd.. The sizes didn't match, email apt@packages.debian.org";
/*		_error->Warning("How odd.. The sizes didn't match, email apt@packages.debian.org");*/
	}

	// Number of bytes
// 	if (DebBytes != FetchBytes)
// 	    ioprintf(c1out, "Need to get %sB/%sB of archives.\n",
// 		    SizeToStr(FetchBytes).c_str(),SizeToStr(DebBytes).c_str());
// 	else if (DebBytes != 0)
// 	    ioprintf(c1out, "Need to get %sB of archives.\n",
// 		    SizeToStr(DebBytes).c_str());

	// Size delta
// 	if (Cache->UsrSize() >= 0)
// 	    ioprintf(c1out, "After this operation, %sB of additional disk space will be used.\n",
// 		    SizeToStr(Cache->UsrSize()).c_str());
// 	else
// 	    ioprintf(c1out, "After this operation, %sB disk space will be freed.\n",
// 		    SizeToStr(-1*Cache->UsrSize()).c_str());

	if (_error->PendingError() == true) {
	    cout << "PendingError " << endl;
		return false;
	}

	/* Check for enough free space */
	struct statvfs Buf;
	string OutputDir = _config->FindDir("Dir::Cache::Archives");
	if (statvfs(OutputDir.c_str(),&Buf) != 0) {
		return _error->Errno("statvfs",
				     "Couldn't determine free space in %s",
				     OutputDir.c_str());
	}
	if (unsigned(Buf.f_bfree) < (FetchBytes - FetchPBytes)/Buf.f_bsize)
	{
		struct statfs Stat;
		if (statfs(OutputDir.c_str(), &Stat) != 0 ||
		    unsigned(Stat.f_type)            != RAMFS_MAGIC)
		{
			pk_backend_error_code(m_backend,
					      PK_ERROR_ENUM_NO_SPACE_ON_DEVICE,
					      string("You don't have enough free space in ").append(OutputDir).c_str());
			return _error->Error("You don't have enough free space in %s.",
					    OutputDir.c_str());
		}
	}

	if (!checkTrusted(fetcher, m_backend)) {
		return false;
	}

	pk_backend_set_status (m_backend, PK_STATUS_ENUM_DOWNLOAD);
	pk_backend_set_simultaneous_mode(m_backend, true);
	// Download and check if we can continue
	if (fetcher.Run() != pkgAcquire::Continue
	    && _cancel == false)
	{
		// We failed and we did not cancel
		show_errors(m_backend, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED);
		return false;
	}
	pk_backend_set_simultaneous_mode(m_backend, false);

	if (_error->PendingError() == true) {
		cout << "PendingError download" << endl;
		return false;
	}

	// Right now it's not safe to cancel
	pk_backend_set_allow_cancel (m_backend, false);

	// TODO true or false?
	if (_cancel) {
		return true;
	}

	// Download should be finished by now, changing it's status
	pk_backend_set_status (m_backend, PK_STATUS_ENUM_RUNNING);
	pk_backend_set_percentage(m_backend, PK_BACKEND_PERCENTAGE_INVALID);
	pk_backend_set_sub_percentage(m_backend, PK_BACKEND_PERCENTAGE_INVALID);

	// TODO DBus activated does not have all vars
	// we could try to see if this is the case
	setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
	_system->UnLock();

	pkgPackageManager::OrderResult res;
	res = PM->DoInstallPreFork();
	if (res == pkgPackageManager::Failed) {
		g_warning ("Failed to prepare installation");
		show_errors(m_backend, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED);
		return false;
	}

	// File descriptors for reading dpkg --status-fd
	int readFromChildFD[2];
	if (pipe(readFromChildFD) < 0) {
		cout << "Failed to create a pipe" << endl;
		return false;
	}

	int pty_master;
	m_child_pid = forkpty(&pty_master, NULL, NULL, NULL);
	if (m_child_pid == -1) {
		return false;
	}

	if (m_child_pid == 0) {
		//cout << "FORKED: installPackages(): DoInstall" << endl;

		// close pipe we don't need
		close(readFromChildFD[0]);

		// Change the locale to not get libapt localization
		setlocale(LC_ALL, "C");

		// Debconf handlying
		gchar *socket;
		if (socket = pk_backend_get_frontend_socket(m_backend)) {
			setenv("DEBIAN_FRONTEND", "passthrough", 1);
			setenv("DEBCONF_PIPE", socket, 1);
		} else {
			// we don't have a socket set, let's fallback to noninteractive
			setenv("DEBIAN_FRONTEND", "noninteractive", 1);
		}

		gchar *locale;
		// Set the LANGUAGE so debconf messages get localization
		if (locale = pk_backend_get_locale(m_backend)) {
			setenv("LANGUAGE", locale, 1);
			setenv("LANG", locale, 1);
		//setenv("LANG", "C", 1);
		}

		// Pass the write end of the pipe to the install function
		res = PM->DoInstallPostFork(readFromChildFD[1]);

		// dump errors into cerr (pass it to the parent process)
		_error->DumpErrors();

		close(readFromChildFD[1]);

		_exit(res);
	}

	cout << "PARENT proccess running..." << endl;
	// make it nonblocking, verry important otherwise
	// when the child finish we stay stuck.
	fcntl(readFromChildFD[0], F_SETFL, O_NONBLOCK);

	// init the timer
	m_lastTermAction = time(NULL);
	m_startCounting = false;

	// Check if the child died
	int ret;
	while (waitpid(m_child_pid, &ret, WNOHANG) == 0) {
		updateInterface(readFromChildFD[0], pty_master);
	}

	close(readFromChildFD[0]);
	close(readFromChildFD[1]);
	close(pty_master);

	cout << "Parent finished..." << endl;
	return true;
}

