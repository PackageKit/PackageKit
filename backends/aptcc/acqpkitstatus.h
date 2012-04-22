/* acqpkitstatus.h
 *
 * Copyright (c) 2009 Daniel Nicoletti <dantti12@gmail.com>
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

#ifndef ACQ_PKIT_STATUS_H
#define ACQ_PKIT_STATUS_H

#include <apt-pkg/acquire.h>
#include <pk-backend.h>

#include "apt-intf.h"

class AcqPackageKitStatus : public pkgAcquireStatus
{
public:
    AcqPackageKitStatus(AptIntf *apt, PkBackend *backend, bool &cancelled);

    virtual bool MediaChange(string Media, string Drive);
    virtual void IMSHit(pkgAcquire::ItemDesc &Itm);
    virtual void Fetch(pkgAcquire::ItemDesc &Itm);
    virtual void Done(pkgAcquire::ItemDesc &Itm);
    virtual void Fail(pkgAcquire::ItemDesc &Itm);
    virtual void Start();
    virtual void Stop();

    bool Pulse(pkgAcquire *Owner);

    void addPackage(const pkgCache::VerIterator &ver);

private:
    PkBackend *m_backend;
    unsigned long ID;
    bool &_cancelled;

    unsigned long last_percent;
    unsigned long last_sub_percent;
    double        last_CPS;
    string        last_package_name;
    AptIntf       *m_apt;

    PkgList packages;
    set<string> currentPackages;

    void emit_package(const string &name, bool finished);
};

#endif
