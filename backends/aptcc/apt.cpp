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

#include <fstream>
#include <dirent.h>

aptcc::aptcc()
	:
	packageRecords(0),
	cacheFile(0),
	Map(0),
	DCache(0)
{
}

bool aptcc::init(const char *locale, pkgSourceList &apt_source_list)
{
	// Generate it and map it
	setlocale(LC_ALL, locale);
	pkgMakeStatusCache(apt_source_list, Progress, &Map, true);
	cacheFile = new pkgCache(Map);
	if(_error->PendingError())
		return false;

	// Create the text record parser
	packageRecords = new pkgRecords (*cacheFile);

	// create depcache
	pkgPolicy Plcy(cacheFile);
	if(_error->PendingError()) {
		return false;
	}

	if(!ReadPinFile(Plcy)) {
		return false;
	}

	DCache = new pkgDepCache(cacheFile, &Plcy);
	if(_error->PendingError()) {
		return false;
	}

	DCache->Init(&Progress);
	if(_error->PendingError()) {
		return false;
	}
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

	delete Map;
}

pkgCache::VerIterator aptcc::find_ver(pkgCache::PkgIterator pkg)
{
	// if the package is installed return the current version
	if(!pkg.CurrentVer().end()) {
		return pkg.CurrentVer();
	}

	// Else get the candidate version iterator
	pkgCache::VerIterator candver=(*DCache)[pkg].CandidateVerIter(*DCache);
	if(!candver.end())
	{
	    return candver;
	}

	// return the version list as a last resource
	return pkg.VersionList();
}

// used to emit packages it collects all the needed info
void emit_package (PkBackend *backend, pkgRecords *records, PkBitfield filters,
		   const pkgCache::PkgIterator &pkg,
		   const pkgCache::VerIterator &ver)
{
	PkInfoEnum state;
	if (pkg->CurrentState == pkgCache::State::Installed) {
		state = PK_INFO_ENUM_INSTALLED;
	} else {
		state = PK_INFO_ENUM_AVAILABLE;
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
			    section.compare("libdevel"))
				return;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT)) {
			std::string pkgName = pkg.Name();
			if (ends_with(pkgName, "-dev") ||
			    ends_with(pkgName, "-dbg") ||
			    !section.compare("devel") ||
			    !section.compare("libdevel"))
				return;
		}

		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_GUI)) {
			// if ver.end() means unknow
			// strcmp will be true when it's different than x11
			if (section.compare("x11") && section.compare("gnome") &&
			    section.compare("kde") && section.compare("graphics"))
				return;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_GUI)) {
			if (!section.compare("x11") || !section.compare("gnome") ||
			    !section.compare("kde") || !section.compare("graphics"))
				return;
		}

		// TODO add Ubuntu handling
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_FREE)) {
			if (!repo_section.compare("contrib") || !repo_section.compare("non-free"))
				return;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_FREE)) {
			if (repo_section.compare("contrib") && repo_section.compare("non-free"))
				return;
		}

		// TODO test this one..
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_COLLECTIONS)) {
			if (!repo_section.compare("metapackages"))
				return;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_COLLECTIONS)) {
			if (repo_section.compare("metapackages"))
				return;
		}

	}
	pkgCache::VerFileIterator vf = ver.FileList();

	gchar *package_id;
	package_id = pk_package_id_build ( pkg.Name(),
					ver.VerStr(),
					ver.Arch() ? ver.Arch() : "N/A", // _("N/A")
					vf.File().Archive() ? vf.File().Archive() : "<NULL>");//  _("<NULL>")
	pk_backend_package (backend, state, package_id, get_short_description(ver, records).c_str() );
}

// used to emit packages it collects all the needed info
void emit_details (PkBackend *backend, pkgRecords *records,
		   const pkgCache::PkgIterator &pkg,
		   const pkgCache::VerIterator &ver)
{
	std::string section = ver.Section();

	size_t found;
	found = section.find_last_of("/");
	section = section.substr(found + 1);

	pkgCache::VerFileIterator vf = ver.FileList();
	pkgRecords::Parser &rec = records->Lookup(vf);

	std::string homepage;
// TODO support this
// #ifdef APT_HAS_HOMEPAGE
	if(rec.Homepage() != "")
		homepage = rec.Homepage();
// #endif

	gchar *package_id;
	package_id = pk_package_id_build ( pkg.Name(),
					ver.VerStr(),
					ver.Arch() ? ver.Arch() : "N/A", // _("N/A")
					vf.File().Archive() ? vf.File().Archive() : "<NULL>");//  _("<NULL>")
	pk_backend_details (backend,
			    package_id,
			    "GPL2",
			    get_enum_group(section),
			    get_long_description_parsed(ver, records).c_str(),
			    homepage.c_str(), ver->Size);
}

// used to emit packages it collects all the needed info
void emit_requires (PkBackend *backend, pkgRecords *records, PkBitfield filters,
		   const pkgCache::PkgIterator &pkg,
		   const pkgCache::VerIterator &ver)
{

//       cout << "Dependencies: " << endl;
      for (pkgCache::VerIterator Cur = pkg.VersionList(); Cur.end() != true; Cur++)
      {
// 	 cout << Cur.VerStr() << " - ";
//TODO check depends type
	 for (pkgCache::DepIterator Dep = Cur.DependsList(); Dep.end() != true; Dep++)
// 	    cout << Dep.TargetPkg().Name() << " (" << (int)Dep->CompareOp << " " << DeNull(Dep.TargetVer()) << ") ";
// 	 cout << endl;
if (Dep.TargetPkg().VersionList().end() == false) {
	    emit_package (backend, records, filters, Dep.TargetPkg(), Dep.TargetPkg().VersionList());
}
      }

}

// used to emit files it reads the info directly from the files
vector<string> search_file (PkBackend *backend, const string &file_name)
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
		return vector<string>();
	}

	string line;
	while ((dirp = readdir(dp)) != NULL) {
		if (ends_with(dirp->d_name, ".list")) {
			string f = "/var/lib/dpkg/info/" + string(dirp->d_name);
			ifstream in(f.c_str());
			if (!in != 0) {
				continue;
			}
			while (!in.eof()) {
				getline(in, line);
				if (m_matcher->matches(line)) {
					string file(dirp->d_name);
					packageList.push_back(file.erase(file.size() - 5, file.size()));
					break;
				}
			}
		}
	}
	closedir(dp);
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
