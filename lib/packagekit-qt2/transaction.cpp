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

#include "transaction.h"
#include "transactionprivate.h"
#include "transactionproxy.h"

#include "daemonprivate.h"
#include "common.h"
#include "package.h"
#include "util.h"

#define CHECK_TRANSACTION                           \
        if (r.isError()) {                          \
            d->error = Util::errorFromString(r.error().name()); \
        }                                           \

#define RUN_TRANSACTION(blurb)                      \
        Q_D(Transaction);                           \
        QDBusPendingReply<> r = d->p->blurb;        \
        r.waitForFinished();                        \
        CHECK_TRANSACTION                           \

using namespace PackageKit;

Transaction::Transaction(QObject *parent, const QString &tid) :
    QObject(parent),
    d_ptr(new TransactionPrivate(this))
{
    Q_D(Transaction);

    d->tid = tid;
    d->oldtrans = false;
    d->p = 0;

    // If the user used a null tid
    // he want us to get it
    if (tid.isNull()) {
        d->tid = Daemon::global()->getTid();
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
        d->error = Transaction::InternalErrorDaemonUnreachable;
        return;
    } else {
        d->error = Transaction::NoInternalError;
        setHints(Daemon::hints());
    }

    connect(Daemon::global(), SIGNAL(daemonQuit()), SLOT(daemonQuit()));

    connect(d->p, SIGNAL(Changed()),
            this, SIGNAL(changed()));
    connect(d->p, SIGNAL(Category(const QString&, const QString&, const QString&, const QString&, const QString&)),
            this, SIGNAL(category(const QString&, const QString&, const QString&, const QString&, const QString&)));
    connect(d->p, SIGNAL(Destroy()),
            SLOT(destroy()));
    connect(d->p, SIGNAL(Details(const QString&, const QString&, const QString&, const QString&, const QString&, qulonglong)),
            SLOT(details(const QString&, const QString&, const QString&, const QString&, const QString&, qulonglong)));
    connect(d->p, SIGNAL(DistroUpgrade(const QString&, const QString&, const QString&)),
            SLOT(distroUpgrade(const QString&, const QString&, const QString&)));
    connect(d->p, SIGNAL(ErrorCode(const QString&, const QString&)),
            SLOT(errorCode(const QString&, const QString&)));
    connect(d->p, SIGNAL(Files(const QString&, const QString&)),
            SLOT(files(const QString&, const QString&)));
    connect(d->p, SIGNAL(Finished(const QString&, uint)),
            SLOT(finished(const QString&, uint)));
    connect(d->p, SIGNAL(Message(const QString&, const QString&)),
            SLOT(message(const QString&, const QString&)));
    connect(d->p, SIGNAL(Package(const QString&, const QString&, const QString&)),
            SLOT(package(const QString&, const QString&, const QString&)));
    connect(d->p, SIGNAL(RepoDetail(const QString&, const QString&, bool)),
            this, SIGNAL(repoDetail(const QString&, const QString&, bool)));
    connect(d->p, SIGNAL(RepoSignatureRequired(const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&)),
            SLOT(repoSignatureRequired(const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&)));
    connect(d->p, SIGNAL(EulaRequired(const QString&, const QString&, const QString&, const QString&)),
            SLOT(eulaRequired(const QString&, const QString&, const QString&, const QString&)));
    connect(d->p, SIGNAL(MediaChangeRequired(const QString&, const QString&, const QString&)),
            SLOT(mediaChangeRequired(const QString&, const QString&, const QString&)));
    connect(d->p, SIGNAL(RequireRestart(const QString&, const QString&)),
            SLOT(requireRestart(const QString&, const QString&)));
    connect(d->p, SIGNAL(Transaction(const QString&, const QString&, bool, const QString&, uint, const QString&, uint, const QString&)),
            SLOT(transaction(const QString&, const QString&, bool, const QString&, uint, const QString&, uint, const QString&)));
    connect(d->p, SIGNAL(UpdateDetail(const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&)),
            SLOT(updateDetail(const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&)));

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
    d->role = static_cast<Transaction::Role>(Util::enumFromString<Transaction>(role, "Role", "Role"));
    d->duration = duration;
    d->data = data;
    d->uid = uid;
    d->cmdline = cmdline;
    d->error = NoInternalError;
    d->destroyed = true;
}

Transaction::~Transaction()
{
    Q_D(Transaction);
    qDebug() << "Destroying transaction with tid" << d->tid;
    delete d;
}

QString Transaction::tid() const
{
    Q_D(const Transaction);
    return d->tid;
}

Transaction::InternalError Transaction::error() const
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
    return d->p->allowCancel();
}

bool Transaction::isCallerActive() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return false;
    }
    return d->p->callerActive();
}

void Transaction::cancel()
{
    Q_D(Transaction);
    if (d->destroyed) {
        return;
    }

    QDBusReply<void> r = d->p->Cancel();
    if (!r.isValid()) {
        d->error = Util::errorFromString(r.error().message());
    }
}

Package Transaction::lastPackage() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return Package();
    }
    return Package(d->p->lastPackage());
}

uint Transaction::percentage() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return 0;
    }
    return d->p->percentage();
}

uint Transaction::subpercentage() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return 0;
    }
    return d->p->subpercentage();
}

uint Transaction::elapsedTime() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return 0;
    }
    return d->p->elapsedTime();
}

uint Transaction::remainingTime() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return 0;
    }
    return d->p->remainingTime();
}

uint Transaction::speed() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return 0;
    }
    return d->p->speed();
}

Transaction::Role Transaction::role() const
{
    Q_D(const Transaction);
    if(d->oldtrans) {
        return d->role;
    }

    if (d->destroyed) {
        return Transaction::UnknownRole;
    }
    return static_cast<Transaction::Role>(Util::enumFromString<Transaction>(d->p->role(), "Role", "Role"));
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

Transaction::Status Transaction::status() const
{
    Q_D(const Transaction);
    if (d->destroyed) {
        return Transaction::UnknownStatus;
    }
    return static_cast<Transaction::Status>(Util::enumFromString<Transaction>(d->p->status(), "Status", "Status"));
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

void Transaction::acceptEula(const QString &eulaId)
{
    RUN_TRANSACTION(AcceptEula(eulaId))
}

void Transaction::downloadPackages(const QList<Package> &packages, bool storeInCache)
{
    RUN_TRANSACTION(DownloadPackages(storeInCache, Util::packageListToPids(packages)))
}

void Transaction::downloadPackages(const Package &package, bool storeInCache)
{
    downloadPackages(QList<Package>() << package, storeInCache);
}

void Transaction::getCategories()
{
    RUN_TRANSACTION(GetCategories())
}

void Transaction::getDepends(const QList<Package> &packages, Transaction::Filters filters, bool recursive)
{
    RUN_TRANSACTION(GetDepends(TransactionPrivate::filtersToString(filters), Util::packageListToPids(packages), recursive))
}

void Transaction::getDepends(const Package &package, Transaction::Filters filters, bool recursive)
{
    getDepends(QList<Package>() << package, filters, recursive);
}

void Transaction::getDetails(const QList<Package> &packages)
{
    Q_D(Transaction);
    foreach (const Package &package, packages) {
        d->packageMap.insert(package.id(), package);
    }

    QDBusPendingReply<> r = d->p->GetDetails(Util::packageListToPids(packages));
    r.waitForFinished();

    CHECK_TRANSACTION
}

void Transaction::getDetails(const Package &package)
{
    getDetails(QList<Package>() << package);
}

void Transaction::getFiles(const QList<Package> &packages)
{
    RUN_TRANSACTION(GetFiles(Util::packageListToPids(packages)))
}

void Transaction::getFiles(const Package &package)
{
    getFiles(QList<Package>() << package);
}

void Transaction::getOldTransactions(uint number)
{
    RUN_TRANSACTION(GetOldTransactions(number))
}

void Transaction::getPackages(Transaction::Filters filters)
{
    RUN_TRANSACTION(GetPackages(TransactionPrivate::filtersToString(filters)))
}

void Transaction::getRepoList(Transaction::Filters filters)
{
    RUN_TRANSACTION(GetRepoList(TransactionPrivate::filtersToString(filters)))
}

void Transaction::getRequires(const QList<Package> &packages, Transaction::Filters filters, bool recursive)
{
    RUN_TRANSACTION(GetRequires(TransactionPrivate::filtersToString(filters), Util::packageListToPids(packages), recursive))
}

void Transaction::getRequires(const Package &package, Transaction::Filters filters, bool recursive)
{
    getRequires(QList<Package>() << package, filters, recursive);
}

void Transaction::getUpdateDetail(const QList<Package> &packages)
{
    RUN_TRANSACTION(GetUpdateDetail(Util::packageListToPids(packages)))
}

void Transaction::getUpdateDetail(const Package &package)
{
    getUpdateDetail(QList<Package>() << package);
}

void Transaction::getUpdates(Transaction::Filters filters)
{
    RUN_TRANSACTION(GetUpdates(TransactionPrivate::filtersToString(filters)))
}

void Transaction::getDistroUpgrades()
{
    RUN_TRANSACTION(GetDistroUpgrades())
}

void Transaction::installFiles(const QStringList &files, bool onlyTrusted)
{
    RUN_TRANSACTION(InstallFiles(onlyTrusted, files))
}

void Transaction::installFiles(const QString &file, bool onlyTrusted)
{
    installFiles(QStringList() << file, onlyTrusted);
}

void Transaction::installPackages(const QList<Package> &packages, bool onlyTrusted)
{
    RUN_TRANSACTION(InstallPackages(onlyTrusted, Util::packageListToPids(packages)))
}

void Transaction::installPackages(const Package &package, bool onlyTrusted)
{
    installPackages(QList<Package>() << package, onlyTrusted);
}

void Transaction::installSignature(Signature::Type type, const QString &keyId, const Package &package)
{
    RUN_TRANSACTION(InstallSignature(Util::enumToString<Signature>(type, "Type", "Signature"), keyId, package.id()))
}

void Transaction::refreshCache(bool force)
{
    RUN_TRANSACTION(RefreshCache(force))
}

void Transaction::removePackages(const QList<Package> &packages, bool allowDeps, bool autoremove)
{
    RUN_TRANSACTION(RemovePackages(Util::packageListToPids(packages), allowDeps, autoremove))
}

void Transaction::removePackages(const Package &package, bool allowDeps, bool autoremove)
{
    removePackages(QList<Package>() << package, allowDeps, autoremove);
}

void Transaction::repoEnable(const QString &repoId, bool enable)
{
    RUN_TRANSACTION(RepoEnable(repoId, enable))
}

void Transaction::repoSetData(const QString &repoId, const QString &parameter, const QString &value)
{
    RUN_TRANSACTION(RepoSetData(repoId, parameter, value))
}

void Transaction::resolve(const QStringList &packageNames, Transaction::Filters filters)
{
    RUN_TRANSACTION(Resolve(TransactionPrivate::filtersToString(filters), packageNames))
}

void Transaction::resolve(const QString &packageName, Transaction::Filters filters)
{
    resolve(QStringList() << packageName, filters);
}

void Transaction::searchFiles(const QStringList &search, Transaction::Filters filters)
{
    RUN_TRANSACTION(SearchFiles(TransactionPrivate::filtersToString(filters), search))
}

void Transaction::searchFiles(const QString &search, Transaction::Filters filters)
{
    searchFiles(QStringList() << search, filters);
}

void Transaction::searchDetails(const QStringList &search, Transaction::Filters filters)
{
    RUN_TRANSACTION(SearchDetails(TransactionPrivate::filtersToString(filters), search))
}

void Transaction::searchDetails(const QString &search, Transaction::Filters filters)
{
    searchDetails(QStringList() << search, filters);
}

void Transaction::searchGroups(const QStringList &groups, Transaction::Filters filters)
{
    RUN_TRANSACTION(SearchGroups(TransactionPrivate::filtersToString(filters), groups))
}

void Transaction::searchGroups(const QString &group, Transaction::Filters filters)
{
    searchGroups(QStringList() << group, filters);
}

void Transaction::searchGroups(Package::Groups groups, Transaction::Filters filters)
{
    QStringList groupsSL;
    foreach (const Package::Group &group, groups) {
        groupsSL << Util::enumToString<Package>(group, "Group", "Group");
    }

    searchGroups(groupsSL, filters);
}

void Transaction::searchGroups(Package::Group group, Transaction::Filters filters)
{
    searchGroups(Package::Groups() << group, filters);
}

void Transaction::searchNames(const QStringList &search, Transaction::Filters filters)
{
    RUN_TRANSACTION(SearchNames(TransactionPrivate::filtersToString(filters), search))
}

void Transaction::searchNames(const QString &search, Transaction::Filters filters)
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

void Transaction::simulateInstallPackages(const QList<Package> &packages)
{
    RUN_TRANSACTION(SimulateInstallPackages(Util::packageListToPids(packages)))
}

void Transaction::simulateInstallPackages(const Package &package)
{
    simulateInstallPackages(QList<Package>() << package);
}

void Transaction::simulateRemovePackages(const QList<Package> &packages, bool autoremove)
{
    RUN_TRANSACTION(SimulateRemovePackages(Util::packageListToPids(packages), autoremove))
}

void Transaction::simulateRemovePackages(const Package &package, bool autoremove)
{
    simulateRemovePackages(QList<Package>() << package, autoremove);
}

void Transaction::simulateUpdatePackages(const QList<Package> &packages)
{
    RUN_TRANSACTION(SimulateUpdatePackages(Util::packageListToPids(packages)))
}

void Transaction::simulateUpdatePackages(const Package &package)
{
    simulateUpdatePackages(QList<Package>() << package);
}

void Transaction::updatePackages(const QList<Package> &packages, bool onlyTrusted)
{
    RUN_TRANSACTION(UpdatePackages(onlyTrusted, Util::packageListToPids(packages)))
}

void Transaction::updatePackages(const Package &package, bool onlyTrusted)
{
    updatePackages(QList<Package>() << package, onlyTrusted);
}

void Transaction::updateSystem(bool onlyTrusted)
{
    RUN_TRANSACTION(UpdateSystem(onlyTrusted))
}

void Transaction::whatProvides(Transaction::Provides type, const QStringList &search, Transaction::Filters filters)
{
    RUN_TRANSACTION(WhatProvides(TransactionPrivate::filtersToString(filters), Util::enumToString<Transaction>(type, "Provides", "Provides"), search))
}

void Transaction::whatProvides(Transaction::Provides type, const QString &search, Transaction::Filters filters)
{
    whatProvides(type, QStringList() << search, filters);
}

#include "transaction.moc"
