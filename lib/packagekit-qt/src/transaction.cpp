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

	connect(d->p, SIGNAL(AllowCancel(bool)), this, SIGNAL(allowCancelChanged(bool)));
	connect(d->p, SIGNAL(Category(const QString&, const QString&, const QString&, const QString&, const QString&)), this, SIGNAL(category(const QString&, const QString&, const QString&, const QString&, const QString&)));
	connect(d->p, SIGNAL(Details(const QString&, const QString&, const QString&, const QString&, const QString&, qulonglong)), d, SLOT(details(const QString&, const QString&, const QString&, const QString&, const QString&, qulonglong)));
	connect(d->p, SIGNAL(DistroUpgrade(const QString&, const QString&, const QString&)), d, SLOT(distroUpgrade(const QString&, const QString&, const QString&)));
	connect(d->p, SIGNAL(ErrorCode(const QString&, const QString&)), d, SLOT(errorCode(const QString&, const QString&)));
	connect(d->p, SIGNAL(Files(const QString&, const QString&)), d, SLOT(files(const QString&, const QString&)));
	connect(d->p, SIGNAL(Finished(const QString&, uint)), d, SLOT(finished(const QString&, uint)));
	connect(d->p, SIGNAL(Message(const QString&, const QString&)), d, SLOT(message(const QString&, const QString&)));
	connect(d->p, SIGNAL(Package(const QString&, const QString&, const QString&)), d, SLOT(package(const QString&, const QString&, const QString&)));
	connect(d->p, SIGNAL(ProgressChanged(uint, uint, uint, uint)), d, SLOT(progressChanged(uint, uint, uint, uint)));
	connect(d->p, SIGNAL(RepoDetail(const QString&, const QString&, bool)), this, SIGNAL(repoDetail(const QString&, const QString&, bool)));
	connect(d->p, SIGNAL(RepoSignatureRequired(const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&)), d, SLOT(repoSignatureRequired(const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&, const QString&)));
	connect(d->p, SIGNAL(EulaRequired(const QString&, const QString&, const QString&, const QString&)), d, SLOT(eulaRequired(const QString&, const QString&, const QString&, const QString&)));
	connect(d->p, SIGNAL(MediaChangeRequired(const QString&, const QString&, const QString&)), d, SLOT(mediaChangeRequired(const QString&, const QString&, const QString&)));
	connect(d->p, SIGNAL(RequireRestart(const QString&, const QString&)), d, SLOT(requireRestart(const QString&, const QString&)));
	connect(d->p, SIGNAL(StatusChanged(const QString&)), d, SLOT(statusChanged(const QString&)));
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
	RoleInfo r;
	r.action = (Client::Action)Util::enumFromString<Client>(role, "Action", "Action");
	d->role = r;
	d->duration = duration;
	d->data = data;
	d->uid = uid;
	d->cmdline = cmdline;
	d->client = parent;
}

Transaction::~Transaction()
{
//	qDebug() << "Destroying transaction with tid" << tid();
}

QString Transaction::tid() const
{
	return d->tid;
}

bool Transaction::allowCancel()
{
	return d->p->GetAllowCancel().value();
}

bool Transaction::callerActive()
{
	return d->p->IsCallerActive().value();
}

void Transaction::cancel()
{
	d->p->Cancel();
}

Package* Transaction::lastPackage()
{
	return new Package(d->p->GetPackageLast().value());
}

Transaction::ProgressInfo Transaction::progress()
{
	uint p, subp, elaps, rem;
	p = d->p->GetProgress(subp, elaps, rem);
	ProgressInfo i;
	i.percentage = p;
	i.subpercentage = subp;
	i.elapsed = elaps;
	i.remaining = rem;

	return i;
}

Transaction::RoleInfo Transaction::role()
{
	if(d->oldtrans)
		return d->role;

	QString terms;
	RoleInfo i;

	int roleValue = Util::enumFromString<Client>(d->p->GetRole(terms).value(), "Action", "Action");
	if(roleValue == -1)
		i.action = Client::UnkownAction;
	else
		i.action = (Client::Action)roleValue;

	i.terms = terms.split(";");

	return i;
}

void Transaction::setLocale(const QString& locale)
{
	d->p->SetLocale(locale);
}

Transaction::Status Transaction::status()
{
	int statusValue = Util::enumFromString<Transaction>(d->p->GetStatus().value(), "Status", "Status");
	if(statusValue == -1)
		return UnknownStatus;
	else
		return (Transaction::Status)statusValue;
}

QDateTime Transaction::timespec()
{
	return d->timespec;
}

bool Transaction::succeeded()
{
	return d->succeeded;
}

uint Transaction::duration()
{
	return d->duration;
}

QString Transaction::data()
{
	return d->data;
}

uint Transaction::uid()
{
	return d->uid;
}

QString Transaction::cmdline()
{
	return d->cmdline;
}

#include "transaction.moc"

