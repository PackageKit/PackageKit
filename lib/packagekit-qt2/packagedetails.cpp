/***************************************************************************
 *   Copyright (C) 2012 by Daniel Nicoletti                                *
 *   dantti12@gmail.com                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; see the file COPYING. If not, write to       *
 *   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,  *
 *   Boston, MA 02110-1301, USA.                                           *
 ***************************************************************************/

#include "packagedetails.h"

#include <QtCore/QDebug>

using namespace PackageKit;

PackageDetails::PackageDetails(const QString &package_id, const QString &license, uint group, const QString &detail, const QString &url, qulonglong size) :
Package(package_id),
    d(new PackageDetailsPrivate)
{
    d->license = license;
    d->group = group;
    d->detail = detail;
    d->url = url;
    d->size = size;
}

PackageDetails::PackageDetails() :
    d(new PackageDetailsPrivate)
{
    d->group = GroupUnknown;
    d->size = 0;
}

PackageDetails::PackageDetails(const PackageDetails &other) :
    Package(other),
    d(other.d)
{
}

PackageDetails::~PackageDetails()
{
}

QString PackageDetails::license() const
{
    return d->license;
}

PackageDetails::Group PackageDetails::group() const
{
    return static_cast<PackageDetails::Group>(d->group);
}

QString PackageDetails::detail() const
{
    return d->detail;
}

QString PackageDetails::url() const
{
    return d->url;
}

qulonglong PackageDetails::size() const
{
    return d->size;
}

#include "packagedetails.moc"
