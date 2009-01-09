
#include "apt.h"
#include "apt-utils.h"
#include <pk-backend.h>
#include <apt-pkg/sourcelist.h>

apt_init::apt_init(const char *locale, pkgSourceList &apt_source_list)
	:
	packageRecords(0),
	cacheFile(0),
	Map(0),
	DCache(0)
{
	// Generate it and map it
	setlocale(LC_ALL, locale);
	pkgMakeStatusCache(apt_source_list, Progress, &Map, true);
	cacheFile = new pkgCache(Map);

	// Create the text record parser
	packageRecords = new pkgRecords (*cacheFile);


pkgSourceList List;
  if(!List.ReadMainList())
  printf("-->The list of sources could not be read.\n");

	// create depcache
	pkgPolicy Plcy(cacheFile);
// if(_error->PendingError())
// printf("-->PendingError\n");
printf("-->ReadPinFile\n");
if(!ReadPinFile(Plcy))
    printf("-->return false;\n");

DCache = new pkgDepCache(cacheFile, &Plcy);
// if(_error->PendingError())
// printf("-->PendingError\n");

  DCache->Init(&Progress/*, WithLock, do_initselections, status_fname*/);
  Progress.Done();
//   if(_error->PendingError())
printf("-->end\n");
}

apt_init::~apt_init()
{
	if (packageRecords)
	{
		egg_debug ("~apt_init packageRecords");
		delete packageRecords;
		packageRecords = NULL;
	}

	if (cacheFile)
	{
		egg_debug ("~apt_init cacheFile");
		delete cacheFile;
		cacheFile = NULL;
	}

	if (DCache)
	{
		egg_debug ("~apt_init DCache");
		delete DCache;
		DCache = NULL;
	}

	delete Map;
}

pkgCache::VerIterator apt_init::find_ver(pkgCache::PkgIterator pkg)
{
// printf("-->sourcestr %s - %d<<<>>%d<\n", sourcestr.c_str(), source, cmdline_version_curr_or_cand);
//   switch(source)
//     {
//     case cmdline_version_curr_or_cand:
      if(!pkg.CurrentVer().end())
	return pkg.CurrentVer();
      // Fall-through.
//     case cmdline_version_cand:
//       {

pkgDepCache::StateCache & State = (*DCache)[pkg];
//    if (State.CandidateVer == 0)
//       return NULL;
//    return State.CandidateVerIter(*_depcache).VerStr();
// printf("-->candver\n");
pkgCache::VerIterator candver=State.CandidateVerIter(*DCache);
// ::StateCache
// 	pkgCache::VerIterator candver=Plcy.GetCandidateVer(pkg);
// printf("-->candver\n");
// 	pkgCache::VerIterator candver=((pkgDepCache::StateCache) (*DCache)[pkg]).CandidateVerIter(*apt_cache_file);
// printf("-->candver\n");
	if(!candver.end())
	  {
	    return candver;
	}
// printf("-->candver\n");
	  return pkg.VersionList();
// 	    if(source == cmdline_version_cand)
// 	      printf(_("No candidate version found for %s\n"), pkg.Name());
// 	    else
// 	      printf(_("No current or candidate version found for %s\n"), pkg.Name());
// 	  }

// 	return candver;
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
			if (pkgName.compare(pkgName.size() - 4, 4, "-dev") &&
			    pkgName.compare(pkgName.size() - 4, 4, "-dbg") &&
			    section.compare("devel") &&
			    section.compare("libdevel"))
				return;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT)) {
			std::string pkgName = pkg.Name();
			if (!pkgName.compare(pkgName.size() - 4, 4, "-dev") ||
			    !pkgName.compare(pkgName.size() - 4, 4, "-dbg") ||
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
			    get_long_description(ver, records).c_str(),
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
