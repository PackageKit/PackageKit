/*
 * This file is part of the QPackageKit project
 * Copyright (C) 2008 Adrien Bustany <madcat@mymadcat.com>
 * Copyright (C) 2010-2011 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
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

#include "package.h"

#include <QtSql/QSqlQuery>
#include <QtCore/QDebug>
#include <QtCore/QFile>

using namespace PackageKit;

Package::Package(const QString &packageId, Info info, const QString &summary) :
    d(new PackagePrivate)
{
    d->id = packageId;
    d->info = info;
    d->summary = summary;
}

Package::Package() :
    d(new PackagePrivate)
{
    d->info = InfoUnknown;
}

Package::Package(const Package &other) :
    d(other.d)
{
}

Package::~Package()
{
}

bool Package::isValid() const
{
    int sepCount = d->id.count(QLatin1Char(';'));
    if (sepCount == 3) {
        // A valid pk-id "name;version;arch;data"
        return true;
    } else if (sepCount == 0) {
        // its also valid just "name"
        return !d->id.isEmpty();
    }
    return false;
}

QString Package::id() const
{
    return d->id;
}

QString Package::name() const
{
    return d->id.section(QLatin1Char(';'), 0, 0);
}

QString Package::version() const
{
    return d->id.section(QLatin1Char(';'), 1, 1);
}

QString Package::arch() const
{
    return d->id.section(QLatin1Char(';'), 2, 2);
}

QString Package::data() const
{
    return d->id.section(QLatin1Char(';'), 3, 3);
}

QString Package::summary() const
{
    return d->summary;
}

Package::Info Package::info() const
{
    return static_cast<Package::Info>(d->info);
}

QString Package::iconPath() const
{
    QString path;
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isOpen()) {
        qDebug() << "Desktop files database is not open";
        return path;
    }

    QSqlQuery q(db);
    q.prepare("SELECT filename FROM cache WHERE package = :name");
    q.bindValue(":name", name());
    if (q.exec()) {
        if (q.next()) {
            QFile desktopFile(q.value(0).toString());
            if (desktopFile.open (QIODevice::ReadOnly | QIODevice::Text)) {
                while (!desktopFile.atEnd ()) {
                    QByteArray line = desktopFile.readLine().trimmed();
                    if (line.startsWith("Icon=")) {
                        path = line.mid(5);
                        break;
                    }
                }
                desktopFile.close();
            } else {
                qDebug() << "Cannot open desktop file " << q.value(0).toString();
            }
        }
    } else {
        qDebug() << "Error while running query " << q.executedQuery();
    }

    return path;
}

bool Package::operator==(const Package &package) const
{
    return id() == package.id() && info() == package.info();
}

Package& Package::operator=(const Package &package)
{
    d = package.d;
}

#include "package.moc"
