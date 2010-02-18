// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: acqprogress.cc,v 1.24 2003/04/27 01:56:48 doogie Exp $
/* ######################################################################

   Acquire Progress - Command line progress meter 

   ##################################################################### */
									/*}}}*/
// Include files							/*{{{*/
#include "acqprogress.h"
#include <apt-pkg/acquire-item.h>
#include <apt-pkg/acquire-worker.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/error.h>

#include <stdio.h>
#include <signal.h>
#include <iostream>
									/*}}}*/

using namespace std;

// AcqPackageKitStatus::AcqPackageKitStatus - Constructor				/*{{{*/
// ---------------------------------------------------------------------
/* */
AcqPackageKitStatus::AcqPackageKitStatus(aptcc *apt, PkBackend *backend, bool &cancelled) :
	m_apt(apt),
	m_backend(backend),
	_cancelled(cancelled),
	last_percent(PK_BACKEND_PERCENTAGE_INVALID),
	last_sub_percent(PK_BACKEND_PERCENTAGE_INVALID)
{
}
									/*}}}*/
// AcqPackageKitStatus::Start - Downloading has started			/*{{{*/
// ---------------------------------------------------------------------
/* */
void AcqPackageKitStatus::Start()
{
   pkgAcquireStatus::Start();
   BlankLine[0] = 0;
   ID = 1;
};
									/*}}}*/
// AcqPackageKitStatus::IMSHit - Called when an item got a HIT response	/*{{{*/
// ---------------------------------------------------------------------
/* */
void AcqPackageKitStatus::IMSHit(pkgAcquire::ItemDesc &Itm)
{
//    if (Quiet <= 0)
//       cout << '\r' << BlankLine << '\r';
// 
//    cout << /*_*/("Hit ") << Itm.Description;
//    if (Itm.Owner->FileSize != 0)
//       cout << " [" << SizeToStr(Itm.Owner->FileSize) << "B]";
//    cout << endl;
   Update = true;
};
									/*}}}*/
// AcqPackageKitStatus::Fetch - An item has started to download		/*{{{*/
// ---------------------------------------------------------------------
/* This prints out the short description and the expected size */
void AcqPackageKitStatus::Fetch(pkgAcquire::ItemDesc &Itm)
{
   Update = true;
   if (Itm.Owner->Complete == true)
      return;

   Itm.Owner->ID = ID++;

//    if (Quiet > 1)
//       return;
// 
//    if (Quiet <= 0)
//       cout << '\r' << BlankLine << '\r';
// 
//    cout << /*_*/("Get:") << Itm.Owner->ID << ' ' << Itm.Description;
//    if (Itm.Owner->FileSize != 0)
//       cout << " [" << SizeToStr(Itm.Owner->FileSize) << "B]";
//    cout << endl;
};
									/*}}}*/
// AcqPackageKitStatus::Done - Completed a download				/*{{{*/
// ---------------------------------------------------------------------
/* We don't display anything... */
void AcqPackageKitStatus::Done(pkgAcquire::ItemDesc &Itm)
{
   Update = true;
};
									/*}}}*/
// AcqPackageKitStatus::Fail - Called when an item fails to download		/*{{{*/
// ---------------------------------------------------------------------
/* We print out the error text  */
void AcqPackageKitStatus::Fail(pkgAcquire::ItemDesc &Itm)
{
	// Ignore certain kinds of transient failures (bad code)
	if (Itm.Owner->Status == pkgAcquire::Item::StatIdle) {
		return;
	}

	if (Itm.Owner->Status == pkgAcquire::Item::StatDone)
	{
		// TODO add a PK message
		cout << /*_*/("Ign ") << Itm.Description << endl;
	} else {
		// an error was found (maybe 404, 403...)
		// the item that got the error and the error text
		_error->Error("Error %s\n  %s",
			      Itm.Description.c_str(),
			      Itm.Owner->ErrorText.c_str());
	}

	Update = true;
};
									/*}}}*/
// AcqPackageKitStatus::Stop - Finished downloading				/*{{{*/
// ---------------------------------------------------------------------
/* This prints out the bytes downloaded and the overall average line
   speed */
void AcqPackageKitStatus::Stop()
{
   pkgAcquireStatus::Stop();

   if (FetchedBytes != 0 && _error->PendingError() == false)
      ioprintf(cout,/*_*/("Fetched %sB in %s (%sB/s)\n"),
	       SizeToStr(FetchedBytes).c_str(),
	       TimeToStr(ElapsedTime).c_str(),
	       SizeToStr(CurrentCPS).c_str());
}
									/*}}}*/
// AcqPackageKitStatus::Pulse - Regular event pulse				/*{{{*/
// ---------------------------------------------------------------------
/* This draws the current progress. Each line has an overall percent
   meter and a per active item status meter along with an overall 
   bandwidth and ETA indicator. */
bool AcqPackageKitStatus::Pulse(pkgAcquire *Owner)
{
	pkgAcquireStatus::Pulse(Owner);

	enum {Long = 0,Medium,Short} Mode = Long;

	char Buffer[sizeof(BlankLine)];
	char *End = Buffer + sizeof(Buffer);
	char *S = Buffer;

	unsigned long percent_done;
	percent_done = long(double((CurrentBytes + CurrentItems)*100.0)/double(TotalBytes+TotalItems));
	
	// Emit the percent done
	if (last_percent != percent_done) {
		if (last_percent < percent_done) {
			pk_backend_set_percentage(m_backend, percent_done);
		} else {
			pk_backend_set_percentage(m_backend, PK_BACKEND_PERCENTAGE_INVALID);
			pk_backend_set_percentage(m_backend, percent_done);
		}
		last_percent = percent_done;
	}

	for (pkgAcquire::Worker *I = Owner->WorkersBegin(); I != 0;
		I = Owner->WorkerStep(I))
	{
		// There is no item running
		if (I->CurrentItem == 0)
		{
			continue;
		}
		emit_package(I->CurrentItem->ShortDesc);

		// Add the total size and percent
		if (I->TotalSize > 0 && I->CurrentItem->Owner->Complete == false)
		{
			unsigned long sub_percent;
			sub_percent = long(double(I->CurrentSize*100.0)/double(I->TotalSize));
			if (last_sub_percent != sub_percent) {
				if (last_sub_percent < sub_percent) {
					pk_backend_set_sub_percentage(m_backend, sub_percent);
				} else {
					pk_backend_set_sub_percentage(m_backend, PK_BACKEND_PERCENTAGE_INVALID);
					pk_backend_set_sub_percentage(m_backend, sub_percent);
				}
				last_sub_percent = sub_percent;
			}
		} else {
			if (last_sub_percent != PK_BACKEND_PERCENTAGE_INVALID) {
				pk_backend_set_sub_percentage(m_backend,
							      PK_BACKEND_PERCENTAGE_INVALID);
				last_sub_percent = PK_BACKEND_PERCENTAGE_INVALID;
			}
		}
	}

	/* Put in the ETA and cps meter, block off signals to prevent strangeness
	    during resizing */
	sigset_t Sigs,OldSigs;
	sigemptyset(&Sigs);
	sigaddset(&Sigs,SIGWINCH);
	sigprocmask(SIG_BLOCK,&Sigs,&OldSigs);

// 	if (CurrentCPS != 0)
// 	{
// 		char Tmp[300];
// 		unsigned long ETA = (unsigned long)((TotalBytes - CurrentBytes)/CurrentCPS);
// 		sprintf(Tmp," %sB/s %s",SizeToStr(CurrentCPS).c_str(),TimeToStr(ETA).c_str());
// 		unsigned int Len = strlen(Buffer);
// 		unsigned int LenT = strlen(Tmp);
// 	//       if (Len + LenT < ScreenWidth)
// 	//       {
// 	// 	 memset(Buffer + Len,' ',ScreenWidth - Len);
// 	// 	 strcpy(Buffer + ScreenWidth - LenT,Tmp);
// 	//       }
// 	}
	Buffer[/*ScreenWidth*/1024] = 0;
	BlankLine[/*ScreenWidth*/1024] = 0;
	sigprocmask(SIG_SETMASK,&OldSigs,0);

	Update = false;

	return !_cancelled;;
}
									/*}}}*/
// AcqPackageKitStatus::MediaChange - Media need to be swapped		/*{{{*/
// ---------------------------------------------------------------------
/* Prompt for a media swap */
bool AcqPackageKitStatus::MediaChange(string Media, string Drive)
{
	pk_backend_media_change_required(m_backend,
					 PK_MEDIA_TYPE_ENUM_DISC,
					 Media.c_str(),
					 Media.c_str());

	char errorMsg[400];
	sprintf(errorMsg,
		"Media change: please insert the disc labeled"
		" '%s' "
		"in the drive '%s' and try again.",
		Media.c_str(),
		Drive.c_str());

	pk_backend_error_code(m_backend,
			      PK_ERROR_ENUM_MEDIA_CHANGE_REQUIRED,
			      errorMsg);

	// Set this so we can fail the transaction
	Update = true;
	return false;
}
									/*}}}*/

void AcqPackageKitStatus::addPackagePair(pair<pkgCache::PkgIterator, pkgCache::VerIterator> packagePair)
{
	packages.push_back(packagePair);
}

void AcqPackageKitStatus::emit_package(const string &name)
{
	if (name.compare(last_package_name) != 0 && packages.size()) {
		// find the package
		for(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> >::iterator it = packages.begin();
			    it != packages.end(); ++it)
		{
			if (_cancelled) {
				break;
			}
			// try to see if any package matches
			if (name.compare(it->first.Name()) == 0) {
				m_apt->emit_package(it->first,
						    it->second,
						    PK_INFO_ENUM_UNKNOWN, PK_INFO_ENUM_DOWNLOADING);
				last_package_name = name;
				break;
			}
		}
	}
}
