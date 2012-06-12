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

#include "daemon.h"
#include "common.h"
#include "util.h"
#include "signature.h"
#include "package.h"

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

Transaction::Transaction(QObject *parent) :
    QObject(parent),
    d_ptr(new TransactionPrivate(this))
{
    init();
}

Transaction::Transaction(const QDBusObjectPath &tid, QObject *parent) :
    QObject(parent),
    d_ptr(new TransactionPrivate(this))
{
    init(tid);
}

void Transaction::init(const QDBusObjectPath &tid)
{
    Q_D(Transaction);

    d->tid = tid;
    d->oldtrans = false;
    d->p = 0;

    // If the user used a null tid
    // he want us to get it
    if (tid.path().isNull()) {
        d->tid = Daemon::global()->getTid();
    }

    int retry = 0;
    do {
        delete d->p;
        d->p = new TransactionProxy(QLatin1String(PK_NAME),
                                    d->tid.path(),
                                    QDBusConnection::systemBus(),
                                    this);
        if (!d->p->isValid()) {
            qDebug() << "Error, cannot create transaction proxy" << d->p->lastError();
            QDBusMessage message;
            message = QDBusMessage::createMethodCall(QLatin1String("org.freedesktop.DBus"),
                                                     QLatin1String("/"),
                                                     QLatin1String("org.freedesktop.DBus"),
                                                     QLatin1String("StartServiceByName"));
            message << qVariantFromValue(QString("org.freedesktop.PackageKit"));
            message << qVariantFromValue((uint) 0);
            QDBusConnection::sessionBus().call(message, QDBus::BlockWithGui);
            retry++;
        } else {
            retry = 0;
        }
    } while (retry == 1);

    if (d->tid.path().isEmpty()) {
        d->error = Transaction::InternalErrorDaemonUnreachable;
        return;
    } else {
        d->error = Transaction::NoInternalError;
        setHints(Daemon::hints());
    }

    connect(Daemon::global(), SIGNAL(daemonQuit()), SLOT(daemonQuit()));

    connect(d->p, SIGNAL(Changed()),
            this, SIGNAL(changed()));
    connect(d->p, SIGNAL(Category(QString,QString,QString,QString,QString)),
            this, SIGNAL(category(QString,QString,QString,QString,QString)));
    connect(d->p, SIGNAL(Destroy()),
            SLOT(destroy()));
    connect(d->p, SIGNAL(Details(QString,QString,uint,QString,QString,qulonglong)),
            SLOT(details(QString,QString,uint,QString,QString,qulonglong)));
    connect(d->p, SIGNAL(DistroUpgrade(uint,QString,QString)),
            SLOT(distroUpgrade(uint,QString,QString)));
    connect(d->p, SIGNAL(ErrorCode(uint,QString)),
            SLOT(errorCode(uint,QString)));
    connect(d->p, SIGNAL(Files(QString,QString)),
            SLOT(files(QString,QString)));
    connect(d->p, SIGNAL(Finished(uint,uint)),
            SLOT(finished(uint,uint)));
    connect(d->p, SIGNAL(Message(uint,QString)),
            SLOT(message(uint,QString)));
    connect(d->p, SIGNAL(Package(uint,QString,QString)),
            SLOT(package(uint,QString,QString)));
    connect(d->p, SIGNAL(RepoDetail(QString,QString,bool)),
            this, SIGNAL(repoDetail(QString,QString,bool)));
    connect(d->p, SIGNAL(RepoSignatureRequired(QString,QString,QString,QString,QString, QString,QString,uint)),
            SLOT(repoSignatureRequired(QString,QString,QString,QString,QString, QString,QString,uint)));
    connect(d->p, SIGNAL(EulaRequired(QString,QString,QString,QString)),
            SLOT(eulaRequired(QString,QString,QString,QString)));
    connect(d->p, SIGNAL(MediaChangeRequired(uint,QString,QString)),
            SLOT(mediaChangeRequired(uint,QString,QString)));
    connect(d->p, SIGNAL(ItemProgress(QString,uint)),
            SIGNAL(ItemProgress(QString,uint)));
    connect(d->p, SIGNAL(RequireRestart(uint,QString)),
            SLOT(requireRestart(uint,QString)));
    connect(d->p, SIGNAL(Transaction(QDBusObjectPath,QString,bool,uint,uint,QString,uint,QString)),
            SLOT(transaction(QDBusObjectPath,QString,bool,uint,uint,QString,uint,QString)));
    connect(d->p, SIGNAL(UpdateDetail(QString,QString,QString,QString,QString,QString, uint,QString,QString,uint,QString,QString)),
            SLOT(updateDetail(QString,QString,QString,QString,QString,QString, uint,QString,QString,uint,QString,QString)));

}

Transaction::Transaction(const QDBusObjectPath &tid,
                         const QString &timespec,
                         bool succeeded,
                         Role role,
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
    d->role = role;
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
//     qDebug() << "Destroying transaction with tid" << d->tid;
    delete d;
}

QDBusObjectPath Transaction::tid() const
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
        return Transaction::RoleUnknown;
    }
    return static_cast<Transaction::Role>(d->p->role());
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
        return Transaction::StatusUnknown;
    }
    return static_cast<Transaction::Status>(d->p->status());
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

void Transaction::downloadPackage(const Package &package, bool storeInCache)
{
    downloadPackages(QList<Package>() << package, storeInCache);
}

void Transaction::getCategories()
{
    RUN_TRANSACTION(GetCategories())
}

void Transaction::getDepends(const QList<Package> &packages, Transaction::Filters filters, bool recursive)
{
    RUN_TRANSACTION(GetDepends(filters, Util::packageListToPids(packages), recursive))
}

void Transaction::getDepends(const Package &package, Transaction::Filters filters, bool recursive)
{
    getDepends(QList<Package>() << package, filters, recursive);
}

void Transaction::getDetails(const QList<Package> &packages)
{
    RUN_TRANSACTION(GetDetails(Util::packageListToPids(packages)))
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
    RUN_TRANSACTION(GetPackages(filters))
}

void Transaction::getRepoList(Transaction::Filters filters)
{
    RUN_TRANSACTION(GetRepoList(filters))
}

void Transaction::getRequires(const QList<Package> &packages, Transaction::Filters filters, bool recursive)
{
    RUN_TRANSACTION(GetRequires(filters, Util::packageListToPids(packages), recursive))
}

void Transaction::getRequires(const Package &package, Transaction::Filters filters, bool recursive)
{
    getRequires(QList<Package>() << package, filters, recursive);
}

void Transaction::getUpdatesDetails(const QList<Package> &packages)
{
    RUN_TRANSACTION(GetUpdateDetail(Util::packageListToPids(packages)))
}

void Transaction::getUpdateDetail(const Package &package)
{
    getUpdatesDetails(QList<Package>() << package);
}

void Transaction::getUpdates(Transaction::Filters filters)
{
    RUN_TRANSACTION(GetUpdates(filters))
}

void Transaction::getDistroUpgrades()
{
    RUN_TRANSACTION(GetDistroUpgrades())
}

void Transaction::installFiles(const QStringList &files, TransactionFlags flags)
{
    RUN_TRANSACTION(InstallFiles(flags, files))
}

void Transaction::installFile(const QString &file, TransactionFlags flags)
{
    installFiles(QStringList() << file, flags);
}

void Transaction::installPackages(const QList<Package> &packages, TransactionFlags flags)
{
    RUN_TRANSACTION(InstallPackages(flags, Util::packageListToPids(packages)))
}

void Transaction::installPackage(const Package &package, TransactionFlags flags)
{
    installPackages(QList<Package>() << package, flags);
}

void Transaction::installSignature(const Signature &sig)
{
    RUN_TRANSACTION(InstallSignature(sig.type,
                                     sig.keyId,
                                     sig.package.id()))
}

void Transaction::refreshCache(bool force)
{
    RUN_TRANSACTION(RefreshCache(force))
}

void Transaction::removePackages(const QList<Package> &packages, bool allowDeps, bool autoremove, TransactionFlags flags)
{
    RUN_TRANSACTION(RemovePackages(flags, Util::packageListToPids(packages), allowDeps, autoremove))
}

void Transaction::removePackage(const Package &package, bool allowDeps, bool autoremove, TransactionFlags flags)
{
    removePackages(QList<Package>() << package, allowDeps, autoremove, flags);
}

void Transaction::repairSystem(TransactionFlags flags)
{
    RUN_TRANSACTION(RepairSystem(flags))
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
    RUN_TRANSACTION(Resolve(filters, packageNames))
}

void Transaction::resolve(const QString &packageName, Transaction::Filters filters)
{
    resolve(QStringList() << packageName, filters);
}

void Transaction::searchFiles(const QStringList &search, Transaction::Filters filters)
{
    RUN_TRANSACTION(SearchFiles(filters, search))
}

void Transaction::searchFiles(const QString &search, Transaction::Filters filters)
{
    searchFiles(QStringList() << search, filters);
}

void Transaction::searchDetails(const QStringList &search, Transaction::Filters filters)
{
    RUN_TRANSACTION(SearchDetails(filters, search))
}

void Transaction::searchDetails(const QString &search, Transaction::Filters filters)
{
    searchDetails(QStringList() << search, filters);
}

void Transaction::searchGroups(const QStringList &groups, Transaction::Filters filters)
{
    RUN_TRANSACTION(SearchGroups(filters, groups))
}

void Transaction::searchGroup(const QString &group, Transaction::Filters filters)
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

void Transaction::searchGroup(Package::Group group, Transaction::Filters filters)
{
    searchGroups(Package::Groups() << group, filters);
}

void Transaction::searchNames(const QStringList &search, Transaction::Filters filters)
{
    RUN_TRANSACTION(SearchNames(filters, search))
}

void Transaction::searchNames(const QString &search, Transaction::Filters filters)
{
    searchNames(QStringList() << search, filters);
}

void Transaction::updatePackages(const QList<Package> &packages, TransactionFlags flags)
{
    RUN_TRANSACTION(UpdatePackages(flags, Util::packageListToPids(packages)))
}

void Transaction::updatePackage(const Package &package, TransactionFlags flags)
{
    updatePackages(QList<Package>() << package, flags);
}

void Transaction::updateSystem(bool onlyTrusted)
{
    RUN_TRANSACTION(UpdateSystem(onlyTrusted))
}

void Transaction::upgradeSystem(const QString &distroId, UpgradeKind kind)
{
    RUN_TRANSACTION(UpgradeSystem(distroId, kind))
}

void Transaction::whatProvides(Transaction::Provides type, const QStringList &search, Transaction::Filters filters)
{
    RUN_TRANSACTION(WhatProvides(filters, type, search))
}

void Transaction::whatProvides(Transaction::Provides type, const QString &search, Transaction::Filters filters)
{
    whatProvides(type, QStringList() << search, filters);
}

#include "transaction.moc"
