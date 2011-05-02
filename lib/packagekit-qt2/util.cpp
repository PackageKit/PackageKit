/*
 * This file is part of the QPackageKit project
 * Copyright (C) 2008 Adrien Bustany <madcat@mymadcat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB. If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "util.h"
#include "package.h"

#include <QtCore/QStringList>

using namespace PackageKit;

QStringList Util::packageListToPids(const QList<Package> &packages)
{
    QStringList pids;
    foreach (const Package &package, packages) {
        pids << package.id();
    }

    return pids;
}

Transaction::InternalError Util::errorFromString(const QString &errorName)
{
    QString error = errorName;
    if (error.startsWith(QLatin1String("org.freedesktop.packagekit."))) {
        return Transaction::InternalErrorFailedAuth;
    }

    error.remove("org.freedesktop.PackageKit.Transaction.");

    if (error.startsWith(QLatin1String("PermissionDenied")) ||
        error.startsWith(QLatin1String("RefusedByPolicy"))) {
        return Transaction::InternalErrorFailedAuth;
    }

    if (error.startsWith(QLatin1String("PackageIdInvalid")) ||
        error.startsWith(QLatin1String("SearchInvalid")) ||
        error.startsWith(QLatin1String("FilterInvalid")) ||
        error.startsWith(QLatin1String("InvalidProvide")) ||
        error.startsWith(QLatin1String("InputInvalid"))) {
        return Transaction::InternalErrorInvalidInput;
    }

    if (error.startsWith(QLatin1String("PackInvalid")) ||
        error.startsWith(QLatin1String("NoSuchFile")) ||
        error.startsWith(QLatin1String("NoSuchDirectory"))) {
        return Transaction::InternalErrorInvalidFile;
    }

    if (error.startsWith(QLatin1String("NotSupported"))) {
        return Transaction::InternalErrorFunctionNotSupported;
    }

    return Transaction::InternalErrorFailed;
}
