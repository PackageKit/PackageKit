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
#include <apt-pkg/cachefile.h>
#include <apt-pkg/policy.h>

#include <pk-backend.h>

#include <set>

using namespace std;

/**
*  Emits files of packages
*/
void emit_files (PkBackend *backend, const gchar *pi);

/**
*  returns a list of packages names
*/
vector<string> search_files (PkBackend *backend, gchar **values, bool &_cancel);

/**
*  returns a list of packages names
*/
vector<string> searchMimeType (PkBackend *backend, gchar **values, bool &error, bool &_cancel);

class pkgProblemResolver;
class aptcc
{
//     typedef int user_tag_reference;
public:
	aptcc(PkBackend *backend, bool &cancel);
	~aptcc();

	bool init();
	void cancel();

	// Check the returned VerIterator.end()
	// if it's true we could not find it
	pair<pkgCache::PkgIterator, pkgCache::VerIterator>
			find_package_id(const gchar *package_id, bool &found);
	pkgCache::VerIterator find_ver(const pkgCache::PkgIterator &pkg);
	pkgCache::VerIterator find_candidate_ver(const pkgCache::PkgIterator &pkg);

	/**
	 *  runs a transaction to install/remove/update packages
	 *  - for install and update, \p remove should be set to false
	 *  - if you are going to remove, \p remove should be true
	 *  - if you don't want to actually install/update/remove
	 *    \p simulate should be true, in this case packages with
	 *    what's going to happen will be emitted.
	 */
	bool runTransaction(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > &pkgs,
			    bool simulate,
			    bool remove);

	/**
	 *  Get depends
	 */
	void get_depends(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > &output,
			 pkgCache::PkgIterator pkg,
			 bool recursive);

	/**
	 *  Get requires
	 */
	void get_requires(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > &output,
			  pkgCache::PkgIterator pkg,
			  bool recursive);

	/**
	 *  Emits a package if it match the filters
	 */
	void emit_package(const pkgCache::PkgIterator &pkg,
			  const pkgCache::VerIterator &ver,
			  PkBitfield filters = PK_FILTER_ENUM_NONE,
			  PkInfoEnum state = PK_INFO_ENUM_UNKNOWN);

	void emit_packages(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > &output,
			   PkBitfield filters = PK_FILTER_ENUM_NONE,
			   PkInfoEnum state = PK_INFO_ENUM_UNKNOWN);

	void emitUpdates(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > &output,
			 PkBitfield filters = PK_FILTER_ENUM_NONE);

	/**
	 *  Emits details
	 */
	void emit_details(const pkgCache::PkgIterator &pkg, const pkgCache::VerIterator &ver);

	/**
	 *  Emits update detail
	 */
	void emit_update_detail(const pkgCache::PkgIterator &pkg, const pkgCache::VerIterator &ver);

	/**
	 *  seems to install packages
	 */
	bool installPackages(pkgCacheFile &Cache);

	/**
	 *  check if the package provides the codec
	 */
	void povidesCodec(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > &output,
			  gchar **values);

	pkgRecords    *packageRecords;
	pkgCache      *packageCache;
	pkgDepCache   *packageDepCache;
	pkgSourceList *packageSourceList;

private:
	MMap       *Map;
	OpProgress Progress;
	pkgPolicy  *Policy;
	PkBackend  *m_backend;
	bool       &_cancel;

	bool TryToInstall(pkgCache::PkgIterator Pkg,
			  pkgDepCache &Cache,
			  pkgProblemResolver &Fix,
			  bool Remove,
			  bool BrokenFix,
			  unsigned int &ExpectedInst);

	/**
	 *  interprets dpkg status fd
	*/
	void updateInterface(int readFd, int writeFd);
	bool DoAutomaticRemove(pkgCacheFile &Cache);
	void emitChangedPackages(pkgCacheFile &Cache);
	bool removingEssentialPackages(pkgCacheFile &Cache);

	vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > m_pkgs;
	void populateInternalPackages(pkgCacheFile &Cache);
	void emitTransactionPackage(string name, PkInfoEnum state);
	time_t     m_lastTermAction;
	string     m_lastPackage;
	uint       m_lastSubProgress;
	PkInfoEnum m_state;
	bool       m_startCounting;
	// when the internal terminal timesout after no activity
	int m_terminalTimeout;
	pid_t m_child_pid;
};

#endif
