/* pkg_acqfile.h
 *
 * Copyright (c) 2002, 2005 Daniel Burrows
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

#ifndef PKG_ACQFILE_H
#define PKG_ACQFILE_H

#include <apt-pkg/acquire-item.h>

/** \file pkg_acqfile.h
 */

class pkgAcqFileSane : public pkgAcquire::Item
// This is frustrating: pkgAcqFile is **almost** good enough, but has some
// hardcoded stuff that makes it not quite work.
//
//  Based heavily on that class, though.
{
  pkgAcquire::ItemDesc Desc;
  string Md5Hash;
  unsigned int Retries;

public:
  pkgAcqFileSane(pkgAcquire *Owner, string URI,
         string Description, string ShortDesc, string filename);

  void Failed(string Message, pkgAcquire::MethodConfig *Cnf);
  string MD5Sum() {return Md5Hash;}
  string DescURI() {return Desc.URI;}
  virtual ~pkgAcqFileSane() {}
};

/** Like pkgAcqArchive, but uses generic File objects to download to
 *  the cwd (and copies from file:/ URLs).
 */
bool get_archive(pkgAcquire *Owner, pkgSourceList *Sources,
		pkgRecords *Recs, pkgCache::VerIterator const &Version,
		std::string directory, std::string &StoreFilename);

#endif
