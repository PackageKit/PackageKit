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

#include "transaction.h"
#include "client.h"
#include "common.h"
#include "package.h"
#include "transactionprivate.h"
#include "transactionproxy.h"
#include "util.h"

using namespace PackageKit;

Transaction::Transaction(const QString& tid, Client* parent) : QObject(parent)
{
	d = new TransactionPrivate(this);

	d->oldtrans = FALSE;
	d->tid = tid;
	d->client = parent;
	d->p = new TransactionProxy(PK_NAME, tid, QDBusConnection::systemBus(), this);
	if(!d->p->isValid())
		qDebug("Error, cannot create transaction proxy");

	d->error = Client::NoError;

	connect(d->p, SIGNAL(Changed()), this, SIGNAL(changed()));
	connect(d->p, SIGNAL(Category(const QString&, const QString&, const QString&, const QString&, const QString&)), this, SIGNAL(category(const QString&, const QString&, const QString&, const QString&, const QString&)));
	connect(d->p, SIGNAL(Destroy()), d, SLOT(destroy()));
	connect(d->p, SIGNAL(Details(const QString&, const QString&, const QString&, const QString&, const QString&, qulonglong)), d, SLOT(details(const QString&, const QString&, const QString&, const QString&, const QString&, qulonglong)));
	connect(d->p, SIGNAL(DistroUpgrade(const QString&, const QString&, const QString&)), d, SLOT(distroUpgrade(const QString&, const QString&, const QString&)));
	connect(d->p, SIGNAL(ErrorCode(const QString&, const QString&)), d, SLOT(errorCode(const QString&, const QString&)));
	connect(d->p, SIGNAL(Files(const QString&, const QString&)), d, SLOT(files(const QString&, const QString&)));
	connect(d->p, SIGNAL(Finished(const QString&, uint)), d, SLOT(finished(const QString&, uint)));
	connect(d->p, SIGNAL(Message(const QString&, const QString&)), d, SLOT(message(const QString&, const QString&)));
	connect(d->p, SIGNAL(Package(const QString&, const QString&, const QString&)), d, SLOT(package(const QString&, const QString&, const QString&)));
	connect(d->p, SIGNAL(RepoDetail(const QString&, const QString&, bool)), this, SIGNAL(repoDetail(const QString&, const QString&, bool)));
	connect(d->p, SIGNAL(RepoSignatureRequired(const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&)), d, SLOT(repoSignatureRequired(const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&)));
	connect(d->p, SIGNAL(EulaRequired(const QString&, const QString&, const QString&, const QString&)), d, SLOT(eulaRequired(const QString&, const QString&, const QString&, const QString&)));
	connect(d->p, SIGNAL(MediaChangeRequired(const QString&, const QString&, const QString&)), d, SLOT(mediaChangeRequired(const QString&, const QString&, const QString&)));
	connect(d->p, SIGNAL(RequireRestart(const QString&, const QString&)), d, SLOT(requireRestart(const QString&, const QString&)));
	connect(d->p, SIGNAL(Transaction(const QString&, const QString&, bool, const QString&, uint, const QString&, uint, const QString&)), d, SLOT(transaction(const QString&, const QString&, bool, const QString&, uint, const QString&, uint, const QString&)));
	connect(d->p, SIGNAL(UpdateDetail(const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&)), d, SLOT(updateDetail(const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&)));

}

Transaction::Transaction(const QString& tid, const QString& timespec, bool succeeded, const QString& role, uint duration, const QString& data, uint uid, const QString& cmdline, Client* parent) : QObject(parent)
{
	d = new TransactionPrivate(this);

	d->oldtrans = TRUE;
	d->tid = tid;
	d->timespec = QDateTime::fromString(timespec, Qt::ISODate);
	d->succeeded = succeeded;
	d->role = (Client::Action)Util::enumFromString<Client>(role, "Action", "Action");
	d->duration = duration;
	d->data = data;
	d->uid = uid;
	d->cmdline = cmdline;
	d->client = parent;
	d->error = Client::NoError;
}

Transaction::~Transaction()
{
//	qDebug() << "Destroying transaction with tid" << tid();
}

QString Transaction::tid() const
{
	return d->tid;
}

Client::DaemonError Transaction::error () const
{
	return d->error;
}

bool Transaction::allowCancel() const
{
	return d->p->allowCancel ();
}

bool Transaction::callerActive() const
{
	return d->p->callerActive ();
}

void Transaction::cancel()
{
	QDBusReply<void> r = d->p->Cancel ();
	if (!r.isValid ()) {
		d->error = Util::errorFromString (r.error ().message ());
	}
}

Package* Transaction::lastPackage() const
{
	return new Package(d->p->lastPackage ());
}

uint Transaction::percentage() const
{
	return d->p->percentage ();
}

uint Transaction::subpercentage() const
{
	return d->p->subpercentage ();
}

uint Transaction::elapsedTime() const
{
	return d->p->elapsedTime ();
}

uint Transaction::remainingTime() const
{
	return d->p->remainingTime ();
}

uint Transaction::speed() const
{
	return d->p->speed ();
}

Client::Action Transaction::role() const
{
	if(d->oldtrans)
		return d->role;

	return (Client::Action) Util::enumFromString<Client>(d->p->role (), "Action", "Action");
}

void Transaction::setHints(const QStringList& hints)
{
	d->p->SetHints(hints);
}

void Transaction::setHints(const QString& hints)
{
	setHints(QStringList() << hints);
}

Transaction::Status Transaction::status() const
{
	return (Transaction::Status) Util::enumFromString<Transaction>(d->p->status (), "Status", "Status");
}

QDateTime Transaction::timespec() const
{
	return d->timespec;
}

bool Transaction::succeeded() const
{
	return d->succeeded;
}

uint Transaction::duration() const
{
	return d->duration;
}

QString Transaction::data() const
{
	return d->data;
}

uint Transaction::uid() const
{
	if(d->p) {
		return d->p->uid();
	}
	return d->uid;
}

QString Transaction::cmdline() const
{
	return d->cmdline;
}

#include "transaction.moc"
