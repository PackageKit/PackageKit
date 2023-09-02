/*
 * Copyright (C) Serenity Cybersecurity, LLC <license@futurecrew.ru>
 *               Author: Gleb Popov <arrowd@FreeBSD.org>
 *
 * Licensed under the GNU General Public License Version 2
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma once

class PKJobCanceller {
public:
    PKJobCanceller(PkBackendJob* _job)
    : job(_job) {
        g_assert (pk_backend_job_get_user_data (_job) == NULL
            && "JobCanceller is used with a job that already has user_data attached!");

        jobData = g_new0 (PkBackendFreeBSDJobData, 1);
        jobData->cancellable = g_cancellable_new ();
        jobData->canceller = this;

        pk_backend_job_set_user_data (_job, jobData);
        allowCancel ();
    }

    bool cancelIfRequested() {
        // pk_backend_stop_job might destroy our private pointers before
        // this call. Use bool flag to check for that.
        if (aborting)
            return true;
        if (g_cancellable_is_cancelled (jobData->cancellable)) {
            pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_CANCELLED,
                "The task was stopped successfully");
            aborting = true;
            return true;
        }
        return false;
    }

    void allowCancel() const { pk_backend_job_set_allow_cancel (job, TRUE); }
    void disallowCancel() const { pk_backend_job_set_allow_cancel (job, FALSE); }

    void abort() { aborting = true; }

    // No need in destructor, job_data is destroyed in pk_backend_stop_job
private:
    PkBackendJob* job;
    PkBackendFreeBSDJobData *jobData;
    bool aborting = false;
};
