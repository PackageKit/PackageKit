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
#include "util.h"

using namespace PackageKit;

TransactionPrivate::TransactionPrivate(Transaction* parent) :
    q_ptr(parent),
    p(0),
    destroyed(false)
{
}

void TransactionPrivate::details(const QString &pid,
                                 const QString &license,
                                 const QString &group,
                                 const QString &detail,
                                 const QString &url,
                                 qulonglong size)
{
    Q_Q(Transaction);
    Package package(pid);
    package.setLicense(license);
    package.setGroup(static_cast<Package::Group>(Util::enumFromString<Package>(group, "Group", "Group")));
    package.setDescription(detail);
    package.setUrl(url);
    package.setSize(size);

    q->package(package);
}

QString TransactionPrivate::filtersToString(const QFlags<PackageKit::Transaction::Filter> &flags)
{
    QStringList flagStrings;
    for (int i = Transaction::UnknownFilter; i < Transaction::FilterLast; i *= 2) {
        if (static_cast<Transaction::Filter>(i) & flags) {
            flagStrings.append(Util::enumToString<Transaction>(static_cast<Transaction::Filter>(i), "Filter", "Filter"));
        }
    }

    return flagStrings.join(";");
}

void TransactionPrivate::distroUpgrade(const QString &type, const QString &name, const QString &description)
{
    Q_Q(Transaction);
    int distroUpgradeType = Util::enumFromString<Transaction>(type, "DistroUpgrade", "DistroUpgrade");
    q->distroUpgrade(static_cast<Transaction::DistroUpgrade>(distroUpgradeType),
                     name,
                     description);
}

void TransactionPrivate::errorCode(const QString &error, const QString &details)
{
    Q_Q(Transaction);
    q->errorCode(static_cast<Transaction::Error>(Util::enumFromString<Transaction>(error, "Error", "Error")), details);
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

void TransactionPrivate::mediaChangeRequired(const QString &mediaType, const QString &mediaId, const QString &mediaText)
{
    Q_Q(Transaction);
    int mediaTypeEnum = Util::enumFromString<Transaction>(mediaType, "MediaType", "Media");
    q->mediaChangeRequired(static_cast<Transaction::MediaType>(mediaTypeEnum),
                           mediaId,
                           mediaText);
}

void TransactionPrivate::files(const QString &pid, const QString &filenames)
{
    Q_Q(Transaction);
    q->files(Package(pid), filenames.split(";"));
}

void TransactionPrivate::finished(const QString &exitCode, uint runtime)
{
    Q_Q(Transaction);
    int exitValue = Util::enumFromString<Transaction>(exitCode, "Exit", "Exit");
    q->finished(static_cast<Transaction::Exit>(exitValue), runtime);
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

void TransactionPrivate::message(const QString &type, const QString &message)
{
    Q_Q(Transaction);
    q->message(static_cast<Transaction::Message>(Util::enumFromString<Transaction>(type, "Message", "Message")), message);
}

void TransactionPrivate::package(const QString &info, const QString &pid, const QString &summary)
{
    Q_Q(Transaction);
    q->package(Package(pid, static_cast<Package::Info>(Util::enumFromString<Package>(info, "Info", "Info")), summary));
}

void TransactionPrivate::repoSignatureRequired(const QString &pid,
                                               const QString &repoName,
                                               const QString &keyUrl,
                                               const QString &keyUserid,
                                               const QString &keyId,
                                               const QString &keyFingerprint,
                                               const QString &keyTimestamp,
                                               const QString &type)
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
    i.type = static_cast<Signature::Type>(Util::enumFromString<Signature>(type, "Type", "Signature"));

    q->repoSignatureRequired(i);
}

void TransactionPrivate::requireRestart(const QString &type, const QString &pid)
{
    Q_Q(Transaction);
    q->requireRestart(static_cast<Package::Restart>(Util::enumFromString<Package>(type, "Restart", "Restart")), Package(pid));
}

void TransactionPrivate::transaction(const QString &oldTid,
                                     const QString &timespec,
                                     bool succeeded,
                                     const QString &role,
                                     uint duration,
                                     const QString &data,
                                     uint uid,
                                     const QString &cmdline)
{
    Q_Q(Transaction);
    q->transaction(new Transaction(oldTid, timespec, succeeded, role, duration, data, uid, cmdline, q->parent()));
}

void TransactionPrivate::updateDetail(const QString &pid,
                                      const QString &updates,
                                      const QString &obsoletes,
                                      const QString &vendorUrl,
                                      const QString &bugzillaUrl,
                                      const QString &cveUrl,
                                      const QString &restart,
                                      const QString &updateText,
                                      const QString &changelog,
                                      const QString &state,
                                      const QString &issued,
                                      const QString &updated)
{
    Q_Q(Transaction);

    Package package(pid);
    if (!updates.isEmpty()) {
        QList<Package> updatesList;
        foreach (const QString &pid, updates.split("&")) {
            updatesList << Package(pid);
        }
        package.setUpdates(updatesList);
    }
    if( !obsoletes.isEmpty() ) {
        QList<Package> obsoletesList;
        foreach (const QString &pid, obsoletes.split("&")) {
            obsoletesList << Package(pid);
        }
        package.setObsoletes(obsoletesList);
    }
    package.setVendorUrl(vendorUrl);
    package.setBugzillaUrl(bugzillaUrl);
    package.setCveUrl(cveUrl);
    package.setRestart(static_cast<Package::Restart>(Util::enumFromString<Package>(restart, "Restart", "Restart")));
    package.setUpdateText(updateText);
    package.setChangelog(changelog);
    package.setState(static_cast<Package::UpdateState>(Util::enumFromString<Package>(state, "UpdateState", "UpdateState")));
    package.setIssued(QDateTime::fromString(issued, Qt::ISODate));
    package.setUpdated(QDateTime::fromString(updated, Qt::ISODate));

    q->package(package);
}
