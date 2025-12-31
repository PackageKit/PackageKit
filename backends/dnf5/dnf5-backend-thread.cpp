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

#include "dnf5-backend-thread.hpp"
#include "dnf5-backend-utils.hpp"
#include <libdnf5/base/goal.hpp>
#include <libdnf5/comps/environment/query.hpp>
#include <libdnf5/comps/group/query.hpp>
#include <libdnf5/advisory/advisory_query.hpp>
#include <libdnf5/rpm/reldep_list.hpp>
#include <libdnf5/base/transaction.hpp>
#include <libdnf5/repo/package_downloader.hpp>
#include <libdnf5/conf/config_parser.hpp>
#include <packagekit-glib2/pk-common-private.h>
#include <packagekit-glib2/pk-update-detail.h>
#include <rpm/rpmlib.h>
#include <glib/gstdio.h>
#include <filesystem>
#include <map>

void
dnf5_query_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	PkBackend *backend = (PkBackend *) pk_backend_job_get_backend (job);
	PkBackendDnf5Private *priv = (PkBackendDnf5Private *) pk_backend_get_user_data (backend);
	PkRoleEnum role = pk_backend_job_get_role (job);
	
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);
	
	try {
		if (role == PK_ROLE_ENUM_SEARCH_NAME || role == PK_ROLE_ENUM_SEARCH_DETAILS || role == PK_ROLE_ENUM_SEARCH_FILE || role == PK_ROLE_ENUM_RESOLVE || role == PK_ROLE_ENUM_WHAT_PROVIDES) {
			PkBitfield filters;
			g_auto(GStrv) values = NULL;
			g_variant_get (params, "(t^as)", &filters, &values);
			
			g_debug("Query role=%d, filters=%lu", role, (unsigned long)filters);
			
			std::vector<libdnf5::rpm::Package> results;
			libdnf5::rpm::PackageQuery query(*priv->base);
			
			std::vector<std::string> search_terms;
			for (int i = 0; values[i]; i++) search_terms.push_back(values[i]);
			
			if (role == PK_ROLE_ENUM_SEARCH_NAME) {
				query.filter_name(search_terms, libdnf5::sack::QueryCmp::ICONTAINS);
			} else if (role == PK_ROLE_ENUM_SEARCH_FILE) {
				query.filter_file(search_terms);
			} else if (role == PK_ROLE_ENUM_RESOLVE) {
				// For RESOLVE, filter by name FIRST, then apply other filters
				// This matches the old DNF backend behavior
				for (const auto &term : search_terms)
					g_debug("Resolving package name: %s", term.c_str());
				query.filter_name(search_terms, libdnf5::sack::QueryCmp::EQ);
				g_debug("After filter_name: query has %zu packages", query.size());
			} else if (role == PK_ROLE_ENUM_WHAT_PROVIDES) {
				std::vector<std::string> provides;
				for (const auto &term : search_terms) {
					provides.push_back(term);
					provides.push_back("gstreamer0.10(" + term + ")");
					provides.push_back("gstreamer1(" + term + ")");
					provides.push_back("font(" + term + ")");
					provides.push_back("mimehandler(" + term + ")");
					provides.push_back("postscriptdriver(" + term + ")");
					provides.push_back("plasma4(" + term + ")");
					provides.push_back("plasma5(" + term + ")");
					provides.push_back("language(" + term + ")");
				}
				query.filter_provides(provides);
			} else if (role == PK_ROLE_ENUM_SEARCH_DETAILS) {
				libdnf5::rpm::PackageQuery query_sum(*priv->base);
				query.filter_description(search_terms, libdnf5::sack::QueryCmp::ICONTAINS);
				query_sum.filter_summary(search_terms, libdnf5::sack::QueryCmp::ICONTAINS);
				// Apply filters to both queries before merging
				dnf5_apply_filters(*priv->base, query, filters);
				dnf5_apply_filters(*priv->base, query_sum, filters);
				for (auto p : query_sum) {
					if (dnf5_package_filter(p, filters))
						results.push_back(p);
				}
			}
			
			// Apply filters AFTER filtering by name/file/provides for most roles
			// Exception: SEARCH_DETAILS already applied filters above
			if (role != PK_ROLE_ENUM_SEARCH_DETAILS) {
				g_debug("Before dnf5_apply_filters: query has %zu packages", query.size());
				dnf5_apply_filters(*priv->base, query, filters);
				g_debug("After dnf5_apply_filters: query has %zu packages", query.size());
			}
			
			// For RESOLVE, we've already applied all necessary filters via dnf5_apply_filters
			// Don't apply dnf5_package_filter again as it causes incorrect filtering
			if (role == PK_ROLE_ENUM_RESOLVE) {
				for (auto p : query) {
					results.push_back(p);
				}
			} else {
				for (auto p : query) {
					if (dnf5_package_filter(p, filters))
						results.push_back(p);
				}
			}
			g_debug("Final results: %zu packages", results.size());
			dnf5_sort_and_emit(job, results);


			
		} else if (role == PK_ROLE_ENUM_DEPENDS_ON || role == PK_ROLE_ENUM_REQUIRED_BY) {
			PkBitfield filters;
			g_auto(GStrv) package_ids = NULL;
			gboolean recursive;
			g_variant_get (params, "(t^asb)", &filters, &package_ids, &recursive);
			
			auto input_pkgs = dnf5_resolve_package_ids(*priv->base, package_ids);
			std::vector<libdnf5::rpm::Package> results;
			for (const auto &pkg : input_pkgs) {
				auto deps = dnf5_process_dependency(*priv->base, pkg, role, recursive);
				for (auto dep : deps) {
					if (dnf5_package_filter(dep, filters))
						results.push_back(dep);
				}
			}
			dnf5_sort_and_emit(job, results);

		} else if (role == PK_ROLE_ENUM_GET_PACKAGES || role == PK_ROLE_ENUM_GET_UPDATES) {
			PkBitfield filters;
			g_variant_get (params, "(t)", &filters);
			
			libdnf5::rpm::PackageQuery query(*priv->base);
			dnf5_apply_filters(*priv->base, query, filters);
			
			if (role == PK_ROLE_ENUM_GET_UPDATES) {
				libdnf5::Goal goal(*priv->base);
				if (dnf5_force_distupgrade_on_upgrade (*priv->base))
					goal.add_rpm_distro_sync();
				else
					goal.add_rpm_upgrade();
				auto trans = goal.resolve();
				
				std::vector<libdnf5::rpm::Package> update_pkgs;
				for (const auto &item : trans.get_transaction_packages()) {
					auto action = item.get_action();
					if (action == libdnf5::transaction::TransactionItemAction::UPGRADE || action == libdnf5::transaction::TransactionItemAction::INSTALL) {
						update_pkgs.push_back(item.get_package());
					}
				}
				
				libdnf5::advisory::AdvisoryQuery adv_query(*priv->base);
				libdnf5::rpm::PackageSet pkg_set(priv->base->get_weak_ptr());
				for (const auto &pkg : update_pkgs) pkg_set.add(pkg);
				adv_query.filter_packages(pkg_set);
				
				std::map<std::string, libdnf5::advisory::Advisory> pkg_to_advisory;
				for (const auto &adv_pkg : adv_query.get_advisory_packages_sorted(pkg_set)) {
					std::string key = adv_pkg.get_name() + ";" + adv_pkg.get_evr() + ";" + adv_pkg.get_arch();
					pkg_to_advisory.emplace(key, adv_pkg.get_advisory());
				}
				
				for (const auto &pkg : update_pkgs) {
					if (dnf5_package_filter(pkg, filters)) {
						PkInfoEnum info = PK_INFO_ENUM_UNKNOWN;
						PkInfoEnum severity = PK_INFO_ENUM_UNKNOWN;
						
						std::string key = pkg.get_name() + ";" + pkg.get_evr() + ";" + pkg.get_arch();
						auto it = pkg_to_advisory.find(key);
						if (it != pkg_to_advisory.end()) {
							info = dnf5_advisory_kind_to_info_enum(it->second.get_type());
							severity = dnf5_update_severity_to_enum(it->second.get_severity());
						}
						dnf5_emit_pkg(job, pkg, info, severity);
					}
				}
			} else {
				std::vector<libdnf5::rpm::Package> results;
				for (auto p : query) {
					if (dnf5_package_filter(p, filters))
						results.push_back(p);
				}
				dnf5_sort_and_emit(job, results);
			}
		} else if (role == PK_ROLE_ENUM_GET_DETAILS || role == PK_ROLE_ENUM_GET_FILES || role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES || role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
			g_auto(GStrv) package_ids = NULL;
			if (role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
				gchar *directory = NULL;
				g_variant_get (params, "(^as&s)", &package_ids, &directory);
				auto pkgs = dnf5_resolve_package_ids(*priv->base, package_ids);
				libdnf5::repo::PackageDownloader downloader(*priv->base);
				uint64_t total_download_size = 0;
				for (const auto &pkg : pkgs) total_download_size += pkg.get_download_size();
				
				priv->base->set_download_callbacks(std::make_unique<Dnf5DownloadCallbacks>(job, total_download_size));
				for (auto &pkg : pkgs) {
					dnf5_emit_pkg(job, pkg, PK_INFO_ENUM_DOWNLOADING);
					downloader.add(pkg, directory);
				}
				downloader.download();
				
				std::vector<char*> files_c;
				for (auto &pkg : pkgs) {
					std::string path = pkg.get_package_path();
					if (!path.empty()) files_c.push_back(g_strdup(path.c_str()));
				}
				files_c.push_back(nullptr);
				pk_backend_job_files (job, NULL, files_c.data());
				for (auto p : files_c) g_free(p);
				pk_backend_job_finished (job);
				return;
			} else {
				g_variant_get (params, "(^as)", &package_ids);
			}
			
			auto pkgs = dnf5_resolve_package_ids(*priv->base, package_ids);
			if (role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
				libdnf5::advisory::AdvisoryQuery adv_query(*priv->base);
				libdnf5::rpm::PackageSet pkg_set(priv->base->get_weak_ptr());
				for (const auto &pkg : pkgs) pkg_set.add(pkg);
				adv_query.filter_packages(pkg_set);
				
				std::map<std::string, libdnf5::advisory::AdvisoryPackage> pkg_to_adv_pkg;
				for (const auto &adv_pkg : adv_query.get_advisory_packages_sorted(pkg_set)) {
					std::string key = adv_pkg.get_name() + ";" + adv_pkg.get_evr() + ";" + adv_pkg.get_arch();
					pkg_to_adv_pkg.emplace(key, adv_pkg);
				}
				
				g_autoptr(GPtrArray) update_details = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
				for (auto &pkg : pkgs) {
					std::string repo_id = pkg.get_repo_id();
					if (pkg.get_install_time() > 0) repo_id = "installed";
					std::string pid = pkg.get_name() + ";" + pkg.get_evr() + ";" + pkg.get_arch() + ";" + repo_id;
					
					std::string key = pkg.get_name() + ";" + pkg.get_evr() + ";" + pkg.get_arch();
					auto it = pkg_to_adv_pkg.find(key);
					if (it != pkg_to_adv_pkg.end()) {
						auto advisory = it->second.get_advisory();
						g_autoptr(PkUpdateDetail) item = pk_update_detail_new ();
						
						std::vector<std::string> bugzilla_urls, cve_urls, vendor_urls;
						for (const auto &ref : advisory.get_references()) {
							if (ref.get_url().empty()) continue;
							if (ref.get_type() == "bugzilla") bugzilla_urls.push_back(ref.get_url());
							else if (ref.get_type() == "cve") cve_urls.push_back(ref.get_url());
							else if (ref.get_type() == "vendor") vendor_urls.push_back(ref.get_url());
						}
						
						auto buildtime = advisory.get_buildtime();
						g_autoptr(GDateTime) dt = g_date_time_new_from_unix_local(buildtime);
						g_autofree gchar *date_str = g_date_time_format(dt, "%Y-%m-%d");
						
						PkRestartEnum restart = PK_RESTART_ENUM_NONE;
						if (it->second.get_reboot_suggested()) restart = PK_RESTART_ENUM_SYSTEM;
						else if (it->second.get_restart_suggested()) restart = PK_RESTART_ENUM_APPLICATION;
						else if (it->second.get_relogin_suggested()) restart = PK_RESTART_ENUM_SESSION;
						
						g_auto(GStrv) bugzilla_strv = (gchar **) g_new0 (gchar *, bugzilla_urls.size() + 1);
						for (size_t i = 0; i < bugzilla_urls.size(); i++) bugzilla_strv[i] = g_strdup(bugzilla_urls[i].c_str());
						g_auto(GStrv) cve_strv = (gchar **) g_new0 (gchar *, cve_urls.size() + 1);
						for (size_t i = 0; i < cve_urls.size(); i++) cve_strv[i] = g_strdup(cve_urls[i].c_str());
						g_auto(GStrv) vendor_strv = (gchar **) g_new0 (gchar *, vendor_urls.size() + 1);
						for (size_t i = 0; i < vendor_urls.size(); i++) vendor_strv[i] = g_strdup(vendor_urls[i].c_str());
						
						g_object_set (item,
								  "package-id", pid.c_str(),
								  "bugzilla-urls", bugzilla_strv,
								  "cve-urls", cve_strv,
								  "vendor-urls", vendor_strv,
								  "update-text", advisory.get_description().c_str(),
								  "restart", restart,
								  "state", PK_UPDATE_STATE_ENUM_STABLE,
								  "issued", date_str,
								  "updated", date_str,
								  NULL);
						g_ptr_array_add (update_details, g_steal_pointer (&item));
					}
				}
				pk_backend_job_update_details (job, update_details);
				pk_backend_job_finished (job);
				return;
			}
			
			for (auto &pkg : pkgs) {
				std::string repo_id = pkg.get_repo_id();
				if (pkg.get_install_time() > 0) repo_id = "installed";
				std::string pid = pkg.get_name() + ";" + pkg.get_evr() + ";" + pkg.get_arch() + ";" + repo_id;
				
				if (role == PK_ROLE_ENUM_GET_DETAILS) {
					std::string license = pkg.get_license();
					if (license.empty()) license = "unknown";
					pk_backend_job_details(job, pid.c_str(), pkg.get_summary().c_str(), license.c_str(), PK_GROUP_ENUM_UNKNOWN, pkg.get_description().c_str(), pkg.get_url().c_str(), pkg.get_install_size(), pkg.get_download_size());
				} else if (role == PK_ROLE_ENUM_GET_FILES) {
					auto files_vec = pkg.get_files();
					std::vector<char*> files_c;
					for (const auto &f : files_vec) files_c.push_back(const_cast<char*>(f.c_str()));
					files_c.push_back(nullptr);
					pk_backend_job_files(job, pid.c_str(), files_c.data());
				}
			}
		} else if (role == PK_ROLE_ENUM_GET_DETAILS_LOCAL || role == PK_ROLE_ENUM_GET_FILES_LOCAL) {
			g_auto(GStrv) files = NULL;
			g_variant_get (params, "(^as)", &files);
			libdnf5::Base local_base;
			local_base.load_config();
			local_base.get_config().get_pkg_gpgcheck_option().set(false);
			local_base.setup();
			std::vector<std::string> paths;
			for (int i = 0; files[i]; i++) paths.push_back(files[i]);
			auto added = local_base.get_repo_sack()->add_cmdline_packages(paths);
			for (const auto &pair : added) {
				const auto &pkg = pair.second;
				std::string pid = pkg.get_name() + ";" + pkg.get_evr() + ";" + pkg.get_arch() + ";" + (pkg.get_repo_id().empty() ? "local" : pkg.get_repo_id());
				if (role == PK_ROLE_ENUM_GET_DETAILS_LOCAL) {
					pk_backend_job_details(job, pid.c_str(), pkg.get_summary().c_str(), pkg.get_license().c_str(), PK_GROUP_ENUM_UNKNOWN, pkg.get_description().c_str(), pkg.get_url().c_str(), pkg.get_install_size(), 0);
				} else {
					auto files_vec = pkg.get_files();
					std::vector<char*> files_c;
					for (const auto &f : files_vec) files_c.push_back(const_cast<char*>(f.c_str()));
					files_c.push_back(nullptr);
					pk_backend_job_files(job, pid.c_str(), files_c.data());
				}
			}
		} else if (role == PK_ROLE_ENUM_GET_REPO_LIST) {
			PkBitfield filters;
			g_variant_get (params, "(t)", &filters);
			libdnf5::repo::RepoQuery query(*priv->base);
			for (auto repo : query) {
				std::string id = repo->get_id();
				if (id == "@System" || id == "@commandline") continue;
				if (!dnf5_backend_pk_repo_filter(*repo, filters)) continue;
				pk_backend_job_repo_detail(job, id.c_str(), repo->get_name().c_str(), repo->is_enabled());
			}
		}
	} catch (const std::exception &e) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_ERROR, "%s", e.what());
	}
	pk_backend_job_finished (job);
}

void
dnf5_transaction_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	PkBackend *backend = (PkBackend *) pk_backend_job_get_backend (job);
	PkBackendDnf5Private *priv = (PkBackendDnf5Private *) pk_backend_get_user_data (backend);
	PkRoleEnum role = pk_backend_job_get_role (job);
	
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);
	
	try {
		if (role == PK_ROLE_ENUM_UPGRADE_SYSTEM) {
			gchar *distro_id = NULL;
			PkUpgradeKindEnum upgrade_kind;
			PkBitfield transaction_flags;
			g_variant_get (params, "(t&su)", &transaction_flags, &distro_id, &upgrade_kind);
			if (distro_id) {
				dnf5_setup_base(priv, TRUE, TRUE, distro_id);
				
				g_debug("Checking repositories for system upgrade to %s:", distro_id);
				// ... logging code ...
				libdnf5::repo::RepoQuery query(*priv->base);
				for (auto repo : query) {
					// Check if baseurl contains the correct version
					auto baseurl = repo->get_config().get_baseurl_option().get_value();
					std::string url_str = baseurl.empty() ? "null" : baseurl[0];
					g_debug("Repo %s: enabled=%d, url=%s",
						repo->get_id().c_str(),
						repo->is_enabled(),
						url_str.c_str());
				}				
			}
		}

		libdnf5::Goal goal(*priv->base);
		PkBitfield transaction_flags = 0;
		
		if (role == PK_ROLE_ENUM_INSTALL_PACKAGES || role == PK_ROLE_ENUM_UPDATE_PACKAGES || role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
			g_auto(GStrv) package_ids = NULL;
			if (role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
				gboolean allow_deps, autoremove;
				g_variant_get (params, "(t^asbb)", &transaction_flags, &package_ids, &allow_deps, &autoremove);
				if (autoremove) priv->base->get_config().get_clean_requirements_on_remove_option().set(true);
			} else {
				g_variant_get (params, "(t^as)", &transaction_flags, &package_ids);
			}
			
			auto pkgs = dnf5_resolve_package_ids(*priv->base, package_ids);
			if (pkgs.empty() && role != PK_ROLE_ENUM_UPDATE_PACKAGES) {
				pk_backend_job_error_code (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "No packages found");
				pk_backend_job_finished (job);
				return;
			}
			
			for (auto &pkg : pkgs) {
				if (role == PK_ROLE_ENUM_INSTALL_PACKAGES) goal.add_rpm_install(pkg);
				else if (role == PK_ROLE_ENUM_REMOVE_PACKAGES) goal.add_rpm_remove(pkg);
				else if (role == PK_ROLE_ENUM_UPDATE_PACKAGES) goal.add_rpm_upgrade(pkg);
			}
			if (role == PK_ROLE_ENUM_UPDATE_PACKAGES && pkgs.empty()) {
				if (dnf5_force_distupgrade_on_upgrade (*priv->base))
					goal.add_rpm_distro_sync();
				else
					goal.add_rpm_upgrade();
			}
			
		} else if (role == PK_ROLE_ENUM_INSTALL_FILES) {
			g_auto(GStrv) full_paths = NULL;
			g_variant_get (params, "(t^as)", &transaction_flags, &full_paths);
			std::vector<std::string> paths;
			for (int i = 0; full_paths[i]; i++) paths.push_back(full_paths[i]);
			auto added = priv->base->get_repo_sack()->add_cmdline_packages(paths);
			for (const auto &p : added) goal.add_rpm_install(p.second);
		} else if (role == PK_ROLE_ENUM_UPGRADE_SYSTEM) {
			const gchar *distro_id = NULL;
			PkUpgradeKindEnum upgrade_kind;
			g_variant_get (params, "(t&su)", &transaction_flags, &distro_id, &upgrade_kind);
			
			// System upgrades require allowing erasure of packages (e.g. obsoletes)
			// and downgrades if necessary to match repo versions.
			goal.set_allow_erasing(true);
			goal.add_rpm_distro_sync();
			// System upgrades require processing groups to be upgraded
			libdnf5::comps::GroupQuery q_groups(*priv->base);
			q_groups.filter_installed(true);
			for (const auto & grp : q_groups) {
				goal.add_group_upgrade(grp.get_groupid());
			}
			libdnf5::comps::EnvironmentQuery q_environments(*priv->base);
			q_environments.filter_installed(true);
			for (const auto & env : q_environments) {
				goal.add_group_upgrade(env.get_environmentid());
            }
		} else if (role == PK_ROLE_ENUM_REPAIR_SYSTEM) {
			g_variant_get (params, "(t)", &transaction_flags);
			if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
				pk_backend_job_finished (job);
				return;
			}
			std::filesystem::path rpm_db_path("/var/lib/rpm");
			if (std::filesystem::exists(rpm_db_path) && std::filesystem::is_directory(rpm_db_path)) {
				for (const auto& entry : std::filesystem::directory_iterator(rpm_db_path)) {
					if (entry.is_regular_file() && entry.path().filename().string().starts_with("__db.")) {
						std::filesystem::remove(entry.path());
					}
				}
			}
			pk_backend_job_finished (job);
			return;
		}
		
		pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
		auto trans = goal.resolve();
		auto problems = trans.get_transaction_problems();
		if (!problems.empty()) {
			std::string msg;
			for (const auto &p : problems) msg += p + "; ";
			pk_backend_job_error_code (job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "%s", msg.c_str());
			pk_backend_job_finished (job);
			return;
		}

		g_debug("Resolved transaction has %zu packages", trans.get_transaction_packages().size());
		for (const auto &item : trans.get_transaction_packages()) {
			g_debug("Transaction item: %s - %d", item.get_package().get_name().c_str(), (int)item.get_action());
		}
		
		if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
			std::set<std::string> continuing_names;
			for (const auto &item : trans.get_transaction_packages()) {
				auto action = item.get_action();
				if (action == libdnf5::transaction::TransactionItemAction::UPGRADE ||
				    action == libdnf5::transaction::TransactionItemAction::DOWNGRADE ||
				    action == libdnf5::transaction::TransactionItemAction::REINSTALL) {
					continuing_names.insert(item.get_package().get_name());
				}
			}

			for (const auto &item : trans.get_transaction_packages()) {
				auto action = item.get_action();
				PkInfoEnum info = PK_INFO_ENUM_UNKNOWN;
				if (action == libdnf5::transaction::TransactionItemAction::INSTALL) info = PK_INFO_ENUM_INSTALLING;
				else if (action == libdnf5::transaction::TransactionItemAction::UPGRADE) info = PK_INFO_ENUM_UPDATING;
				else if (action == libdnf5::transaction::TransactionItemAction::REMOVE) info = PK_INFO_ENUM_REMOVING;
				else if (action == libdnf5::transaction::TransactionItemAction::REINSTALL) info = PK_INFO_ENUM_REINSTALLING;
				else if (action == libdnf5::transaction::TransactionItemAction::DOWNGRADE) info = PK_INFO_ENUM_DOWNGRADING;
				else if (action == libdnf5::transaction::TransactionItemAction::REPLACED) {
					if (continuing_names.find(item.get_package().get_name()) == continuing_names.end()) {
						info = PK_INFO_ENUM_OBSOLETING;
					}
				}
				
				if (info != PK_INFO_ENUM_UNKNOWN)
					dnf5_emit_pkg(job, item.get_package(), info);
			}
			pk_backend_job_finished (job);
			return;
		}
		
		pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);
		
		uint64_t total_download_size = 0;
		for (const auto &item : trans.get_transaction_packages()) {
			if (libdnf5::transaction::transaction_item_action_is_inbound(item.get_action())) {
				auto pkg = item.get_package();
				if (!pkg.is_available_locally()) {
					total_download_size += pkg.get_download_size();
				}
			}
		}
		
		priv->base->set_download_callbacks(std::make_unique<Dnf5DownloadCallbacks>(job, total_download_size));
		trans.download();

		if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD)) {
			// Iterate over transaction items and report them as if they were being processed
			for (const auto &item : trans.get_transaction_packages()) {
				auto action = item.get_action();
				PkInfoEnum info = PK_INFO_ENUM_UNKNOWN;
				
				if (action == libdnf5::transaction::TransactionItemAction::INSTALL) info = PK_INFO_ENUM_INSTALLING;
				else if (action == libdnf5::transaction::TransactionItemAction::UPGRADE) info = PK_INFO_ENUM_UPDATING;
				else if (action == libdnf5::transaction::TransactionItemAction::REMOVE) info = PK_INFO_ENUM_REMOVING;
				else if (action == libdnf5::transaction::TransactionItemAction::REINSTALL) info = PK_INFO_ENUM_REINSTALLING;
				else if (action == libdnf5::transaction::TransactionItemAction::DOWNGRADE) info = PK_INFO_ENUM_DOWNGRADING;
				
				if (info != PK_INFO_ENUM_UNKNOWN)
					dnf5_emit_pkg(job, item.get_package(), info);
			}
			pk_backend_job_finished (job);
			return;
		}

		pk_backend_job_set_status (job, PK_STATUS_ENUM_RUNNING);
		trans.set_callbacks(std::make_unique<Dnf5TransactionCallbacks>(job));
		auto res = trans.run();
		g_debug("Transaction run result: %s", libdnf5::base::Transaction::transaction_result_to_string(res).c_str());
		if (res != libdnf5::base::Transaction::TransactionRunResult::SUCCESS) {
			std::string msg;
			for (const auto &p : trans.get_transaction_problems()) msg += p + "; ";
			pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_ERROR, "Transaction failed: %s", msg.c_str());
		}
		
		// Post-transaction base re-initialization to ensure state consistency
		dnf5_setup_base (priv);
		
	} catch (const std::exception &e) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_ERROR, "%s", e.what());
	}
	pk_backend_job_finished (job);
}

void
dnf5_repo_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	PkBackend *backend = (PkBackend *) pk_backend_job_get_backend (job);
	PkBackendDnf5Private *priv = (PkBackendDnf5Private *) pk_backend_get_user_data (backend);
	PkRoleEnum role = pk_backend_job_get_role (job);
	
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);
	
	try {
		if (role == PK_ROLE_ENUM_REPO_ENABLE || role == PK_ROLE_ENUM_REPO_SET_DATA) {
			gchar *repo_id = NULL;
			const gchar *parameter, *value;
			if (role == PK_ROLE_ENUM_REPO_ENABLE) {
				gboolean enabled;
				g_variant_get (params, "(&sb)", &repo_id, &enabled);
				parameter = "enabled";
				value = enabled ? "1" : "0";
			} else {
				g_variant_get (params, "(&s&s&s)", &repo_id, &parameter, &value);
			}
			
			libdnf5::repo::RepoQuery query(*priv->base);
			query.filter_id(repo_id);
			for (auto repo : query) {
				if (g_strcmp0(parameter, "enabled") == 0) {
					bool enable = (g_strcmp0(value, "1") == 0 || g_strcmp0(value, "true") == 0);
					if (repo->is_enabled() == enable) {
						pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_ALREADY_SET, "Repo already in state");
						pk_backend_job_finished (job);
						return;
					}
					if (enable) repo->enable(); else repo->disable();
					libdnf5::ConfigParser parser;
					parser.read(repo->get_repo_file_path());
					parser.set_value(repo_id, "enabled", value);
					parser.write(repo->get_repo_file_path(), false);
				}
			}
			dnf5_setup_base (priv);
		} else if (role == PK_ROLE_ENUM_REPO_REMOVE) {
			gchar *repo_id = NULL;
			gboolean autoremove;
			PkBitfield transaction_flags;
			g_variant_get (params, "(t&sb)", &transaction_flags, &repo_id, &autoremove);
			
			libdnf5::repo::RepoQuery query(*priv->base);
			query.filter_id(repo_id);
			std::string repo_file;
			for (auto repo : query) {
				repo_file = repo->get_repo_file_path();
				break;
			}
			
			if (repo_file.empty()) {
				pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_FOUND, "Repo %s not found", repo_id);
				pk_backend_job_finished (job);
				return;
			}
			
			g_debug("Repo %s uses file %s", repo_id, repo_file.c_str());

			// Find all repos in the same file to track all packages that should be removed
			std::vector<std::string> all_repo_ids;
			libdnf5::repo::RepoQuery all_repos_query(*priv->base);
			for (auto repo : all_repos_query) {
				if (repo->get_repo_file_path() == repo_file) {
					all_repo_ids.push_back(repo->get_id());
				}
			}

			libdnf5::Goal goal(*priv->base);
			
			// Remove the owner package(s) of the repo file
			libdnf5::rpm::PackageQuery owner_query(*priv->base);
			owner_query.filter_installed();
			owner_query.filter_file({repo_file});
			
			if (owner_query.empty()) {
				g_debug("filter_file failed, trying provides for %s", repo_file.c_str());
				owner_query.filter_provides(repo_file);
			}

			for (auto pkg : owner_query) {
				g_debug("Adding owner package %s to removal goal", pkg.get_name().c_str());
				goal.add_remove(pkg.get_name());
			}
			
			// If autoremove is true, also remove packages installed from these repos
			if (autoremove) {
				libdnf5::rpm::PackageQuery inst_query(*priv->base);
				inst_query.filter_installed();
				for (auto pkg : inst_query) {
					std::string from_repo = pkg.get_from_repo_id();
					for (const auto &id : all_repo_ids) {
						if (from_repo == id) {
							goal.add_remove(pkg.get_name());
							break;
						}
					}
				}
				// Also enable unused dependency removal
				priv->base->get_config().get_clean_requirements_on_remove_option().set(true);
			}
			
			pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
			auto trans = goal.resolve();
			g_debug("Transaction has %zu packages", trans.get_transaction_packages().size());
			if (!trans.get_transaction_packages().empty()) {
				for (const auto &item : trans.get_transaction_packages()) {
					auto action = item.get_action();
					PkInfoEnum info = PK_INFO_ENUM_UNKNOWN;
					if (action == libdnf5::transaction::TransactionItemAction::INSTALL || action == libdnf5::transaction::TransactionItemAction::UPGRADE) info = PK_INFO_ENUM_INSTALLING;
					else if (action == libdnf5::transaction::TransactionItemAction::REMOVE || action == libdnf5::transaction::TransactionItemAction::REPLACED) info = PK_INFO_ENUM_REMOVING;
					else if (action == libdnf5::transaction::TransactionItemAction::REINSTALL) info = PK_INFO_ENUM_REINSTALLING;
					else if (action == libdnf5::transaction::TransactionItemAction::DOWNGRADE) info = PK_INFO_ENUM_DOWNGRADING;
					
					dnf5_emit_pkg(job, item.get_package(), info);
				}
			}

			if (!trans.get_transaction_problems().empty()) {
				std::string msg;
				for (const auto &p : trans.get_transaction_problems()) msg += p + "; ";
				pk_backend_job_error_code (job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "%s", msg.c_str());
				pk_backend_job_finished (job);
				return;
			}
			
			if (role == PK_ROLE_ENUM_REPO_REMOVE || !pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
				pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);
				g_debug("Starting transaction download...");
				trans.download();
				pk_backend_job_set_status (job, PK_STATUS_ENUM_RUNNING);
				g_debug("Starting transaction execution...");
				trans.set_description("PackageKit: repo-remove " + std::string(repo_id));
				auto res = trans.run();
				g_debug("Transaction run result: %s", libdnf5::base::Transaction::transaction_result_to_string(res).c_str());
				if (res != libdnf5::base::Transaction::TransactionRunResult::SUCCESS) {
					std::vector<std::string> problems = trans.get_transaction_problems();
					std::string msg;
					for (const auto &p : problems) msg += p + "; ";
					g_warning("Transaction failed: %s", msg.c_str());
					pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_ERROR, "Transaction failed: %s", msg.c_str());
				} else {
					g_debug("Transaction completed successfully");
				}
				dnf5_setup_base (priv);
			} else {
				g_debug("Simulation completed, finishing job...");
			}
		}
	} catch (const std::exception &e) {
		g_warning("Exception in dnf5_repo_thread: %s", e.what());
		pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_ERROR, "%s", e.what());
	}
	g_debug("Calling pk_backend_job_finished in dnf5_repo_thread");
	pk_backend_job_finished (job);
}
