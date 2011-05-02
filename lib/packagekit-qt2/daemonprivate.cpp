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

#include "daemonprivate.h"
#include "transaction.h"
#include "common.h"

#include <QDBusConnection>

using namespace PackageKit;

DaemonPrivate::DaemonPrivate(Daemon* parent) :
    q_ptr(parent)
{
    m_watcher = new QDBusServiceWatcher(PK_NAME,
                                        QDBusConnection::systemBus(),
                                        QDBusServiceWatcher::WatchForUnregistration,
                                        q_ptr);
    q_ptr->connect(m_watcher, SIGNAL(serviceUnregistered(const QString &)),
                   SLOT(serviceUnregistered()));
}

QList<Transaction*> DaemonPrivate::transactions(const QStringList& tids, QObject *parent)
{
    QList<Transaction*> transactionList;
    foreach (const QString &tid, tids) {
        Transaction *transaction = new Transaction(tid, parent);
        transactionList << transaction;
    }
    return transactionList;
}

void DaemonPrivate::serviceUnregistered()
{
    Q_Q(Daemon);

    q->daemonQuit();

    // We don't have more transactions running
    q->transactionListChanged(QStringList());
}
