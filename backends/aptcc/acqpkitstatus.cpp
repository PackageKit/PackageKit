/* acqpkitstatus.cpp
 *
 * Copyright (c) 2009 Daniel Nicoletti <dantti@gmail.com>
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

#include "apt-intf.h"

#include <apt-pkg/acquire-item.h>
#include <apt-pkg/acquire-worker.h>

// AcqPackageKitStatus::AcqPackageKitStatus - Constructor
// ---------------------------------------------------------------------
AcqPackageKitStatus::AcqPackageKitStatus(AptIntf *apt, PkBackend *backend, bool &cancelled) :
    m_apt(apt),
    m_backend(backend),
    _cancelled(cancelled),
    m_lastPercent(PK_BACKEND_PERCENTAGE_INVALID)
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
    if (m_packages.size() == 0) {
        pk_backend_repo_detail(m_backend,
                               "",
                               Itm.Description.c_str(),
                               true);
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
        if (m_packages.size() == 0) {
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
// void AcqPackageKitStatus::Stop()
// {
//     pkgAcquireStatus::Stop();
//     // the items that still on the set are finished
//     for (set<string>::iterator it = m_currentPackages.begin();
//          it != currentPackages.end();
//          it++ )
//     {
//         emit_package(*it, true);
//     }
// }


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
    if (m_lastPercent != percent_done) {
        if (m_lastPercent < percent_done) {
            pk_backend_set_percentage(m_backend, percent_done);
        } else {
            pk_backend_set_percentage(m_backend, PK_BACKEND_PERCENTAGE_INVALID);
            pk_backend_set_percentage(m_backend, percent_done);
        }
        m_lastPercent = percent_done;
    }

    for (pkgAcquire::Worker *I = Owner->WorkersBegin(); I != 0;
         I = Owner->WorkerStep(I))
    {
        // Check if there is no item running or if we don't have
        // any packages set we are probably refreshing the cache
        if (I->CurrentItem == 0 || m_packages.size() == 0) {
            continue;
        }

        // Try to find the package in question
        pkgCache::VerIterator ver;
        ver = findPackage(I->CurrentItem->ShortDesc);
        if (ver.end() == true) {
            continue;
        }

        if (I->CurrentItem->Owner->Complete == true) {
            // emit the package as finished
            m_apt->emitPackage(ver, PK_INFO_ENUM_FINISHED);
        } else {
            // emit the package
            m_apt->emitPackage(ver, PK_INFO_ENUM_DOWNLOADING);

            // Add the total size and percent
            if (I->TotalSize > 0) {
                unsigned long sub_percent;
                sub_percent = long(double(I->CurrentSize*100.0)/double(I->TotalSize));

                // Emit the individual progress
                m_apt->emitPackageProgress(ver, static_cast<uint>(sub_percent));
            }
        }
    }

    // calculate the overall speed
    double localCPS = (CurrentCPS >= 0) ? CurrentCPS : -1 * CurrentCPS;
    if (localCPS != m_lastCPS)
    {
        m_lastCPS = localCPS;
        pk_backend_set_speed(m_backend, static_cast<uint>(m_lastCPS));
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


void AcqPackageKitStatus::addPackage(const pkgCache::VerIterator &ver)
{
    m_packages.push_back(ver);
}

pkgCache::VerIterator AcqPackageKitStatus::findPackage(const std::string &name) const
{
    pkgCache::VerIterator ver;
    for (PkgList::const_iterator i = m_packages.begin(); i != m_packages.end(); ++i) {
        // try to see if any package matches
        if (name.compare(i->ParentPkg().Name()) == 0) {
            ver = *i;
            break;
        }
    }
    return ver;
}
