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

#include "transactionprivate.h"
#include "transaction.h"
#include "package.h"
#include "util.h"

using namespace PackageKit;

TransactionPrivate::TransactionPrivate(Transaction* parent) : QObject(parent), t(parent), p(0)
{
}

TransactionPrivate::~TransactionPrivate()
{
}

// Signals mangling

void TransactionPrivate::details(const QString& pid, const QString& license, const QString& group, const QString& detail, const QString& url, qulonglong size)
{
	QSharedPointer<Package> p = packageMap.value(pid, QSharedPointer<Package> (NULL));
	if(p) {
		packageMap.remove(pid);
	} else {
		p = QSharedPointer<Package> (new Package(pid));
	}

	Package::Details* d = new Package::Details(p, license, group, detail, url, size);
	p->setDetails(d);
	t->details(p);
}

void TransactionPrivate::distroUpgrade(const QString& type, const QString& name, const QString& description)
{
	t->distroUpgrade((Enum::DistroUpgrade)Util::enumFromString<Enum>(type, "DistroUpgrade", "DistroUpgrade"), name, description);
}

void TransactionPrivate::errorCode(const QString& error, const QString& details)
{
	t->errorCode((Enum::Error)Util::enumFromString<Enum>(error, "Error", "Error"), details);
}

void TransactionPrivate::eulaRequired(const QString& eulaId, const QString& pid, const QString& vendor, const QString& licenseAgreement)
{
	Client::EulaInfo i;
	i.id = eulaId;
	i.package = QSharedPointer<Package> (new Package(pid));
	i.vendorName = vendor;
	i.licenseAgreement = licenseAgreement;
	t->eulaRequired(i);
}

void TransactionPrivate::mediaChangeRequired(const QString& mediaType, const QString& mediaId, const QString& mediaText)
{
	t->mediaChangeRequired((Enum::MediaType)Util::enumFromString<Enum>(mediaType, "MediaType", "Media"), mediaId, mediaText);
}

void TransactionPrivate::files(const QString& pid, const QString& filenames)
{
	t->files(QSharedPointer<Package> (new Package(pid)), filenames.split(";"));
}

void TransactionPrivate::finished(const QString& exitCode, uint runtime)
{
	int exitValue = Util::enumFromString<Enum>(exitCode, "Exit", "Exit");
	t->finished((Enum::Exit)exitValue, runtime);
}

void TransactionPrivate::destroy()
{
	emit t->destroy();
	client->destroyTransaction(tid);
}

void TransactionPrivate::message(const QString& type, const QString& message)
{
	t->message((Enum::Message)Util::enumFromString<Enum>(type, "Message", "Message"), message);
}

void TransactionPrivate::package(const QString& info, const QString& pid, const QString& summary)
{
	t->package(QSharedPointer<Package> (new Package(pid, info, summary)));
}

void TransactionPrivate::repoSignatureRequired(const QString& pid, const QString& repoName, const QString& keyUrl, const QString& keyUserid, const QString& keyId, const QString& keyFingerprint, const QString& keyTimestamp, const QString& type)
{
	Client::SignatureInfo i;
	i.package = QSharedPointer<Package> (new Package(pid));
	i.repoId = repoName;
	i.keyUrl = keyUrl;
	i.keyUserid = keyUserid;
	i.keyId = keyId;
	i.keyFingerprint = keyFingerprint;
	i.keyTimestamp = keyTimestamp;
	i.type = (Enum::SigType)Util::enumFromString<Enum>(type, "SigType", "Signature");

	t->repoSignatureRequired(i);
}

void TransactionPrivate::requireRestart(const QString& type, const QString& pid)
{
	t->requireRestart((Enum::Restart)Util::enumFromString<Enum>(type, "Restart", "Restart"), QSharedPointer<Package> (new Package(pid)));
}

void TransactionPrivate::transaction(const QString& oldTid, const QString& timespec, bool succeeded, const QString& role, uint duration, const QString& data, uint uid, const QString& cmdline)
{
	t->transaction(new Transaction(oldTid, timespec, succeeded, role, duration, data, uid, cmdline, client));
}

void TransactionPrivate::updateDetail(const QString& pid, const QString& updates, const QString& obsoletes, const QString& vendorUrl, const QString& bugzillaUrl, const QString& cveUrl, const QString& restart, const QString& updateText, const QString& changelog, const QString& state, const QString& issued, const QString& updated)
{
	Client::UpdateInfo i;
	i.package = QSharedPointer<Package> (new Package(pid));
	if( !updates.isEmpty() ) {
		foreach(const QString p, updates.split("&")) {
			i.updates.append(QSharedPointer<Package> (new Package(p)));
		}
	}
	if( !obsoletes.isEmpty() ) {
		foreach(const QString p, obsoletes.split("&")) {
			i.obsoletes.append(QSharedPointer<Package> (new Package(p)));
		}
	}
	i.vendorUrl = vendorUrl;
	i.bugzillaUrl = bugzillaUrl;
	i.cveUrl = cveUrl;
	i.restart = (Enum::Restart)Util::enumFromString<Enum>(restart, "Restart", "Restart");
	i.updateText = updateText;
	i.changelog = changelog;
	i.state = (Enum::UpdateState)Util::enumFromString<Enum>(state, "UpdateState", "Update");
	i.issued = QDateTime::fromString(issued, Qt::ISODate);
	i.updated = QDateTime::fromString(updated, Qt::ISODate);

	t->updateDetail(i);
}

#include "transactionprivate.moc"
