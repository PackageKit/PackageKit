/***************************************************************************
 *   Copyright (C) 2012 by Daniel Nicoletti                                *
 *   dantti12@gmail.com                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; see the file COPYING. If not, write to       *
 *   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,  *
 *   Boston, MA 02110-1301, USA.                                           *
 ***************************************************************************/

#ifndef PACKAGEKIT_PACKAGE_UPDATE_DETAILS_H
#define PACKAGEKIT_PACKAGE_UPDATE_DETAILS_H

#include "package.h"

#include <QtCore/QStringList>
#include <QtCore/QDateTime>

namespace PackageKit {

    class PackageUpdateDetailsPrivate : public QSharedData
    {
    public:
        QStringList updates;
        QStringList obsoletes;
        QStringList vendor_urls;
        QStringList bugzilla_urls;
        QStringList cve_urls;
        uint restart;
        QString update_text;
        QString changelog;
        uint state;
        QDateTime issued;
        QDateTime updated;
    };

/**
 * \class PackageUpdateDetails packageupdateDetails.h PackageUpdateDetails
 * \author Daniel Nicoletti \e <dantti12@gmail.com>
 *
 * \brief Represents a software package with update details
 *
 * This class represents a software package with update details.
 */
class PackageUpdateDetails : public Package
{
    Q_GADGET
    Q_ENUMS(UpdateState)
    Q_ENUMS(Restart)
    Q_PROPERTY(PackageList updates READ updates)
    Q_PROPERTY(PackageList obsoletes READ obsoletes)
    Q_PROPERTY(QStringList vendorUrls READ vendorUrls)
    Q_PROPERTY(QStringList bugzillaUrls READ bugzillaUrls)
    Q_PROPERTY(QStringList cveUrls READ cveUrls)
    Q_PROPERTY(Restart restart READ restart)
    Q_PROPERTY(QString updateText READ updateText)
    Q_PROPERTY(QString changelog READ changelog)
    Q_PROPERTY(UpdateState state READ state)
    Q_PROPERTY(QDateTime issued READ issued)
    Q_PROPERTY(QDateTime updated READ updated)
public:
    /**
     * Describes an update's state
     */
    typedef enum {
        UpdateStateUnknown,
        UpdateStateStable,
        UpdateStateUnstable,
        UpdateStateTesting,
    } UpdateState;
    
    /**
     * Describes a restart type
     */
    typedef enum {
        RestartUnknown,
        RestartNone,
        RestartApplication,
        RestartSession,
        RestartSystem,
        RestartSecuritySession, /* a library that is being used by this package has been updated for security */
        RestartSecuritySystem
    } Restart;

    /**
     * Constructs package
     */
    PackageUpdateDetails(const QString &package_id,
                         const QStringList &updates,
                         const QStringList &obsoletes,
                         const QStringList &vendor_urls,
                         const QStringList &bugzilla_urls,
                         const QStringList &cve_urls,
                         uint restart,
                         const QString &update_text,
                         const QString &changelog,
                         uint state,
                         const QDateTime &issued,
                         const QDateTime &updated);

    /**
     * Constructs a copy of other.
     */
    PackageUpdateDetails(const PackageUpdateDetails &other);

    /**
     * Constructs an invalid package.
     */
    PackageUpdateDetails();

    /**
     * Destructor
     */
    ~PackageUpdateDetails();

    /**
     * Returns the package list of packages that will be updated by updating this package
     */
    PackageList updates() const;

    /**
     * Returns the package list of packages that will be obsoleted by this update
     */
    PackageList obsoletes() const;

    /**
     * Returns the verdor URL of this update
     */
    QStringList vendorUrls() const;

    /**
     * Returns the bugzilla URL of this update
     */
    QStringList bugzillaUrls() const;

    /**
     * Returns the CVE (Common Vulnerabilities and Exposures) URL of this update
     */
    QStringList cveUrls() const;

    /**
     * Returns the what kind of restart will be required after this update
     */
    Restart restart() const;

    /**
     * Returns the update description's
     */
    QString updateText() const;

    /**
     * Returns the update changelog's
     */
    QString changelog() const;

    /**
     * Returns the category of the update, eg. stable or testing
     */
    UpdateState state() const;

    /**
     * Returns the date and time when this update was first issued
     */
    QDateTime issued() const;

    /**
     * Returns the date and time when this updated was updated
     */
    QDateTime updated() const;

private:
    QSharedDataPointer<PackageUpdateDetailsPrivate> d;
};

} // End namespace PackageKit

#endif
