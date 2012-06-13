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
}

Daemon::~Daemon()
{
}

Transaction::Roles Daemon::actions()
{
    qulonglong roles = global()->d_ptr->daemon->roles();

    Transaction::Roles flags;
    flags |= static_cast<Transaction::Role>(roles);
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
    return static_cast<Transaction::Filter>(global()->d_ptr->daemon->filters());
}

PackageDetails::Groups Daemon::groups()
{
    return global()->d_ptr->daemon->groups();
}

bool Daemon::locked()
{
    return global()->d_ptr->daemon->locked();
}

QStringList Daemon::mimeTypes()
{
    return global()->d_ptr->daemon->mimeTypes();
}

Daemon::Network Daemon::networkState()
{
    return static_cast<Daemon::Network>(global()->d_ptr->daemon->networkState());
}

QString Daemon::distroId()
{
    return global()->d_ptr->daemon->distroId();
}

Daemon::Authorize Daemon::canAuthorize(const QString &actionId)
{
    uint ret;
    ret = global()->d_ptr->daemon->CanAuthorize(actionId);
    return static_cast<Daemon::Authorize>(ret);
}

QDBusObjectPath Daemon::getTid()
{
    return global()->d_ptr->daemon->CreateTransaction();
}

uint Daemon::getTimeSinceAction(Transaction::Role role)
{
    return global()->d_ptr->daemon->GetTimeSinceAction(role);
}

QList<QDBusObjectPath> Daemon::getTransactionList()
{
    return global()->d_ptr->daemon->GetTransactionList();
}

QList<Transaction*> Daemon::getTransactionObjects(QObject *parent)
{
    return global()->d_ptr->transactions(getTransactionList(), parent);
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

Transaction::InternalError Daemon::setProxy(const QString& http_proxy, const QString& https_proxy, const QString& ftp_proxy, const QString& socks_proxy, const QString& no_proxy, const QString& pac)
{
    QDBusPendingReply<> r = global()->d_ptr->daemon->SetProxy(http_proxy, https_proxy, ftp_proxy, socks_proxy, no_proxy, pac);
    r.waitForFinished();
    if (r.isError ()) {
        return Transaction::parseError(r.error().name());
    } else {
        return Transaction::InternalErrorNone;
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
