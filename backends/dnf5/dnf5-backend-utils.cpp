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
#include <pk-common-private.h>
#include <pk-shared.h>
#include <pk-update-detail.h>
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
#include <stdexcept>
#include <cstring>
#include "dnf5-backend-vendor.hpp"

/**
 * A release version becomes a component of the metadata cache directory below
 * /var/cache/PackageKit, which this backend writes to as root. When it upgrades
 * the system, the release version is the distribution ID the client asked for,
 * so it must name a single directory entry and nothing else. The daemon already
 * rejects anything else, but the backend must not rely on being called through
 * it.
 */
static bool
dnf5_releasever_is_valid(const char *releasever)
{
	if (releasever == nullptr || releasever[0] == '\0')
		return false;

	// never an option, a relative path or a hidden directory
	if (!g_ascii_isalnum(releasever[0]))
		return false;

	for (const char *c = releasever; *c != '\0'; c++) {
		if (g_ascii_isalnum(*c))
			continue;
		if (*c == '.' || *c == '_' || *c == '+' || *c == '-')
			continue;
		return false;
	}

	// no path traversal, even though there is no directory separator left
	return strstr(releasever, "..") == nullptr;
}

void
dnf5_setup_base(
	PkBackendDnf5Private *priv,
	gboolean refresh,
	gboolean force,
	const char *releasever,
	gboolean online,
	PkBackendJob *job)
{
	if (releasever != nullptr && !dnf5_releasever_is_valid(releasever))
		throw std::invalid_argument(std::string("Invalid release version: ") + releasever);

	priv->base = std::make_unique<libdnf5::Base>();

	priv->base->load_config();

	auto &config = priv->base->get_config();
	if (priv->conf != NULL) {
		g_autofree gchar *root_dir = pk_util_get_root_dir(priv->conf);
		if (g_strcmp0(root_dir, "/") != 0) {
			config.get_installroot_option().set(libdnf5::Option::Priority::COMMANDLINE, root_dir);
		}

		gboolean keep_cache = g_key_file_get_boolean(priv->conf, "Daemon", "KeepCache", NULL);
		config.get_keepcache_option().set(libdnf5::Option::Priority::COMMANDLINE, keep_cache != FALSE);

		g_autofree gchar *distro_version = NULL;
		if (releasever == NULL) {
			g_autoptr(GError) error = NULL;
			distro_version = pk_get_distro_version_id(&error);
		} else {
			distro_version = g_strdup(releasever);
		}

		if (distro_version != NULL) {
			priv->base->get_vars()->set("releasever", distro_version);
			const char *root = (root_dir != NULL) ? root_dir : "/";
			g_autofree gchar *cache_dir =
				g_build_filename(root, "/var/cache/PackageKit", distro_version, "metadata", NULL);
			g_debug("Using cachedir: %s", cache_dir);
			config.get_cachedir_option().set(libdnf5::Option::Priority::COMMANDLINE, cache_dir);
		}

		auto &optional_metadata_types = config.get_optional_metadata_types_option();
		auto &optional_metadata_types_setting = optional_metadata_types.get_value();
		if (!optional_metadata_types_setting.contains(libdnf5::METADATA_TYPE_ALL)) {
			// Ensure all required repodata types are downloaded
			if (!optional_metadata_types_setting.contains(libdnf5::METADATA_TYPE_COMPS)) {
				optional_metadata_types.add_item(
					libdnf5::Option::Priority::RUNTIME,
					libdnf5::METADATA_TYPE_COMPS);
			}
			if (!optional_metadata_types_setting.contains(libdnf5::METADATA_TYPE_UPDATEINFO)) {
				optional_metadata_types.add_item(
					libdnf5::Option::Priority::RUNTIME,
					libdnf5::METADATA_TYPE_UPDATEINFO);
			}
			if (!optional_metadata_types_setting.contains(libdnf5::METADATA_TYPE_APPSTREAM)) {
				optional_metadata_types.add_item(
					libdnf5::Option::Priority::RUNTIME,
					libdnf5::METADATA_TYPE_APPSTREAM);
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
			distro_version = pk_get_distro_version_id(&error);
		} else {
			distro_version = g_strdup(releasever);
		}
		if (distro_version != NULL) {
			priv->base->get_vars()->set("releasever", distro_version);
		}
	}

	// online defaults to TRUE. Only change the configuration if the caller passed FALSE
	if (!online) {
		// When offline, use cache-only mode to avoid network errors
		g_debug("Using cache-only mode (not refreshing metadata)");
		config.get_cacheonly_option().set(libdnf5::Option::Priority::RUNTIME, "all");
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
	if (job != nullptr) {
		// report repository metadata downloads
		Dnf5ScopedDownloadCallbacks callbacks(*priv->base, std::make_unique<Dnf5DownloadCallbacks>(job));
		repo_sack->load_repos();
	} else {
		repo_sack->load_repos();
	}

	libdnf5::repo::RepoQuery query(*priv->base);
	query.filter_enabled(true);
	for (auto repo : query) {
		g_debug("Enabled repository: %s", repo->get_id().c_str());
	}
}

void
dnf5_update_network_state(PkBackendDnf5Private *priv, gboolean online)
{
	auto &config = priv->base->get_config();
	if (online) {
		g_debug("Clearing cache-only mode (online)");
		config.get_cacheonly_option().set(libdnf5::Option::Priority::RUNTIME, "none");
	} else {
		g_debug("Setting cache-only mode (offline)");
		config.get_cacheonly_option().set(libdnf5::Option::Priority::RUNTIME, "all");
	}
}

void
dnf5_refresh_cache(PkBackendDnf5Private *priv, PkBackendJob *job, gboolean force)
{
	dnf5_setup_base(priv, TRUE, force, nullptr, TRUE, job);
}

PkInfoEnum
dnf5_advisory_kind_to_info_enum(const std::string &type)
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
dnf5_update_severity_to_enum(const std::string &severity)
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
dnf5_force_distupgrade_on_upgrade(libdnf5::Base &base)
{
	std::vector<std::string> distroverpkg_names = {"system-release", "distribution-release"};
	std::vector<std::string> distupgrade_provides = {"system-upgrade(dsync)", "product-upgrade() = dup"};

	libdnf5::rpm::PackageQuery query(base);
	query.filter_installed();
	query.filter_provides(distroverpkg_names);
	query.filter_provides(distupgrade_provides);

	return !query.empty();
}

bool
dnf5_repo_is_devel(const libdnf5::repo::Repo &repo)
{
	std::string id = repo.get_id();
	return (id.ends_with("-debuginfo") || id.ends_with("-debugsource") || id.ends_with("-devel"));
}

bool
dnf5_repo_is_source(const libdnf5::repo::Repo &repo)
{
	std::string id = repo.get_id();
	return id.ends_with("-source");
}

bool
dnf5_repo_is_supported(const libdnf5::repo::Repo &repo)
{
	return dnf5_validate_supported_repo(repo.get_id());
}

bool
dnf5_backend_pk_repo_filter(const libdnf5::repo::Repo &repo, PkBitfield filters)
{
	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_DEVELOPMENT) && !dnf5_repo_is_devel(repo))
		return false;
	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_DEVELOPMENT) && dnf5_repo_is_devel(repo))
		return false;

	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_SOURCE) && !dnf5_repo_is_source(repo))
		return false;
	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_SOURCE) && dnf5_repo_is_source(repo))
		return false;

	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED) && !repo.is_enabled())
		return false;
	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED) && repo.is_enabled())
		return false;

	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_SUPPORTED) && !dnf5_repo_is_supported(repo))
		return false;
	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_SUPPORTED) && dnf5_repo_is_supported(repo))
		return false;

	return true;
}

bool
dnf5_package_is_gui(const libdnf5::rpm::Package &pkg)
{
	for (const auto &provide : pkg.get_provides()) {
		std::string name = provide.get_name();
		if (name.starts_with("application("))
			return true;
	}
	return false;
}

bool
dnf5_package_filter(const libdnf5::rpm::Package &pkg, PkBitfield filters)
{
	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_GUI) && !dnf5_package_is_gui(pkg))
		return false;
	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_GUI) && dnf5_package_is_gui(pkg))
		return false;

	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_DOWNLOADED) && !pkg.is_available_locally())
		return false;
	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_DOWNLOADED) && pkg.is_available_locally())
		return false;

	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_DEVELOPMENT)
	    || pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_DEVELOPMENT)
	    || pk_bitfield_contain(filters, PK_FILTER_ENUM_SOURCE)
	    || pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_SOURCE)
	    || pk_bitfield_contain(filters, PK_FILTER_ENUM_SUPPORTED)
	    || pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_SUPPORTED)) {
		auto repo_weak = pkg.get_repo();
		if (repo_weak.is_valid()) {
			if (!dnf5_backend_pk_repo_filter(*repo_weak, filters))
				return false;
		}
	}

	return true;
}

std::vector<libdnf5::rpm::Package>
dnf5_process_dependency(libdnf5::Base &base, const libdnf5::rpm::Package &pkg, PkRoleEnum role, gboolean recursive)
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
		if (role == PK_ROLE_ENUM_DEPENDS_ON)
			reldeps = curr.get_requires();
		else
			reldeps = curr.get_provides();

		for (const auto &reldep : reldeps) {
			std::string req = reldep.to_string();
			libdnf5::rpm::PackageQuery query(base);
			if (role == PK_ROLE_ENUM_DEPENDS_ON)
				query.filter_provides(req);
			else
				query.filter_requires(req);

			// Filter for latest version and supported architectures to avoid duplicates
			// for available packages
			query.filter_latest_evr();
			query.filter_arch(libdnf5::rpm::get_supported_arches());

			for (const auto &res : query) {
				std::string res_nevra = res.get_name() + ";" + res.get_evr() + ";" + res.get_arch();
				if (visited.find(res_nevra) == visited.end()) {
					visited.insert(res_nevra);
					results.push_back(res);
					if (recursive)
						queue.push(res);
				}
			}
		}
	}
	return results;
}

std::string
dnf5_build_package_id(const libdnf5::rpm::Package &pkg)
{
	std::string evr = pkg.get_evr();
	std::string repo_id = pkg.get_repo_id();
	if (pkg.get_install_time() > 0) {
		std::string from_repo = pkg.get_from_repo_id();
		if (!from_repo.empty()) {
			repo_id = "installed:" + from_repo;
		} else {
			repo_id = "installed";
		}
	}

	return pkg.get_name() + ";" + evr + ";" + pkg.get_arch() + ";" + repo_id;
}

void
dnf5_emit_pkg(PkBackendJob *job, const libdnf5::rpm::Package &pkg, PkInfoEnum info, PkInfoEnum severity)
{
	if (info == PK_INFO_ENUM_UNKNOWN) {
		info = PK_INFO_ENUM_AVAILABLE;
		if (pkg.get_install_time() > 0) {
			info = PK_INFO_ENUM_INSTALLED;
		}
	}

	std::string package_id = dnf5_build_package_id(pkg);
	if (severity != PK_INFO_ENUM_UNKNOWN) {
		pk_backend_job_package_full(job, info, package_id.c_str(), pkg.get_summary().c_str(), severity);
	} else {
		pk_backend_job_package(job, info, package_id.c_str(), pkg.get_summary().c_str());
	}
}

void
dnf5_sort_and_emit(PkBackendJob *job, std::vector<libdnf5::rpm::Package> &pkgs)
{
	std::sort(pkgs.begin(), pkgs.end(), [](const libdnf5::rpm::Package &a, const libdnf5::rpm::Package &b) {
		bool a_installed = (a.get_install_time() > 0);
		bool b_installed = (b.get_install_time() > 0);
		if (a_installed != b_installed)
			return a_installed;
		if (a.get_name() != b.get_name())
			return a.get_name() < b.get_name();
		if (a.get_arch() != b.get_arch())
			return a.get_arch() < b.get_arch();
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
dnf5_apply_filters(libdnf5::Base &base, libdnf5::rpm::PackageQuery &query, PkBitfield filters)
{
	gboolean installed = pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED);
	gboolean available = pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED);

	if (installed && !available) {
		query.filter_installed();
	} else if (!installed && available) {
		query.filter_available();
	}

	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_ARCH)) {
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

	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NEWEST)) {
		query.filter_latest_evr();
	}
}

std::vector<libdnf5::rpm::Package>
dnf5_resolve_package_ids(libdnf5::Base &base, gchar **package_ids, bool allow_cmdline_packages)
{
	std::vector<libdnf5::rpm::Package> pkgs;
	if (!package_ids)
		return pkgs;

	// Packages added from local files live in the shared base's @commandline
	// pseudo-repository, and any client may learn their package IDs from an
	// InstallFiles simulation. Callers that act on the system must never be able
	// to address them, or an unauthenticated InstallFiles could stage a package
	// that a later, differently authorized transaction then installs.
	auto is_cmdline_package = [&](const libdnf5::rpm::Package &pkg) {
		if (allow_cmdline_packages)
			return false;
		if (pkg.get_repo_id() != DNF5_CMDLINE_REPO_ID)
			return false;
		g_warning(
			"Refusing to resolve command line package %s for this transaction",
			pkg.get_full_nevra().c_str());
		return true;
	};

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
							pkg.get_name().c_str(),
							pkg.get_evr().c_str(),
							pkg.get_arch().c_str(),
							pkg.get_repo_id().c_str());
						if (is_cmdline_package(pkg))
							continue;
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
		if (!split)
			continue;

		try {
			libdnf5::rpm::PackageQuery query(base);
			g_debug("Resolving package ID: name=%s, version=%s, arch=%s, repo=%s",
				split[PK_PACKAGE_ID_NAME],
				split[PK_PACKAGE_ID_VERSION],
				split[PK_PACKAGE_ID_ARCH],
				split[PK_PACKAGE_ID_DATA]);
			query.filter_name(split[PK_PACKAGE_ID_NAME]);
			query.filter_evr(split[PK_PACKAGE_ID_VERSION]);
			query.filter_arch(split[PK_PACKAGE_ID_ARCH]);

			if (g_str_has_prefix(split[PK_PACKAGE_ID_DATA], "installed")) {
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
						p.get_name().c_str(),
						p.get_evr().c_str(),
						p.get_arch().c_str(),
						p.get_repo_id().c_str());
				}
			}

			for (auto pkg : query) {
				if (is_cmdline_package(pkg))
					continue;
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
dnf5_remove_old_cache_directories(PkBackend *backend, const gchar *release_ver)
{
	PkBackendDnf5Private *priv = (PkBackendDnf5Private *) pk_backend_get_user_data(backend);
	g_assert(priv->conf != NULL);

	/* cache cleanup disabled? */
	if (g_key_file_get_boolean(priv->conf, "Daemon", "KeepCache", NULL)) {
		g_debug("KeepCache config option set; skipping old cache directory cleanup");
		return;
	}

	/* only do cache cleanup for regular installs */
	g_autofree gchar *root_dir = pk_util_get_root_dir(priv->conf);
	if (g_strcmp0(root_dir, "/") != 0) {
		g_debug("RootDir config option set; skipping old cache directory cleanup");
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
		if (rpmvercmp(filename.c_str(), release_ver) < 0) {
			g_debug("removing old cache directory %s", entry.path().c_str());
			std::error_code ec;
			std::filesystem::remove_all(entry.path(), ec);
			if (ec)
				g_warning(
					"failed to remove directory %s: %s",
					entry.path().c_str(),
					ec.message().c_str());
		}
	}
}

Dnf5DownloadCallbacks::Dnf5DownloadCallbacks(PkBackendJob *job)
    : job(job),
      total_size(0),
      downloaded_size(0),
      transferred_size(0),
      percentage(-1),
      speed_timestamp(0),
      speed_transferred_size(0),
      next_id(1)
{
}

void
Dnf5DownloadCallbacks::add_package(const libdnf5::rpm::Package &pkg)
{
	// libdnf5 describes package downloads by their full NEVRA
	add_package(pkg.get_full_nevra(), dnf5_build_package_id(pkg), pkg.get_download_size(), pkg.get_summary());
}

void
Dnf5DownloadCallbacks::add_package(
	const std::string &description,
	const std::string &package_id,
	uint64_t download_size,
	const std::string &summary)
{
	std::lock_guard<std::mutex> lock(mutex);
	package_ids[description] = package_id;
	summaries[package_id] = summary;
	total_size += download_size;
}

void *
Dnf5DownloadCallbacks::add_new_download(void *user_data, const char *description, double total_to_download)
{
	std::lock_guard<std::mutex> lock(mutex);
	void *id = reinterpret_cast<void *>(next_id++);
	Item item = {};
	item.total_size = total_to_download > 0 ? total_to_download : 0;

	if (!package_ids.empty()) {
		// packages that are already available locally are not registered
		// and do not count towards the download size
		auto it = package_ids.find(description != nullptr ? description : "");
		if (it != package_ids.end())
			item.package_id = it->second;
	}
	items[id] = item;
	return id;
}

double
Dnf5DownloadCallbacks::item_fraction(const Item &item) const
{
	double fraction = 0;
	if (item.total_size > 0)
		fraction = std::clamp(item.downloaded / item.total_size, 0.0, 1.0);

	guint completed = item.failed ? item.ends - 1 : item.ends;
	if (!package_ids.empty())
		return completed > 0 ? 1.0 : fraction;

	// Repository metadata is downloaded in two stages: first repomd.xml or the
	// metalink is fetched to check whether the cache is still in sync, then
	// the remaining metadata only for repositories that are not. Both stages
	// end the same download, and which repositories need the second stage is
	// only known once it starts, so each stage accounts for half of a
	// repository to keep the overall percentage from going backwards.
	if (completed > 1)
		return 1.0;
	if (completed == 1)
		return item.reopened ? 0.5 + fraction / 2 : 0.5;
	return fraction / 2;
}

void
Dnf5DownloadCallbacks::announce(Item &item)
{
	if (item.announced || item.package_id.empty())
		return;
	item.announced = true;
	pk_backend_job_package(
		job,
		PK_INFO_ENUM_DOWNLOADING,
		item.package_id.c_str(),
		summaries[item.package_id].c_str());
}

void
Dnf5DownloadCallbacks::update_item_progress(Item &item)
{
	if (item.package_id.empty())
		return;

	guint item_percentage = (guint) (item_fraction(item) * 100);
	if (item_percentage == item.percentage)
		return;
	item.percentage = item_percentage;
	pk_backend_job_set_item_progress(job, item.package_id.c_str(), PK_STATUS_ENUM_DOWNLOAD, item_percentage);
}

void
Dnf5DownloadCallbacks::update_progress()
{
	if (!items.empty()) {
		gint new_percentage = -1;
		if (package_ids.empty()) {
			double sum = 0;
			for (auto const &[id, item] : items)
				sum += item_fraction(item);
			new_percentage = (gint) (sum * 100 / items.size());
		} else if (total_size > 0) {
			new_percentage = (gint) (std::min(downloaded_size, (double) total_size) * 100 / total_size);
		}
		// the daemon refuses a percentage going down, and the metadata sizes
		// are only learned while downloading
		if (new_percentage > percentage) {
			percentage = new_percentage;
			pk_backend_job_set_percentage(job, (guint) percentage);
		}
	}

	if (total_size > 0) {
		double remaining = std::max((double) total_size - downloaded_size, 0.0);
		pk_backend_job_set_download_size_remaining(job, (guint64) remaining);
	}

	// start measuring with the first data, as connecting to the mirrors
	// would otherwise be averaged into the rate
	gint64 now = g_get_monotonic_time();
	if (speed_timestamp == 0) {
		if (transferred_size > 0) {
			speed_timestamp = now;
			speed_transferred_size = transferred_size;
		}
		return;
	}

	// average over at least a second so the rate does not jump around
	gint64 elapsed = now - speed_timestamp;
	if (elapsed >= G_USEC_PER_SEC) {
		double speed = (transferred_size - speed_transferred_size) * G_USEC_PER_SEC / elapsed;
		pk_backend_job_set_speed(job, (guint) std::max(speed, 0.0));
		speed_timestamp = now;
		speed_transferred_size = transferred_size;
	}
}

int
Dnf5DownloadCallbacks::progress(void *user_cb_data, double total_to_download, double downloaded)
{
	std::lock_guard<std::mutex> lock(mutex);
	auto it = items.find(user_cb_data);
	if (it == items.end())
		return OK;
	Item &item = it->second;

	// only registered packages count towards the download size
	if (package_ids.empty() || !item.package_id.empty()) {
		downloaded_size += downloaded - item.downloaded;
		transferred_size += downloaded - item.downloaded;
	}

	// the second stage of a repository metadata download
	if (item.ends > 0)
		item.reopened = true;
	if (total_to_download > 0)
		item.total_size = total_to_download;
	item.downloaded = downloaded;

	announce(item);
	update_item_progress(item);
	update_progress();
	return OK;
}

int
Dnf5DownloadCallbacks::end(void *user_cb_data, TransferStatus status, const char *msg)
{
	std::lock_guard<std::mutex> lock(mutex);
	auto it = items.find(user_cb_data);
	if (it == items.end())
		return OK;
	Item &item = it->second;

	if (status != TransferStatus::ERROR && !package_ids.empty() && !item.package_id.empty()) {
		// packages that already exist report no progress at all
		downloaded_size += item.total_size - item.downloaded;
		item.downloaded = item.total_size;
	}
	item.ends++;
	item.failed = status == TransferStatus::ERROR;

	announce(item);
	update_item_progress(item);
	update_progress();

	bool finished = std::all_of(items.begin(), items.end(), [](const auto &entry) {
		return entry.second.ends > 0;
	});
	if (finished)
		pk_backend_job_set_speed(job, 0);
	return OK;
}

Dnf5ScopedDownloadCallbacks::Dnf5ScopedDownloadCallbacks(
	libdnf5::Base &base,
	std::unique_ptr<Dnf5DownloadCallbacks> callbacks)
    : base(base)
{
	base.set_download_callbacks(std::move(callbacks));
}

Dnf5ScopedDownloadCallbacks::~Dnf5ScopedDownloadCallbacks()
{
	base.set_download_callbacks(nullptr);
}

Dnf5TransactionCallbacks::Dnf5TransactionCallbacks(PkBackendJob *job)
    : job(job),
      total_items(0),
      processed_items(0),
      processed_share(0),
      current_share(0),
      current_status(PK_STATUS_ENUM_UNKNOWN),
      current_percentage(-1),
      current_complete(false),
      running_hooks(false)
{
}

void
Dnf5TransactionCallbacks::add_package(const libdnf5::base::TransactionPackage &item)
{
	auto pkg = item.get_package();
	bool inbound = libdnf5::transaction::transaction_item_action_is_inbound(item.get_action());
	add_package(pkg.get_full_nevra(), inbound, pkg.get_install_size());
}

void
Dnf5TransactionCallbacks::add_package(const std::string &full_nevra, bool inbound, uint64_t installed_size)
{
	elements[{full_nevra, inbound}] = {inbound, installed_size};
}

double
Dnf5TransactionCallbacks::element_share(const std::string &full_nevra, bool inbound) const
{
	if (elements.empty())
		return total_items > 0 ? 1.0 / total_items : 0;

	// elements that were not registered do not move the percentage
	auto it = elements.find({full_nevra, inbound});
	if (it == elements.end())
		return 0;
	if (!inbound)
		return 1.0 / elements.size();

	// removing a package takes about as long whatever its size, but
	// installing one mostly depends on how much there is to write
	uint64_t inbound_count = 0;
	uint64_t inbound_size = 0;
	for (const auto &[key, element] : elements) {
		if (element.inbound) {
			inbound_count++;
			inbound_size += element.installed_size;
		}
	}
	double inbound_share = (double) inbound_count / elements.size();
	if (inbound_size == 0)
		return inbound_share / inbound_count;
	return inbound_share * it->second.installed_size / inbound_size;
}

void
Dnf5TransactionCallbacks::start_element(
	const std::string &full_nevra,
	bool inbound,
	const std::string &package_id,
	PkInfoEnum info)
{
	current_share = element_share(full_nevra, inbound);
	current_package_id = package_id;
	current_percentage = -1;
	current_complete = false;
	current_status = inbound ? PK_STATUS_ENUM_INSTALL : PK_STATUS_ENUM_REMOVE;
	if (info == PK_INFO_ENUM_UPDATING)
		current_status = PK_STATUS_ENUM_UPDATE;
	else if (info == PK_INFO_ENUM_CLEANUP)
		current_status = PK_STATUS_ENUM_CLEANUP;
	if (!current_package_id.empty()) {
		pk_backend_job_package(job, info, current_package_id.c_str(), nullptr);
		pk_backend_job_set_item_progress(job, current_package_id.c_str(), current_status, 0);
		current_percentage = 0;
	}
}

void
Dnf5TransactionCallbacks::element_progress(uint64_t amount, uint64_t total)
{
	double fraction = total > 0 ? std::min((double) amount / total, 1.0) : 0;
	guint percentage = total > 0 ? (guint) (fraction * 100) : PK_BACKEND_PERCENTAGE_INVALID;
	if (!current_package_id.empty() && current_percentage != (gint) percentage) {
		pk_backend_job_set_item_progress(job, current_package_id.c_str(), current_status, percentage);
		current_percentage = (gint) percentage;
	}
	if (total == 0)
		return;
	current_complete = amount >= total;
	// the shares add up with rounding errors, which must not cut off 100%
	double done = processed_share + fraction * current_share;
	pk_backend_job_set_percentage(job, (guint) std::min(done * 100 + 1e-6, 100.0));
}

void
Dnf5TransactionCallbacks::stop_element()
{
	// rpm can omit the last progress callback, especially for removals.
	if (!current_complete)
		element_progress(1, 1);
	if (!current_package_id.empty()) {
		pk_backend_job_set_item_progress(job, current_package_id.c_str(), PK_STATUS_ENUM_FINISHED, 100);
		pk_backend_job_package(job, PK_INFO_ENUM_FINISHED, current_package_id.c_str(), nullptr);
		current_package_id.clear();
	}
	processed_items++;
	processed_share += current_share;
	current_share = 0;
}

void
Dnf5TransactionCallbacks::start_scriptlet()
{
	// %posttrans scriptlets and file triggers run once every element has been
	// processed, and can take a while without any other progress
	if (running_hooks || total_items == 0 || processed_items < total_items)
		return;
	running_hooks = true;
	pk_backend_job_set_status(job, PK_STATUS_ENUM_RUN_HOOK);
	pk_backend_job_set_percentage(job, PK_BACKEND_PERCENTAGE_INVALID);
}

void
Dnf5TransactionCallbacks::before_begin(uint64_t total)
{
	total_items = total;
}

void
Dnf5TransactionCallbacks::verify_start(uint64_t total)
{
	pk_backend_job_set_status(job, PK_STATUS_ENUM_SIG_CHECK);
	pk_backend_job_set_percentage(job, 0);
}

void
Dnf5TransactionCallbacks::verify_progress(uint64_t amount, uint64_t total)
{
	if (total > 0)
		pk_backend_job_set_percentage(job, (uint) (amount * 100 / total));
}

void
Dnf5TransactionCallbacks::transaction_start(uint64_t total)
{
	// rpm is preparing the transaction, there is no progress to report
	pk_backend_job_set_status(job, PK_STATUS_ENUM_COMMIT);
	pk_backend_job_set_percentage(job, PK_BACKEND_PERCENTAGE_INVALID);
}

void
Dnf5TransactionCallbacks::install_start(const libdnf5::base::TransactionPackage &item, uint64_t total)
{
	auto action = item.get_action();
	PkInfoEnum info = PK_INFO_ENUM_INSTALLING;
	if (action == libdnf5::transaction::TransactionItemAction::UPGRADE
	    || action == libdnf5::transaction::TransactionItemAction::DOWNGRADE) {
		info = PK_INFO_ENUM_UPDATING;
	}
	auto pkg = item.get_package();
	start_element(pkg.get_full_nevra(), true, dnf5_build_package_id(pkg), info);
}

void
Dnf5TransactionCallbacks::install_progress(
	const libdnf5::base::TransactionPackage &item,
	uint64_t amount,
	uint64_t total)
{
	element_progress(amount, total);
}

void
Dnf5TransactionCallbacks::install_stop(const libdnf5::base::TransactionPackage &item, uint64_t amount, uint64_t total)
{
	stop_element();
}

void
Dnf5TransactionCallbacks::uninstall_start(const libdnf5::base::TransactionPackage &item, uint64_t total)
{
	auto action = item.get_action();
	PkInfoEnum info = PK_INFO_ENUM_REMOVING;
	if (action == libdnf5::transaction::TransactionItemAction::REPLACED) {
		info = PK_INFO_ENUM_CLEANUP;
	}
	auto pkg = item.get_package();
	start_element(pkg.get_full_nevra(), false, dnf5_build_package_id(pkg), info);
}

void
Dnf5TransactionCallbacks::uninstall_progress(
	const libdnf5::base::TransactionPackage &item,
	uint64_t amount,
	uint64_t total)
{
	element_progress(amount, total);
}

void
Dnf5TransactionCallbacks::uninstall_stop(const libdnf5::base::TransactionPackage &item, uint64_t amount, uint64_t total)
{
	stop_element();
}

void
Dnf5TransactionCallbacks::script_start(
	const libdnf5::base::TransactionPackage *item,
	libdnf5::rpm::Nevra nevra,
	ScriptType type)
{
	start_scriptlet();
}
