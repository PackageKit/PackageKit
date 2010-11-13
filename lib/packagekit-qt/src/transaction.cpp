/*
 * This file is part of the QPackageKit project
 * Copyright (C) 2008 Adrien Bustany <madcat@mymadcat.com>
 * Copyright (C) 2010 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
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

#include "transaction.h"
#include "transactionprivate.h"
#include "transactionproxy.h"

#include "clientprivate.h"
#include "common.h"
#include "package.h"
#include "util.h"

#define CHECK_TRANSACTION                           \
        if (r.isError()) {                          \
            d->error = daemonErrorFromDBusReply(r); \
        }                                           \

#define RUN_TRANSACTION(blurb)                      \
        Q_D(Transaction);                           \
        QDBusPendingReply<> r = d->p->blurb;        \
        r.waitForFinished();                        \
        CHECK_TRANSACTION                           \

using namespace PackageKit;

template<class T> Client::DaemonError daemonErrorFromDBusReply(QDBusPendingReply<T> e) {
    return Util::errorFromString(e.error().name());
}

Transaction::Transaction(const QString &tid, QObject *parent)
 : QObject(parent),
   d_ptr(new TransactionPrivate(this))
{
    Q_D(Transaction);

    d->tid = tid;
    d->oldtrans = FALSE;
    d->p = 0;

    // If the user used a null tid
    // he want us to get it
    if (tid.isNull()) {
        d->tid = Client::instance()->getTid();
    }

    int retry = 0;
    do {
        delete d->p;
        d->p = new TransactionProxy(PK_NAME, d->tid, QDBusConnection::systemBus(), this);
        if (d->p->lastError().isValid()) {
            qDebug() << "Error, cannot create transaction proxy";
            qDebug() << d->p->lastError();
            QDBusMessage message;
            message = QDBusMessage::createMethodCall("org.freedesktop.DBus",
                                                     "/",
                                                     "org.freedesktop.DBus",
                                                     QLatin1String("StartServiceByName"));
            message << qVariantFromValue(QString("org.freedesktop.PackageKit"));
            message << qVariantFromValue((uint) 0);
            QDBusConnection::sessionBus().call(message, QDBus::BlockWithGui);
            retry++;
        } else {
            retry = 0;
        }
    } while (retry == 1);

    if (d->tid.isEmpty()) {
        d->error = Client::ErrorDaemonUnreachable;
    } else {
        d->error = Client::NoError;
        Client::instance()->d_ptr->runningTransactions.insert(d->tid, this);
        setHints(Client::instance()->d_ptr->hints);
    }

    connect(d->p, SIGNAL(Changed()),
            this, SIGNAL(changed()));
    connect(d->p, SIGNAL(Category(const QString&, const QString&, const QString&, const QString&, const QString&)),
            this, SIGNAL(category(const QString&, const QString&, const QString&, const QString&, const QString&)));
    connect(d->p, SIGNAL(Destroy()),
            d, SLOT(destroy()));
    connect(d->p, SIGNAL(Details(const QString&, const QString&, const QString&, const QString&, const QString&, qulonglong)),
            d, SLOT(details(const QString&, const QString&, const QString&, const QString&, const QString&, qulonglong)));
    connect(d->p, SIGNAL(DistroUpgrade(const QString&, const QString&, const QString&)),
            d, SLOT(distroUpgrade(const QString&, const QString&, const QString&)));
    connect(d->p, SIGNAL(ErrorCode(const QString&, const QString&)),
            d, SLOT(errorCode(const QString&, const QString&)));
    connect(d->p, SIGNAL(Files(const QString&, const QString&)),
            d, SLOT(files(const QString&, const QString&)));
    connect(d->p, SIGNAL(Finished(const QString&, uint)),
            d, SLOT(finished(const QString&, uint)));
    connect(d->p, SIGNAL(Message(const QString&, const QString&)),
            d, SLOT(message(const QString&, const QString&)));
    connect(d->p, SIGNAL(Package(const QString&, const QString&, const QString&)),
            d, SLOT(package(const QString&, const QString&, const QString&)));
    connect(d->p, SIGNAL(RepoDetail(const QString&, const QString&, bool)),
            this, SIGNAL(repoDetail(const QString&, const QString&, bool)));
    connect(d->p, SIGNAL(RepoSignatureRequired(const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&)),
            d, SLOT(repoSignatureRequired(const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&)));
    connect(d->p, SIGNAL(EulaRequired(const QString&, const QString&, const QString&, const QString&)),
            d, SLOT(eulaRequired(const QString&, const QString&, const QString&, const QString&)));
    connect(d->p, SIGNAL(MediaChangeRequired(const QString&, const QString&, const QString&)), d, SLOT(mediaChangeRequired(const QString&, const QString&, const QString&)));
    connect(d->p, SIGNAL(RequireRestart(const QString&, const QString&)),
            d, SLOT(requireRestart(const QString&, const QString&)));
    connect(d->p, SIGNAL(Transaction(const QString&, const QString&, bool, const QString&, uint, const QString&, uint, const QString&)),
            d, SLOT(transaction(const QString&, const QString&, bool, const QString&, uint, const QString&, uint, const QString&)));
    connect(d->p, SIGNAL(UpdateDetail(const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&)),
            d, SLOT(updateDetail(const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&)));

}

Transaction::Transaction(const QString &tid,
                         const QString &timespec,
                         bool succeeded,
                         const QString &role,
                         uint duration,
                         const QString &data,
                         uint uid,
                         const QString &cmdline,
                         QObject *parent)
 : QObject(parent),
   d_ptr(new TransactionPrivate(this))
{
    Q_D(Transaction);
    d->oldtrans = TRUE;
    d->tid = tid;
    d->timespec = QDateTime::fromString(timespec, Qt::ISODate);
    d->succeeded = succeeded;
    d->role = (Enum::Role)Util::enumFromString<Enum>(role, "Role", "Role");
    d->duration = duration;
    d->data = data;
    d->uid = uid;
    d->cmdline = cmdline;
    d->error = Client::NoError;
    d->destroyed = true;
}

Transaction::~Transaction()
{
//	qDebug() << "Destroying transaction with tid" << tid();
}

QString Transaction::tid() const
{
    Q_D(const Transaction);
    return d->tid;
}

Client::DaemonError Transaction::error () const
{
    Q_D(const Transaction);
    return d->error;
}

bool Transaction::allowCancel() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return false;
    }
    return d->p->allowCancel ();
}

bool Transaction::callerActive() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return false;
    }
    return d->p->callerActive ();
}

void Transaction::cancel()
{
    Q_D(Transaction);
    if (d->destroyed) {
        return;
    }
    QDBusReply<void> r = d->p->Cancel ();
    if (!r.isValid ()) {
        d->error = Util::errorFromString (r.error ().message ());
    }
}

QSharedPointer<Package> Transaction::lastPackage() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return QSharedPointer<Package>();
    }
    return QSharedPointer<Package> (new Package(d->p->lastPackage ()));
}

uint Transaction::percentage() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return 0;
    }
    return d->p->percentage ();
}

uint Transaction::subpercentage() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return 0;
    }
    return d->p->subpercentage ();
}

uint Transaction::elapsedTime() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return 0;
    }
    return d->p->elapsedTime ();
}

uint Transaction::remainingTime() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return 0;
    }
    return d->p->remainingTime ();
}

uint Transaction::speed() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return 0;
    }
    return d->p->speed ();
}

Enum::Role Transaction::role() const
{
    Q_D(const Transaction);
    if(d->oldtrans)
        return d->role;

    if (d->destroyed) {
        return Enum::UnknownRole;
    }
    return (Enum::Role) Util::enumFromString<Enum>(d->p->role (), "Role", "Role");
}

void Transaction::setHints(const QStringList &hints)
{
    Q_D(Transaction);
    if (!d->destroyed) {
        d->p->SetHints(hints);
    }
}

void Transaction::setHints(const QString &hints)
{
    setHints(QStringList() << hints);
}

Enum::Status Transaction::status() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return Enum::UnknownStatus;
    }
    return (Enum::Status) Util::enumFromString<Enum>(d->p->status (), "Status", "Status");
}

QDateTime Transaction::timespec() const
{
    Q_D(const Transaction);
    return d->timespec;
}

bool Transaction::succeeded() const
{
    Q_D(const Transaction);
    return d->succeeded;
}

uint Transaction::duration() const
{
    Q_D(const Transaction);
    return d->duration;
}

QString Transaction::data() const
{
    Q_D(const Transaction);
    return d->data;
}

uint Transaction::uid() const
{
    Q_D(const Transaction);
    if(d->destroyed) {
        return d->uid;
    }
    return d->p->uid();
}

QString Transaction::cmdline() const
{
    Q_D(const Transaction);
    return d->cmdline;
}

////// Transaction functions

void Transaction::acceptEula(const Client::EulaInfo &info)
{
    RUN_TRANSACTION(AcceptEula(info.id))
}

void Transaction::downloadPackages(const QList<QSharedPointer<Package> > &packages)
{
    RUN_TRANSACTION(DownloadPackages(Util::packageListToPids(packages)))
}

void Transaction::downloadPackages(const QSharedPointer<Package> &package)
{
    downloadPackages(QList<QSharedPointer<Package> >() << package);
}

void Transaction::getDepends(const QList<QSharedPointer<Package> > &packages, Enum::Filters filters, bool recursive)
{
    RUN_TRANSACTION(GetDepends(Util::filtersToString(filters), Util::packageListToPids(packages), recursive))
}

void Transaction::getDepends(const QSharedPointer<Package> &package, Enum::Filters filters, bool recursive)
{
    getDepends(QList<QSharedPointer<Package> >() << package, filters, recursive);
}

void Transaction::getDetails(const QList<QSharedPointer<Package> > &packages)
{
    Q_D(Transaction);
    foreach (const QSharedPointer<Package> &package, packages) {
        d->packageMap.insert(package->id(), package);
    }

    QDBusPendingReply<> r = d->p->GetDetails(Util::packageListToPids(packages));
    r.waitForFinished ();

    CHECK_TRANSACTION
}

void Transaction::getDetails(const QSharedPointer<Package> &package)
{
    getDetails(QList<QSharedPointer<Package> >() << package);
}

void Transaction::getFiles(const QList<QSharedPointer<Package> > &packages)
{
    RUN_TRANSACTION(GetFiles(Util::packageListToPids(packages)))
}

void Transaction::getFiles(const QSharedPointer<Package> &package)
{
    getFiles(QList<QSharedPointer<Package> >() << package);
}

void Transaction::getOldTransactions(uint number)
{
    RUN_TRANSACTION(GetOldTransactions(number))
}

void Transaction::getPackages(Enum::Filters filters)
{
    RUN_TRANSACTION(GetPackages(Util::filtersToString(filters)))
}

void Transaction::getRepoList(Enum::Filters filters)
{
    RUN_TRANSACTION(GetRepoList(Util::filtersToString(filters)))
}

void Transaction::getRequires(const QList<QSharedPointer<Package> > &packages, Enum::Filters filters, bool recursive)
{
    RUN_TRANSACTION(GetRequires(Util::filtersToString(filters), Util::packageListToPids(packages), recursive))
}

void Transaction::getRequires(const QSharedPointer<Package> &package, Enum::Filters filters, bool recursive)
{
    getRequires(QList<QSharedPointer<Package> >() << package, filters, recursive);
}

void Transaction::getUpdateDetail(const QList<QSharedPointer<Package> > &packages)
{
    RUN_TRANSACTION(GetUpdateDetail(Util::packageListToPids(packages)))
}

void Transaction::getUpdateDetail(const QSharedPointer<Package> &package)
{
    getUpdateDetail(QList<QSharedPointer<Package> >() << package);
}

void Transaction::getUpdates(Enum::Filters filters)
{
    RUN_TRANSACTION(GetUpdates(Util::filtersToString(filters)))
}

void Transaction::getDistroUpgrades()
{
    RUN_TRANSACTION(GetDistroUpgrades())
}

void Transaction::installFiles(const QStringList &files, bool only_trusted)
{
    RUN_TRANSACTION(InstallFiles(only_trusted, files))
}

void Transaction::installFiles(const QString &file, bool only_trusted)
{
    installFiles(QStringList() << file, only_trusted);
}

void Transaction::installPackages(bool only_trusted, const QList<QSharedPointer<Package> > &packages)
{
    RUN_TRANSACTION(InstallPackages(only_trusted, Util::packageListToPids(packages)))
}

void Transaction::installPackages(bool only_trusted, const QSharedPointer<Package> &package)
{
    installPackages(only_trusted, QList<QSharedPointer<Package> >() << package);
}

void Transaction::installSignature(Enum::SigType type, const QString &key_id, const QSharedPointer<Package> &package)
{
    RUN_TRANSACTION(InstallSignature(Util::enumToString<Enum>(type, "SigType", "Signature"), key_id, package->id()))
}

void Transaction::refreshCache(bool force)
{
    RUN_TRANSACTION(RefreshCache(force))
}

void Transaction::removePackages(const QList<QSharedPointer<Package> > &packages, bool allow_deps, bool autoremove)
{
    RUN_TRANSACTION(RemovePackages(Util::packageListToPids(packages), allow_deps, autoremove))
}

void Transaction::removePackages(const QSharedPointer<Package> &package, bool allow_deps, bool autoremove)
{
    removePackages(QList<QSharedPointer<Package> >() << package, allow_deps, autoremove);
}

void Transaction::repoEnable(const QString &repo_id, bool enable)
{
    RUN_TRANSACTION(RepoEnable(repo_id, enable))
}

void Transaction::repoSetData(const QString &repo_id, const QString &parameter, const QString &value)
{
    RUN_TRANSACTION(RepoSetData(repo_id, parameter, value))
}

void Transaction::resolve(const QStringList &packageNames, Enum::Filters filters)
{
    RUN_TRANSACTION(Resolve(Util::filtersToString(filters), packageNames))
}

void Transaction::resolve(const QString &packageName, Enum::Filters filters)
{
    resolve(QStringList() << packageName, filters);
}

void Transaction::searchFiles(const QStringList &search, Enum::Filters filters)
{
    RUN_TRANSACTION(SearchFiles(Util::filtersToString(filters), search))
}

void Transaction::searchFiles(const QString &search, Enum::Filters filters)
{
    searchFiles(QStringList() << search, filters);
}

void Transaction::searchDetails(const QStringList &search, Enum::Filters filters)
{
    RUN_TRANSACTION(SearchDetails(Util::filtersToString(filters), search))
}

void Transaction::searchDetails(const QString &search, Enum::Filters filters)
{
    searchDetails(QStringList() << search, filters);
}

void Transaction::searchGroups(const QStringList &groups, Enum::Filters filters)
{
    RUN_TRANSACTION(SearchGroups(Util::filtersToString(filters), groups))
}

void Transaction::searchGroups(const QString &group, Enum::Filters filters)
{
    searchGroups(QStringList() << group, filters);
}

void Transaction::searchGroups(Enum::Groups groups, Enum::Filters filters)
{
    QStringList groupsSL;
    foreach (const Enum::Group group, groups) {
        groupsSL << Util::enumToString<Enum>(group, "Group", "Group");
    }

    searchGroups(groups, filters);
}

void Transaction::searchGroups(Enum::Group group, Enum::Filters filters)
{
    searchGroups(Enum::Groups() << group, filters);
}

void Transaction::searchNames(const QStringList &search, Enum::Filters filters)
{
    RUN_TRANSACTION(SearchNames(Util::filtersToString(filters), search))
}

void Transaction::searchNames(const QString &search, Enum::Filters filters)
{
    searchNames(QStringList() << search, filters);
}

void Transaction::simulateInstallFiles(const QStringList &files)
{
    RUN_TRANSACTION(SimulateInstallFiles(files))
}

void Transaction::simulateInstallFiles(const QString &file)
{
    simulateInstallFiles(QStringList() << file);
}

void Transaction::simulateInstallPackages(const QList<QSharedPointer<Package> > &packages)
{
    RUN_TRANSACTION(SimulateInstallPackages(Util::packageListToPids(packages)))
}

void Transaction::simulateInstallPackages(const QSharedPointer<Package> &package)
{
    simulateInstallPackages(QList<QSharedPointer<Package> >() << package);
}

void Transaction::simulateRemovePackages(const QList<QSharedPointer<Package> > &packages, bool autoremove)
{
    RUN_TRANSACTION(SimulateRemovePackages(Util::packageListToPids(packages), autoremove))
}

void Transaction::simulateRemovePackages(const QSharedPointer<Package> &package, bool autoremove)
{
    simulateRemovePackages(QList<QSharedPointer<Package> >() << package, autoremove);
}

void Transaction::simulateUpdatePackages(const QList<QSharedPointer<Package> > &packages)
{
    RUN_TRANSACTION(SimulateUpdatePackages(Util::packageListToPids(packages)))
}

void Transaction::simulateUpdatePackages(const QSharedPointer<Package> &package)
{
    simulateUpdatePackages(QList<QSharedPointer<Package> >() << package);
}

void Transaction::updatePackages(bool only_trusted, const QList<QSharedPointer<Package> > &packages)
{
    RUN_TRANSACTION(UpdatePackages(only_trusted, Util::packageListToPids(packages)))
}

void Transaction::updatePackages(bool only_trusted, const QSharedPointer<Package> &package)
{
    updatePackages(only_trusted, QList<QSharedPointer<Package> >() << package);
}

void Transaction::updateSystem(bool only_trusted)
{
    RUN_TRANSACTION(UpdateSystem(only_trusted))
}

void Transaction::whatProvides(Enum::Provides type, const QStringList &search, Enum::Filters filters)
{
    RUN_TRANSACTION(WhatProvides(Util::filtersToString(filters), Util::enumToString<Enum>(type, "Provides", "Provides"), search))
}

void Transaction::whatProvides(Enum::Provides type, const QString &search, Enum::Filters filters)
{
    whatProvides(type, QStringList() << search, filters);
}

#include "transaction.moc"
