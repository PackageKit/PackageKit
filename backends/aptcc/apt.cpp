// apt.cc
//
//  Copyright 1999-2008 Daniel Burrows
//  Copyright (C) 2009 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
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

#include <apt-pkg/error.h>
#include <apt-pkg/tagfile.h>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/init.h>

#include <fstream>
#include <dirent.h>
#include <assert.h>

aptcc::aptcc(PkBackend *backend)
	:
	packageRecords(0),
	cacheFile(0),
	Map(0),
	DCache(0),
	Policy(0),
	m_backend(backend)
{
}

bool aptcc::init(const char *locale, pkgSourceList &apt_source_list)
{
	// Generate it and map it
	setlocale(LC_ALL, locale);
	bool Res = pkgMakeStatusCache(apt_source_list, Progress, &Map, true);
	Progress.Done();
	if(!Res) {
		return false;
		//_("The package lists or status file could not be parsed or opened.")
	}

	cacheFile = new pkgCache(Map);
	if (_error->PendingError()) {
		return false;
	}

	// create depcache
	Policy = new pkgPolicy(cacheFile);
	if (_error->PendingError()) {
		return false;
	}

	if (!ReadPinFile(*Policy)) {
		return false;
	}

	DCache = new pkgDepCache(cacheFile, Policy);
	if (_error->PendingError()) {
		return false;
	}

	DCache->Init(&Progress);
	Progress.Done();
	if (_error->PendingError()) {
		return false;
	}

	// Create the text record parser
	packageRecords = new pkgRecords(*DCache);
}

aptcc::~aptcc()
{
	if (packageRecords)
	{
		egg_debug ("~apt_init packageRecords");
		delete packageRecords;
	}

	if (cacheFile)
	{
		egg_debug ("~apt_init cacheFile");
		delete cacheFile;
	}

	if (DCache)
	{
		egg_debug ("~apt_init DCache");
		delete DCache;
	}

	if (Policy)
	{
		egg_debug ("~apt_init Policy");
		delete Policy;
	}

	delete Map;
}

pkgCache::VerIterator aptcc::find_candidate_ver(const pkgCache::PkgIterator &pkg)
{
	// get the candidate version iterator
	return (*DCache)[pkg].CandidateVerIter(*DCache);
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

pkgDepCache::StateCache aptcc::get_state(const pkgCache::PkgIterator &pkg)
{
	return (*DCache)[pkg];
}

bool aptcc::is_held(const pkgCache::PkgIterator &pkg)
{
//   aptitude_state state=get_ext_state(pkg);
//       pkgTagFile tagfile(&state_file);
//       pkgTagSection section;

	pkgCache::VerIterator candver = find_candidate_ver(pkg);

	return !pkg.CurrentVer().end() &&
	    (pkg->SelectedState == pkgCache::State::Hold ||
	    (!candver.end() && false/*candver.VerStr() == state.forbidver*/));
	// TODO add forbid ver support
}

void aptcc::mark_all_upgradable(bool with_autoinst,
					bool ignore_removed/*,
					undo_group *undo*/)
{
//   if(read_only && !read_only_permission())
//     {
//       if(group_level == 0)
// 	read_only_fail();
//       return;
//     }

//   pre_package_state_changed();

// //   action_group group(*this, NULL);


	for(int iter=0; iter==0 || (iter==1 && with_autoinst); ++iter) {
		// Do this twice, only turning auto-install on the second time.
		// A reason for this is the following scenario:
		//
		// Packages A and B are installed at 1.0.  Package C is not installed.
		// Version 2.0 of each package is available.
		//
		// Version 2.0 of A depends on "C (= 2.0) | B (= 2.0)".
		//
		// Upgrading A if B is not upgraded will cause this dependency to
		// break.  Auto-install will then cheerfully fulfill it by installing
		// C.
		//
		// A real-life example of this is xemacs21, xemacs21-mule, and
		// xemacs21-nomule; aptitude would keep trying to install the mule
		// version on upgrades.
		bool do_autoinstall=(iter==1);

		for(pkgCache::PkgIterator i=DCache->PkgBegin(); !i.end(); i++) {
			pkgDepCache::StateCache state = get_state(i);
// 			aptitude_state &estate = get_ext_state(i);

			if(i.CurrentVer().end()){
				continue;
			}

			bool do_upgrade = false;

			if(!ignore_removed) {
			    do_upgrade = state.Status > 0 && !is_held(i);
			} else {
				switch(i->SelectedState) {
				    // This case shouldn't really happen:
				    // if this shouldn't happen i guess we don't
				    // even need to worry? am i right?
		    // 		case pkgCache::State::Unknown:
		    // 		  estate.selection_state=pkgCache::State::Install;

				    // Fall through
				case pkgCache::State::Install:
					if(state.Status > 0 && !is_held(i)) {
						do_upgrade = true;
					}
					break;
				default:
					break;
				}
			}

			if(do_upgrade) {
		// 	      pre_package_state_changed();
			    dirty = true;
			    DCache->MarkInstall(i, do_autoinstall);
			}
		}
	}
}

// used to emit packages it collects all the needed info
void aptcc::emit_package(const pkgCache::PkgIterator &pkg,
			 const pkgCache::VerIterator &ver,
			 PkBitfield filters,
			 PkInfoEnum state)
{
	// check the state enum to see if it was not set.
	if (state == PK_INFO_ENUM_UNKNOWN) {
		if (pkg->CurrentState == pkgCache::State::Installed) {
			state = PK_INFO_ENUM_INSTALLED;
		} else {
			state = PK_INFO_ENUM_AVAILABLE;
		}
	}

	if (filters != 0) {
		std::string str = ver.Section();
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
			if (!repo_section.compare("contrib") || !repo_section.compare("non-free")) {
				return;
			}
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_FREE)) {
			if (repo_section.compare("contrib") && repo_section.compare("non-free")) {
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
					 vf.File().Archive());
	pk_backend_package(m_backend,
			   state,
			   package_id,
			   get_short_description(ver, packageRecords).c_str());
}

// used to emit packages it collects all the needed info
void aptcc::emit_details(const pkgCache::PkgIterator &pkg)
{
	pkgCache::VerIterator ver = find_ver(pkg);
	std::string section = ver.Section();

	size_t found;
	found = section.find_last_of("/");
	section = section.substr(found + 1);

	pkgCache::VerFileIterator vf = ver.FileList();
	pkgRecords::Parser &rec = packageRecords->Lookup(vf);

	std::string homepage;
// TODO support this
// #ifdef APT_HAS_HOMEPAGE
	if(rec.Homepage() != "") {
		homepage = rec.Homepage();
	}
// #endif

	gchar *package_id;
	package_id = pk_package_id_build(pkg.Name(),
					ver.VerStr(),
					ver.Arch(),
					vf.File().Archive());
	pk_backend_details(m_backend,
			   package_id,
			   "unknown",
			   get_enum_group(section),
			   get_long_description_parsed(ver, packageRecords).c_str(),
			   homepage.c_str(),
			   ver->Size);
}

// used to emit packages it collects all the needed info
void aptcc::emit_update_detail(const pkgCache::PkgIterator &pkg)
{
	pkgCache::VerIterator candver = find_candidate_ver(pkg);

	pkgCache::VerFileIterator vf = candver.FileList();
	pkgRecords::Parser &rec = packageRecords->Lookup(vf);
	string archive(vf.File().Archive());
	gchar *package_id;
	package_id = pk_package_id_build(pkg.Name(),
					candver.VerStr(),
					candver.Arch(),
					archive.c_str());

	pkgCache::VerIterator currver = find_ver(pkg);
	pkgCache::VerFileIterator currvf = currver.FileList();
	gchar *current_package_id;
	current_package_id = pk_package_id_build(pkg.Name(),
						currver.VerStr(),
						currver.Arch(),
						currvf.File().Archive());

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
	pk_backend_update_detail(m_backend,
				 package_id,
				 current_package_id,//const gchar *updates
				 "",//const gchar *obsoletes
				 "",//const gchar *vendor_url
				 "",//const gchar *bugzilla_url
				 "",//const gchar *cve_url
				 PK_RESTART_ENUM_NONE,//PkRestartEnum restart
				 "",//const gchar *update_text
				 "",//const gchar *changelog
				 updateState,//PkUpdateStateEnum state
				 "",//const gchar *issued_text
				 ""//const gchar *updated_text
				 );
}

void aptcc::get_depends(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > &output,
			pkgCache::PkgIterator pkg,
			bool recursive,
			bool &_cancel)
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
					get_depends(output, dep.TargetPkg(), recursive, _cancel);
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
			bool recursive,
			bool &_cancel)
{
	for (pkgCache::PkgIterator parentPkg = cacheFile->PkgBegin(); !parentPkg.end(); ++parentPkg) {
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
			get_depends(deps, parentPkg, false, _cancel);
			for (vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> >::iterator i=deps.begin();
			    i != deps.end();
			    ++i)
			{
				if (i->first == pkg) {
					if (recursive) {
						if (!contains(output, parentPkg)) {
							output.push_back(pair<pkgCache::PkgIterator, pkgCache::VerIterator>(parentPkg, ver));
							get_requires(output, parentPkg, recursive, _cancel);
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

// used to emit files it reads the info directly from the files
vector<string> search_file (PkBackend *backend, const string &file_name, bool &_cancel)
{
	vector<string> packageList;

	matcher *m_matcher = new matcher(string(file_name));
	if (m_matcher->hasError()) {
		egg_debug("Regex compilation error");
		delete m_matcher;
		return vector<string>();
	}

	DIR *dp;
	struct dirent *dirp;
	if (!(dp = opendir("/var/lib/dpkg/info/"))) {
		egg_debug ("Error opening /var/lib/dpkg/info/\n");
		delete m_matcher;
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
			map<int, bool> matchers_used;
			while (!in.eof()) {
				getline(in, line);
				if (m_matcher->matchesFile(line, matchers_used)) {
					string file(dirp->d_name);
					printf("matchers_used: %d", matchers_used.size());
					packageList.push_back(file.erase(file.size() - 5, file.size()));
					break;
				}
			}
		}
	}
	closedir(dp);
	delete m_matcher;
	return packageList;
}

// used to emit files it reads the info directly from the files
void emit_files (PkBackend *backend, const PkPackageId *pi)
{
	static string filelist;
	string line;

	filelist.erase(filelist.begin(), filelist.end());

	string f = "/var/lib/dpkg/info/" + string(pi->name) + ".list";
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
			pk_backend_files (backend, pk_package_id_to_string(pi), filelist.c_str());
		}
	}
}
