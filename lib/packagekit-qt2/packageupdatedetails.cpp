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

#include <QtCore/QDebug>

#include "packageupdatedetails.h"

using namespace PackageKit;

PackageUpdateDetails::PackageUpdateDetails(const QString &package_id,
                                           const QStringList &updates,
                                           const QStringList &obsoletes,
                                           const QStringList &vendor_urls,
                                           const QStringList &bugzilla_urls,
                                           const QStringList &cve_urls,
                                           uint restart,
                                           const QString &update_text,
                                           const QString &changelog,
                                           uint state,
                                           const QDateTime &issued,
                                           const QDateTime &updated) :
    Package(package_id),
    d(new PackageUpdateDetailsPrivate)
{
    d->updates = updates;
    d->obsoletes = obsoletes;
    d->vendor_urls = vendor_urls;
    d->bugzilla_urls = bugzilla_urls;
    d->cve_urls = cve_urls;
    d->restart = restart;
    d->update_text = update_text;
    d->changelog = changelog;
    d->state = state;
    d->issued = issued;
    d->updated = updated;
}

PackageUpdateDetails::PackageUpdateDetails() :
    d(new PackageUpdateDetailsPrivate)
{
}

PackageUpdateDetails::PackageUpdateDetails(const PackageUpdateDetails &other) :
    d(other.d)
{
    *this = other;
}

PackageUpdateDetails::~PackageUpdateDetails()
{
}

PackageList PackageUpdateDetails::updates() const
{
    PackageList ret;
    foreach (const QString &package_id, d->updates) {
        ret << Package(package_id);
    }
    return ret;
}

PackageList PackageUpdateDetails::obsoletes() const
{
    PackageList ret;
    foreach (const QString &package_id, d->obsoletes) {
        ret << Package(package_id);
    }
    return ret;
}

QStringList PackageUpdateDetails::vendorUrls() const
{
    return d->vendor_urls;
}

QStringList PackageUpdateDetails::bugzillaUrls() const
{
    return d->bugzilla_urls;
}

QStringList PackageUpdateDetails::cveUrls() const
{
    return d->cve_urls;
}

PackageUpdateDetails::Restart PackageUpdateDetails::restart() const
{
    return static_cast<PackageUpdateDetails::Restart>(d->restart);
}

QString PackageUpdateDetails::updateText() const
{
    return d->update_text;
}

QString PackageUpdateDetails::changelog() const
{
    return d->changelog;
}

PackageUpdateDetails::UpdateState PackageUpdateDetails::state() const
{
    return static_cast<PackageUpdateDetails::UpdateState>(d->state);
}

QDateTime PackageUpdateDetails::issued() const
{
    return d->issued;
}

QDateTime PackageUpdateDetails::updated() const
{
    return d->updated;
}
