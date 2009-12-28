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
#include "daemonproxy.h"
#include "transaction.h"
#include "transactionprivate.h"
#include "util.h"
#include "common.h"

using namespace PackageKit;

ClientPrivate::ClientPrivate(Client* parent) : QObject(parent), c(parent)
{
}

ClientPrivate::~ClientPrivate()
{
}

Transaction* ClientPrivate::createNewTransaction()
{
	Transaction* t = new Transaction(daemon->GetTid(), c);
	if (t->tid().isEmpty()) {
		qDebug() << "empty tid, the daemon is probably not here anymore";
		return t;
	}

	if(!hints.isEmpty())
		t->setHints(hints);

// 	qDebug() << "creating a transaction : " << t->tid();
	runningTransactions.insert(t->tid(), t);

	return t;
}

QList<Transaction*> ClientPrivate::transactions(const QStringList& tids)
{
// 	qDebug() << "entering transactionListChanged";
	QList<Transaction*> trans;
	foreach(const QString& tid, tids) {
		if(runningTransactions.contains(tid)) {
//			qDebug() << "reusing transaction from pool : " << tid;
			trans.append(runningTransactions.value(tid));
		} else {
//			qDebug() << "external transaction : " << tid;
			Transaction* t = new Transaction(tid, c);
			trans.append(t);
			runningTransactions.insert(tid, t);
		}
	}
	return trans;
}

void ClientPrivate::transactionListChanged(const QStringList& tids)
{
	c->transactionListChanged(transactions(tids));
}

void ClientPrivate::serviceOwnerChanged (const QString& name, const QString& oldOnwer, const QString& newOwner)
{
	if (name != PK_NAME)
		return;
	if (!newOwner.isEmpty ())
		return;
	
	error = Client::ErrorDaemonUnreachable;
	c->error(error);

	foreach(Transaction *t, runningTransactions.values ()) {
		t->finished (Transaction::ExitFailed, 0);
		t->d->destroy ();
	}
}

void ClientPrivate::removeTransactionFromPool(const QString &tid)
{
// 	qDebug() << "removing transaction from pool: " << tid;

	runningTransactions[tid]->deleteLater();
	runningTransactions.remove(tid);
}

#include "clientprivate.moc"
