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
#include "util.h"

#define PK_DESKTOP_DEFAULT_DATABASE		LOCALSTATEDIR "/lib/PackageKit/desktop-files.db"

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
    d->daemon = new ::DaemonProxy(PK_NAME, PK_PATH, QDBusConnection::systemBus(), this);

    connect(d->daemon, SIGNAL(Changed()),
            this, SIGNAL(changed()));
    connect(d->daemon, SIGNAL(RepoListChanged()),
            this, SIGNAL(repoListChanged()));
    connect(d->daemon, SIGNAL(RestartSchedule()),
            this, SIGNAL(restartScheduled()));
    connect(d->daemon, SIGNAL(TransactionListChanged(const QStringList&)),
            this, SIGNAL(transactionListChanged(const QStringList&)));
    connect(d->daemon, SIGNAL(UpdatesChanged()),
            this, SIGNAL(updatesChanged()));

    // Set up database for desktop files
    QSqlDatabase db;
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName (PK_DESKTOP_DEFAULT_DATABASE);
    if (!db.open()) {
        qDebug() << "Failed to initialize the desktop files database";
    }
}

Daemon::~Daemon()
{
}

Transaction::Roles Daemon::actions()
{
    QStringList roles = global()->d_ptr->daemon->roles().split(";");

    Transaction::Roles flags;
    foreach (const QString &role, roles) {
        flags |= static_cast<Transaction::Role>(Util::enumFromString<Transaction>(role, "Role", "Role"));
    }
    return flags;
}

QString Daemon::backendName()
{
    return global()->d_ptr->daemon->backendName();
}

QString Daemon::backendDescription()
{
    return global()->d_ptr->daemon->backendDescription();
}

QString Daemon::backendAuthor()
{
    return global()->d_ptr->daemon->backendAuthor();
}

Transaction::Filters Daemon::filters()
{
    QStringList filters = global()->d_ptr->daemon->filters().split(";");

    // Adapt a slight difference in the enum
    if(filters.contains("none")) {
        filters[filters.indexOf("none")] = "no-filter";
    }

    Transaction::Filters flags;
    foreach (const QString &filter, filters) {
        flags |= static_cast<Transaction::Filter>(Util::enumFromString<Transaction>(filter, "Filter", "Filter"));
    }
    return flags;
}

Package::Groups Daemon::groups()
{
    QStringList groups = global()->d_ptr->daemon->groups().split(";");

    Package::Groups flags;
    foreach (const QString &group, groups) {
        flags.insert(static_cast<Package::Group>(Util::enumFromString<Package>(group, "Group", "Group")));
    }
    return flags;
}

bool Daemon::locked()
{
    return global()->d_ptr->daemon->locked();
}

QStringList Daemon::mimeTypes()
{
    return global()->d_ptr->daemon->mimeTypes().split(";");
}

Daemon::Network Daemon::networkState()
{
    QString state = global()->d_ptr->daemon->networkState();
    return static_cast<Daemon::Network>(Util::enumFromString<Daemon>(state, "Network", "Network"));
}

QString Daemon::distroId()
{
    return global()->d_ptr->daemon->distroId();
}

Daemon::Authorize Daemon::canAuthorize(const QString &actionId)
{
    QString result = global()->d_ptr->daemon->CanAuthorize(actionId);
    return static_cast<Daemon::Authorize>(Util::enumFromString<Daemon>(result, "Authorize", "Authorize"));
}

QString Daemon::getTid()
{
    return global()->d_ptr->daemon->GetTid();
}

uint Daemon::getTimeSinceAction(Transaction::Role role)
{
    QString roleName = Util::enumToString<Transaction>(role, "Role", "Role");
    return global()->d_ptr->daemon->GetTimeSinceAction(roleName);
}

QStringList Daemon::getTransactions()
{
    return global()->d_ptr->daemon->GetTransactionList();
}

QList<Transaction*> Daemon::getTransactionsObj(QObject *parent)
{
    return global()->d_ptr->transactions(getTransactions(), parent);
}

void Daemon::setHints(const QStringList& hints)
{
    global()->d_ptr->hints = hints;
}

void Daemon::setHints(const QString& hints)
{
    global()->d_ptr->hints = QStringList() << hints;
}

QStringList Daemon::hints()
{
    return global()->d_ptr->hints;
}

Transaction::InternalError Daemon::setProxy(const QString& http_proxy, const QString& ftp_proxy)
{
    return Daemon::setProxy(http_proxy, QString(), ftp_proxy, QString(), QString(), QString());
}

Transaction::InternalError Daemon::setProxy(const QString& http_proxy, const QString& https_proxy, const QString& ftp_proxy, const QString& socks_proxy, const QString& no_proxy, const QString& pac)
{
    QDBusPendingReply<> r = global()->d_ptr->daemon->SetProxy(http_proxy, https_proxy, ftp_proxy, socks_proxy, no_proxy, pac);
    r.waitForFinished();
    if (r.isError ()) {
        return Util::errorFromString(r.error().name());
    } else {
        return Transaction::NoInternalError;
    }
}

void Daemon::stateHasChanged(const QString& reason)
{
    global()->d_ptr->daemon->StateHasChanged(reason);
}

void Daemon::suggestDaemonQuit()
{
    global()->d_ptr->daemon->SuggestDaemonQuit();
}

uint Daemon::versionMajor()
{
    return global()->d_ptr->daemon->versionMajor();
}

uint Daemon::versionMinor()
{
    return global()->d_ptr->daemon->versionMinor();
}

uint Daemon::versionMicro()
{
    return global()->d_ptr->daemon->versionMicro();
}

#include "daemon.moc"
