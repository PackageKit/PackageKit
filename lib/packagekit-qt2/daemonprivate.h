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

#ifndef DAEMON_PRIVATE_H
#define DAEMON_PRIVATE_H

#include <QtCore/QStringList>
#include <QtDBus/QDBusServiceWatcher>

#include "daemon.h"

class DaemonProxy;

namespace PackageKit {

class DaemonPrivate
{
    Q_DECLARE_PUBLIC(Daemon)
protected:
    DaemonPrivate(Daemon *parent);
    virtual ~DaemonPrivate() {};

    Daemon *q_ptr;
    ::DaemonProxy *daemon;

    QStringList hints;

    QList<Transaction*> transactions(const QStringList &tids, QObject *parent);

    /*
     * Describes the different errors that might happen on the bus
     *
     * \sa Daemon::error
     * \sa Transaction::error
     */
    typedef enum {
        NoError = 0,
        ErrorFailed,
        ErrorFailedAuth,
        ErrorNoTid,
        ErrorAlreadyTid,
        ErrorRoleUnkown,
        ErrorCannotStartDaemon,
        ErrorInvalidInput,
        ErrorInvalidFile,
        ErrorNotSupported
    } TransactionError;

protected Q_SLOTS:
    void serviceUnregistered();

private:
    QDBusServiceWatcher *m_watcher;
};

} // End namespace PackageKit

#endif
