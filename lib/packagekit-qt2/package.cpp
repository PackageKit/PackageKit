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

#include <QtSql/QSqlQuery>
#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QVariant>
#include <QtCore/QStringList>
#include <QtCore/QMetaEnum>

#include "package.h"
#include "transaction.h"
#include "util.h"

namespace PackageKit {

class DetailsPrivate
{
public:
    QString license;
    Package::Group group;
    QString description;
    QString url;
    uint size;
};

class UpdateDetailsPrivate {
public:
    QList<Package> updates;
    QList<Package> obsoletes;
    QString vendorUrl;
    QString bugzillaUrl;
    QString cveUrl;
    Package::Restart restart;
    QString updateText;
    QString changelog;
    Package::UpdateState state;
    QDateTime issued;
    QDateTime updated;
};

class PackagePrivate
{
public:
    QString id;
    QString name;
    QString version;
    QString arch;
    QString data;
    QString summary;
    Package::Info info;
    DetailsPrivate *details;
    UpdateDetailsPrivate *updateDetails;
    QString iconPath;
};

}

using namespace PackageKit;

Package::Package(const QString &packageId, Info info, const QString &summary)
    : d_ptr(new PackagePrivate)
{
    d_ptr->id = packageId;
    d_ptr->info = info;
    d_ptr->summary = summary;
    d_ptr->details = 0;
    d_ptr->updateDetails = 0;

    // Break down the packageId
    QStringList tokens = packageId.split(";");
    if(tokens.size() == 4) {
        d_ptr->name = tokens.at(0);
        d_ptr->version = tokens.at(1);
        d_ptr->arch = tokens.at(2);
        d_ptr->data = tokens.at(3);
    }
}

Package::Package()
    : d_ptr(new PackagePrivate)
{
    d_ptr->details = 0;
    d_ptr->updateDetails = 0;
    d_ptr->info = UnknownInfo;
}

Package::Package(const Package &other)
    : d_ptr(new PackagePrivate)
{
    d_ptr->details = 0;
    d_ptr->updateDetails = 0;
    d_ptr->info = UnknownInfo;

    *this = other;
}

Package::~Package()
{
    Q_D(Package);
    if (d->details) {
        delete d->details;
    }
    if (d->updateDetails) {
        delete d->updateDetails;
    }
    delete d;
}

QString Package::id() const
{
    Q_D(const Package);
    return d->id;
}

QString Package::name() const
{
    Q_D(const Package);
    return d->name;
}

QString Package::version() const
{
    Q_D(const Package);
    return d->version;
}

QString Package::arch() const
{
    Q_D(const Package);
    return d->arch;
}

QString Package::data() const
{
    Q_D(const Package);
    return d->data;
}

QString Package::summary() const
{
    Q_D(const Package);
    return d->summary;
}

Package::Info Package::info() const
{
    Q_D(const Package);
    return d->info;
}

bool Package::hasDetails() const
{
    Q_D(const Package);
    return d->details;
}

QString Package::iconPath() const
{
    Q_D(const Package);

    QString path;
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isOpen()) {
        qDebug() << "Desktop files database is not open";
        return path;
    }

    QSqlQuery q(db);
    q.prepare("SELECT filename FROM cache WHERE package = :name");
    q.bindValue(":name", d->name);
    if (q.exec()) {
        if (q.next()) {
            QFile desktopFile (q.value(0).toString());
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

QString Package::license() const
{
    Q_D(const Package);
    if (d->details) {
        return d->details->license;
    }
    return QString();
}

void Package::setLicense(const QString &license)
{
    Q_D(Package);
    if (!d->details) {
        d->details = new DetailsPrivate;
    }
    d->details->license = license;
}

Package::Group Package::group() const
{
    Q_D(const Package);
    if (d->details) {
        return d->details->group;
    }
    return UnknownGroup;
}

void Package::setGroup(Group group)
{
    Q_D(Package);
    if (!d->details) {
        d->details = new DetailsPrivate;
    }
    d->details->group = group;
}

QString Package::description() const
{
    Q_D(const Package);
    if (d->details) {
        return d->details->description;
    }
    return QString();
}

void Package::setDescription(const QString &description)
{
    Q_D(Package);
    if (!d->details) {
        d->details = new DetailsPrivate;
    }
    d->details->description = description;
}

QString Package::url() const
{
    Q_D(const Package);
    if (d->details) {
        return d->details->url;
    }
    return QString();
}

void Package::setUrl(const QString &url)
{
    Q_D(Package);
    if (!d->details) {
        d->details = new DetailsPrivate;
    }
    d->details->url = url;
}

qulonglong Package::size() const
{
    Q_D(const Package);
    if (d->details) {
        return d->details->size;
    }
    return 0;
}

void Package::setSize(qulonglong size)
{
    Q_D(Package);
    if (!d->details) {
        d->details = new DetailsPrivate;
    }
    d->details->size = size;
}

bool Package::hasUpdateDetails() const
{
    Q_D(const Package);
    return d->updateDetails;
}

QList<Package> Package::updates() const
{
    Q_D(const Package);
    if (d->updateDetails) {
        return d->updateDetails->updates;
    }
    return QList<Package>();
}

void Package::setUpdates(const QList<Package> &updates)
{
    Q_D(Package);
    if (!d->updateDetails) {
        d->updateDetails = new UpdateDetailsPrivate;
    }
    d->updateDetails->updates = updates;
}

QList<Package> Package::obsoletes() const
{
    Q_D(const Package);
    if (d->updateDetails) {
        return d->updateDetails->obsoletes;
    }
    return QList<Package>();
}

void Package::setObsoletes(const QList<Package> &obsoletes)
{
    Q_D(Package);
    if (!d->updateDetails) {
        d->updateDetails = new UpdateDetailsPrivate;
    }
    d->updateDetails->obsoletes = obsoletes;
}

QString Package::vendorUrl() const
{
    Q_D(const Package);
    if (d->updateDetails) {
        return d->updateDetails->vendorUrl;
    }
    return QString();
}

void Package::setVendorUrl(const QString &vendorUrl)
{
    Q_D(Package);
    if (!d->updateDetails) {
        d->updateDetails = new UpdateDetailsPrivate;
    }
    d->updateDetails->vendorUrl = vendorUrl;
}

QString Package::bugzillaUrl() const
{
    Q_D(const Package);
    if (d->updateDetails) {
        return d->updateDetails->bugzillaUrl;
    }
    return QString();
}

void Package::setBugzillaUrl(const QString &bugzillaUrl)
{
    Q_D(Package);
    if (!d->updateDetails) {
        d->updateDetails = new UpdateDetailsPrivate;
    }
    d->updateDetails->bugzillaUrl = bugzillaUrl;
}

QString Package::cveUrl() const
{
    Q_D(const Package);
    if (d->updateDetails) {
        return d->updateDetails->cveUrl;
    }
    return QString();
}

void Package::setCveUrl(const QString &cveUrl)
{
    Q_D(Package);
    if (!d->updateDetails) {
        d->updateDetails = new UpdateDetailsPrivate;
    }
    d->updateDetails->cveUrl = cveUrl;
}

Package::Restart Package::restart() const
{
    Q_D(const Package);
    if (d->updateDetails) {
        return d->updateDetails->restart;
    }
    return Package::UnknownRestart;
}

void Package::setRestart(Package::Restart restart)
{
    Q_D(Package);
    if (!d->updateDetails) {
        d->updateDetails = new UpdateDetailsPrivate;
    }
    d->updateDetails->restart = restart;
}

QString Package::updateText() const
{
    Q_D(const Package);
    if (d->updateDetails) {
        return d->updateDetails->updateText;
    }
    return QString();
}

void Package::setUpdateText(const QString &updateText)
{
    Q_D(Package);
    if (!d->updateDetails) {
        d->updateDetails = new UpdateDetailsPrivate;
    }
    d->updateDetails->updateText = updateText;
}

QString Package::changelog() const
{
    Q_D(const Package);
    if (d->updateDetails) {
        return d->updateDetails->changelog;
    }
    return QString();
}

void Package::setChangelog(const QString &changelog)
{
    Q_D(Package);
    if (!d->updateDetails) {
        d->updateDetails = new UpdateDetailsPrivate;
    }
    d->updateDetails->changelog = changelog;
}

Package::UpdateState Package::state() const
{
    Q_D(const Package);
    if (d->updateDetails) {
        return d->updateDetails->state;
    }
    return UnknownUpdateState;
}

void Package::setState(UpdateState state)
{
    Q_D(Package);
    if (!d->updateDetails) {
        d->updateDetails = new UpdateDetailsPrivate;
    }
    d->updateDetails->state = state;
}

QDateTime Package::issued() const
{
    Q_D(const Package);
    if (d->updateDetails) {
        return d->updateDetails->issued;
    }
    return QDateTime();
}

void Package::setIssued(const QDateTime &issued)
{
    Q_D(Package);
    if (!d->updateDetails) {
        d->updateDetails = new UpdateDetailsPrivate;
    }
    d->updateDetails->issued = issued;
}

QDateTime Package::updated() const
{
    Q_D(const Package);
    if (d->updateDetails) {
        return d->updateDetails->updated;
    }
    return QDateTime();
}

void Package::setUpdated(const QDateTime &updated)
{
    Q_D(Package);
    if (!d->updateDetails) {
        d->updateDetails = new UpdateDetailsPrivate;
    }
    d->updateDetails->updated = updated;
}

bool Package::operator==(const Package &package) const
{
    Q_D(const Package);
    return d->id == package.id();
}

Package& Package::operator=(const Package &package)
{
    Q_D(Package);

    if (this != &package) // protect against invalid self-assignment
    {
        d->id = package.id();
        d->name = package.name();
        d->version = package.version();
        d->arch = package.arch();
        d->data = package.data();

        d->summary = package.summary();
        d->info = package.info();

        if (package.hasDetails()) {
            if (!d->details) {
                d->details = new DetailsPrivate;
            }
            d->details->license = package.license();
            d->details->group = package.group();
            d->details->description = package.description();
            d->details->url = package.url();
            d->details->size = package.size();
        }

        if (package.hasUpdateDetails()) {
            if (!d->updateDetails) {
                d->updateDetails = new UpdateDetailsPrivate;
            }
            d->updateDetails->updates = package.updates();
            d->updateDetails->obsoletes = package.obsoletes();
            d->updateDetails->vendorUrl = package.vendorUrl();
            d->updateDetails->bugzillaUrl = package.bugzillaUrl();
            d->updateDetails->cveUrl = package.cveUrl();
            d->updateDetails->restart = package.restart();
            d->updateDetails->state = package.state();
            d->updateDetails->updateText = package.updateText();
            d->updateDetails->changelog = package.changelog();
            d->updateDetails->state = package.state();
            d->updateDetails->issued = package.issued();
            d->updateDetails->updated = package.updated();
        }
    }
    return *this;
}

#include "package.moc"
