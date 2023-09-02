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

#include <pkg.h>
#include "PackageView.hpp"

class Jobs {
public:
    class iterator {
        friend class Jobs;
    public:
        bool operator==(const iterator& other) const {
            return pkgIter == other.pkgIter;
        }
        bool operator!=(const iterator& other) const {
            return pkgIter != other.pkgIter;
        }

        iterator& operator++() {
            if (!pkg_jobs_iter(jobsHandle, &pkgIter, &newPkg, &oldPkg, &type))
                pkgIter = nullptr;
            return *this;
        }

        PackageView oldPkgView() const { return oldPkg; }
        PackageView newPkgView() const { return newPkg; }
        pkg* oldPkgHandle() const { return oldPkg; }
        pkg* newPkgHandle() const { return newPkg; }
        int itemType() const { return type; }
    private:
        pkg_jobs* jobsHandle;
        void* pkgIter = nullptr;
        pkg* oldPkg;
        pkg* newPkg;
        int type;
    };

    Jobs(pkg_jobs_t jobsType, pkgdb* dbHandle, const char* _context = "")
    : context(_context), jobsHandle(nullptr), jobsFlags(PKG_FLAG_NONE) {
        if (pkg_jobs_new(&jobsHandle, jobsType, dbHandle) != EPKG_OK)
            g_error("%s: pkg_jobs_new failed", context);
    }

    Jobs& operator<<(pkg_flags flag) {
        jobsFlags = static_cast<pkg_flags> (jobsFlags | flag);
        return *this;
    }

    void setFlags(pkg_flags flags) { jobsFlags = flags; }

    void setDestination(const std::string& dest) const { setDestination(dest.c_str()); }
    void setDestination(const char* dest) const {
        if (pkg_jobs_set_destdir(jobsHandle, dest) != EPKG_OK)
            g_error("%s: pkg_jobs_add failed", context);
    }

    void add(match_t matchType, char **argv, int argc) {
        if (pkg_jobs_add(jobsHandle, matchType, argv, argc) == EPKG_FATAL)
            g_error("%s: pkg_jobs_add failed", context);
    }

    void add(match_t matchType, std::vector<char*> argv) {
        if (pkg_jobs_add(jobsHandle, matchType, argv.data(), argv.size()) == EPKG_FATAL)
            g_error("%s: pkg_jobs_add failed", context);
    }

    int solve() {
        pkg_jobs_set_flags(jobsHandle, jobsFlags);

        if (pkg_jobs_solve(jobsHandle) != EPKG_OK)
            g_warning("%s: pkg_jobs_solve failed", context);

        jobsCount = pkg_jobs_count(jobsHandle);

        return jobsCount;
    }

    int count() const { return jobsCount; }

    bool hasLockedPackages() const { return pkg_jobs_has_lockedpkgs(jobsHandle); }

    bool apply() {
        int retcode;
        do {
            retcode = pkg_jobs_apply(jobsHandle);
            if (retcode == EPKG_CONFLICT) {
                g_warning("Conflicts with the existing packages "
                            "have been found. One more solver "
                            "iteration is needed to resolve them.");
            }
            else if (retcode == EPKG_CANCEL) {
                g_message ("%s: pkg_jobs_apply cancelled", context);
                return true;
            }
            else if (retcode != EPKG_OK) {
                // libpkg doesn't yet return sensible error codes from pkg_jobs_apply
                g_warning ("%s: pkg_jobs_apply failed", context);
                return false;
            }
        } while (retcode != EPKG_OK);

        return true;
    }

    iterator begin() {
        void *pkgIter = nullptr;
        pkg *newPkg = nullptr, *oldPkg = nullptr;
        int type;
        if (pkg_jobs_iter(jobsHandle, &pkgIter, &newPkg, &oldPkg, &type)) {
            // TODO: I find it stupid that an iterator stores so much information
            iterator ret;
            ret.jobsHandle = jobsHandle;
            ret.pkgIter = pkgIter;
            ret.oldPkg = oldPkg;
            ret.newPkg = newPkg;
            ret.type = type;
            return ret;
        }
        else return end();
    }

    iterator end() { return iterator(); }

    ~Jobs() {
        pkg_jobs_free(jobsHandle);
    }
private:
    const char* context;
    pkg_jobs* jobsHandle;
    pkg_flags jobsFlags;
    int jobsCount;
};
