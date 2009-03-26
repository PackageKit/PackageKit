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

class AcqPackageKitStatus : public pkgAcquireStatus
{
	PkBackend *m_backend;
	char BlankLine[1024];
	unsigned long ID;
	unsigned long Quiet;
	bool &_cancelled;

public:
	virtual bool MediaChange(string Media,string Drive);
	virtual void IMSHit(pkgAcquire::ItemDesc &Itm);
	virtual void Fetch(pkgAcquire::ItemDesc &Itm);
	virtual void Done(pkgAcquire::ItemDesc &Itm);
	virtual void Fail(pkgAcquire::ItemDesc &Itm);
	virtual void Start();
	virtual void Stop();

	bool Pulse(pkgAcquire *Owner);

	AcqPackageKitStatus(PkBackend *backend, bool &cancelled, unsigned int Quiet);
};

#endif
