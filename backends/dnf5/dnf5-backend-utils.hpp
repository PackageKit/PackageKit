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
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Private data structures
typedef struct {
	std::unique_ptr<libdnf5::Base> base;
	GKeyFile *conf;
	GMutex mutex;
} PkBackendDnf5Private;

void dnf5_setup_base(PkBackendDnf5Private *priv, gboolean refresh = FALSE, gboolean force = FALSE, const char *releasever = nullptr);
void dnf5_refresh_cache(PkBackendDnf5Private *priv, gboolean force);
PkInfoEnum dnf5_advisory_kind_to_info_enum(const std::string &type);
PkInfoEnum dnf5_update_severity_to_enum(const std::string &severity);
bool dnf5_force_distupgrade_on_upgrade(libdnf5::Base &base);
bool dnf5_repo_is_devel(const libdnf5::repo::Repo &repo);
bool dnf5_repo_is_source(const libdnf5::repo::Repo &repo);
bool dnf5_repo_is_supported(const libdnf5::repo::Repo &repo);
bool dnf5_backend_pk_repo_filter(const libdnf5::repo::Repo &repo, PkBitfield filters);
bool dnf5_package_is_gui(const libdnf5::rpm::Package &pkg);
bool dnf5_package_filter(const libdnf5::rpm::Package &pkg, PkBitfield filters);
std::vector<libdnf5::rpm::Package> dnf5_process_dependency(libdnf5::Base &base, const libdnf5::rpm::Package &pkg, PkRoleEnum role, gboolean recursive);
void dnf5_emit_pkg(PkBackendJob *job, const libdnf5::rpm::Package &pkg, PkInfoEnum info = PK_INFO_ENUM_UNKNOWN, PkInfoEnum severity = PK_INFO_ENUM_UNKNOWN);
void dnf5_sort_and_emit(PkBackendJob *job, std::vector<libdnf5::rpm::Package> &pkgs);
void dnf5_apply_filters(libdnf5::Base &base, libdnf5::rpm::PackageQuery &query, PkBitfield filters);
std::vector<libdnf5::rpm::Package> dnf5_resolve_package_ids(libdnf5::Base &base, gchar **package_ids);



void dnf5_remove_old_cache_directories(PkBackend *backend, const gchar *release_ver);

class Dnf5DownloadCallbacks : public libdnf5::repo::DownloadCallbacks {
public:
	explicit Dnf5DownloadCallbacks(PkBackendJob *job, uint64_t total_size = 0);
	void * add_new_download(void *user_data, const char *description, double total_to_download) override;
	int progress(void *user_cb_data, double total_to_download, double downloaded) override;
	int end(void *user_cb_data, TransferStatus status, const char *msg) override;
private:
	PkBackendJob *job;
	uint64_t total_size;
	double finished_size;
	std::map<void*, double> item_progress;
	std::mutex mutex;
	uint64_t next_id;
};

class Dnf5TransactionCallbacks : public libdnf5::rpm::TransactionCallbacks {
public:
	explicit Dnf5TransactionCallbacks(PkBackendJob *job);
	void before_begin(uint64_t total) override;
	void elem_progress(const libdnf5::base::TransactionPackage &item, uint64_t amount, uint64_t total) override;
	void install_progress(const libdnf5::base::TransactionPackage &item, uint64_t amount, uint64_t total) override;
	void install_start(const libdnf5::base::TransactionPackage &item, uint64_t total) override;
	void uninstall_progress(const libdnf5::base::TransactionPackage &item, uint64_t amount, uint64_t total) override;
	void uninstall_start(const libdnf5::base::TransactionPackage &item, uint64_t total) override;
private:
	PkBackendJob *job;
	uint64_t total_items;
	uint64_t current_item_index;
};
