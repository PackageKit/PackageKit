// apt.h  -*-c++-*-
//
//  Copyright 1999-2002, 2004-2005, 2007-2008 Daniel Burrows
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
//

#ifndef APT_H
#define APT_H

#include <apt-pkg/pkgrecords.h>
#include <apt-pkg/pkgcachegen.h>
#include <apt-pkg/policy.h>

#include <pk-backend.h>

using namespace std;

/**
*  Emits files of packages
*/
void emit_files (PkBackend *backend, const PkPackageId *pi);

/**
*  returns a list of packages names
*/
vector<string> search_file (PkBackend *backend, const string &file_name, bool &_cancel);

class aptcc
{
public:
	aptcc();
	~aptcc();

	bool init(const char *locale, pkgSourceList &apt_source_list);

	pkgCache::VerIterator find_ver(pkgCache::PkgIterator pkg);

	/**
	 *  Get depends
	 */
	vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > get_depends(pkgCache::PkgIterator pkg,
										bool recursive,
										bool &_cancel);

	/**
	 *  Get requires
	 */
	vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > get_requires(pkgCache::PkgIterator pkg,
										 bool recursive,
										 bool &_cancel);

	/**
	 *  Emits a package if it match the filters
	 */
	void emit_package(PkBackend *backend,
			  PkBitfield filters,
			  const pkgCache::PkgIterator &pkg,
			  const pkgCache::VerIterator &ver);

	/**
	 *  Emits details
	 */
	void emit_details(PkBackend *backend,
			  const pkgCache::PkgIterator &pkg);

	pkgRecords    *packageRecords;
	pkgCache      *cacheFile;
private:
	MMap          *Map;

	OpProgress    Progress;
	pkgDepCache   *DCache;
};

#endif
