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

#include "dnf5-backend-utils.hpp"
#include "dnf5-test-job.hpp"
#include <stdexcept>
#include <vector>

using TransferStatus = libdnf5::repo::DownloadCallbacks::TransferStatus;

static const char *FOO_NEVRA = "foo-0:1.0-1.x86_64";
static const char *FOO_ID = "foo;1.0-1;x86_64;fedora";
static const char *BAR_NEVRA = "bar-0:2.0-1.noarch";
static const char *BAR_ID = "bar;2.0-1;noarch;updates";

// the job is never dereferenced, all calls end up in the recording stubs
static PkBackendJob *const test_job = reinterpret_cast<PkBackendJob *>(0x1);

static void
dnf5_test_assert_percentages(const std::vector<guint> &expected)
{
	g_assert_cmpuint(dnf5_test_job.percentages.size(), ==, expected.size());
	for (size_t i = 0; i < expected.size(); i++)
		g_assert_cmpuint(dnf5_test_job.percentages[i], ==, expected[i]);
}

static void
dnf5_test_assert_item_progress(size_t index, const char *package_id, guint percentage)
{
	g_assert_cmpuint(dnf5_test_job.item_progress.size(), >, index);
	const auto &item = dnf5_test_job.item_progress[index];
	g_assert_cmpstr(item.package_id.c_str(), ==, package_id);
	g_assert_cmpint(item.status, ==, PK_STATUS_ENUM_DOWNLOAD);
	g_assert_cmpuint(item.percentage, ==, percentage);
}

static void
dnf5_test_download_packages(void)
{
	dnf5_test_job.reset();
	Dnf5DownloadCallbacks cb(test_job);
	cb.add_package(FOO_NEVRA, FOO_ID, 100);
	cb.add_package(BAR_NEVRA, BAR_ID, 300);

	void *foo = cb.add_new_download(nullptr, FOO_NEVRA, 100);
	void *bar = cb.add_new_download(nullptr, BAR_NEVRA, 300);
	// already available locally, so it was not registered
	void *local = cb.add_new_download(nullptr, "local-0:1-1.x86_64", 50);

	// nothing is reported before the downloads actually start
	g_assert_true(dnf5_test_job.package_statuses.empty());
	g_assert_true(dnf5_test_job.percentages.empty());

	g_assert_cmpint(cb.progress(foo, 100, 50), ==, 0);
	g_assert_cmpuint(dnf5_test_job.package_statuses.size(), ==, 1);
	g_assert_cmpstr(dnf5_test_job.package_statuses[0].package_id.c_str(), ==, FOO_ID);
	g_assert_cmpint(dnf5_test_job.package_statuses[0].info, ==, PK_INFO_ENUM_DOWNLOADING);
	dnf5_test_assert_item_progress(0, FOO_ID, 50);
	dnf5_test_assert_percentages({12});
	g_assert_cmpuint(dnf5_test_job.download_size_remaining.back(), ==, 350);

	// an unregistered package counts neither towards the size nor the items
	cb.progress(local, 50, 50);
	g_assert_cmpuint(dnf5_test_job.package_statuses.size(), ==, 1);
	g_assert_cmpuint(dnf5_test_job.item_progress.size(), ==, 1);
	dnf5_test_assert_percentages({12});
	g_assert_cmpuint(dnf5_test_job.download_size_remaining.back(), ==, 350);

	g_assert_cmpint(cb.end(foo, TransferStatus::SUCCESSFUL, nullptr), ==, 0);
	dnf5_test_assert_item_progress(1, FOO_ID, 100);
	dnf5_test_assert_percentages({12, 25});
	g_assert_cmpuint(dnf5_test_job.download_size_remaining.back(), ==, 300);

	// a package that already exists reports no progress, only its end
	cb.end(bar, TransferStatus::ALREADYEXISTS, "Already downloaded");
	g_assert_cmpuint(dnf5_test_job.package_statuses.size(), ==, 2);
	g_assert_cmpstr(dnf5_test_job.package_statuses[1].package_id.c_str(), ==, BAR_ID);
	g_assert_cmpint(dnf5_test_job.package_statuses[1].info, ==, PK_INFO_ENUM_DOWNLOADING);
	dnf5_test_assert_item_progress(2, BAR_ID, 100);
	dnf5_test_assert_percentages({12, 25, 100});
	g_assert_cmpuint(dnf5_test_job.download_size_remaining.back(), ==, 0);

	// the speed drops to zero only once every download has ended
	g_assert_true(dnf5_test_job.speeds.empty());
	cb.end(local, TransferStatus::ALREADYEXISTS, nullptr);
	g_assert_cmpuint(dnf5_test_job.speeds.size(), ==, 1);
	g_assert_cmpuint(dnf5_test_job.speeds.back(), ==, 0);
}

static void
dnf5_test_download_percentage_monotonic(void)
{
	dnf5_test_job.reset();
	Dnf5DownloadCallbacks cb(test_job);
	cb.add_package(FOO_NEVRA, FOO_ID, 200);
	void *foo = cb.add_new_download(nullptr, FOO_NEVRA, 200);

	cb.progress(foo, 200, 100);
	// the download restarts on another mirror
	cb.progress(foo, 200, 20);
	dnf5_test_assert_percentages({50});
	g_assert_cmpuint(dnf5_test_job.download_size_remaining.back(), ==, 180);

	cb.progress(foo, 200, 150);
	dnf5_test_assert_percentages({50, 75});
}

static void
dnf5_test_download_failed(void)
{
	dnf5_test_job.reset();
	Dnf5DownloadCallbacks cb(test_job);
	cb.add_package(FOO_NEVRA, FOO_ID, 100);
	cb.add_package(BAR_NEVRA, BAR_ID, 100);
	void *foo = cb.add_new_download(nullptr, FOO_NEVRA, 100);
	cb.add_new_download(nullptr, BAR_NEVRA, 100);

	cb.progress(foo, 100, 40);
	cb.end(foo, TransferStatus::ERROR, "Curl error");

	// a failed download is not reported as downloaded
	g_assert_cmpuint(dnf5_test_job.download_size_remaining.back(), ==, 160);
	dnf5_test_assert_percentages({20});
	dnf5_test_assert_item_progress(dnf5_test_job.item_progress.size() - 1, FOO_ID, 40);
	// and the transfer that is still running keeps the speed
	g_assert_true(dnf5_test_job.speeds.empty());
}

static void
dnf5_test_download_unknown_data(void)
{
	dnf5_test_job.reset();
	Dnf5DownloadCallbacks cb(test_job);
	cb.add_package(FOO_NEVRA, FOO_ID, 100);

	void *unknown = reinterpret_cast<void *>(0x42);
	g_assert_cmpint(cb.progress(unknown, 100, 50), ==, 0);
	g_assert_cmpint(cb.end(unknown, TransferStatus::SUCCESSFUL, nullptr), ==, 0);
	g_assert_true(dnf5_test_job.percentages.empty());
	g_assert_true(dnf5_test_job.item_progress.empty());
	g_assert_true(dnf5_test_job.package_statuses.empty());
	g_assert_true(dnf5_test_job.speeds.empty());
}

static void
dnf5_test_download_speed(void)
{
	dnf5_test_job.reset();
	Dnf5DownloadCallbacks cb(test_job);
	cb.add_package(FOO_NEVRA, FOO_ID, 1000);
	void *foo = cb.add_new_download(nullptr, FOO_NEVRA, 1000);

	// the rate is only reported once at least a second has passed
	cb.progress(foo, 1000, 100);
	g_assert_true(dnf5_test_job.speeds.empty());

	g_usleep(G_USEC_PER_SEC + G_USEC_PER_SEC / 10);
	cb.progress(foo, 1000, 600);
	g_assert_cmpuint(dnf5_test_job.speeds.size(), ==, 1);
	g_assert_cmpuint(dnf5_test_job.speeds[0], >, 0);
	g_assert_cmpuint(dnf5_test_job.speeds[0], <=, 600);

	cb.end(foo, TransferStatus::SUCCESSFUL, nullptr);
	g_assert_cmpuint(dnf5_test_job.speeds.back(), ==, 0);
}

static void
dnf5_test_download_speed_starts_with_data(void)
{
	dnf5_test_job.reset();
	Dnf5DownloadCallbacks cb(test_job);
	cb.add_package(FOO_NEVRA, FOO_ID, 100000);
	void *foo = cb.add_new_download(nullptr, FOO_NEVRA, 100000);

	// connecting to the mirrors takes a while before any data arrives,
	// which must not be averaged into the rate
	g_usleep(G_USEC_PER_SEC + G_USEC_PER_SEC / 10);
	cb.progress(foo, 100000, 1000);
	g_assert_true(dnf5_test_job.speeds.empty());

	g_usleep(G_USEC_PER_SEC + G_USEC_PER_SEC / 10);
	cb.progress(foo, 100000, 45000);
	g_assert_cmpuint(dnf5_test_job.speeds.size(), ==, 1);
	// 44000 bytes in about 1.1 seconds
	g_assert_cmpuint(dnf5_test_job.speeds[0], >, 0);
	g_assert_cmpuint(dnf5_test_job.speeds[0], <=, 44000);
}

static void
dnf5_test_download_speed_ignores_existing(void)
{
	dnf5_test_job.reset();
	Dnf5DownloadCallbacks cb(test_job);
	cb.add_package(FOO_NEVRA, FOO_ID, 1000000);
	cb.add_package(BAR_NEVRA, BAR_ID, 100000);
	void *foo = cb.add_new_download(nullptr, FOO_NEVRA, 1000000);
	void *bar = cb.add_new_download(nullptr, BAR_NEVRA, 100000);

	// a package that already exists is copied, not transferred
	cb.end(foo, TransferStatus::ALREADYEXISTS, nullptr);
	g_usleep(G_USEC_PER_SEC + G_USEC_PER_SEC / 10);
	cb.progress(bar, 100000, 1000);
	g_assert_true(dnf5_test_job.speeds.empty());

	g_usleep(G_USEC_PER_SEC + G_USEC_PER_SEC / 10);
	cb.progress(bar, 100000, 45000);
	g_assert_cmpuint(dnf5_test_job.speeds.size(), ==, 1);
	g_assert_cmpuint(dnf5_test_job.speeds[0], >, 0);
	g_assert_cmpuint(dnf5_test_job.speeds[0], <=, 44000);
}

static void
dnf5_test_scoped_download_callbacks(void)
{
	libdnf5::Base base;
	g_assert_null(base.get_download_callbacks());

	{
		Dnf5ScopedDownloadCallbacks scoped(base, std::make_unique<Dnf5DownloadCallbacks>(test_job));
		g_assert_nonnull(base.get_download_callbacks());
	}
	g_assert_null(base.get_download_callbacks());

	// the job must not stay behind when downloading fails either
	try {
		Dnf5ScopedDownloadCallbacks scoped(base, std::make_unique<Dnf5DownloadCallbacks>(test_job));
		throw std::runtime_error("download failed");
	} catch (const std::runtime_error &) {
	}
	g_assert_null(base.get_download_callbacks());
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/dnf5/download/packages", dnf5_test_download_packages);
	g_test_add_func("/dnf5/download/percentage-monotonic", dnf5_test_download_percentage_monotonic);
	g_test_add_func("/dnf5/download/failed", dnf5_test_download_failed);
	g_test_add_func("/dnf5/download/unknown-data", dnf5_test_download_unknown_data);
	g_test_add_func("/dnf5/download/speed", dnf5_test_download_speed);
	g_test_add_func("/dnf5/download/speed-starts-with-data", dnf5_test_download_speed_starts_with_data);
	g_test_add_func("/dnf5/download/speed-ignores-existing", dnf5_test_download_speed_ignores_existing);
	g_test_add_func("/dnf5/download/scoped-callbacks", dnf5_test_scoped_download_callbacks);

	return g_test_run();
}
