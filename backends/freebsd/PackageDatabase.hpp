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

#include <functional>
#include <memory>

#include <pk-backend.h>
#include <pkg.h>

#include "Deleters.hpp"

class PackageDatabase {
public:
    PackageDatabase (PkBackendJob* _job,
                     pkgdb_lock_t _lockType = PKGDB_LOCK_READONLY,
                     pkgdb_t _dbType = PKGDB_MAYBE_REMOTE)
    : job(_job), lockType(_lockType), dbType(_dbType), dbHandle(nullptr) {
        // GOS-395: After implementing a proper ref counting, we can probably allow
        // pk_backend_supports_parallelization() to return true, so that multiple
        // jobs can be executed concurrently.
        // If we go this route remember to adapt pk_backend_get_repo_list() and pk_backend_refresh_cache()
        // which don't use PackageDatabase.
        // For now initialize and deinitialize libpkg on each call.
        g_assert(!pkg_initialized());

        pkg_event_register(&pkgEventHandler, this);

        if (pkg_ini(NULL, NULL, PKG_INIT_FLAG_USE_IPV4) != EPKG_OK)
            g_error("pkg_ini failure");
        // can't pass nullptr here, unique_ptr won't call the deleter
        libpkgDeleter = deleted_unique_ptr<void>(reinterpret_cast<void*>(0xDEADC0DE), [](void* p) { pkg_shutdown(); });
    }
    ~PackageDatabase() {
        for (auto* cb : cleanupCallbacks)
            delete cb;
    }

    pkgdb* handle() {
        if (!dbHandle)
            open();
        return dbHandle;
    }

    void setEventHandler(std::function<int(pkg_event *ev)> handler) { userEventHandler = handler; }

private:
    static int pkgEventHandler(void* data, pkg_event *ev) {
        PackageDatabase* pkgDB = reinterpret_cast<PackageDatabase*>(data);

        // if the event is purely informational just log it and return
        if (handleInformationalEvents(ev))
            return 0;

        // use default libpkg implementations for sandboxed calls
        if (ev->type == PKG_EVENT_SANDBOX_CALL)
            return pkg_handle_sandboxed_call(ev->e_sandbox_call.call,
                                             ev->e_sandbox_call.fd,
                                             ev->e_sandbox_call.userdata);
        else if (ev->type == PKG_EVENT_SANDBOX_GET_STRING)
            return pkg_handle_sandboxed_get_string(ev->e_sandbox_call_str.call,
                                                   ev->e_sandbox_call_str.result,
                                                   ev->e_sandbox_call_str.len,
                                                   ev->e_sandbox_call_str.userdata);
        // handle cleanup callbacks
        else if (ev->type == PKG_EVENT_CLEANUP_CALLBACK_REGISTER) {
            auto* cleanup = new cleanup_cb;
            cleanup->cb = ev->e_cleanup_callback.cleanup_cb;
            cleanup->data = ev->e_cleanup_callback.data;
            pkgDB->cleanupCallbacks.push_back(cleanup);
            return 0;
        }
        else if (ev->type == PKG_EVENT_CLEANUP_CALLBACK_UNREGISTER) {
            auto it = std::find_if(
                pkgDB->cleanupCallbacks.begin(),
                pkgDB->cleanupCallbacks.end(),
                [ev](cleanup_cb* cleanup) {
                    return cleanup->cb == ev->e_cleanup_callback.cleanup_cb
                        && cleanup->data == ev->e_cleanup_callback.data;
            });

            if (it != pkgDB->cleanupCallbacks.end()) {
                delete *it;
                pkgDB->cleanupCallbacks.erase(it);
            }
        }

        // otherwise pass the event for the job's handling
        if (pkgDB->userEventHandler) {
            bool shouldCancel = pkgDB->userEventHandler(ev);
            if (shouldCancel) {
                for (auto* cleanup : pkgDB->cleanupCallbacks)
                    cleanup->cb(cleanup->data);
                return 1;
            }
        }

        return 0;
    }

    static bool handleInformationalEvents(pkg_event *ev) {
        switch(ev->type) {
            case PKG_EVENT_ERRNO:
                g_warning("libpkg: %s(%s): %s", ev->e_errno.func, ev->e_errno.arg,
                          strerror(ev->e_errno.no));
                return true;
            case PKG_EVENT_ERROR:
                g_warning("libpkg: %s", ev->e_pkg_error.msg);
                return true;
            case PKG_EVENT_INTEGRITYCHECK_BEGIN:
                g_message("libpkg: Checking integrity...");
                return true;
            case PKG_EVENT_INTEGRITYCHECK_FINISHED:
                g_message("libpkg: done checking integrity (%d conflicting)", ev->e_integrity_finished.conflicting);
                return true;
            case PKG_EVENT_INTEGRITYCHECK_CONFLICT:
            {
                g_warning("libpkg: Conflict found on path '%s' between '%s' and ...",
                          ev->e_integrity_conflict.pkg_path,
                          ev->e_integrity_conflict.pkg_uid);
                pkg_event_conflict* cur_conflict = ev->e_integrity_conflict.conflicts;
                while (cur_conflict) {
                    g_warning("libpkg: '%s'", cur_conflict->uid);
                    cur_conflict = cur_conflict->next;
                }
                return true;
            }
            case PKG_EVENT_LOCKED:
            {
                PackageView pkgView(ev->e_locked.pkg);
                g_warning("libpkg: '%s' is locked and may not be modified", pkgView.nameversion());
                return true;
            }
            case PKG_EVENT_REQUIRED:
            {
                PackageView pkgView(ev->e_required.pkg);
                g_warning("libpkg: '%s' is required by other packages", pkgView.nameversion());
                return true;
            }
            case PKG_EVENT_NOT_FOUND:
                // this probably should never happen
                g_warning("libpkg: '%s' was not found in the repositories",
                          ev->e_not_found.pkg_name);
                return true;
            case PKG_EVENT_MISSING_DEP:
                // this probably should never happen
                g_warning("libpkg: Missing dependency '%s'",
                          pkg_dep_name(ev->e_missing_dep.dep));
                return true;
            case PKG_EVENT_NOREMOTEDB:
                g_warning("libpkg: Unable to open remote database %s",
                          ev->e_remotedb.repo);
                return true;
            case PKG_EVENT_NOLOCALDB:
                g_warning("libpkg: Local package database does not exist");
                return true;
            case PKG_EVENT_NEWPKGVERSION:
                g_warning("libpkg: New version of pkg detected; it needs to be installed first");
                return true;
            case PKG_EVENT_FILE_MISMATCH:
            {
                PackageView pkgView(ev->e_file_mismatch.pkg);
                g_warning("libpkg: '%s': checksum mismatch", pkgView.nameversion());
                return true;
            }
            case PKG_EVENT_FILE_MISSING:
            {
                PackageView pkgView(ev->e_file_missing.pkg);
                g_warning("libpkg: '%s': missing some files", pkgView.nameversion());
                return true;
            }
            case PKG_EVENT_PLUGIN_ERRNO:
                g_warning("libpkg: '%s' plugin: %s(%s): %s",
                          pkg_plugin_get(ev->e_plugin_errno.plugin, PKG_PLUGIN_NAME),
                          ev->e_plugin_errno.func, ev->e_plugin_errno.arg,
                          strerror(ev->e_plugin_errno.no));
                return true;
            case PKG_EVENT_PLUGIN_ERROR:
                g_warning("libpkg: '%s' plugin: %s",
                          pkg_plugin_get(ev->e_plugin_error.plugin, PKG_PLUGIN_NAME),
                          ev->e_plugin_error.msg);
                return true;
            case PKG_EVENT_INCREMENTAL_UPDATE:
                g_message("libpkg: %s repository update completed. %d packages processed.\n",
                    ev->e_incremental_update.reponame,
                    ev->e_incremental_update.processed);
                return true;
            case PKG_EVENT_QUERY_YESNO:
                // this should never happen, so use g_error
                g_error("libpkg: asking for yes/no");
                return true;
            case PKG_EVENT_QUERY_SELECT:
                // this should never happen, so use g_error
                g_error("libpkg: queries for selection");
                return true;
            case PKG_EVENT_TRIGGER:
                if (ev->e_trigger.cleanup)
                    g_message("libpkg: cleaning up trigger %s", ev->e_trigger.name);
                else
                    g_message("libpkg: running trigger %s", ev->e_trigger.name);
                return true;
            case PKG_EVENT_BACKUP:
                g_message("libpkg: backing up");
                return true;
            case PKG_EVENT_RESTORE:
                g_message("libpkg: restoring");
                return true;
            case PKG_EVENT_MESSAGE:
                g_message("libpkg: %s", ev->e_pkg_message.msg);
                return true;
            default:
                return false;
        }
    }

    void open() {
        // TODO: call pkgdb_access here?

        if (pkgdb_open (&dbHandle, dbType) != EPKG_OK)
            g_error("pkgdb_open failed"); // TODO: this kills whole daemon, maybe this is too much?
        dbDeleter = deleted_unique_ptr<struct pkgdb>(dbHandle, [](pkgdb* dbHandle) {pkgdb_close (dbHandle); });

        while (pkgdb_obtain_lock(dbHandle, lockType) != EPKG_OK) {
            g_warning("Cannot get a lock on the database, it is locked by another process");
        }

        if (lockType != PKGDB_LOCK_READONLY)
            pk_backend_job_set_locked (job, TRUE);

        lockDeleter = deleted_unique_ptr<struct pkgdb>(dbHandle, [this](pkgdb* dbHandle) {
            pkgdb_release_lock (dbHandle, lockType);
            if (lockType != PKGDB_LOCK_READONLY)
                pk_backend_job_set_locked (job, FALSE);
        });
    }

    struct cleanup_cb
    {
        void *data;
        void (*cb)(void *);
    };

    PkBackendJob* job;
    pkgdb_lock_t lockType;
    pkgdb_t dbType;
    pkgdb* dbHandle;
    std::function<int(pkg_event *ev)> userEventHandler;
    std::vector<cleanup_cb*> cleanupCallbacks;
    deleted_unique_ptr<void> libpkgDeleter;
    deleted_unique_ptr<struct pkgdb> dbDeleter;
    deleted_unique_ptr<struct pkgdb> lockDeleter;
};
