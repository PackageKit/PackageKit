/*
 * This file is part of the QPackageKit project
 * Copyright (C) 2008 Adrien Bustany <madcat@mymadcat.com>
 * Copyright (C) 2010-2012 Daniel Nicoletti <dantti12@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB. If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef PACKAGEKIT_DAEMON_H
#define PACKAGEKIT_DAEMON_H

#include <QtCore/QObject>
#include <QtCore/QMetaEnum>

#include "transaction.h"

namespace PackageKit {

/**
 * \class Daemon daemon.h Daemon
 * \author Adrien Bustany \e <madcat@mymadcat.com>
 * \author Daniel Nicoletti \e <dantti12@gmail.com>
 *
 * \brief Base class used to interact with the PackageKit daemon
 *
 * This class holds all the functions enabling the user to interact with the PackageKit daemon.
 *
 * Most methods are static so that you can just call Daemon::backendName() to get the name of the backend.
 * 
 * This class is a singleton, its constructor is private. Call Daemon::global() to get
 * an instance of the Daemon object, you only need Daemon::global() when connecting to the signals
 * of this class.
 */
class DaemonPrivate;
class Daemon : public QObject
{
    Q_OBJECT
    Q_ENUMS(Network)
    Q_ENUMS(Authorize)
public:
    /**
     * Describes the current network state
     */
    enum Network {
        NetworkUnknown,
        NetworkOffline,
        NetworkOnline,
        NetworkWired,
        NetworkWifi,
        NetworkMobile
    };

    /**
     * Describes the authorization result
     * \sa canAuthorize()
     */
    enum Authorize {
        AuthorizeUnknown,
        AuthorizeYes,
        AuthorizeNo,
        AuthorizeInteractive
    };

    /**
     * \brief Returns an instance of the Daemon
     *
     * The Daemon class is a singleton, you can call this method several times,
     * a single Daemon object will exist.
     * Use this only when connecting to this class signals
     */
    static Daemon* global();

    /**
     * Destructor
     */
    ~Daemon();

    /**
     * Returns all the actions supported by the current backend
     */
    static Transaction::Roles actions();

    /**
     * The backend name, e.g. "yum".
     */
    static QString backendName();

    /**
     * The backend description, e.g. "Yellow Dog Update Modifier".
     */
    static QString backendDescription();

    /**
     * The backend author, e.g. "Joe Bloggs <joe@blogs.com>"
     */
    static QString backendAuthor();

    /**
     * Returns the package filters supported by the current backend
     */
    static Transaction::Filters filters();

    /**
     * Returns the package groups supported by the current backend
     */
    static Transaction::Groups groups();

    /**
     * Set when the backend is locked and native tools would fail.
     */
    static bool locked();

    /**
     * Returns a list containing the MIME types supported by the current backend
     */
    static QStringList mimeTypes();

    /**
     * Returns the current network state
     */
    static Daemon::Network networkState();

    /**
     * The distribution identifier in the
     * distro;version;arch form,
     * e.g. "debian;squeeze/sid;x86_64".
     */
    static QString distroId();

    /**
     * Allows a client to find out if it would be allowed to authorize an action.
     * The action ID, e.g. org.freedesktop.packagekit.system-network-proxy-configure
     * specified in \p actionId
     * Returm might be either yes, no or interactive \sa Authorize.
     */
    static Authorize canAuthorize(const QString &actionId);

    /**
     * Returns the time (in seconds) since the specified \p action
     */
    static uint getTimeSinceAction(Transaction::Role action);

    /**
     * \brief creates a new transaction path
     * 
     * This function register a new DBus path on PackageKit
     * allowing a \c Transaction object to be created.
     * 
     * \note Unless you want to know the transaction id
     * before creating the \c Transaction object this function
     * is not useful as simply creating a \c Transaction object will
     * automatically create this path.
     */
    static QDBusObjectPath getTid();

    /**
     * Returns the list of current transactions
     */
    static QList<QDBusObjectPath> getTransactionList();

    /**
     * Convenience function
     * Returns the list of current transactions as \c Transaction objects
     *
     * You must delete these yourself or pass a
     * \p parent for these comming transactions
     */
    static QList<Transaction*> getTransactionObjects(QObject *parent = 0);

    /**
     * \brief Sets a global hints for all the transactions to be created
     *
     * This method allows the calling session to set transaction \p hints for
     * the package manager which can change as the transaction runs.
     *
     * This method can be sent before the transaction has been run
     * (by using Daemon::setHints) or whilst it is running
     * (by using Transaction::setHints).
     * There is no limit to the number of times this
     * method can be sent, although some backends may only use the values
     * that were set before the transaction was started.
     *
     * The \p hints can be filled with entries like these
     * ('locale=en_GB.utf8','idle=true','interactive=false').
     *
     * \sa Transaction::setHints
     */
    static void setHints(const QStringList &hints);

    /**
     * Convenience function to set global hints
     * \sa setHints(const QStringList &hints)
     */
    static void setHints(const QString &hints);

    /**
     * This method returns the current hints
     */
    static QStringList hints();

    /**
     * Sets a proxy to be used for all the network operations
     */
    static Transaction::InternalError setProxy(const QString &http_proxy, const QString &https_proxy, const QString &ftp_proxy, const QString &socks_proxy, const QString &no_proxy, const QString &pac);

    /**
     * \brief Tells the daemon that the system state has changed, to make it reload its cache
     *
     * \p reason can be resume or posttrans
     */
    static void stateHasChanged(const QString &reason);

    /**
     * Asks PackageKit to quit, for example to let a native package manager operate
     */
    static void suggestDaemonQuit();

    /**
     * Returns the major version number.
     */
    static uint versionMajor();

    /**
     * The minor version number.
     */
    static uint versionMinor();

    /**
     * The micro version number.
     */
    static uint versionMicro();
    
    /**
     * Returns the string representing the enum
     * Useful for PackageDetails::Group
     */
    template<class T> static QString enumToString(int value, const char *enumName)
    {
        QString prefix = enumName;
        int id = T::staticMetaObject.indexOfEnumerator(enumName);
        QMetaEnum e = T::staticMetaObject.enumerator(id);
        if (!e.isValid ()) {
//             qDebug() << "Invalid enum " << prefix;
            return QString();
        }
        QString enumString = e.valueToKey(value);
        if (enumString.isNull()) {
//             qDebug() << "Enum key not found while searching for value" << QString::number(value) << "in enum" << prefix;
            return QString();
        }

        // Remove the prefix
        if(!prefix.isNull() && enumString.indexOf(prefix) == 0) {
            enumString.remove(0, prefix.length());
        }

        QString pkName;
        for(int i = 0 ; i < enumString.length() - 1 ; ++i) {
            pkName += enumString[i];
            if(enumString[i+1].isUpper())
                pkName += QChar('-');
        }
        pkName += enumString[enumString.length() - 1];

        return pkName.toLower();
    }
    
    template<class T> static int enumFromString(const QString &str, const char *enumName)
    {
        QString prefix = enumName;
        QString realName;
        bool lastWasDash = false;
        QChar buf;

        for(int i = 0 ; i < str.length() ; ++i) {
            buf = str[i].toLower();
            if(i == 0 || lastWasDash) {
                buf = buf.toUpper();
            }

            lastWasDash = false;
            if(buf == QLatin1Char('-')) {
                lastWasDash = true;
            } else if(buf == QLatin1Char('~')) {
                lastWasDash = true;
                realName += "Not";
            } else {
                realName += buf;
            }
        };

        if (!prefix.isNull()) {
            realName = prefix + realName;
        }

        int id = T::staticMetaObject.indexOfEnumerator(enumName);
        QMetaEnum e = T::staticMetaObject.enumerator(id);
        int enumValue = e.keyToValue(realName.toAscii().data());

        if (enumValue == -1) {
            enumValue = e.keyToValue(prefix.append("Unknown").toAscii().data());
            if (!QString(enumName).isEmpty()) {
//                 qDebug() << "enumFromString (" << enumName << ") : converted" << str << "to" << QString("Unknown").append(enumName) << ", enum id" << id;
            }
        }
        return enumValue;
    }

Q_SIGNALS:
    /**
     * This signal is emitted when a property on the interface changes.
     */
    void changed();

    /**
     * Emitted when the list of repositories changes
     */
    void repoListChanged();

    /**
     * Emmitted when a restart is scheduled
     */
    void restartScheduled();

    /**
     * \brief Emitted when the current transactions list changes.
     *
     * \note This is mostly useful for monitoring the daemon's state.
     */
    void transactionListChanged(const QStringList &tids);

    /**
     * Emitted when new updates are available
     */
    void updatesChanged();

    /**
     * Emitted when the daemon quits
     */
    void daemonQuit();

protected:
    DaemonPrivate * const d_ptr;

private:
    Q_DECLARE_PRIVATE(Daemon);
    Q_PRIVATE_SLOT(d_ptr, void serviceUnregistered());
    Daemon(QObject *parent = 0);
    static Daemon *m_global;
};

} // End namespace PackageKit

#endif
