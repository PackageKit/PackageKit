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

#include <functional>
#include <optional>
#include <memory>
#include <vector>

#include "stolen.h"
#include "PackageView.hpp"
#include "PackageDatabase.hpp"
#include "PKJobFinisher.hpp"

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
    g_error("pk_backend_depends_on not implemented yet");
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
    g_error("pk_backend_get_details not implemented yet");
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

    pkg_flags jobs_flags = static_cast<pkg_flags> (
        PKG_FLAG_NONE
        | PKG_FLAG_PKG_VERSION_TEST
        | PKG_FLAG_DRY_RUN);

    guint size = g_strv_length (package_ids);
    for (guint i = 0; i < size; i++) {
        // in this approach we call `pkg upgrade <pkg>` in a loop and present
        // the user an upgrading plan for each invocation
        // an alternative approach is to make only one call `pkg upgrade <pkg1> <pkg2> ...`
        // and output a combined plan. TODO: decide which way is better
        gchar* package_id = package_ids[i];

        // TODO: deduplicate this with similar code in pk_packend_download()
        gchar** package_id_parts = pk_package_id_split (package_id);
        gchar* package_namever = g_strconcat(package_id_parts[PK_PACKAGE_ID_NAME], "-", package_id_parts[PK_PACKAGE_ID_VERSION], NULL);
        g_strfreev(package_id_parts);
        // TODO: we leak package_namever here

        struct pkg_jobs	*jobs = NULL;
        if (pkg_jobs_new(&jobs, PKG_JOBS_UPGRADE, pkgDb.handle()) != EPKG_OK)
            g_error("update_detail: pkg_jobs_new failed");
        auto jobsDeleter = deleted_unique_ptr<struct pkg_jobs>(jobs, [](struct pkg_jobs* jobs) { pkg_jobs_free(jobs);});

        if (pkg_jobs_add(jobs, MATCH_GLOB, &package_namever, 1) == EPKG_FATAL)
            g_error("update_detail: pkg_jobs_add failed");

        pkg_jobs_set_flags(jobs, jobs_flags);

        // TODO: handle reponame?

        if (pkg_jobs_solve(jobs) != EPKG_OK)
            g_error("update_detail: pkg_jobs_solve failed");

        if (pkg_jobs_count(jobs) == 0)
            return;

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

        void *iter = NULL;
        struct pkg *new_pkg, *old_pkg;
        int type;
        while (pkg_jobs_iter(jobs, &iter, &new_pkg, &old_pkg, &type)) {
            PackageView oldPkg(old_pkg);
            switch (type) {
            case PKG_SOLVED_UPGRADE_REMOVE:
                obsoletes.push_back(g_strdup(oldPkg.packageKitId()));
                break;
            default:
                updates.push_back(g_strdup(oldPkg.packageKitId()));
                break;
            }
        }

        updates.push_back(nullptr);
        obsoletes.push_back(nullptr);

        pk_backend_job_update_detail (job, package_id,
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

        g_strfreev (updates.data());
        g_strfreev (obsoletes.data());
    }
}

void
pk_backend_get_updates (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
    PKJobFinisher jf (job);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

    // TODO: what filters could we possibly get there?
    g_assert (filters == 0);

    PackageDatabase pkgDb (job);

    struct pkg_jobs	*jobs = NULL;
    pkg_flags jobs_flags = static_cast<pkg_flags> (
        PKG_FLAG_NONE
        | PKG_FLAG_PKG_VERSION_TEST
        | PKG_FLAG_DRY_RUN);

    if (pkg_jobs_new(&jobs, PKG_JOBS_UPGRADE, pkgDb.handle()) != EPKG_OK)
        g_error("pkg_jobs_new failed");

    auto jobsDeleter = deleted_unique_ptr<struct pkg_jobs>(jobs, [](struct pkg_jobs* jobs) { pkg_jobs_free(jobs); });

    pkg_jobs_set_flags(jobs, jobs_flags);

    // TODO: handle reponame?

    if (pkg_jobs_solve(jobs) != EPKG_OK)
        g_error("pkg_jobs_solve failed");

    if (pkg_jobs_count(jobs) == 0)
        return;

    void *iter = NULL;
    struct pkg *new_pkg, *old_pkg;
    int type;
    while (pkg_jobs_iter(jobs, &iter, &new_pkg, &old_pkg, &type)) {
        // Do not report packages that will be removed by the upgrade
        if (type == PKG_SOLVED_UPGRADE_REMOVE)
            continue;
        backendJobPackageFromPkg (job, new_pkg, PK_INFO_ENUM_NORMAL);
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

    struct pkg_jobs	*jobs = NULL;
    pkg_flags	 jobs_flags = static_cast<pkg_flags> (
        PKG_FLAG_NONE | PKG_FLAG_PKG_VERSION_TEST);

    if (pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD))
        jobs_flags = static_cast<pkg_flags>(jobs_flags | PKG_FLAG_SKIP_INSTALL);

    if (pkg_jobs_new(&jobs, PKG_JOBS_INSTALL, pkgDb.handle()) != EPKG_OK) {
        g_error("install_packages: pkg_jobs_new failed");
        return;
    }
    auto jobsDeleter = deleted_unique_ptr<struct pkg_jobs>(jobs, [](struct pkg_jobs* jobs) { pkg_jobs_free(jobs); });

    pkg_jobs_set_flags(jobs, jobs_flags);

    guint size = g_strv_length (package_ids);
    std::vector<gchar*> names;
    names.reserve (size);
    for (guint i = 0; i < size; i++) {
        // in this approach we call `pkg upgrade <pkg>` in a loop and present
        // the user an upgrading plan for each invocation
        // an alternative approach is to make only one call `pkg upgrade <pkg1> <pkg2> ...`
        // and output a combined plan. TODO: decide which way is better
        gchar* package_id = package_ids[i];

        // TODO: deduplicate this with similar code in pk_packend_download()
        gchar** package_id_parts = pk_package_id_split (package_id);
        gchar* package_namever = g_strconcat(package_id_parts[PK_PACKAGE_ID_NAME], "-", package_id_parts[PK_PACKAGE_ID_VERSION], NULL);
        names.push_back(package_namever);

        g_strfreev(package_id_parts);
        // TODO: we leak package_namever here
    }

    if (pkg_jobs_add(jobs, MATCH_GLOB, names.data(), size) != EPKG_OK)
        g_error ("install_packages: pkg_jobs_add failed");

    pk_backend_job_set_status (job, PK_STATUS_ENUM_DEP_RESOLVE);

    if (pkg_jobs_solve(jobs) != EPKG_OK)
        g_error ("install_packages: pkg_jobs_solve failed");

    if (pkg_jobs_count(jobs) == 0)
        return;

    void *iter = NULL;
    struct pkg *new_pkg, *old_pkg;
    int type;
    while (pkg_jobs_iter(jobs, &iter, &new_pkg, &old_pkg, &type)) {
        if (type == PKG_SOLVED_DELETE) {
            g_warning ("install_packages: have to remove some packages");
            continue;
        }

        backendJobPackageFromPkg (job, new_pkg, PK_INFO_ENUM_NORMAL);
    }

    if (pk_bitfield_contain(transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE))
        return;

    pk_backend_job_set_status (job, PK_STATUS_ENUM_INSTALL);

    int retcode;
    do {
        retcode = pkg_jobs_apply(jobs);
        if (retcode == EPKG_CONFLICT) {
            g_warning("Conflicts with the existing packages "
                        "have been found. One more solver "
                        "iteration is needed to resolve them.");
            continue;
        }
        else if (retcode != EPKG_OK) {
            g_error ("install_packages: pkg_jobs_apply failed");
        }
    } while (retcode == EPKG_CONFLICT);
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
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

    guint size = g_strv_length (packages);
    std::vector<gchar*> names;
    names.reserve(size);
    for (guint i = 0; i < size; i++) {
        gchar* pkg = packages[i];

        if (pk_package_id_check(pkg) == false) {
            // if pkgid isn't a valid package ID, treat it as regexp suitable for pk_freebsd_search
            names.push_back(pkg);
            // TODO: maybe search by glob there?
        }
        else {
            // TODO: deduplicate this with similar code in pk_packend_download()
            gchar** package_id_parts = pk_package_id_split (pkg);
            gchar* package_namever = g_strconcat(package_id_parts[PK_PACKAGE_ID_NAME], "-", package_id_parts[PK_PACKAGE_ID_VERSION], NULL);
            names.push_back(package_namever);
            g_strfreev(package_id_parts);
            // TODO: we leak package_namever here
        }
    }
    // Null-terminator for GLib-style arrays
    names.push_back (nullptr);
    pk_freebsd_search (job, filters, names.data());
}

void
pk_backend_remove_packages (PkBackend *backend, PkBackendJob *job,
                            PkBitfield transaction_flags,
                            gchar **package_ids,
                            gboolean allow_deps,
                            gboolean autoremove)
{
    g_error("pk_backend_remove_packages not implemented yet");
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
    g_error("pk_backend_update_packages not implemented yet");
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
pk_backend_download_packages (PkBackend *backend, PkBackendJob *job, gchar **package_ids, const gchar *directory)
{
    pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);

    withRepo(PKGDB_LOCK_READONLY, job, [package_ids, directory, job](struct pkgdb* db) {
        struct pkg_jobs *jobs = NULL;
        pkg_flags pkg_flags = PKG_FLAG_NONE;
        const gchar *cache_dir = "/var/cache/pkg"; // TODO: query this from libpkg
        const gchar *package_id;
        gchar** package_id_parts;
        gchar* package_namever;
        uint i, size;
        // This is required to convince libpkg to download into an arbitrary directory
        if (directory != NULL)
            pkg_flags = PKG_FLAG_FETCH_MIRROR;

        size = g_strv_length (package_ids);
        for (i = 0; i < size; i++) {
            bool need_break = 0;
            const gchar* file, *filename;
            gchar* files[] = {NULL, NULL};

            if (pkg_jobs_new(&jobs, PKG_JOBS_FETCH, db) != EPKG_OK) {
                g_error("pkg_jobs_new failed");
                return;
            }
            auto jobsDeleter = deleted_unique_ptr<struct pkg_jobs>(jobs, [](struct pkg_jobs* jobs) { pkg_jobs_free(jobs); });

            // TODO: set reponame when libpkg start reporting it
            // if (reponame != NULL && pkg_jobs_set_repository(jobs, reponame) != EPKG_OK)
            // 	goto cleanup;

            if (directory != NULL && pkg_jobs_set_destdir(jobs, directory) != EPKG_OK) {
                g_error("pkg_jobs_set_destdir failed for %s", directory);
                goto exit4;
            }

            pkg_jobs_set_flags(jobs, pkg_flags);

            package_id = package_ids[i];
            package_id_parts = pk_package_id_split (package_id);
            package_namever = g_strconcat(package_id_parts[PK_PACKAGE_ID_NAME], "-", package_id_parts[PK_PACKAGE_ID_VERSION], NULL);

            if (pkg_jobs_add(jobs, MATCH_GLOB, &package_namever, 1) != EPKG_OK) {
                g_error("pkg_jobs_add failed for %s", package_id);
                need_break = 1;
                goto exit4;
            }

            if (pkg_jobs_solve(jobs) != EPKG_OK) {
                g_error("pkg_jobs_solve failed");
                need_break = 1;
                goto exit4;
            }

            if (pkg_jobs_count(jobs) == 0)
                goto exit4;

            if (pkg_jobs_apply(jobs) != EPKG_OK) {
                g_error("pkg_jobs_apply failed");
                need_break = 1;
                goto exit4;
            }

            filename = g_strconcat(package_namever, ".pkg", NULL);
            file = directory == NULL
                ? g_build_path("/", cache_dir, filename, NULL)
                : g_build_path("/", directory, "All", filename, NULL);

            files[0] = (gchar*)file;
            pk_backend_job_files(job, package_id, files);

            g_free((gchar*)filename);
            g_free((gchar*)file);
    exit4:
            g_free(package_namever);
            g_strfreev(package_id_parts);
            if (need_break)
                break;
        }
    });

    pk_backend_job_finished (job);
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
    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

    //g_error(pk_filter_bitfield_to_string(filters));

    // TODO: what can we possible get in filters?
    // We ignore ~installed as there is no support in libpkg
    if (! (filters == 0 || pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED))) {
        g_error(pk_filter_bitfield_to_string(filters));
    }

    withRepo(PKGDB_LOCK_READONLY, job, [values, job](struct pkgdb* db) {
        struct pkgdb_it* it = NULL;
        struct pkg *pkg = NULL;
        pkgdb_field searched_field = FIELD_NAMEVER;

        switch (pk_backend_job_get_role (job))
        {
        case PK_ROLE_ENUM_SEARCH_DETAILS:
            // TODO: can we search both?
            searched_field = FIELD_COMMENT;
            break;
        case PK_ROLE_ENUM_SEARCH_GROUP:
            // TODO: this is the best approximation for non-existing FIELD_CATEGORIES
            searched_field = FIELD_ORIGIN;
            break;
        case PK_ROLE_ENUM_SEARCH_FILE:
            // TODO: we don't support searching for packages that provide given file
            return;
        default: break;
        }
        // TODO: take filters and all values into account
        it = pkgdb_repo_search (db, values[0], MATCH_REGEX, searched_field, FIELD_NAMEVER, NULL);

        while (pkgdb_it_next (it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
			backendJobPackageFromPkg (job, pkg, std::nullopt);

			if (pk_backend_job_is_cancelled (job))
				break;
		}
	});

	pk_backend_job_finished(job);
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
