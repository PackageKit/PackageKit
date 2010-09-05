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

#ifndef CLIENTPRIVATE_H
#define CLIENTPRIVATE_H

#include <QtCore>
#include "client.h"

class DaemonProxy;

namespace PackageKit {

class Transaction;

class ClientPrivate : public QObject
{
	Q_OBJECT

public:
	~ClientPrivate();

	::DaemonProxy* daemon;
	Client* c;
	QStringList hints;
	QHash<QString, Transaction*> runningTransactions;
	Client::DaemonError error;

    QList<Transaction*> transactions(const QStringList& tids, QObject *parent);
	void destroyTransaction(const QString &tid);

public Q_SLOTS:
	void transactionListChanged(const QStringList& tids);
	void serviceOwnerChanged(const QString&, const QString&, const QString&);

private:
    friend class Client;
    bool startDaemon;
    ClientPrivate(Client* parent);
};

} // End namespace PackageKit

#endif
