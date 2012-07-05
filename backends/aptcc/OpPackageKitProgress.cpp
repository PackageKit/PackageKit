/* OpPackageKitProgress.cpp
 * 
 * Copyright (c) 2012 Daniel Nicoletti <dantti12@gmail.com>
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

#include "OpPackageKitProgress.h"

OpPackageKitProgress::OpPackageKitProgress(PkBackendJob *job) :
    m_job(job)
{
    // Set PackageKit status
    pk_backend_job_set_status(m_job, PK_STATUS_ENUM_LOADING_CACHE);
}

OpPackageKitProgress::~OpPackageKitProgress()
{
    Done();
}

void OpPackageKitProgress::Done()
{
    pk_backend_job_set_percentage(m_job, 100);
}

void OpPackageKitProgress::Update()
{
    if (CheckChange() == false) {
        // No change has happened skip
        return;
    }

    // Set the new percent
    pk_backend_job_set_percentage(m_job, static_cast<unsigned int>(Percent));
}
