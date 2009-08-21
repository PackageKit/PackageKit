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
#include "client.h"
#include "daemonproxy.h"
#include "transaction.h"
#include "util.h"

using namespace PackageKit;

ClientPrivate::ClientPrivate(Client* parent) : QObject(parent), c(parent)
{
}

ClientPrivate::~ClientPrivate()
{
}

Transaction* ClientPrivate::createNewTransaction()
{
	QMutexLocker locker(&runningTransactionsLocker);
	Transaction* t = new Transaction(daemon->GetTid(), c);
	if (t->tid().isEmpty()) {
		qDebug() << "empty tid, the daemon is probably not here anymore";
		return t;
	}

	if(!locale.isNull())
		t->setLocale(locale);

//	qDebug() << "creating a transaction : " << t->tid();
	runningTransactions.insert(t->tid(), t);
	connect(t, SIGNAL(destroyed(const QString&)), this, SLOT(removeTransactionFromPool(const QString&)));

	return t;
}

void ClientPrivate::transactionListChanged(const QStringList& tids)
{
	QMutexLocker locker(&runningTransactionsLocker);
//	qDebug() << "entering transactionListChanged";
	QList<Transaction*> transactions;
	foreach(const QString& tid, tids) {
		if(runningTransactions.keys().contains(tid)) {
//			qDebug() << "reusing transaction from pool : " << tid;
			transactions.append(runningTransactions.value(tid));
		} else {
//			qDebug() << "external transaction : " << tid;
			Transaction* t = new Transaction(tid, c);
			transactions.append(t);
			runningTransactions.insert(tid, t);
		}
	}

	c->transactionListChanged(transactions);
//	qDebug() << "leaving transactionListChanged";
}

void ClientPrivate::networkStateChanged(const QString& state)
{
	int value = Util::enumFromString<Client>(state, "NetworkState", "Network");
	if(value == -1) {
		return;
	}
	c->networkStateChanged((Client::NetworkState)value);
}

void ClientPrivate::removeTransactionFromPool(const QString& tid)
{
	qDebug() << "removing transaction from pool : " << tid;
	runningTransactions.remove(tid);
}

#include "clientprivate.moc"

