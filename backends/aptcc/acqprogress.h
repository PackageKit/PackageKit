// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: acqprogress.h,v 1.5 2003/02/02 22:24:11 jgg Exp $
/* ######################################################################

   Acquire Progress - Command line progress meter

   ##################################################################### */
									/*}}}*/
#ifndef ACQPROGRESS_H
#define ACQPROGRESS_H

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

	void addPackagePair(pair<pkgCache::PkgIterator, pkgCache::VerIterator> packagePair);

private:
	PkBackend *m_backend;
	unsigned long ID;
	bool &_cancelled;

	unsigned long last_percent;
	unsigned long last_sub_percent;
	double        last_CPS;
	string        last_package_name;
	AptIntf         *m_apt;

	vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > packages;
	set<string> currentPackages;

	void emit_package(const string &name, bool finished);
};

#endif
