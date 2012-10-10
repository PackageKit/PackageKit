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

#include <QtSql/QSqlQuery>

#define CHECK_TRANSACTION                           \
        if (r.isError()) {                          \
            d->error = Transaction::parseError(r.error().name()); \
        }                                           \

#define RUN_TRANSACTION(blurb)                      \
        Q_D(Transaction);                           \
        if (init()) {                               \
            QDBusPendingReply<> r = d->p->blurb;    \
            r.waitForFinished();                    \
            CHECK_TRANSACTION                       \
        }                                           \

using namespace PackageKit;

Transaction::Transaction(QObject *parent) :
    QObject(parent),
    d_ptr(new TransactionPrivate(this))
{
    connect(Daemon::global(), SIGNAL(daemonQuit()), SLOT(daemonQuit()));
}

Transaction::Transaction(const QDBusObjectPath &tid, QObject *parent) :
    QObject(parent),
    d_ptr(new TransactionPrivate(this))
{
    connect(Daemon::global(), SIGNAL(daemonQuit()), SLOT(daemonQuit()));
    init(tid);
}

bool Transaction::init(const QDBusObjectPath &tid)
{
    Q_D(Transaction);

    if (d->p) {
        return true;
    }

    // If the user used a null tid
    // he want us to get it
    if (tid.path().isNull()) {
        d->tid = Daemon::global()->getTid();
    } else {
        d->tid = tid;
    }

    if (d->tid.path().isEmpty()) {
        d->error = Transaction::InternalErrorDaemonUnreachable;
        return false;
    } else {
        
    }

    int retry = 0;
    do {
        d->p = new TransactionProxy(QLatin1String(PK_NAME),
                                    d->tid.path(),
                                    QDBusConnection::systemBus(),
                                    this);
        if (!d->p->isValid()) {
            qWarning() << "Error, cannot create transaction proxy" << d->p->lastError();
            QDBusMessage message;
            message = QDBusMessage::createMethodCall(QLatin1String("org.freedesktop.DBus"),
                                                     QLatin1String("/"),
                                                     QLatin1String("org.freedesktop.DBus"),
                                                     QLatin1String("StartServiceByName"));
            message << qVariantFromValue(QString("org.freedesktop.PackageKit"));
            message << qVariantFromValue(0U);
            QDBusConnection::sessionBus().call(message, QDBus::BlockWithGui);

            // The transaction was not created
            delete d->p;
            d->p = 0;
            retry++;
        } else {
            retry = 0;
        }
    } while (retry == 1);

    // if the transaction proxy was not created return false
    if (!d->p) {
        return false;
    } else {
        d->error = Transaction::InternalErrorNone;
        if (!Daemon::hints().isEmpty()) {
            setHints(Daemon::hints());
        }
    }

    connect(d->p, SIGNAL(Changed()),
            SIGNAL(changed()));
    connect(d->p, SIGNAL(Category(QString,QString,QString,QString,QString)),
            SIGNAL(category(QString,QString,QString,QString,QString)));
    connect(d->p, SIGNAL(Destroy()),
            SLOT(destroy()));
    connect(d->p, SIGNAL(Details(QString,QString,uint,QString,QString,qulonglong)),
            SLOT(Details(QString,QString,uint,QString,QString,qulonglong)));
    connect(d->p, SIGNAL(DistroUpgrade(uint,QString,QString)),
            SLOT(distroUpgrade(uint,QString,QString)));
    connect(d->p, SIGNAL(ErrorCode(uint,QString)),
            SLOT(errorCode(uint,QString)));
    connect(d->p, SIGNAL(Files(QString,QStringList)),
            SLOT(files(QString,QStringList)));
    connect(d->p, SIGNAL(Finished(uint,uint)),
            SLOT(finished(uint,uint)));
    connect(d->p, SIGNAL(Message(uint,QString)),
            SLOT(message(uint,QString)));
    connect(d->p, SIGNAL(Package(uint,QString,QString)),
            SLOT(Package(uint,QString,QString)));
    connect(d->p, SIGNAL(RepoDetail(QString,QString,bool)),
            SIGNAL(repoDetail(QString,QString,bool)));
    connect(d->p, SIGNAL(RepoSignatureRequired(QString,QString,QString,QString,QString, QString,QString,uint)),
            SLOT(RepoSignatureRequired(QString,QString,QString,QString,QString, QString,QString,uint)));
    connect(d->p, SIGNAL(EulaRequired(QString,QString,QString,QString)),
            SIGNAL(eulaRequired(QString,QString,QString,QString)));
    connect(d->p, SIGNAL(MediaChangeRequired(uint,QString,QString)),
            SLOT(mediaChangeRequired(uint,QString,QString)));
    connect(d->p, SIGNAL(ItemProgress(QString,uint,uint)),
            SLOT(ItemProgress(QString,uint,uint)));
    connect(d->p, SIGNAL(RequireRestart(uint,QString)),
            SLOT(requireRestart(uint,QString)));
    connect(d->p, SIGNAL(Transaction(QDBusObjectPath,QString,bool,uint,uint,QString,uint,QString)),
            SLOT(transaction(QDBusObjectPath,QString,bool,uint,uint,QString,uint,QString)));
    connect(d->p, SIGNAL(UpdateDetail(QString,QStringList,QStringList,QStringList,QStringList,QStringList, uint,QString,QString,uint,QString,QString)),
            SLOT(UpdateDetail(QString,QStringList,QStringList,QStringList,QStringList,QStringList,uint,QString,QString,uint,QString,QString)));
    return true;
}

Transaction::Transaction(const QDBusObjectPath &tid,
                         const QString &timespec,
                         bool succeeded,
                         Role role,
                         uint duration,
                         const QString &data,
                         uint uid,
                         const QString &cmdline,
                         QObject *parent) :
    QObject(parent),
    d_ptr(new TransactionPrivate(this))
{
    Q_D(Transaction);
    d->tid = tid;
    d->timespec = QDateTime::fromString(timespec, Qt::ISODate);
    d->succeeded = succeeded;
    d->role = role;
    d->duration = duration;
    d->data = data;
    d->uid = uid;
    d->cmdline = cmdline;
    d->error = InternalErrorNone;
}

Transaction::~Transaction()
{
    Q_D(Transaction);
//     qDebug() << "Destroying transaction with tid" << d->tid;
    delete d;
}

void Transaction::reset()
{
    Q_D(Transaction);
    d->destroy();
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
    if (d->p) {
        return d->p->allowCancel();
    }
    return false;
}

bool Transaction::isCallerActive() const
{
    Q_D(const Transaction);
    if (d->p) {
        return d->p->callerActive();
    }
    return false;
}

void Transaction::cancel()
{
    RUN_TRANSACTION(Cancel())
}

QString Transaction::packageName(const QString &packageID)
{
    return packageID.section(QLatin1Char(';'), 0, 0);
}

QString Transaction::packageVersion(const QString &packageID)
{
    return packageID.section(QLatin1Char(';'), 1, 1);
}

QString Transaction::packageArch(const QString &packageID)
{
    return packageID.section(QLatin1Char(';'), 2, 2);
}

QString Transaction::packageData(const QString &packageID)
{
    return packageID.section(QLatin1Char(';'), 3, 3);
}

QString Transaction::packageIcon(const QString &packageID)
{
    QString path;
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isOpen()) {
        qDebug() << "Desktop files database is not open";
        return path;
    }

    QSqlQuery q(db);
    q.prepare("SELECT filename FROM cache WHERE package = :name");
    q.bindValue(":name", Transaction::packageName(packageID));
    if (q.exec()) {
        if (q.next()) {
            QFile desktopFile(q.value(0).toString());
            if (desktopFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                while (!desktopFile.atEnd()) {
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

QString Transaction::lastPackage() const
{
    Q_D(const Transaction);
    if (d->p) {
        return d->p->lastPackage();
    }
    return QString();
}

uint Transaction::percentage() const
{
    Q_D(const Transaction);
    if (d->p) {
        return d->p->percentage();
    }
    return 0;
}

uint Transaction::elapsedTime() const
{
    Q_D(const Transaction);
    if (d->p) {
        return d->p->elapsedTime();
    }
    return 0;
}

uint Transaction::remainingTime() const
{
    Q_D(const Transaction);
    if (d->p) {
        return d->p->remainingTime();
    }
    return 0;
}

uint Transaction::speed() const
{
    Q_D(const Transaction);
    if (d->p) {
        return d->p->speed();
    }
    return 0;
}

qulonglong Transaction::downloadSizeRemaining() const
{
    Q_D(const Transaction);
    if (d->p) {
        return d->p->downloadSizeRemaining();
    }
    return 0;
}    

Transaction::Role Transaction::role() const
{
    Q_D(const Transaction);
    if (d->p) {
        return static_cast<Transaction::Role>(d->p->role());
    }
    return d->role;
}

void Transaction::setHints(const QStringList &hints)
{
    Q_D(Transaction);
    if (d->p) {
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
    if (d->p) {
        return static_cast<Transaction::Status>(d->p->status());
    }
    return Transaction::StatusUnknown;
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
    if(d->p) {
        return d->p->uid();
    }
    return d->uid;
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

void Transaction::downloadPackages(const QStringList &packageIDs, bool storeInCache)
{
    RUN_TRANSACTION(DownloadPackages(storeInCache, packageIDs))
}

void Transaction::downloadPackage(const QString &packageID, bool storeInCache)
{
    downloadPackages(QStringList() << packageID, storeInCache);
}

void Transaction::getCategories()
{
    RUN_TRANSACTION(GetCategories())
}

void Transaction::getDepends(const QStringList &packageIDs, Transaction::Filters filters, bool recursive)
{
    RUN_TRANSACTION(GetDepends(filters, packageIDs, recursive))
}

void Transaction::getDepends(const QString &packageID, Transaction::Filters filters, bool recursive)
{
    getDepends(QStringList() << packageID, filters, recursive);
}

void Transaction::getDetails(const QStringList &packageIDs)
{
    RUN_TRANSACTION(GetDetails(packageIDs))
}

void Transaction::getDetails(const QString &packageID)
{
    getDetails(QStringList() << packageID);
}

void Transaction::getFiles(const QStringList &packageIDs)
{
    RUN_TRANSACTION(GetFiles(packageIDs))
}

void Transaction::getFiles(const QString &packageID)
{
    getFiles(QStringList() << packageID);
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

void Transaction::getRequires(const QStringList &packageIDs, Transaction::Filters filters, bool recursive)
{
    RUN_TRANSACTION(GetRequires(filters, packageIDs, recursive))
}

void Transaction::getRequires(const QString &packageID, Transaction::Filters filters, bool recursive)
{
    getRequires(QStringList() << packageID, filters, recursive);
}

void Transaction::getUpdatesDetails(const QStringList &packageIDs)
{
    RUN_TRANSACTION(GetUpdateDetail(packageIDs))
}

void Transaction::getUpdateDetail(const QString &packageID)
{
    getUpdatesDetails(QStringList() << packageID);
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

void Transaction::installPackages(const QStringList &packageIDs, TransactionFlags flags)
{
    RUN_TRANSACTION(InstallPackages(flags, packageIDs))
}

void Transaction::installPackage(const QString &packageID, TransactionFlags flags)
{
    installPackages(QStringList() << packageID, flags);
}

void Transaction::installSignature(SigType type, const QString &keyID, const QString &packageID)
{
    RUN_TRANSACTION(InstallSignature(type, keyID, packageID))
}

void Transaction::refreshCache(bool force)
{
    RUN_TRANSACTION(RefreshCache(force))
}

void Transaction::removePackages(const QStringList &packageIDs, bool allowDeps, bool autoremove, TransactionFlags flags)
{
    RUN_TRANSACTION(RemovePackages(flags, packageIDs, allowDeps, autoremove))
}

void Transaction::removePackage(const QString &packageID, bool allowDeps, bool autoremove, TransactionFlags flags)
{
    removePackages(QStringList() << packageID, allowDeps, autoremove, flags);
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

void Transaction::searchGroup(Group group, Filters filters)
{
    QString groupString = Daemon::enumToString<Transaction>(group, "Group");
    searchGroup(groupString, filters);
}

void Transaction::searchGroups(Groups groups, Transaction::Filters filters)
{
    searchGroups(groups, filters);
}

void Transaction::searchNames(const QStringList &search, Transaction::Filters filters)
{
    RUN_TRANSACTION(SearchNames(filters, search))
}

void Transaction::searchNames(const QString &search, Transaction::Filters filters)
{
    searchNames(QStringList() << search, filters);
}

void Transaction::updatePackages(const QStringList &packageIDs, TransactionFlags flags)
{
    RUN_TRANSACTION(UpdatePackages(flags, packageIDs))
}

void Transaction::updatePackage(const QString &packageID, TransactionFlags flags)
{
    updatePackages(QStringList() << packageID, flags);
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

Transaction::InternalError Transaction::parseError(const QString &errorName)
{
    QString error = errorName;
    if (error.startsWith(QLatin1String("org.freedesktop.packagekit."))) {
        return Transaction::InternalErrorFailedAuth;
    }
    
    error.remove(QLatin1String("org.freedesktop.PackageKit.Transaction."));
    
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

    qWarning() << "Transaction::parseError: unknown error" << errorName;
    return Transaction::InternalErrorFailed;
}

#include "transaction.moc"
