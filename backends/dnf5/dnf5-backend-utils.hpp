/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2025 Neal Gompa <neal@gompa.dev>
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

#pragma once

#include <pk-backend.h>
#include <libdnf5/base/base.hpp>
#include <libdnf5/rpm/package_query.hpp>
#include <libdnf5/repo/repo_query.hpp>
#include <libdnf5/repo/download_callbacks.hpp>
#include <libdnf5/rpm/transaction_callbacks.hpp>
#include <glib.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// libdnf5's pseudo-repository holding packages that were added from local files.
// Packages in it carry no repository trust whatsoever.
inline constexpr const char *DNF5_CMDLINE_REPO_ID = "@commandline";

// Private data structures
typedef struct {
	std::unique_ptr<libdnf5::Base> base;
	GKeyFile *conf;
	GMutex mutex;
	gint64 last_notification_timestamp;
} PkBackendDnf5Private;

void dnf5_setup_base(
	PkBackendDnf5Private *priv,
	gboolean refresh = FALSE,
	gboolean force = FALSE,
	const char *releasever = nullptr,
	gboolean online = TRUE,
	PkBackendJob *job = nullptr);
void dnf5_update_network_state(PkBackendDnf5Private *priv, gboolean online);
void dnf5_refresh_cache(PkBackendDnf5Private *priv, PkBackendJob *job, gboolean force);
PkInfoEnum dnf5_advisory_kind_to_info_enum(const std::string &type);
PkInfoEnum dnf5_update_severity_to_enum(const std::string &severity);
bool dnf5_force_distupgrade_on_upgrade(libdnf5::Base &base);
bool dnf5_repo_is_devel(const libdnf5::repo::Repo &repo);
bool dnf5_repo_is_source(const libdnf5::repo::Repo &repo);
bool dnf5_repo_is_supported(const libdnf5::repo::Repo &repo);
bool dnf5_backend_pk_repo_filter(const libdnf5::repo::Repo &repo, PkBitfield filters);
bool dnf5_package_is_gui(const libdnf5::rpm::Package &pkg);
bool dnf5_package_filter(const libdnf5::rpm::Package &pkg, PkBitfield filters);
std::vector<libdnf5::rpm::Package>
dnf5_process_dependency(libdnf5::Base &base, const libdnf5::rpm::Package &pkg, PkRoleEnum role, gboolean recursive);
std::string dnf5_build_package_id(const libdnf5::rpm::Package &pkg);

// Report a single package progress event (downloading, installing, ...) as it
// happens during a transaction. Query results must be staged with
// dnf5_stage_pkg() and reported in one go with pk_backend_job_packages().
void dnf5_emit_pkg(PkBackendJob *job, const libdnf5::rpm::Package &pkg, PkInfoEnum info);
// Append a PkPackage for @pkg to @packages, an array created with
// g_ptr_array_new_with_free_func (g_object_unref). Packages with an invalid
// package-id are skipped with a warning.
void dnf5_stage_pkg(
	GPtrArray *packages,
	const libdnf5::rpm::Package &pkg,
	PkInfoEnum info = PK_INFO_ENUM_UNKNOWN,
	PkInfoEnum severity = PK_INFO_ENUM_UNKNOWN);
void dnf5_sort_and_emit(PkBackendJob *job, std::vector<libdnf5::rpm::Package> &pkgs);
void dnf5_apply_filters(libdnf5::Base &base, libdnf5::rpm::PackageQuery &query, PkBitfield filters);
std::vector<libdnf5::rpm::Package>
dnf5_resolve_package_ids(libdnf5::Base &base, gchar **package_ids, bool allow_cmdline_packages = true);
void dnf5_remove_old_cache_directories(PkBackend *backend, const gchar *release_ver);

// Reports download progress to a job. Packages registered with add_package()
// are reported by package-id with item progress and the remaining download
// size. Without any registered package, every download is treated as
// repository metadata and only the overall percentage and speed are reported.
class Dnf5DownloadCallbacks : public libdnf5::repo::DownloadCallbacks
{
    public:
	explicit Dnf5DownloadCallbacks(PkBackendJob *job);
	void add_package(const libdnf5::rpm::Package &pkg);
	// Register a download libdnf5 announces as @description, which is the
	// full NEVRA for packages.
	void add_package(const std::string &description, const std::string &package_id, uint64_t download_size);
	void *add_new_download(void *user_data, const char *description, double total_to_download) override;
	int progress(void *user_cb_data, double total_to_download, double downloaded) override;
	int end(void *user_cb_data, TransferStatus status, const char *msg) override;

    private:
	struct Item {
		std::string package_id;
		double total_size;
		double downloaded;
		guint percentage;
		guint ends;
		// whether the last end was a failure, which does not complete anything
		bool failed;
		bool reopened;
		bool announced;
	};

	double item_fraction(const Item &item) const;
	void announce(Item &item);
	void update_item_progress(Item &item);
	void update_progress();

	PkBackendJob *job;
	std::map<std::string, std::string> package_ids;
	uint64_t total_size;
	std::map<void *, Item> items;
	double downloaded_size;
	// only what came over the network, unlike packages that already exist
	double transferred_size;
	gint percentage;
	gint64 speed_timestamp;
	double speed_transferred_size;
	std::mutex mutex;
	uint64_t next_id;
};

// Installs download callbacks on a base for as long as it is in scope. The base
// outlives the job the callbacks report to, so they must never stay behind.
class Dnf5ScopedDownloadCallbacks
{
    public:
	Dnf5ScopedDownloadCallbacks(libdnf5::Base &base, std::unique_ptr<Dnf5DownloadCallbacks> callbacks);
	~Dnf5ScopedDownloadCallbacks();
	Dnf5ScopedDownloadCallbacks(const Dnf5ScopedDownloadCallbacks &) = delete;
	Dnf5ScopedDownloadCallbacks &operator=(const Dnf5ScopedDownloadCallbacks &) = delete;

    private:
	libdnf5::Base &base;
};

class Dnf5TransactionCallbacks : public libdnf5::rpm::TransactionCallbacks
{
    public:
	explicit Dnf5TransactionCallbacks(PkBackendJob *job);
	void before_begin(uint64_t total) override;
	void elem_progress(const libdnf5::base::TransactionPackage &item, uint64_t amount, uint64_t total) override;
	void install_progress(const libdnf5::base::TransactionPackage &item, uint64_t amount, uint64_t total) override;
	void install_start(const libdnf5::base::TransactionPackage &item, uint64_t total) override;
	void
	uninstall_progress(const libdnf5::base::TransactionPackage &item, uint64_t amount, uint64_t total) override;
	void uninstall_start(const libdnf5::base::TransactionPackage &item, uint64_t total) override;

    private:
	PkBackendJob *job;
	uint64_t total_items;
	uint64_t current_item_index;
};
