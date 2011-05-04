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

#ifndef PACKAGEKIT_TRANSACTION_PRIVATE_H
#define PACKAGEKIT_TRANSACTION_PRIVATE_H

#include <QtCore/QString>
#include <QtCore/QHash>

#include "transaction.h"

class TransactionProxy;

namespace PackageKit {

class TransactionPrivate
{
    Q_DECLARE_PUBLIC(Transaction)
protected:
    TransactionPrivate(Transaction *parent);
    virtual ~TransactionPrivate() {};

    static QString filtersToString(const QFlags<PackageKit::Transaction::Filter> &flags);

    QString tid;
    ::TransactionProxy* p;
    Transaction *q_ptr;

    // Only used for old transactions
    bool oldtrans;
    QDateTime timespec;
    Transaction::Role role;
    bool succeeded;
    uint duration;
    QString data;
    uint uid;
    QString cmdline;
    // used for both old and destroyed transactions
    bool destroyed;

    Transaction::InternalError error;

protected Q_SLOTS:
    void details(const QString &pid, const QString &license, const QString &group, const QString &detail, const QString &url, qulonglong size);
    void distroUpgrade(const QString &type, const QString &name, const QString &description);
    void errorCode(const QString &error, const QString &details);
    void eulaRequired(const QString &eulaId, const QString &pid, const QString& vendor, const QString& licenseAgreement);
    void mediaChangeRequired(const QString& mediaType, const QString& mediaId, const QString& mediaText);
    void files(const QString& pid, const QString& filenames);
    void finished(const QString& exitCode, uint runtime);
    void message(const QString& type, const QString& message);
    void package(const QString& info, const QString& pid, const QString& summary);
    void repoSignatureRequired(const QString& pid, const QString& repoName, const QString& keyUrl, const QString& keyUserid, const QString& keyId, const QString& keyFingerprint, const QString& keyTimestamp, const QString& type);
    void requireRestart(const QString& type, const QString& pid);
    void transaction(const QString& oldTid, const QString& timespec, bool succeeded, const QString& role, uint duration, const QString& data, uint uid, const QString& cmdline);
    void updateDetail(const QString& pid, const QString& updates, const QString& obsoletes, const QString& vendorUrl, const QString& bugzillaUrl, const QString& cveUrl, const QString& restart, const QString& updateText, const QString& changelog, const QString& state, const QString& issued, const QString& updated);
    void destroy();
    void daemonQuit();
};

} // End namespace PackageKit

#endif
