/* acqpkitstatus.cpp
 *
 * Copyright (c) 2009 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
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

#include "acqpkitstatus.h"

#include <apt-pkg/acquire-item.h>
#include <apt-pkg/acquire-worker.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/error.h>

#include <stdio.h>
#include <signal.h>
#include <iostream>

using namespace std;

// AcqPackageKitStatus::AcqPackageKitStatus - Constructor
// ---------------------------------------------------------------------
AcqPackageKitStatus::AcqPackageKitStatus(AptIntf *apt, PkBackend *backend, bool &cancelled) :
    m_apt(apt),
    m_backend(backend),
    _cancelled(cancelled),
    last_percent(PK_BACKEND_PERCENTAGE_INVALID),
    last_sub_percent(PK_BACKEND_PERCENTAGE_INVALID)
{
}

// AcqPackageKitStatus::Start - Downloading has started
// ---------------------------------------------------------------------
void AcqPackageKitStatus::Start()
{
    pkgAcquireStatus::Start();
    ID = 1;
}

// AcqPackageKitStatus::IMSHit - Called when an item got a HIT response	/*{{{*/
// ---------------------------------------------------------------------
void AcqPackageKitStatus::IMSHit(pkgAcquire::ItemDesc &Itm)
{
    if (packages.size() == 0) {
        pk_backend_repo_detail(m_backend,
                               "",
                               Itm.Description.c_str(),
                               false);
    }
    Update = true;
}

// AcqPackageKitStatus::Fetch - An item has started to download
// ---------------------------------------------------------------------
/* This prints out the short description and the expected size */
void AcqPackageKitStatus::Fetch(pkgAcquire::ItemDesc &Itm)
{
    Update = true;
    if (Itm.Owner->Complete == true)
        return;

    Itm.Owner->ID = ID++;
}

// AcqPackageKitStatus::Done - Completed a download
// ---------------------------------------------------------------------
/* We don't display anything... */
void AcqPackageKitStatus::Done(pkgAcquire::ItemDesc &Itm)
{
    Update = true;
}

// AcqPackageKitStatus::Fail - Called when an item fails to download
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
        if (packages.size() == 0) {
            pk_backend_repo_detail(m_backend,
                                   "",
                                   Itm.Description.c_str(),
                                   false);
        }
    } else {
        // an error was found (maybe 404, 403...)
        // the item that got the error and the error text
        _error->Error("Error %s\n  %s",
                      Itm.Description.c_str(),
                      Itm.Owner->ErrorText.c_str());
    }

    Update = true;
}


// AcqTextStatus::Stop - Finished downloading
// ---------------------------------------------------------------------
/* This prints out the bytes downloaded and the overall average line
   speed */
void AcqPackageKitStatus::Stop()
{
    pkgAcquireStatus::Stop();
    // the items that still on the set are finished
    for (set<string>::iterator it = currentPackages.begin();
         it != currentPackages.end();
         it++ )
    {
        emit_package(*it, true);
    }
}


// AcqPackageKitStatus::Pulse - Regular event pulse
// ---------------------------------------------------------------------
/* This draws the current progress. Each line has an overall percent
   meter and a per active item status meter along with an overall
   bandwidth and ETA indicator. */
bool AcqPackageKitStatus::Pulse(pkgAcquire *Owner)
{
    pkgAcquireStatus::Pulse(Owner);

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

    set<string> localCurrentPackages = currentPackages;;
    for (pkgAcquire::Worker *I = Owner->WorkersBegin(); I != 0;
         I = Owner->WorkerStep(I))
    {
        // Check if there is no item running or if we don't have
        // any packages set we are probably refreshing the cache
        if (I->CurrentItem == 0 || packages.size() == 0)
        {
            continue;
        }
        emit_package(I->CurrentItem->ShortDesc, false);
        localCurrentPackages.erase(I->CurrentItem->ShortDesc);

        // Add the total size and percent
        if (I->TotalSize > 0 && I->CurrentItem->Owner->Complete == false)
        {
            unsigned long sub_percent;
            // TODO PackageKit needs to emit package with progress.
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
        }
    }

    // the items that still on the set are finished
    for (set<string>::iterator it = localCurrentPackages.begin();
         it != localCurrentPackages.end();
         it++ )
    {
        emit_package(*it, true);
    }

    double localCPS = (CurrentCPS >= 0) ? CurrentCPS : -1 * CurrentCPS;
    if (localCPS != last_CPS)
    {
        last_CPS = localCPS;
        pk_backend_set_speed(m_backend, (int) last_CPS);
    }

    Update = false;

    return !_cancelled;;
}

// AcqPackageKitStatus::MediaChange - Media need to be swapped
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


void AcqPackageKitStatus::addPackagePair(PkgPair packagePair)
{
    packages.push_back(packagePair);
}

void AcqPackageKitStatus::emit_package(const string &name, bool finished)
{
    if (name.compare(last_package_name) != 0 && packages.size()) {
        // find the package
        for (PkgList::iterator it = packages.begin(); it != packages.end(); ++it) {
            if (_cancelled) {
                break;
            }

            // try to see if any package matches
            if (name.compare(it->first.Name()) == 0) {
                m_apt->emit_package(*it,
                                    PK_INFO_ENUM_UNKNOWN,
                                    finished ? PK_INFO_ENUM_FINISHED : PK_INFO_ENUM_DOWNLOADING);
                last_package_name = name;

                // Find the downloading item
                if (finished) {
                    currentPackages.erase(name);
                } else {
                    currentPackages.insert(name);
                }
                break;
            }
        }
    }
}
