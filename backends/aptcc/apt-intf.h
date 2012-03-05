/* apt-intf.h - Interface to APT
 *
 * Copyright (c) 1999-2002, 2004-2005, 2007-2008 Daniel Burrows
 * Copyright (c) 2009 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
 *               2012 Matthias Klumpp <matthias@tenstral.net>
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

#ifndef APTINTF_H
#define APTINTF_H

#include <apt-pkg/pkgrecords.h>
#include <apt-pkg/pkgcachegen.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/policy.h>

#include <pk-backend.h>

#include <set>

using namespace std;

/**
*  returns a list of packages names
*/
vector<string> search_files (PkBackend *backend, gchar **values, bool &_cancel);

/**
*  returns a list of packages names
*/
vector<string> searchMimeType (PkBackend *backend, gchar **values, bool &error, bool &_cancel);

typedef pair<pkgCache::PkgIterator, pkgCache::VerIterator> PkgPair;
typedef vector<PkgPair> PkgList;

class pkgProblemResolver;

class AptIntf
{

public:
	AptIntf(PkBackend *backend, bool &cancel);
	~AptIntf();

	bool init();
	void cancel();

	// Check the returned VerIterator.end()
	// if it's true we could not find it
	pair<pkgCache::PkgIterator, pkgCache::VerIterator>
			find_package_id(const gchar *package_id, bool &found);
	pkgCache::VerIterator find_ver(const pkgCache::PkgIterator &pkg);
	pkgCache::VerIterator find_candidate_ver(const pkgCache::PkgIterator &pkg);

	PkgList resolvePI(gchar **package_ids);
	bool markDebFileForInstall(const gchar *file, PkgList &install, PkgList &remove);

	/**
	 *  runs a transaction to install/remove/update packages
	 *  - for install and update, \p remove should be set to false
	 *  - if you are going to remove, \p remove should be true
	 *  - if you don't want to actually install/update/remove
	 *    \p simulate should be true, in this case packages with
	 *    what's going to happen will be emitted.
	 */
	bool runTransaction(PkgList &install, PkgList &remove, bool simulate);

	/**
	 *  Get depends
	 */
	void get_depends(PkgList &output,
			 pkgCache::PkgIterator pkg,
			 bool recursive);

	/**
	 *  Get requires
	 */
	void get_requires(PkgList &output,
			  pkgCache::PkgIterator pkg,
			  bool recursive);

	/**
	 *  Emits a package if it match the filters
	 */
	void emit_package(const pkgCache::PkgIterator &pkg,
			  const pkgCache::VerIterator &ver,
			  PkBitfield filters = PK_FILTER_ENUM_NONE,
			  PkInfoEnum state = PK_INFO_ENUM_UNKNOWN);

	void emit_packages(PkgList &output,
			   PkBitfield filters = PK_FILTER_ENUM_NONE,
			   PkInfoEnum state = PK_INFO_ENUM_UNKNOWN);

	void emitUpdates(PkgList &output, PkBitfield filters = PK_FILTER_ENUM_NONE);

	/**
	 *  Emits details
	 */
	void emitDetails(const pkgCache::PkgIterator &pkg, const pkgCache::VerIterator &ver);
	void emitDetails(PkgList &pkgs);

	/**
	 *  Emits update detail
	 */
	void emitUpdateDetails(const pkgCache::PkgIterator &pkg, const pkgCache::VerIterator &ver);
	void emitUpdateDetails(PkgList &pkgs);

	/**
	*  Emits files of packages
	*/
	void emitFiles(PkBackend *backend, const gchar *pi);

	/**
	 *  seems to install packages
	 */
	bool installPackages(pkgCacheFile &Cache);

	/**
	 *  check which package provides the codec
	 */
	void providesCodec(PkgList &output, gchar **values);

	/**
	 *  check which package provides a shared library
	 */
	void providesLibrary(PkgList &output, gchar **values);

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

	bool m_isMultiArch;
	PkgList m_pkgs;
	string m_localDebFile;
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
