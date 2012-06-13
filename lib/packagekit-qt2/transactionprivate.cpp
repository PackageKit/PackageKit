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

#include <QtCore/QStringList>

#include "transactionprivate.h"

#include "package.h"
#include "signature.h"
#include "eula.h"

using namespace PackageKit;

TransactionPrivate::TransactionPrivate(Transaction* parent) :
    q_ptr(parent),
    p(0),
    destroyed(false)
{
}

void TransactionPrivate::details(const QString &pid,
                                 const QString &license,
                                 uint group,
                                 const QString &detail,
                                 const QString &url,
                                 qulonglong size)
{
    Q_Q(Transaction);
    PackageDetails package(pid,
                           license,
                           group,
                           detail,
                           url,
                           size);

    q->packageDetails(package);
}

void TransactionPrivate::distroUpgrade(uint type, const QString &name, const QString &description)
{
    Q_Q(Transaction);
    q->distroUpgrade(static_cast<Transaction::DistroUpgrade>(type),
                     name,
                     description);
}

void TransactionPrivate::errorCode(uint error, const QString &details)
{
    Q_Q(Transaction);
    q->errorCode(static_cast<Transaction::Error>(error), details);
}

void TransactionPrivate::eulaRequired(const QString &eulaId, const QString &pid, const QString &vendor, const QString &licenseAgreement)
{
    Q_Q(Transaction);
    Eula eula;
    eula.id = eulaId;
    eula.package = Package(pid);
    eula.vendor = vendor;
    eula.licenseAgreement = licenseAgreement;

    q->eulaRequired(eula);
}

void TransactionPrivate::mediaChangeRequired(uint mediaType, const QString &mediaId, const QString &mediaText)
{
    Q_Q(Transaction);
    q->mediaChangeRequired(static_cast<Transaction::MediaType>(mediaType),
                           mediaId,
                           mediaText);
}

void TransactionPrivate::files(const QString &pid, const QStringList &fileList)
{
    Q_Q(Transaction);
    q->files(Package(pid), fileList);
}

void TransactionPrivate::finished(uint exitCode, uint runtime)
{
    Q_Q(Transaction);
    q->finished(static_cast<Transaction::Exit>(exitCode), runtime);
}

void TransactionPrivate::destroy()
{
    Q_Q(Transaction);
    q->deleteLater();
}

void TransactionPrivate::daemonQuit()
{
    Q_Q(Transaction);
    q->finished(Transaction::ExitFailed, 0);
    destroy();
}

void TransactionPrivate::message(uint type, const QString &message)
{
    Q_Q(Transaction);
    q->message(static_cast<Transaction::Message>(type), message);
}

void TransactionPrivate::package(uint info, const QString &pid, const QString &summary)
{
    Q_Q(Transaction);
    q->package(Package(pid, static_cast<Package::Info>(info), summary));
}

void TransactionPrivate::repoSignatureRequired(const QString &pid,
                                               const QString &repoName,
                                               const QString &keyUrl,
                                               const QString &keyUserid,
                                               const QString &keyId,
                                               const QString &keyFingerprint,
                                               const QString &keyTimestamp,
                                               uint type)
{
    Q_Q(Transaction);
    Signature i;
    i.package = Package(pid);
    i.repoId = repoName;
    i.keyUrl = keyUrl;
    i.keyUserid = keyUserid;
    i.keyId = keyId;
    i.keyFingerprint = keyFingerprint;
    i.keyTimestamp = keyTimestamp;
    i.type = static_cast<Signature::Type>(type);

    q->repoSignatureRequired(i);
}

void TransactionPrivate::requireRestart(uint type, const QString &pid)
{
    Q_Q(Transaction);
    q->requireRestart(static_cast<PackageUpdateDetails::Restart>(type), Package(pid));
}

void TransactionPrivate::transaction(const QDBusObjectPath &oldTid,
                                     const QString &timespec,
                                     bool succeeded,
                                     uint role,
                                     uint duration,
                                     const QString &data,
                                     uint uid,
                                     const QString &cmdline)
{
    Q_Q(Transaction);
    q->transaction(new Transaction(oldTid, timespec, succeeded, static_cast<Transaction::Role>(role), duration, data, uid, cmdline, q->parent()));
}

void TransactionPrivate::updateDetail(const QString &package_id,
                                      const QStringList &updates,
                                      const QStringList &obsoletes,
                                      const QStringList &vendor_urls,
                                      const QStringList &bugzilla_urls,
                                      const QStringList &cve_urls,
                                      uint restart,
                                      const QString &update_text,
                                      const QString &changelog,
                                      uint state,
                                      const QString &issued,
                                      const QString &updated)
{
    Q_Q(Transaction);

    PackageUpdateDetails package(package_id,
                                 updates,
                                 obsoletes,
                                 vendor_urls,
                                 bugzilla_urls,
                                 cve_urls,
                                 restart,
                                 update_text,
                                 changelog,
                                 state,
                                 QDateTime::fromString(issued, Qt::ISODate),
                                 QDateTime::fromString(updated, Qt::ISODate));

    q->packageUpdateDetails(package);
}

QStringList TransactionPrivate::packageListToPids(const PackageList &packages) const
{
    QStringList pids;
    foreach (const Package &package, packages) {
        pids << package;
    }
    return pids;
}
