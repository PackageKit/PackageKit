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

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>

#include <pk-backend.h>
#include <pk-backend-job.h>

#include <pkg.h>

#include <optional>
#include <string>
#include <memory>
#include <vector>

#include "stolen.h"
#include "PackageView.hpp"
#include "PackageDatabase.hpp"
#include "PKJobFinisher.hpp"
#include "Jobs.hpp"

// TODO: Research pkg-audit
// TODO: Implement proper progress reporting everywhere
// TODO: Implement correct job status reporting everywhere

typedef struct {
    gboolean has_signature;
    gboolean repo_enabled_devel;
    gboolean repo_enabled_fedora;
    gboolean repo_enabled_livna;
    gboolean repo_enabled_local;
    gboolean updated_gtkhtml;
    gboolean updated_kernel;
    gboolean updated_powertop;
    gboolean use_blocked;
    gboolean use_distro_upgrade;
    gboolean use_eula;
    gboolean use_gpg;
    gboolean use_media;
    gboolean use_trusted;
    gchar **package_ids;
    gchar **values;
    PkBitfield filters;
    gboolean fake_db_locked;
} PkBackendFreeBSDPrivate;

typedef struct {
    guint progress_percentage;
    GSocket *socket;
    guint socket_listen_id;
    GCancellable *cancellable;
    gulong signal_timeout;
} PkBackendFreeBSDJobData;

static PkBackendFreeBSDPrivate *priv;

static void backendJobPackageFromPkg (PkBackendJob *job, struct pkg* pkg, std::optional<PkInfoEnum> typeOverride);

static void
pk_freebsd_search(PkBackendJob *job, PkBitfield filters, gchar **values);

void
pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
    /* create private area */
    priv = g_new0 (PkBackendFreeBSDPrivate, 1);
    priv->repo_enabled_fedora = TRUE;
    priv->repo_enabled_devel = TRUE;
    priv->repo_enabled_livna = TRUE;
    priv->use_trusted = TRUE;
}

void
pk_backend_destroy (PkBackend *backend)
{
    g_free (priv);
}

PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
    return pk_bitfield_from_enums (PK_GROUP_ENUM_ACCESSIBILITY, //accessibility
        PK_GROUP_ENUM_COMMUNICATION, //comms
        PK_GROUP_ENUM_DESKTOP_GNOME, //gnome-* ports
        PK_GROUP_ENUM_DESKTOP_KDE, //plasma5-* ports
        PK_GROUP_ENUM_DESKTOP_OTHER, //budgie, enlightenment, etc.
        PK_GROUP_ENUM_DESKTOP_XFCE, //xfce-* ports
        PK_GROUP_ENUM_EDUCATION, //edu
        PK_GROUP_ENUM_FONTS, //x11-fonts
        PK_GROUP_ENUM_GAMES, //games
        PK_GROUP_ENUM_GRAPHICS, //graphics
        PK_GROUP_ENUM_INTERNET, //www
        PK_GROUP_ENUM_NETWORK, //net
        PK_GROUP_ENUM_PROGRAMMING, //devel
        PK_GROUP_ENUM_MULTIMEDIA, //multimedia
        PK_GROUP_ENUM_SECURITY, //security
        PK_GROUP_ENUM_SYSTEM, //sysutils
        PK_GROUP_ENUM_SCIENCE, //science
        -1);
}

PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
    return pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
        PK_FILTER_ENUM_NOT_INSTALLED,
        -1);
}

gchar **
pk_backend_get_mime_types (PkBackend *backend)
{
    const gchar *mime_types[] = {
                "application/x-xar",
                NULL };
    return g_strdupv ((gchar **) mime_types);
}

void
pk_backend_cancel (PkBackend *backend, PkBackendJob *job)
{
    g_error("pk_backend_cancel not implemented yet");
}

void
pk_backend_depends_on (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
    PKJobFinisher jf (job);
    pk_backend_job_set_allow_cancel (job, TRUE);

    // TODO
    if (recursive)
        g_warning ("depends_on: recursive is not yet supported");

    // TODO: what filters could we possibly get there?
    g_warning("depends_on: got filters %s", pk_filter_bitfield_to_string(filters));

    pkgdb_t db_type = PKGDB_MAYBE_REMOTE;
    // Open local DB only when filters require only installed packages
    // TODO: See below the "pkgdb_query" vs "pkgdb_repo_search" comment
    if (pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED)
        && !pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED))
        db_type = PKGDB_DEFAULT;

    PackageDatabase pkgDb (job, PKGDB_LOCK_READONLY, db_type);

    guint size = g_strv_length (package_ids);
    for (guint i = 0; i < size; i++) {
        PackageView pkgView(package_ids[i]);
        struct pkgdb_it* it = NULL;
        struct pkg *pkg = NULL;
        // TODO: take filters into account
        // TODO: It isn't clear from libpkg documentation, but it seems that we
        // should use pkgdb_repo_* only on PKGDB_MAYBE_REMOTE repos
        if (db_type == PKGDB_DEFAULT)
            it = pkgdb_query (pkgDb.handle(), pkgView.nameversion(), MATCH_EXACT);
        else
            it = pkgdb_repo_search (pkgDb.handle(), pkgView.nameversion(), MATCH_EXACT, FIELD_NAMEVER, FIELD_NAMEVER, NULL);

        while (pkgdb_it_next (it, &pkg, PKG_LOAD_BASIC | PKG_LOAD_DEPS) == EPKG_OK) {
            PackageView pkgView(pkg);

            char* dep_namevers0 = NULL;
            pkg_asprintf(&dep_namevers0, "%d%{%dn;%dv;%}", pkg);
            gchar** dep_namevers = g_strsplit (dep_namevers0, ";", 0);
            // delete both pointers by capturing a closure
            auto deps_deleter = deleted_unique_ptr<void>(reinterpret_cast<void*>(0xDEADC0DE), [dep_namevers0, dep_namevers](void* p) {
                free (dep_namevers0);
                g_strfreev (dep_namevers);
            });

            auto pk_type = db_type == PKGDB_DEFAULT ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;

            guint size2 = g_strv_length (dep_namevers);
            size2 -= size % 2;
            for (guint j = 0; j < size2; j+=2) {
                gchar* dep_id = pk_package_id_build (dep_namevers[j], dep_namevers[j+1], pkgView.arch(), pkgView.repository());

                pk_backend_job_package (job, pk_type, dep_id, ""); // TODO: we report an empty string instead of comment here

                g_free (dep_id);
                if (pk_backend_job_is_cancelled (job))
                    return;
            }
        }
    }
}

void
pk_backend_get_details_local (PkBackend *backend, PkBackendJob *job, gchar **files)
{
    g_error("pk_backend_get_details_local not implemented yet");
}

void
pk_backend_get_files_local (PkBackend *backend, PkBackendJob *job, gchar **_files)
{
    g_error("pk_backend_get_files_local not implemented yet");
}

void
pk_backend_get_details (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
    PKJobFinisher jf (job);
    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

    PackageDatabase pkgDb (job);

    guint size = g_strv_length (package_ids);
    for (guint i = 0; i < size; i++) {
        PackageView pkgView(package_ids[i]);
        pkgdb_it* it = pkgdb_query (pkgDb.handle(), pkgView.nameversion(), MATCH_EXACT);
        pkg* pkg = NULL;
        while (pkgdb_it_next (it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
            PackageView pkgView(pkg);
            PkGroupEnum group = PK_GROUP_ENUM_UNKNOWN; // TODO: set correct group
            gchar* description = NULL;
            gchar* url = NULL;
            pk_backend_job_details (job, package_ids[i],
                                    pkgView.comment(),
                                    pkgView.license(),
                                    group,
                                    description,
                                    url,
                                    pkgView.flatsize());

            if (pk_backend_job_is_cancelled (job))
                break;
        }
    }
}

// TODO: This requires pkgbase support
// void
// pk_backend_get_distro_upgrades (PkBackend *backend, PkBackendJob *job)
// {
// }

void
pk_backend_get_files (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
    g_error("pk_backend_get_files not implemented yet");
}

void
pk_backend_required_by (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
    g_error("pk_backend_required_by not implemented yet");
}

void
pk_backend_get_update_detail (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
    PKJobFinisher jf (job);
    PackageDatabase pkgDb (job);

    guint size = g_strv_length (package_ids);
    for (guint i = 0; i < size; i++) {
        // in this approach we call `pkg upgrade <pkg>` in a loop and present
        // the user an upgrading plan for each invocation
        // an alternative approach is to make only one call `pkg upgrade <pkg1> <pkg2> ...`
        // and output a combined plan. TODO: decide which way is better
        Jobs jobs(PKG_JOBS_UPGRADE, pkgDb.handle(), "update_detail");

        jobs << PKG_FLAG_PKG_VERSION_TEST << PKG_FLAG_DRY_RUN;

        PackageView pkg (package_ids[i]);
        gchar* pkg_namever = pkg.nameversion();
        jobs.add(MATCH_GLOB, &pkg_namever, 1);

        // TODO: handle reponame?

        if (jobs.solve() == 0)
            return; // no updates available

        std::vector<gchar*> updates;
        std::vector<gchar*> obsoletes;
        gchar		**vendor_urls = NULL;
        gchar		**bugzilla_urls = NULL;
        gchar		**cve_urls = NULL;
        PkRestartEnum	 restart = PK_RESTART_ENUM_NONE;
        const gchar	*update_text = NULL;
        const gchar	*changelog = NULL;
        PkUpdateStateEnum state = PK_UPDATE_STATE_ENUM_UNKNOWN;
        const gchar	*issued = NULL;
        const gchar	*updated = issued;

        for (auto it = jobs.begin(); it != jobs.end(); ++it) {
            switch (it.itemType()) {
            case PKG_SOLVED_INSTALL:
                updates.push_back(g_strdup(it.newPkgView().packageKitId()));
                break;
            case PKG_SOLVED_UPGRADE_REMOVE:
                obsoletes.push_back(g_strdup(it.oldPkgView().packageKitId()));
                break;
            default:
                updates.push_back(g_strdup(it.oldPkgView().packageKitId()));
                break;
            }
        }

        updates.push_back(nullptr);
        obsoletes.push_back(nullptr);

        pk_backend_job_update_detail (job, pkg.packageKitId(),
                                        updates.data(),
                                        obsoletes.data(),
                                        vendor_urls,
                                        bugzilla_urls,
                                        cve_urls,
                                        restart,
                                        update_text,
                                        changelog,
                                        state,
                                        issued,
                                        updated);

        for (auto* str : updates)
            g_free (str);
        for (auto* str : obsoletes)
            g_free (str);
    }
}

void
pk_backend_get_updates (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
    PKJobFinisher jf (job);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

    // TODO: what filters could we possibly get there?
    if (! (filters == 0
        || filters == pk_bitfield_value(PK_FILTER_ENUM_UNKNOWN)
        || filters == pk_bitfield_value(PK_FILTER_ENUM_NONE)
        || filters == pk_bitfield_value(PK_FILTER_ENUM_NEWEST)
    ))
        g_error("get_updates: unexpected filters %s", pk_filter_bitfield_to_string(filters));

    PackageDatabase pkgDb (job);
    Jobs jobs (PKG_JOBS_UPGRADE, pkgDb.handle(), "get_updates");

    jobs << PKG_FLAG_PKG_VERSION_TEST << PKG_FLAG_DRY_RUN;

    // TODO: handle reponame?

    if (jobs.solve() == 0)
        return; // no updates available

    for (auto it = jobs.begin(); it != jobs.end(); ++it) {
        // Do not report packages that will be removed by the upgrade
        if (it.itemType() == PKG_SOLVED_UPGRADE_REMOVE)
            continue;
        backendJobPackageFromPkg (job, it.newPkgHandle(), PK_INFO_ENUM_NORMAL);
    }
}

void
pk_backend_install_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
    PKJobFinisher jf (job);

    // TODO: handle all of these
    if (! (transaction_flags == 0
            || pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED)
            || pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)
            || pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD))
        )
        g_error("install_packages: unexpected transaction_flags %s", pk_transaction_flag_bitfield_to_string(transaction_flags));

    PackageDatabase pkgDb (job, PKGDB_LOCK_ADVISORY);
    Jobs jobs (PKG_JOBS_INSTALL, pkgDb.handle(), "install_packages");

    jobs << PKG_FLAG_PKG_VERSION_TEST;

    if (pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD))
        jobs << PKG_FLAG_SKIP_INSTALL;

    guint size = g_strv_length (package_ids);
    std::vector<gchar*> names;
    names.reserve (size+1);
    for (guint i = 0; i < size; i++) {
        PackageView pkg(package_ids[i]);
        names.push_back(g_strdup(pkg.nameversion()));
    }

    jobs.add (MATCH_GLOB, names);

    pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);

    if (jobs.solve() == 0)
        return; // nothing can be installed, for example because everything requested is already installed


    for (auto it = jobs.begin(); it != jobs.end(); ++it) {
        if (it.itemType() == PKG_SOLVED_DELETE) {
            g_warning ("install_packages: have to remove some packages");
            continue;
        }
        backendJobPackageFromPkg (job, it.newPkgHandle(), PK_INFO_ENUM_NORMAL);
    }

    if (pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE))
        return;

    pk_backend_job_set_status (job, PK_STATUS_ENUM_INSTALL);

    jobs.apply();

    for (auto* str : names)
        g_free (str);
}

void
pk_backend_install_signature (PkBackend *backend, PkBackendJob *job, PkSigTypeEnum type,
                              const gchar *key_id, const gchar *package_id)
{
    g_error("pk_backend_install_signature not implemented yet");
}

void
pk_backend_install_files (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **full_paths)
{
    g_error("pk_backend_install_files not implemented yet");
}

void
pk_backend_refresh_cache (PkBackend *backend, PkBackendJob *job, gboolean force)
{
    PKJobFinisher jf (job);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_REFRESH_CACHE);

    /* check network state */
    if (!pk_backend_is_online (backend)) {
        pk_backend_job_error_code (job, PK_ERROR_ENUM_NO_NETWORK, "Cannot check when offline");
        pk_backend_job_finished (job);
        return;
    }

    pkg_event_register(&event_callback, nullptr);

    g_assert(!pkg_initialized());
    if (pkg_ini(NULL, NULL, PKG_INIT_FLAG_USE_IPV4) != EPKG_OK)
    {
        g_error("pkg_ini failure");
        return;
    }
    // can't pass nullptr here, unique_ptr won't call the deleter
    auto libpkgDeleter = deleted_unique_ptr<void>(reinterpret_cast<void*>(0xDEADC0DE), [](void* p) { pkg_shutdown(); });

    int ret = pkgdb_access(PKGDB_MODE_WRITE|PKGDB_MODE_CREATE,
                PKGDB_DB_REPO);
    if (ret == EPKG_ENOACCESS) {
        g_error("Insufficient privileges to update the repository catalogue.");
    } else if (ret != EPKG_OK)
        g_error("Error");

    if (pkgdb_access(PKGDB_MODE_READ|PKGDB_MODE_WRITE|PKGDB_MODE_CREATE, PKGDB_DB_REPO) == EPKG_ENOACCESS) {
        g_error("Can't access package DB");
    }

    if (pkg_repos_total_count() == 0) {
        g_warning("No active remote repositories configured");
    }

    // TODO: basic progress reporting

    struct pkg_repo *r = NULL;
    while (pkg_repos(&r) == EPKG_OK) {
        if (!pkg_repo_enabled(r))
                continue;
        pkg_update (r, force);
    }
}

void
pk_backend_resolve (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **packages)
{
    PKJobFinisher jf (job);

    // TODO: should we adhere to PK_FILTER_ENUM_INSTALLED and PK_FILTER_ENUM_NOT_INSTALLED?
    g_warning ("resolve filters: %s", pk_filter_bitfield_to_string(filters));
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

    guint size = g_strv_length (packages);
    std::vector<gchar*> names;
    names.reserve(size+1);
    for (guint i = 0; i < size; i++) {
        gchar* pkg = packages[i];

        if (pk_package_id_check(pkg) == false) {
            // if pkgid isn't a valid package ID, treat it as glob "pkgname-*"
            names.push_back(g_strconcat(pkg, "-*", NULL));
        }
        else {
            PackageView pkgView (pkg);
            names.push_back(g_strdup(pkgView.nameversion()));
        }
    }

    // TODO: we iterate the DB twice to get a distinction between installed packages
    // and available ones
    // TODO: deduplicate identical entries
    {
        PackageDatabase pkgDb (job);

        for (auto* name : names) {
            pkgdb_it* it = pkgdb_query (pkgDb.handle(), name, MATCH_GLOB);
            struct pkg *pkg = NULL;

            while (pkgdb_it_next (it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
                backendJobPackageFromPkg (job, pkg, PK_INFO_ENUM_INSTALLED);
            }
        }
    }

    {
        PackageDatabase pkgDb (job);

        for (auto* name : names) {
            pkgdb_it* it = pkgdb_repo_search (pkgDb.handle(), name, MATCH_GLOB, FIELD_NAMEVER, FIELD_NAMEVER, NULL);
            struct pkg *pkg = NULL;

            while (pkgdb_it_next (it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
                backendJobPackageFromPkg (job, pkg, PK_INFO_ENUM_AVAILABLE);
            }
        }
    }
}

void
pk_backend_remove_packages (PkBackend *backend, PkBackendJob *job,
                            PkBitfield transaction_flags,
                            gchar **package_ids,
                            gboolean allow_deps,
                            gboolean autoremove)
{
    PKJobFinisher jf (job);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_REMOVE);

    guint size = g_strv_length (package_ids);
    // TODO: What should happen when remove_packages() is called with an empty package_ids?
    g_assert (size > 0);

    PackageDatabase pkgDb (job, PKGDB_LOCK_ADVISORY);
    Jobs jobs(PKG_JOBS_DEINSTALL, pkgDb.handle(), "remove_packages");

    if (allow_deps)
        jobs << PKG_FLAG_RECURSIVE;

    std::vector<gchar*> names;
    names.reserve (size);
    for (guint i = 0; i < size; i++) {
        PackageView pkg(package_ids[i]);
        names.push_back(g_strdup(pkg.nameversion()));
    }

    jobs.add(MATCH_GLOB, names);

    pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);

    jobs.solve();

    // TODO: handle locked pkgs
    g_assert (!jobs.hasLockedPackages());
    g_assert (jobs.count() != 0);

    // TODO: We need https://github.com/freebsd/pkg/issues/1271 to be fixed
    // to support "autoremove"

    if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE))
        return;

    pk_backend_job_set_status (job, PK_STATUS_ENUM_REMOVE);

    jobs.apply();

    pkgdb_compact (pkgDb.handle());
}

void
pk_backend_search_details (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
    pk_freebsd_search(job, filters, values);
}

void
pk_backend_search_files (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
    pk_freebsd_search(job, filters, values);
}

void
pk_backend_search_groups (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
    pk_freebsd_search(job, filters, values);
}

void
pk_backend_search_names (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
    pk_freebsd_search (job, filters, values);
}

void
pk_backend_update_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
    PKJobFinisher jf (job);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);

    PackageDatabase pkgDb (job, PKGDB_LOCK_ADVISORY, PKGDB_REMOTE);

    Jobs jobs (PKG_JOBS_UPGRADE, pkgDb.handle(), "update_packages");
    jobs << PKG_FLAG_PKG_VERSION_TEST;

    uint size = g_strv_length (package_ids);
    for (uint i = 0; i < size; i++) {
        PackageView pkg (package_ids[i]);
        gchar* pkg_namever = pkg.nameversion();

        jobs.add (MATCH_EXACT, &pkg_namever, 1);
    }

    if (jobs.solve() == 0)
        return;

    jobs.apply();
}

void
pk_backend_get_repo_list (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
    PKJobFinisher jf (job);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

    g_assert(!pkg_initialized());
    if (pkg_ini(NULL, NULL, PKG_INIT_FLAG_USE_IPV4) != EPKG_OK)
        g_error("get_repo_list: pkg_ini failure");

    pkg_repo* repo = NULL;
    while (pkg_repos(&repo) == EPKG_OK) {
        const gchar* id = pkg_repo_name (repo);
        const gchar* descr = pkg_repo_url (repo);
        gboolean enabled = pkg_repo_enabled (repo);

        pk_backend_job_repo_detail (job, id, descr, enabled);
    }

    pkg_shutdown();
}

void
pk_backend_repo_set_data (PkBackend *backend, PkBackendJob *job, const gchar *rid, const gchar *parameter, const gchar *value)
{
    pk_backend_job_set_status (job, PK_STATUS_ENUM_REQUEST);
    g_warning ("REPO '%s' PARAMETER '%s' TO '%s'", rid, parameter, value);
    pk_backend_job_finished (job);
    g_error("pk_backend_repo_set_data not implemented yet");
}

void
pk_backend_what_provides (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
    PkBackendFreeBSDJobData *job_data = reinterpret_cast<PkBackendFreeBSDJobData*> (pk_backend_job_get_user_data (job));
    priv->values = values;
    //job_data->signal_timeout = g_timeout_add (200, pk_backend_what_provides_timeout, job);
    priv->filters = filters;
    pk_backend_job_set_status (job, PK_STATUS_ENUM_REQUEST);
    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_percentage (job, 0);
    g_error("pk_backend_what_provides not implemented yet");
}

void
pk_backend_get_packages (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
    gchar* values[2] = { (gchar*)(".*"), NULL };
    pk_freebsd_search (job, filters, values);
}

void
pk_backend_download_packages (PkBackend *backend, PkBackendJob *job, gchar **package_ids, const gchar *directory0)
{
    PKJobFinisher jf (job);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);
    pk_backend_job_set_allow_cancel (job, TRUE);

    PackageDatabase pkgDb (job);

    std::string directory (directory0);
    std::string cacheDir = "/var/cache/pkg"; // TODO: query this from libpkg

    uint size = g_strv_length (package_ids);
    for (uint i = 0; i < size; i++) {
        Jobs jobs(PKG_JOBS_FETCH, pkgDb.handle(), "download_packages");

        // TODO: set reponame when libpkg start reporting it
        // if (reponame != NULL && pkg_jobs_set_repository(jobs, reponame) != EPKG_OK)
        // 	goto cleanup;

        if (!directory.empty()) {
            // This flag is required to convince libpkg to download
            // into an arbitrary directory
            jobs << PKG_FLAG_FETCH_MIRROR;
            jobs.setDestination(directory);
        }

        PackageView pkg (package_ids[i]);
        gchar* pkg_namever = pkg.nameversion();

        jobs.add(MATCH_EXACT, &pkg_namever, 1);

        if (jobs.solve() == 0)
            continue; // the package doesn't need to be downloaded

        jobs.apply();

        std::string filepath = directory.empty()
                             ? cacheDir + "/" + pkg_namever + ".pkg"
                             : directory + "/All/" + pkg_namever + ".pkg";

        gchar* files[] = {NULL, NULL};
        files[0] = const_cast<gchar*>(filepath.c_str());
        pk_backend_job_files(job, pkg.packageKitId(), files);

        if (pk_backend_job_is_cancelled (job))
            break;
    }
}

// TODO: Do we want "freebsd-update" support here?
// void
// pk_backend_upgrade_system (PkBackend *backend,
//                            PkBackendJob *job,
//                            PkBitfield transaction_flags,
//                            const gchar *distro_id,
//                            PkUpgradeKindEnum upgrade_kind)
// {
//     pk_backend_job_finished (job);
//     g_error("pk_backend_upgrade_system not implemented yet");
// }

void
pk_backend_repair_system (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags)
{
    pk_backend_job_finished (job);
    g_error("pk_backend_repair_system not implemented yet");
}

void
pk_backend_start_job (PkBackend *backend, PkBackendJob *job)
{
    PkBackendFreeBSDJobData *job_data;

    /* create private state for this job */
    job_data = g_new0 (PkBackendFreeBSDJobData, 1);
    job_data->progress_percentage = 0;
    job_data->cancellable = g_cancellable_new ();

    /* you can use pk_backend_job_error_code() here too */
    pk_backend_job_set_user_data (job, job_data);
}

void
pk_backend_stop_job (PkBackend *backend, PkBackendJob *job)
{
    PkBackendFreeBSDJobData *job_data = reinterpret_cast<PkBackendFreeBSDJobData*> (pk_backend_job_get_user_data (job));

    /* you *cannot* use pk_backend_job_error_code() here,
     * unless pk_backend_get_is_error_set() returns FALSE, and
     * even then it's probably just best to clean up silently */

    /* you cannot do pk_backend_job_finished() here as well as this is
     * needed to fire the job_stop() vfunc */
    g_object_unref (job_data->cancellable);

    /* destroy state for this job */
    g_free (job_data);
}

gboolean
pk_backend_supports_parallelization (PkBackend *backend)
{
    return FALSE;
}

const gchar *
pk_backend_get_description (PkBackend *backend)
{
    return "FreeBSD";
}

const gchar *
pk_backend_get_author (PkBackend *backend)
{
    return "Gleb Popov <arrowd@FreeBSD.org>";
}

static void
pk_freebsd_search(PkBackendJob *job, PkBitfield filters, gchar **values)
{
    PKJobFinisher jf (job);

    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

    // TODO: what can we possibly get in filters?
    // We ignore ~installed as there is no support in libpkg
    // We ignore arch for now
    if (! (filters == 0
        || pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED)
        || pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED)
        || pk_bitfield_contain(filters, PK_FILTER_ENUM_ARCH)
    )) {
        g_error("freebsd_search: unexpected filters %s", pk_filter_bitfield_to_string(filters));
    }

    guint size = g_strv_length (values);
    // TODO: take all values into account
    g_assert (size == 1);

    pkgdb_t db_type = PKGDB_MAYBE_REMOTE;
    // Open local DB only when filters require only installed packages
    // TODO: I don't like it
    if (pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED)
        && !pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED))
        db_type = PKGDB_DEFAULT;

    PackageDatabase pkgDb (job, PKGDB_LOCK_READONLY, db_type);

    struct pkgdb_it* it = NULL;
    struct pkg *pkg = NULL;
    match_t match_type = MATCH_REGEX;
    pkgdb_field searched_field = FIELD_NAMEVER;

    switch (pk_backend_job_get_role (job))
    {
    case PK_ROLE_ENUM_SEARCH_DETAILS:
        // TODO: can we search both?
        searched_field = FIELD_COMMENT;
        break;
    case PK_ROLE_ENUM_SEARCH_GROUP:
        match_type = MATCH_GLOB;
        searched_field = FIELD_ORIGIN;
        for (guint i = 0; i < size; i++) {
            // for each group create a glob in form "group/*"
            gchar* old_value = values[i];
            values[i] = g_strconcat (values[i], "/*", NULL);
            g_free (old_value);
        }
        break;
    case PK_ROLE_ENUM_SEARCH_FILE:
        // TODO: we don't support searching for packages that provide given file
        return;
    default: break;
    }

    // TODO: take filters into account
    // TODO: It isn't clear from libpkg documentation, but it seems that we
    // should use pkgdb_repo_* only on PKGDB_MAYBE_REMOTE repos
    if (db_type == PKGDB_DEFAULT)
        it = pkgdb_query (pkgDb.handle(), values[0], match_type);
    else
        it = pkgdb_repo_search (pkgDb.handle(), values[0], match_type, searched_field, FIELD_NAMEVER, NULL);

    while (pkgdb_it_next (it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
        std::optional<PkInfoEnum> typeOverride;
        // If we are operating on local DB then any package we get is installed
        // this is a bit of improvement for the libpkg deficiency described in backendJobPackageFromPkg()
        // TODO: remove this when backendJobPackageFromPkg's TODO is fixed
        if (db_type == PKGDB_DEFAULT)
            typeOverride = PK_INFO_ENUM_INSTALLED;

        backendJobPackageFromPkg (job, pkg, typeOverride);

        if (pk_backend_job_is_cancelled (job))
            break;
    }
}

static void backendJobPackageFromPkg (PkBackendJob *job, struct pkg* pkg, std::optional<PkInfoEnum> typeOverride) {
	// TODO: libpkg reports this incorrectly
	PkInfoEnum pk_type = pkg_type (pkg) == PKG_INSTALLED
						? PK_INFO_ENUM_INSTALLED
						: PK_INFO_ENUM_AVAILABLE;

	if (typeOverride.has_value())
		pk_type = typeOverride.value();

    PackageView pkgView(pkg);

	pk_backend_job_package (job, pk_type, pkgView.packageKitId(), pkgView.comment());
}
