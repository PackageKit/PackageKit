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

#include <QtSql>

#include "daemon.h"
#include "daemonprivate.h"
#include "daemonproxy.h"

#include "common.h"

using namespace PackageKit;

Daemon* Daemon::m_global = 0;

Daemon* Daemon::global()
{
    if(!m_global) {
        m_global = new Daemon(qApp);
    }

    return m_global;
}

Daemon::Daemon(QObject *parent) :
    QObject(parent),
    d_ptr(new DaemonPrivate(this))
{
    Q_D(Daemon);
    d->daemon = new ::DaemonProxy(QLatin1String(PK_NAME),
                                  QLatin1String(PK_PATH),
                                  QDBusConnection::systemBus(),
                                  this);

    connect(d->daemon, SIGNAL(Changed()),
            this, SIGNAL(changed()));
    connect(d->daemon, SIGNAL(RepoListChanged()),
            this, SIGNAL(repoListChanged()));
    connect(d->daemon, SIGNAL(RestartSchedule()),
            this, SIGNAL(restartScheduled()));
    connect(d->daemon, SIGNAL(TransactionListChanged(QStringList)),
            this, SIGNAL(transactionListChanged(QStringList)));
    connect(d->daemon, SIGNAL(UpdatesChanged()),
            this, SIGNAL(updatesChanged()));

    // Set up database for desktop files
    QSqlDatabase db;
    db = QSqlDatabase::addDatabase("QSQLITE", PK_DESKTOP_DEFAULT_DATABASE);
    db.setDatabaseName(PK_DESKTOP_DEFAULT_DATABASE);
    if (!db.open()) {
        qDebug() << "Failed to initialize the desktop files database";
    }
}

Daemon::~Daemon()
{
}

Transaction::Roles Daemon::actions()
{
    Q_D(const Daemon);
    return d->daemon->roles();
}

QString Daemon::backendName()
{
    Q_D(const Daemon);
    return d->daemon->backendName();
}

QString Daemon::backendDescription()
{
    Q_D(const Daemon);
    return d->daemon->backendDescription();
}

QString Daemon::backendAuthor()
{
    Q_D(const Daemon);
    return d->daemon->backendAuthor();
}

Transaction::Filters Daemon::filters()
{
    Q_D(const Daemon);
    return static_cast<Transaction::Filters>(d->daemon->filters());
}

Transaction::Groups Daemon::groups()
{
    Q_D(const Daemon);
    return static_cast<Transaction::Groups>(d->daemon->groups());
}

bool Daemon::locked()
{
    Q_D(const Daemon);
    return d->daemon->locked();
}

QStringList Daemon::mimeTypes()
{
    Q_D(const Daemon);
    return d->daemon->mimeTypes();
}

Daemon::Network Daemon::networkState()
{
    Q_D(const Daemon);
    return static_cast<Daemon::Network>(d->daemon->networkState());
}

QString Daemon::distroID()
{
    Q_D(const Daemon);
    return d->daemon->distroId();
}

Daemon::Authorize Daemon::canAuthorize(const QString &actionId)
{
    Q_D(const Daemon);
    uint ret;
    ret = d->daemon->CanAuthorize(actionId);
    return static_cast<Daemon::Authorize>(ret);
}

QDBusObjectPath Daemon::getTid()
{
    Q_D(const Daemon);
    return d->daemon->CreateTransaction();
}

uint Daemon::getTimeSinceAction(Transaction::Role role)
{
    Q_D(const Daemon);
    return d->daemon->GetTimeSinceAction(role);
}

QList<QDBusObjectPath> Daemon::getTransactionList()
{
    Q_D(const Daemon);
    return d->daemon->GetTransactionList();
}

QList<Transaction*> Daemon::getTransactionObjects(QObject *parent)
{
    Q_D(Daemon);
    return d->transactions(getTransactionList(), parent);
}

void Daemon::setHints(const QStringList &hints)
{
    Q_D(Daemon);
    d->hints = hints;
}

void Daemon::setHints(const QString &hints)
{
    Q_D(Daemon);
    d->hints = QStringList() << hints;
}

QStringList Daemon::hints()
{
    Q_D(const Daemon);
    return d->hints;
}

Transaction::InternalError Daemon::setProxy(const QString& http_proxy, const QString& https_proxy, const QString& ftp_proxy, const QString& socks_proxy, const QString& no_proxy, const QString& pac)
{
    Q_D(const Daemon);
    QDBusPendingReply<> r = d->daemon->SetProxy(http_proxy, https_proxy, ftp_proxy, socks_proxy, no_proxy, pac);
    r.waitForFinished();
    if (r.isError ()) {
        return Transaction::parseError(r.error().name());
    } else {
        return Transaction::InternalErrorNone;
    }
}

void Daemon::stateHasChanged(const QString& reason)
{
    Q_D(const Daemon);
    d->daemon->StateHasChanged(reason);
}

void Daemon::suggestDaemonQuit()
{
    Q_D(const Daemon);
    d->daemon->SuggestDaemonQuit();
}

uint Daemon::versionMajor()
{
    Q_D(const Daemon);
    return d->daemon->versionMajor();
}

uint Daemon::versionMinor()
{
    Q_D(const Daemon);
    return d->daemon->versionMinor();
}

uint Daemon::versionMicro()
{
    Q_D(const Daemon);
    return d->daemon->versionMicro();
}

#include "daemon.moc"
