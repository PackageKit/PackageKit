/*
 * This file is part of the QPackageKit project
 * Copyright (C) 2008 Adrien Bustany <madcat@mymadcat.com>
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

#include "clientprivate.h"
#include "transaction.h"
#include "transactionprivate.h"
#include "common.h"

using namespace PackageKit;

ClientPrivate::ClientPrivate(Client* parent)
 : QObject(parent),
   c(parent)
{
}

ClientPrivate::~ClientPrivate()
{
}

QList<Transaction*> ClientPrivate::transactions(const QStringList& tids, QObject *parent)
{
// 	qDebug() << "entering transactionListChanged";
	QList<Transaction*> trans;
	foreach(const QString& tid, tids) {
		if(runningTransactions.contains(tid)) {
//			qDebug() << "reusing transaction from pool : " << tid;
			trans.append(runningTransactions.value(tid));
		} else {
//			qDebug() << "external transaction : " << tid;
			Transaction* t = new Transaction(tid, parent);
			trans.append(t);
			runningTransactions.insert(tid, t);
		}
	}
	return trans;
}

void ClientPrivate::transactionListChanged(const QStringList& tids)
{
	c->transactionListChanged(transactions(tids, Client::instance()));
}

void ClientPrivate::serviceOwnerChanged (const QString& name, const QString& oldOnwer, const QString& newOwner)
{
    if (name != PK_NAME){
        return;
    }

    // next time a transaction need to be created
    // we start the Daemon, we have to find a way
    // to avoid DBus error that sericeHasNoOwner
    startDaemon = newOwner.isEmpty();

    if (!newOwner.isEmpty()){
        return;
    }

    error = Client::ErrorDaemonUnreachable;
    c->error(error);

    foreach (Transaction *t, runningTransactions) {
        t->finished (Enum::ExitFailed, 0);
        t->d_ptr->destroy ();
    }

    // We don't have more transactions running
    c->transactionListChanged(QList<Transaction*>());
}

void ClientPrivate::destroyTransaction(const QString &tid)
{
// 	qDebug() << "removing transaction from pool: " << tid;
//     runningTransactions[tid]->deleteLater();
    runningTransactions.remove(tid);
}

#include "clientprivate.moc"
