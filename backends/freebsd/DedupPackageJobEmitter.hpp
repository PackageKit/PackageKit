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

#include <string>
#include <unordered_set>

#include <pkg.h>
#include <pk-backend.h>

#include "PackageView.hpp"

class DedupPackageJobEmitter
{
public:
    DedupPackageJobEmitter(PkBackendJob* _job) : job(_job) {}

    void emitPackageJob(struct pkg* pkg, std::optional<PkInfoEnum> typeOverride = std::nullopt) {
        PackageView pkgView(pkg);

        std::string packageKitId = pkgView.packageKitId();

        if (alreadyEmitted.count (packageKitId))
            return;

        PkInfoEnum pk_type = pkg_type (pkg) == PKG_INSTALLED
                            ? PK_INFO_ENUM_INSTALLED
                            : PK_INFO_ENUM_AVAILABLE;
        if (typeOverride.has_value())
            pk_type = typeOverride.value();

        pk_backend_job_package (job, pk_type, packageKitId.c_str(), pkgView.comment());

        alreadyEmitted.insert(packageKitId);
    }

    void markAsEmitted(struct pkg* pkg) {
        PackageView pkgView(pkg);
        alreadyEmitted.insert(pkgView.packageKitId());
    }

private:
    std::unordered_set<std::string> alreadyEmitted;
    PkBackendJob* job;
};
