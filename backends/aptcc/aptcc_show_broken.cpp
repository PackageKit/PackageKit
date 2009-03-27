// aptcc_show_broken.cc
//
//   Copyright 2004 Daniel Burrows
//   Copyright 2009 Daniel Nicoletti

#include "aptcc_show_broken.h"

#include "apt.h"
#include "apt-utils.h"

#include <stdio.h>
#include <string>
#include <sstream>

using namespace std;

/** Shows broken dependencies for a single package */
static string show_broken_deps(aptcc *m_apt, pkgCache::PkgIterator pkg)
{
	unsigned int indent=strlen(pkg.Name())+3;
	bool is_first_dep=true;
	stringstream output;

	pkgCache::VerIterator ver;
	ver = (*m_apt->packageDepCache)[pkg].InstVerIter(*m_apt->packageDepCache);

	output << "  " << pkg.Name() << ":";
	for (pkgCache::DepIterator dep=ver.DependsList(); !dep.end(); ++dep)
	{
		pkgCache::DepIterator first=dep, prev=dep;

		while (dep->CompareOp & pkgCache::Dep::Or) {
		    ++dep;
		}

		// Yep, it's broken.
		if (dep.IsCritical() &&
		    !((*m_apt->packageDepCache)[dep]&pkgDepCache::DepGInstall))
		{
			bool is_first_of_or=true;
			// Iterate over the OR group, print out the information.

			do
			{
				if(!is_first_dep) {
					for(unsigned int i=0; i<indent; ++i) {
						output << " ";
					}
				}

				is_first_dep=false;

				if (!is_first_of_or) {
					for(unsigned int i=0; i<strlen(dep.DepType())+3; ++i) {
						output << " ";
					}
				} else {
					output << " " << first.DepType() << ": ";
				}

				is_first_of_or=false;

				if (first.TargetVer()) {
					output << first.TargetPkg().Name() << " (" << first.CompType() << " " << first.TargetVer() << ")";
				} else {
					output << first.TargetPkg().Name();
				}

				// FIXME: handle virtual packages sanely.
				pkgCache::PkgIterator target=first.TargetPkg();
				// Don't skip real packages which are provided.
				if (!target.VersionList().end())
				{
					output << " ";

					pkgCache::VerIterator ver=(*m_apt->packageDepCache)[target].InstVerIter(*m_apt->packageDepCache);

					if (!ver.end()) // ok, it's installable.
					{
						char buffer[1024];
						if ((*m_apt->packageDepCache)[target].Install()) {
							sprintf(buffer, "but %s is to be installed.",
							    ver.VerStr());
						} else if ((*m_apt->packageDepCache)[target].Upgradable()) {
							sprintf(buffer, "but %s is installed and it is kept back.",
							    target.CurrentVer().VerStr());
						} else {
							sprintf(buffer, "but %s is installed.",
							    target.CurrentVer().VerStr());
						}
						output << buffer;
					} else {
					    output << "but it is not installable";
					}
				} else {
				    // FIXME: do something sensible here!
				    output << " which is a virtual package.";
				}

				if (first != dep) {
				    output << " or";
				}

				output << endl;

				prev=first;
				++first;
			} while (prev != dep);
		}
	}
	return output.str();
}

bool show_broken(PkBackend *backend, aptcc *m_apt)
{
	pkgvector broken;
	for (pkgCache::PkgIterator i = m_apt->packageDepCache->PkgBegin();
	    !i.end(); ++i)
	{
		if((*m_apt->packageDepCache)[i].InstBroken())
			broken.push_back(i);
	}

	if (!broken.empty())
	    {
		stringstream out;
		out << "The following packages have unmet dependencies:" << endl;

		for (pkgvector::iterator pkg = broken.begin(); pkg != broken.end(); ++pkg) {
			out << show_broken_deps(m_apt, *pkg);
		}

		pk_backend_error_code (backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, out.str().c_str());

		return false;
	}

	return true;
}
