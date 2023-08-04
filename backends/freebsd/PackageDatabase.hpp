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
        // TODO: After implementing a proper ref counting, we can probably allow
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

    pkgdb* handle() {
        if (!dbHandle)
            open();
        return dbHandle;
    }

    void setEventHandler(std::function<void(pkg_event *ev)> handler) { userEventHandler = handler; }

    ~PackageDatabase () {
        dbHandle = nullptr;
    }

private:
    static int pkgEventHandler(void* data, pkg_event *ev) {
        PackageDatabase* pkgDB = reinterpret_cast<PackageDatabase*>(data);

        if (pkgDB->userEventHandler)
            pkgDB->userEventHandler(ev);

        return event_callback(nullptr, ev);
    }

    void open() {
        // TODO: call pkgdb_access here?

        if (pkgdb_open (&dbHandle, dbType) != EPKG_OK)
            g_error("pkgdb_open failed"); // TODO: this kills whole daemon, maybe this is too much?
        dbDeleter = deleted_unique_ptr<struct pkgdb>(dbHandle, [](pkgdb* dbHandle) {pkgdb_close (dbHandle); });

        if (pkgdb_obtain_lock(dbHandle, lockType) != EPKG_OK)
            g_error("Cannot get a lock on the database, it is locked by another process");

        if (lockType != PKGDB_LOCK_READONLY)
            pk_backend_job_set_locked (job, TRUE);

        lockDeleter = deleted_unique_ptr<struct pkgdb>(dbHandle, [this](pkgdb* dbHandle) {
            pkgdb_release_lock (dbHandle, lockType);
            if (lockType != PKGDB_LOCK_READONLY)
                pk_backend_job_set_locked (job, FALSE);
        });
    }
    PkBackendJob* job;
    pkgdb_lock_t lockType;
    pkgdb_t dbType;
    pkgdb* dbHandle;
    std::function<void(pkg_event *ev)> userEventHandler;
    deleted_unique_ptr<void> libpkgDeleter;
    deleted_unique_ptr<struct pkgdb> dbDeleter;
    deleted_unique_ptr<struct pkgdb> lockDeleter;
};
