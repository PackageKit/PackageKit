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

#include "dnf5-backend-utils.hpp"
#include <packagekit-glib2/pk-common-private.h>
#include <packagekit-glib2/pk-update-detail.h>
#include <libdnf5/conf/config_parser.hpp>
#include <libdnf5/conf/const.hpp>
#include <libdnf5/logger/logger.hpp>
#include <libdnf5/rpm/arch.hpp>
#include <libdnf5/repo/package_downloader.hpp>
#include <libdnf5/base/goal.hpp>
#include <libdnf5/advisory/advisory_query.hpp>
#include <libdnf5/rpm/reldep_list.hpp>
#include <libdnf5/base/transaction.hpp>
#include <rpm/rpmlib.h>
#include <glib/gstdio.h>
#include <algorithm>
#include <set>
#include <queue>
#include <filesystem>
#include <map>
#include "dnf5-backend-vendor.hpp"

void
dnf5_setup_base (PkBackendDnf5Private *priv, gboolean refresh, gboolean force, const char *releasever)
{
	priv->base = std::make_unique<libdnf5::Base>();

	priv->base->load_config();

	auto &config = priv->base->get_config();
	if (priv->conf != NULL) {
		g_autofree gchar *destdir = g_key_file_get_string (priv->conf, "Daemon", "DestDir", NULL);
		if (destdir != NULL) {
			config.get_installroot_option().set(libdnf5::Option::Priority::COMMANDLINE, destdir);
		}

		gboolean keep_cache = g_key_file_get_boolean (priv->conf, "Daemon", "KeepCache", NULL);
		config.get_keepcache_option().set(libdnf5::Option::Priority::COMMANDLINE, keep_cache != FALSE);

		g_autofree gchar *distro_version = NULL;
		if (releasever == NULL) {
			g_autoptr(GError) error = NULL;
			distro_version = pk_get_distro_version_id (&error);
		} else {
			distro_version = g_strdup(releasever);
		}

		if (distro_version != NULL) {
			priv->base->get_vars()->set("releasever", distro_version);
			const char *root = (destdir != NULL) ? destdir : "/";
			g_autofree gchar *cache_dir = g_build_filename (root, "/var/cache/PackageKit", distro_version, "metadata", NULL);
			g_debug("Using cachedir: %s", cache_dir);
			config.get_cachedir_option().set(libdnf5::Option::Priority::COMMANDLINE, cache_dir);
		}

		auto &optional_metadata_types = config.get_optional_metadata_types_option();
		auto &optional_metadata_types_setting = optional_metadata_types.get_value();
		if (!optional_metadata_types_setting.contains(libdnf5::METADATA_TYPE_ALL)) {
			// Ensure all required repodata types are downloaded
			if(!optional_metadata_types_setting.contains(libdnf5::METADATA_TYPE_COMPS)) {
				optional_metadata_types.add_item(libdnf5::Option::Priority::RUNTIME, libdnf5::METADATA_TYPE_COMPS);
			}
			if(!optional_metadata_types_setting.contains(libdnf5::METADATA_TYPE_UPDATEINFO)) {
				optional_metadata_types.add_item(libdnf5::Option::Priority::RUNTIME, libdnf5::METADATA_TYPE_UPDATEINFO);
			}
			if(!optional_metadata_types_setting.contains(libdnf5::METADATA_TYPE_APPSTREAM)) {
				optional_metadata_types.add_item(libdnf5::Option::Priority::RUNTIME, libdnf5::METADATA_TYPE_APPSTREAM);
			}
		}
		
		// Always assume yes to avoid interactive prompts failing the transaction
		// TODO: Drop this once InstallSignature is implemented
		config.get_assumeyes_option().set(libdnf5::Option::Priority::COMMANDLINE, true);
	}

	priv->base->setup();
	
	// Ensure releasever is set AFTER setup() because setup() might run auto-detection and overwrite it.
	if (priv->conf != NULL) {
		g_autofree gchar *distro_version = NULL;
		if (releasever == NULL) {
			g_autoptr(GError) error = NULL;
			distro_version = pk_get_distro_version_id (&error);
		} else {
			distro_version = g_strdup(releasever);
		}
		if (distro_version != NULL) {
			priv->base->get_vars()->set("releasever", distro_version);
		}
	}
	
	auto repo_sack = priv->base->get_repo_sack();
	repo_sack->create_repos_from_system_configuration();
	repo_sack->get_system_repo();

	if (refresh && force) {
		libdnf5::repo::RepoQuery query(*priv->base);
		for (auto repo : query) {
			if (repo->is_enabled()) {
				g_debug("Expiring repository metadata: %s", repo->get_id().c_str());
				repo->expire();
			}
		}
	}

	g_debug("Loading repositories");
	repo_sack->load_repos();

	libdnf5::repo::RepoQuery query(*priv->base);
	query.filter_enabled(true);
	for (auto repo : query) {
		g_debug("Enabled repository: %s", repo->get_id().c_str());
	}
}

void
dnf5_refresh_cache(PkBackendDnf5Private *priv, gboolean force)
{
	dnf5_setup_base(priv, TRUE, force);
}

PkInfoEnum
dnf5_advisory_kind_to_info_enum (const std::string &type)
{
	if (type == "security")
		return PK_INFO_ENUM_SECURITY;
	if (type == "bugfix")
		return PK_INFO_ENUM_BUGFIX;
	if (type == "enhancement")
		return PK_INFO_ENUM_ENHANCEMENT;
	if (type == "newpackage")
		return PK_INFO_ENUM_NORMAL;
	return PK_INFO_ENUM_UNKNOWN;
}

PkInfoEnum
dnf5_update_severity_to_enum (const std::string &severity)
{
	if (severity == "low")
		return PK_INFO_ENUM_LOW;
	if (severity == "moderate")
		return PK_INFO_ENUM_NORMAL;
	if (severity == "important")
		return PK_INFO_ENUM_IMPORTANT;
	if (severity == "critical")
		return PK_INFO_ENUM_CRITICAL;
	return PK_INFO_ENUM_UNKNOWN;
}

bool
dnf5_force_distupgrade_on_upgrade (libdnf5::Base &base)
{
	std::vector<std::string> distroverpkg_names = { "system-release", "distribution-release" };
	std::vector<std::string> distupgrade_provides = { "system-upgrade(dsync)", "product-upgrade() = dup" };

	libdnf5::rpm::PackageQuery query(base);
	query.filter_installed();
	query.filter_name(distroverpkg_names);
	query.filter_provides(distupgrade_provides);

	return !query.empty();
}

bool
dnf5_repo_is_devel (const libdnf5::repo::Repo &repo)
{
	std::string id = repo.get_id();
	return (id.ends_with("-debuginfo") || id.ends_with("-debugsource") || id.ends_with("-devel"));
}

bool
dnf5_repo_is_source (const libdnf5::repo::Repo &repo)
{
	std::string id = repo.get_id();
	return id.ends_with("-source");
}

bool
dnf5_repo_is_supported (const libdnf5::repo::Repo &repo)
{
	return dnf5_validate_supported_repo(repo.get_id());
}

bool
dnf5_backend_pk_repo_filter (const libdnf5::repo::Repo &repo, PkBitfield filters)
{
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT) && !dnf5_repo_is_devel (repo))
		return false;
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT) && dnf5_repo_is_devel (repo))
		return false;

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_SOURCE) && !dnf5_repo_is_source (repo))
		return false;
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_SOURCE) && dnf5_repo_is_source (repo))
		return false;

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) && !repo.is_enabled())
		return false;
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED) && repo.is_enabled())
		return false;

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_SUPPORTED) && !dnf5_repo_is_supported (repo))
		return false;
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_SUPPORTED) && dnf5_repo_is_supported (repo))
		return false;

	return true;
}

bool
dnf5_package_is_gui (const libdnf5::rpm::Package &pkg)
{
	for (const auto &provide : pkg.get_provides()) {
		std::string name = provide.get_name();
		if (name.starts_with("application("))
			return true;
	}
	return false;
}

bool
dnf5_package_filter (const libdnf5::rpm::Package &pkg, PkBitfield filters)
{
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_GUI) && !dnf5_package_is_gui (pkg))
		return false;
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_GUI) && dnf5_package_is_gui (pkg))
		return false;

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DOWNLOADED) && !pkg.is_available_locally())
		return false;
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DOWNLOADED) && pkg.is_available_locally())
		return false;

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT) ||
	    pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT) ||
	    pk_bitfield_contain (filters, PK_FILTER_ENUM_SOURCE) ||
	    pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_SOURCE) ||
	    pk_bitfield_contain (filters, PK_FILTER_ENUM_SUPPORTED) ||
	    pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_SUPPORTED)) {
		auto repo_weak = pkg.get_repo();
		if (repo_weak.is_valid()) {
			if (!dnf5_backend_pk_repo_filter(*repo_weak, filters))
				return false;
		}
	}

	return true;
}

std::vector<libdnf5::rpm::Package>
dnf5_process_dependency (libdnf5::Base &base, const libdnf5::rpm::Package &pkg, PkRoleEnum role, gboolean recursive)
{
	std::vector<libdnf5::rpm::Package> results;
	std::set<std::string> visited;
	std::queue<libdnf5::rpm::Package> queue;
	queue.push(pkg);
	visited.insert(pkg.get_name() + ";" + pkg.get_evr() + ";" + pkg.get_arch());
	
	while (!queue.empty()) {
		auto curr = queue.front();
		queue.pop();
		libdnf5::rpm::ReldepList reldeps(base);
		if (role == PK_ROLE_ENUM_DEPENDS_ON) reldeps = curr.get_requires();
		else reldeps = curr.get_provides();
		
		for (const auto &reldep : reldeps) {
			std::string req = reldep.to_string();
			libdnf5::rpm::PackageQuery query(base);
			if (role == PK_ROLE_ENUM_DEPENDS_ON) query.filter_provides(req);
			else query.filter_requires(req);
			
			// Filter for latest version and supported architectures to avoid duplicates
			// for available packages
			query.filter_latest_evr();
			query.filter_arch(libdnf5::rpm::get_supported_arches());
			
			for (const auto &res : query) {
				std::string res_nevra = res.get_name() + ";" + res.get_evr() + ";" + res.get_arch();
				if (visited.find(res_nevra) == visited.end()) {
					visited.insert(res_nevra);
					results.push_back(res);
					if (recursive) queue.push(res);
				}
			}
		}
	}
	return results;
}

void
dnf5_emit_pkg (PkBackendJob *job, const libdnf5::rpm::Package &pkg, PkInfoEnum info, PkInfoEnum severity)
{
	if (info == PK_INFO_ENUM_UNKNOWN) {
		info = PK_INFO_ENUM_AVAILABLE;
		if (pkg.get_install_time() > 0) {
			info = PK_INFO_ENUM_INSTALLED;
		}
	}
	
	std::string evr = pkg.get_evr();
	std::string repo_id = pkg.get_repo_id();
	if (pkg.get_install_time() > 0) {
		repo_id = "installed";
	}
	
	std::string package_id = pkg.get_name() + ";" + evr + ";" + pkg.get_arch() + ";" + repo_id;
	if (severity != PK_INFO_ENUM_UNKNOWN) {
		pk_backend_job_package_full (job, info, package_id.c_str(), pkg.get_summary().c_str(), severity);
	} else {
		pk_backend_job_package (job, info, package_id.c_str(), pkg.get_summary().c_str());
	}
}

void
dnf5_sort_and_emit (PkBackendJob *job, std::vector<libdnf5::rpm::Package> &pkgs)
{
	std::sort(pkgs.begin(), pkgs.end(), [](const libdnf5::rpm::Package &a, const libdnf5::rpm::Package &b) {
		bool a_installed = (a.get_install_time() > 0);
		bool b_installed = (b.get_install_time() > 0);
		if (a_installed != b_installed) return a_installed; 
		if (a.get_name() != b.get_name()) return a.get_name() < b.get_name();
		if (a.get_arch() != b.get_arch()) return a.get_arch() < b.get_arch();
		return a.get_evr() < b.get_evr();
	});

	std::set<std::string> seen_nevras;
	for (auto &pkg : pkgs) {
		std::string nevra = pkg.get_name() + ";" + pkg.get_evr() + ";" + pkg.get_arch();
		if (seen_nevras.find(nevra) == seen_nevras.end()) {
			dnf5_emit_pkg(job, pkg);
			seen_nevras.insert(nevra);
		}
	}
}

void
dnf5_apply_filters (libdnf5::Base &base, libdnf5::rpm::PackageQuery &query, PkBitfield filters)
{
	gboolean installed = pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED);
	gboolean available = pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED);

	if (installed && !available) {
		query.filter_installed();
	} else if (!installed && available) {
		query.filter_available();
	}

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_ARCH)) {
		auto vars = base.get_vars();
		if (vars.is_valid()) {
			std::string arch = vars->get_value("arch");
			if (!arch.empty()) {
				query.filter_arch({arch, "noarch"});
			} else {
				query.filter_arch(libdnf5::rpm::get_supported_arches());
			}
		}
	}

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NEWEST)) {
		query.filter_latest_evr();
	}
}

std::vector<libdnf5::rpm::Package>
dnf5_resolve_package_ids(libdnf5::Base &base, gchar **package_ids)
{
	std::vector<libdnf5::rpm::Package> pkgs;
	if (!package_ids) return pkgs;
	
	for (int i = 0; package_ids[i] != NULL; i++) {
		// Check if this is a simple package name (no semicolons) or a full package ID
		if (strchr(package_ids[i], ';') == NULL) {
			// Simple package name - search by name and get latest available
			try {
				g_debug("Resolving simple package name: %s", package_ids[i]);
				libdnf5::rpm::PackageQuery query(base);
				query.filter_name(std::string(package_ids[i]), libdnf5::sack::QueryCmp::EQ);
				query.filter_available();
				query.filter_latest_evr();
				query.filter_arch(libdnf5::rpm::get_supported_arches());

				
				if (!query.empty()) {
					for (auto pkg : query) {
						g_debug("Found package: name=%s, evr=%s, arch=%s, repo=%s",
							pkg.get_name().c_str(), pkg.get_evr().c_str(), 
							pkg.get_arch().c_str(), pkg.get_repo_id().c_str());
						pkgs.push_back(pkg);
						break; // Take the first match
					}
				} else {
					g_debug("No available package found for name: %s", package_ids[i]);
				}
			} catch (const std::exception &e) {
				g_debug("Exception resolving package name %s: %s", package_ids[i], e.what());
			}
			continue;
		}
		
		// Full package ID - use existing logic
		g_auto(GStrv) split = pk_package_id_split(package_ids[i]);
		if (!split) continue;
		
		try {
			libdnf5::rpm::PackageQuery query(base);
			g_debug("Resolving package ID: name=%s, version=%s, arch=%s, repo=%s",
				split[PK_PACKAGE_ID_NAME], split[PK_PACKAGE_ID_VERSION],
				split[PK_PACKAGE_ID_ARCH], split[PK_PACKAGE_ID_DATA]);
			query.filter_name(split[PK_PACKAGE_ID_NAME]);
			query.filter_evr(split[PK_PACKAGE_ID_VERSION]);
			query.filter_arch(split[PK_PACKAGE_ID_ARCH]);
			
			if (g_strcmp0(split[PK_PACKAGE_ID_DATA], "installed") == 0) {
				query.filter_installed();
			} else {
				 query.filter_repo_id(split[PK_PACKAGE_ID_DATA]);
			}
			
			if (query.empty()) {
				g_debug("No exact match for ID: %s. Listing similar packages...", package_ids[i]);
				libdnf5::rpm::PackageQuery fallback(base);
				fallback.filter_name(split[PK_PACKAGE_ID_NAME]);
				for (const auto &p : fallback) {
					g_debug("Found similar package: name=%s, evr=%s, arch=%s, repo=%s",
						p.get_name().c_str(), p.get_evr().c_str(), p.get_arch().c_str(), p.get_repo_id().c_str());
				}
			}

			for (auto pkg : query) {
				pkgs.push_back(pkg);
				break;
			}
		} catch (const std::exception &e) {
			g_debug("Exception resolving package ID %s: %s", package_ids[i], e.what());
		}
	}
	return pkgs;
}


void
dnf5_remove_old_cache_directories (PkBackend *backend, const gchar *release_ver)
{
	PkBackendDnf5Private *priv = (PkBackendDnf5Private *) pk_backend_get_user_data (backend);
	g_assert (priv->conf != NULL);

	/* cache cleanup disabled? */
	if (g_key_file_get_boolean (priv->conf, "Daemon", "KeepCache", NULL)) {
		g_debug ("KeepCache config option set; skipping old cache directory cleanup");
		return;
	}

	/* only do cache cleanup for regular installs */
	g_autofree gchar *destdir = g_key_file_get_string (priv->conf, "Daemon", "DestDir", NULL);
	if (destdir != NULL) {
		g_debug ("DestDir config option set; skipping old cache directory cleanup");
		return;
	}

	std::filesystem::path cache_path("/var/cache/PackageKit");
	if (!std::filesystem::exists(cache_path) || !std::filesystem::is_directory(cache_path))
		return;

	/* look at each subdirectory */
	for (const auto &entry : std::filesystem::directory_iterator(cache_path)) {
		if (!entry.is_directory())
			continue;

		std::string filename = entry.path().filename().string();

		/* is the version older than the current release ver? */
		if (rpmvercmp (filename.c_str(), release_ver) < 0) {
			g_debug ("removing old cache directory %s", entry.path().c_str());
			std::error_code ec;
			std::filesystem::remove_all(entry.path(), ec);
			if (ec)
				g_warning ("failed to remove directory %s: %s", entry.path().c_str(), ec.message().c_str());
		}
	}
}

Dnf5DownloadCallbacks::Dnf5DownloadCallbacks(PkBackendJob *job, uint64_t total_size)
    : job(job), total_size(total_size), finished_size(0), next_id(1) {}

void *
Dnf5DownloadCallbacks::add_new_download(void *user_data, const char *description, double total_to_download)
{
	std::lock_guard<std::mutex> lock(mutex);
	void *id = reinterpret_cast<void*>(next_id++);
	item_progress[id] = 0;
	return id;
}

int
Dnf5DownloadCallbacks::progress(void *user_cb_data, double total_to_download, double downloaded)
{
	std::lock_guard<std::mutex> lock(mutex);
	item_progress[user_cb_data] = downloaded;
	
	if (total_size > 0) {
		double current_total = finished_size;
		for (auto const& [id, prog] : item_progress) {
			current_total += prog;
		}
		pk_backend_job_set_percentage(job, (uint)(current_total * 100 / total_size));
	}
	return 0;
}

int
Dnf5DownloadCallbacks::end(void *user_cb_data, TransferStatus status, const char *msg)
{
	std::lock_guard<std::mutex> lock(mutex);
	finished_size += item_progress[user_cb_data];
	item_progress.erase(user_cb_data);
	return 0;
}

Dnf5TransactionCallbacks::Dnf5TransactionCallbacks(PkBackendJob *job)
    : job(job), total_items(0), current_item_index(0) {}

void
Dnf5TransactionCallbacks::before_begin(uint64_t total)
{
	total_items = total;
}

void
Dnf5TransactionCallbacks::elem_progress(const libdnf5::base::TransactionPackage &item, uint64_t amount, uint64_t total)
{
	current_item_index = amount;
}

void
Dnf5TransactionCallbacks::install_progress(const libdnf5::base::TransactionPackage &item, uint64_t amount, uint64_t total)
{
	if (total_items > 0 && total > 0) {
		double item_frac = (double)amount / total;
		pk_backend_job_set_percentage(job, (uint)((current_item_index + item_frac) * 100 / total_items));
	}
}

void
Dnf5TransactionCallbacks::install_start(const libdnf5::base::TransactionPackage &item, uint64_t total)
{
	auto action = item.get_action();
	PkInfoEnum info = PK_INFO_ENUM_INSTALLING;
	if (action == libdnf5::transaction::TransactionItemAction::UPGRADE ||
	    action == libdnf5::transaction::TransactionItemAction::DOWNGRADE) {
		info = PK_INFO_ENUM_UPDATING;
	}
	dnf5_emit_pkg(job, item.get_package(), info);
}

void
Dnf5TransactionCallbacks::uninstall_progress(const libdnf5::base::TransactionPackage &item, uint64_t amount, uint64_t total)
{
	if (total_items > 0 && total > 0) {
		double item_frac = (double)amount / total;
		pk_backend_job_set_percentage(job, (uint)((current_item_index + item_frac) * 100 / total_items));
	}
}

void
Dnf5TransactionCallbacks::uninstall_start(const libdnf5::base::TransactionPackage &item, uint64_t total)
{
	auto action = item.get_action();
	PkInfoEnum info = PK_INFO_ENUM_REMOVING;
	if (action == libdnf5::transaction::TransactionItemAction::REPLACED) {
		info = PK_INFO_ENUM_CLEANUP;
	}
	dnf5_emit_pkg(job, item.get_package(), info);
}
