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
#include "pkg_acqfile.h"

#include <apt-pkg/acquire-item.h>
#include <apt-pkg/acquire-worker.h>

// AcqPackageKitStatus::AcqPackageKitStatus - Constructor
// ---------------------------------------------------------------------
AcqPackageKitStatus::AcqPackageKitStatus(AptIntf *apt, PkBackendJob *job) :
    m_job(job),
    m_lastPercent(PK_BACKEND_PERCENTAGE_INVALID),
    m_lastCPS(0),
    m_apt(apt)
{
}

// AcqPackageKitStatus::Start - Downloading has started
// ---------------------------------------------------------------------
void AcqPackageKitStatus::Start()
{
    pk_backend_job_set_status(m_job, PK_STATUS_ENUM_DOWNLOAD);
    pkgAcquireStatus::Start();
}

// AcqPackageKitStatus::Stop - Downloading has stopped
// ---------------------------------------------------------------------
void AcqPackageKitStatus::Stop()
{
    pk_backend_job_set_status (m_job, PK_STATUS_ENUM_RUNNING);
    pkgAcquireStatus::Stop();
}

// AcqPackageKitStatus::IMSHit - Called when an item got a HIT response	/*{{{*/
// ---------------------------------------------------------------------
void AcqPackageKitStatus::IMSHit(pkgAcquire::ItemDesc &Itm)
{
    PkRoleEnum role = pk_backend_job_get_role(m_job);
    if (role == PK_ROLE_ENUM_REFRESH_CACHE) {
        pk_backend_job_repo_detail(m_job,
                                   "",
                                   Itm.Description.c_str(),
                                   true);
    } else {
        updateStatus(Itm, 100);
    }
}

// AcqPackageKitStatus::Fetch - An item has started to download
// ---------------------------------------------------------------------
/* This prints out the short description and the expected size */
void AcqPackageKitStatus::Fetch(pkgAcquire::ItemDesc &Itm)
{
    // Download queued
    updateStatus(Itm, 0);
}

// AcqPackageKitStatus::Done - Completed a download
// ---------------------------------------------------------------------
/* We don't display anything... */
void AcqPackageKitStatus::Done(pkgAcquire::ItemDesc &Itm)
{
    // Download completed
    updateStatus(Itm, 100);
}

// AcqPackageKitStatus::Fail - Called when an item fails to download
// ---------------------------------------------------------------------
/* We print out the error text  */
void AcqPackageKitStatus::Fail(pkgAcquire::ItemDesc &Itm)
{
    // TODO download failed
    updateStatus(Itm, 0);

    // Ignore certain kinds of transient failures (bad code)
    if (Itm.Owner->Status == pkgAcquire::Item::StatIdle) {
        return;
    }

    if (Itm.Owner->Status == pkgAcquire::Item::StatDone)
    {
        PkRoleEnum role = pk_backend_job_get_role(m_job);
        if (role == PK_ROLE_ENUM_REFRESH_CACHE) {
            pk_backend_job_repo_detail(m_job,
                                       "",
                                       Itm.Description.c_str(),
                                       false);
        }
    } else {
        // an error was found (maybe 404, 403...)
        // the item that got the error and the error text
        _error->Error("%s is not (yet) available (%s)",
                      Itm.Description.c_str(),
                      Itm.Owner->ErrorText.c_str());
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
    if (m_lastPercent != percent_done) {
        if (m_lastPercent < percent_done) {
            pk_backend_job_set_percentage(m_job, percent_done);
        } else {
            pk_backend_job_set_percentage(m_job, PK_BACKEND_PERCENTAGE_INVALID);
            pk_backend_job_set_percentage(m_job, percent_done);
        }
        m_lastPercent = percent_done;
    }

    // Emit the download remaining size
    pk_backend_job_set_download_size_remaining(m_job, TotalBytes - CurrentBytes);

    for (pkgAcquire::Worker *I = Owner->WorkersBegin(); I != 0;
         I = Owner->WorkerStep(I)) {
        if (I->CurrentItem == 0){
            continue;
        }

        if (I->TotalSize > 0) {
            updateStatus(*I->CurrentItem,
                         long(double(I->CurrentSize * 100.0) / double(I->TotalSize)));
        } else {
            updateStatus(*I->CurrentItem, 100);
        }
    }

    // calculate the overall speed
    double localCPS = (CurrentCPS >= 0) ? CurrentCPS : -1 * CurrentCPS;
    if (localCPS != m_lastCPS)
    {
        m_lastCPS = localCPS;
        pk_backend_job_set_speed(m_job, static_cast<uint>(m_lastCPS));
    }

    Update = false;

    return !m_apt->cancelled();
}

// AcqPackageKitStatus::MediaChange - Media need to be swapped
// ---------------------------------------------------------------------
/* Prompt for a media swap */
bool AcqPackageKitStatus::MediaChange(string Media, string Drive)
{
    pk_backend_job_media_change_required(m_job,
                                         PK_MEDIA_TYPE_ENUM_DISC,
                                         Media.c_str(),
                                         Media.c_str());

    pk_backend_job_error_code(m_job,
                              PK_ERROR_ENUM_MEDIA_CHANGE_REQUIRED,
                              "Media change: please insert the disc labeled '%s' in the drive '%s' and try again.",
                              Media.c_str(),
                              Drive.c_str());

    // Set this so we can fail the transaction
    Update = true;
    return false;
}

void AcqPackageKitStatus::updateStatus(pkgAcquire::ItemDesc & Itm, int status)
{
    PkRoleEnum role = pk_backend_job_get_role(m_job);
    if (role == PK_ROLE_ENUM_REFRESH_CACHE) {
        // Ignore package update when refreshing the cache
        return;
    }

    // The pkgAcquire::Item had a version hiden on it's subclass
    // pkgAcqArchive but it was protected our subclass exposes that
    pkgAcqArchiveSane *archive = static_cast<pkgAcqArchiveSane*>(Itm.Owner);
    const pkgCache::VerIterator ver = archive->version();
    if (ver.end() == true) {
        return;
    }

    if (status == 100) {
        m_apt->emitPackage(ver, PK_INFO_ENUM_FINISHED);
    } else {
        // emit the package
        m_apt->emitPackage(ver, PK_INFO_ENUM_DOWNLOADING);
        
        // Emit the individual progress
        m_apt->emitPackageProgress(ver, status);
    }
}
