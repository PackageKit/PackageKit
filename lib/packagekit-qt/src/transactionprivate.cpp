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
#include "util.h"

using namespace PackageKit;

TransactionPrivate::TransactionPrivate(Transaction* parent) : QObject(parent), t(parent)
{
}

TransactionPrivate::~TransactionPrivate()
{
}

// Signals mangling

void TransactionPrivate::details(const QString& pid, const QString& license, const QString& group, const QString& detail, const QString& url, qulonglong size)
{
	Package* p = packageMap.value(pid, NULL);
	if(p) {
		packageMap.remove(pid);
	} else {
		p = new Package(pid);
	}

	Package::Details* d = new Package::Details(p, license, group, detail, url, size);
	p->setDetails(d);
	t->details(p);
}

void TransactionPrivate::distroUpgrade(const QString& type, const QString& name, const QString& description)
{
	t->distroUpgrade((Client::UpgradeType)Util::enumFromString<Client>(type, "UpgradeType", "Upgrade"), name, description);
}

void TransactionPrivate::errorCode(const QString& error, const QString& details)
{
	t->errorCode((Client::ErrorType)Util::enumFromString<Client>(error, "ErrorType"), details);
}

void TransactionPrivate::eulaRequired(const QString& eulaId, const QString& pid, const QString& vendor, const QString& licenseAgreement)
{
	Client::EulaInfo i;
	i.id = eulaId;
	i.package = new Package(pid);
	i.vendorName = vendor;
	i.licenseAgreement = licenseAgreement;
	t->eulaRequired(i);
}

void TransactionPrivate::files(const QString& pid, const QString& filenames)
{
	t->files(new Package(pid), filenames.split(";"));
}

void TransactionPrivate::finished(const QString& exitCode, uint runtime)
{
	int exitValue = Util::enumFromString<Transaction>(exitCode, "ExitStatus");
	t->finished((Transaction::ExitStatus)exitValue, runtime);
	t->destroyed(tid);

	t->deleteLater();
}

void TransactionPrivate::message(const QString& type, const QString& message)
{
	t->message((Client::MessageType)Util::enumFromString<Client>(type, "MessageType"), message);
}

void TransactionPrivate::package(const QString& info, const QString& pid, const QString& summary)
{
	t->package(new Package(pid, info, summary));
}

void TransactionPrivate::progressChanged(uint percentage, uint subpercentage, uint elapsed, uint remaining)
{
	Transaction::ProgressInfo i;
	i.percentage = percentage;
	i.subpercentage = subpercentage;
	i.elapsed = elapsed;
	i.remaining = remaining;
	t->progressChanged(i);
}

void TransactionPrivate::repoSignatureRequired(const QString& pid, const QString& repoName, const QString& keyUrl, const QString& keyUserid, const QString& keyId, const QString& keyFingerprint, const QString& keyTimestamp, const QString& type)
{
	Client::SignatureInfo i;
	i.package = new Package(pid);
	i.repoId = repoName;
	i.keyUrl = keyUrl;
	i.keyUserid = keyUserid;
	i.keyId = keyId;
	i.keyFingerprint = keyFingerprint;
	i.keyTimestamp = keyTimestamp;
	i.type = (Client::SignatureType)Util::enumFromString<Client>(type, "SignatureType");

	t->repoSignatureRequired(i);
}

void TransactionPrivate::requireRestart(const QString& type, const QString& pid)
{
	t->requireRestart((Client::RestartType)Util::enumFromString<Client>(type, "RestartType", "Restart"), new Package(pid));
}

void TransactionPrivate::statusChanged(const QString& status)
{
	t->statusChanged((Transaction::Status)Util::enumFromString<Transaction>(status, "Status"));
}

void TransactionPrivate::transaction(const QString& oldTid, const QString& timespec, bool succeeded, const QString& role, uint duration, const QString& data, uint uid, const QString& cmdline)
{
	t->transaction(new Transaction(oldTid, timespec, succeeded, role, duration, data, uid, cmdline, client));
}

void TransactionPrivate::updateDetail(const QString& pid, const QString& updates, const QString& obsoletes, const QString& vendorUrl, const QString& bugzillaUrl, const QString& cveUrl, const QString& restart, const QString& updateText, const QString& changelog, const QString& state, const QString& issued, const QString& updated)
{
	Client::UpdateInfo i;
	i.package = new Package(pid);
	if( !updates.isEmpty() ) {
		foreach(const QString p, updates.split("&")) {
			i.updates.append(new Package(p));
		}
	}
	if( !obsoletes.isEmpty() ) {
		foreach(const QString p, obsoletes.split("&")) {
			i.obsoletes.append(new Package(p));
		}
	}
	i.vendorUrl = vendorUrl;
	i.bugzillaUrl = bugzillaUrl;
	i.cveUrl = cveUrl;
	i.restart = (Client::RestartType)Util::enumFromString<Client>(restart, "RestartType", "Restart");
	i.updateText = updateText;
	i.changelog = changelog;
	i.state = (Client::UpgradeType)Util::enumFromString<Client>(state, "UpgradeType", "Upgrade");
	i.issued = QDateTime::fromString(issued, Qt::ISODate);
	i.updated = QDateTime::fromString(updated, Qt::ISODate);

	t->updateDetail(i);
}

#include "transactionprivate.moc"

