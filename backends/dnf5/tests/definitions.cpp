/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "dnf5-test-job.hpp"

/* Define the daemon symbols used by the dnf5 backend utilities, otherwise we
 * can't link them. The progress reporting ones record what they are called
 * with, so the tests can check what a client would see.
 */

Dnf5TestJob dnf5_test_job;

void
Dnf5TestJob::reset()
{
	statuses.clear();
	percentages.clear();
	speeds.clear();
	download_size_remaining.clear();
	item_progress.clear();
	package_statuses.clear();
}

void
pk_backend_job_set_status(PkBackendJob *job, PkStatusEnum status)
{
	dnf5_test_job.statuses.push_back(status);
}

void
pk_backend_job_set_percentage(PkBackendJob *job, guint percentage)
{
	dnf5_test_job.percentages.push_back(percentage);
}

void
pk_backend_job_set_speed(PkBackendJob *job, guint speed)
{
	dnf5_test_job.speeds.push_back(speed);
}

void
pk_backend_job_set_download_size_remaining(PkBackendJob *job, guint64 download_size_remaining)
{
	dnf5_test_job.download_size_remaining.push_back(download_size_remaining);
}

void
pk_backend_job_set_item_progress(PkBackendJob *job, const gchar *package_id, PkStatusEnum status, guint percentage)
{
	dnf5_test_job.item_progress.push_back({package_id, status, percentage});
}

void
pk_backend_job_package_full(
	PkBackendJob *job,
	PkInfoEnum info,
	const gchar *package_id,
	const gchar *summary,
	PkInfoEnum update_severity)
{
	dnf5_test_job.package_statuses.push_back({package_id, info});
}

void
pk_backend_job_package(PkBackendJob *job, PkInfoEnum info, const gchar *package_id, const gchar *summary)
{
	pk_backend_job_package_full(job, info, package_id, summary, PK_INFO_ENUM_UNKNOWN);
}

void
pk_backend_job_packages(PkBackendJob *job, GPtrArray *packages)
{
}

gpointer
pk_backend_get_user_data(PkBackend *backend)
{
	return NULL;
}

gchar *
pk_util_get_root_dir(GKeyFile *conf)
{
	return g_strdup("/");
}
