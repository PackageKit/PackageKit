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

#ifndef PACKAGEKIT_PACKAGE_H
#define PACKAGEKIT_PACKAGE_H

#include <QtCore/QString>
#include <QtCore/QDateTime>

namespace PackageKit {

/**
 * \class Package package.h Package
 * \author Adrien Bustany \e <madcat@mymadcat.com>
 * \author Daniel Nicoletti \e <dantti85-pk@yahoo.com.br>
 *
 * \brief Represents a software package
 *
 * This class represents a software package.
 *
 * \note All Package objects should be deleted by the user.
 */
class PackagePrivate;
class Package
{
    Q_GADGET
    Q_ENUMS(Info)
    Q_ENUMS(Group)
    Q_ENUMS(UpdateState)
    Q_ENUMS(Restart)
public:
    /**
     * Describes the state of a package
     */
    typedef enum {
        UnknownInfo,
        InfoInstalled,
        InfoAvailable,
        InfoLow,
        InfoEnhancement,
        InfoNormal,
        InfoBugfix,
        InfoImportant,
        InfoSecurity,
        InfoBlocked,
        InfoDownloading,
        InfoUpdating,
        InfoInstalling,
        InfoRemoving,
        InfoCleanup,
        InfoObsoleting,
        InfoCollectionInstalled,
        InfoCollectionAvailable,
        InfoFinished,
        InfoReinstalling,
        InfoDowngrading,
        InfoPreparing,
        InfoDecompressing,
        /* this always has to be at the end of the list */
        LastInfo
    } Info;

    /**
     * Describes the different package groups
     */
    typedef enum {
        UnknownGroup,
        GroupAccessibility,
        GroupAccessories,
        GroupAdminTools,
        GroupCommunication,
        GroupDesktopGnome,
        GroupDesktopKde,
        GroupDesktopOther,
        GroupDesktopXfce,
        GroupEducation,
        GroupFonts,
        GroupGames,
        GroupGraphics,
        GroupInternet,
        GroupLegacy,
        GroupLocalization,
        GroupMaps,
        GroupMultimedia,
        GroupNetwork,
        GroupOffice,
        GroupOther,
        GroupPowerManagement,
        GroupProgramming,
        GroupPublishing,
        GroupRepos,
        GroupSecurity,
        GroupServers,
        GroupSystem,
        GroupVirtualization,
        GroupScience,
        GroupDocumentation,
        GroupElectronics,
        GroupCollections,
        GroupVendor,
        GroupNewest,
        /* this always has to be at the end of the list */
        LastGroup
    } Group;
    typedef QSet<Group> Groups;

    /**
     * Describes an update's state
     */
    typedef enum {
        UnknownUpdateState,
        UpdateStateStable,
        UpdateStateUnstable,
        UpdateStateTesting,
        /* this always has to be at the end of the list */
        LastUpdateState
    } UpdateState;

    /**
     * Describes a restart type
     */
    typedef enum {
        UnknownRestart,
        RestartNone,
        RestartApplication,
        RestartSession,
        RestartSystem,
        RestartSecuritySession,
        RestartSecuritySystem,
        /* this always has to be at the end of the list */
        LastRestart
    } Restart;

    Package(const QString& packageId, Info info = UnknownInfo, const QString &summary = QString());
    Package();
    /**
     * Destructor
     */
    ~Package();

    /**
     * \brief Returns the package ID
     *
     * A PID (package ID) uniquely identifies a package among all software repositories
     */
    QString id() const;

    /**
     * Returns the package name, for example vim
     */
    QString name() const;

    /**
     * Returns the package version, for example 7.0
     */
    QString version() const;

    /**
     * Returns the package's architecture, for example x86_64
     */
    QString arch() const;

    /**
     * Holds additionnal data about the package set by the backend
     */
    QString data() const;

    /**
     * Returns the package's summary. You can get more details by using Client::getDetails
     */
    QString summary() const;

    /**
     * Returns the package's info
     */
    Info info() const;

    /**
     * Checks weither the package has details or not
     * \sa Transaction::getDetails
     * \sa Package::details
    */
    bool hasDetails() const;

    /**
     * Returns the package's license
     * \note this will only return a valid value if \sa hasDetails() is true
     */
    QString license() const;

    /**
     * Define the package's license
     * \note this will make \sa hasDetails() return true
     */
    void setLicense(const QString &license);

    /**
     * Returns the package's group (for example Multimedia, Editors...)
     * \note this will only return a valid value if \sa hasDetails() is true
     */
    Group group() const;

    /**
     * Define the package's group
     * \note this will make \sa hasDetails() return true
     */
    void setGroup(Group group);

    /**
     * Returns the package's long description
     * \note this will only return a valid value if \sa hasDetails() is true
     */
    QString description() const;

    /**
     * Define the package's long description
     * \note this will make \sa hasDetails() return true
     */
    void setDescription(const QString &description);

    /**
     * Returns the software's homepage url
     * \note this will only return a valid value if \sa hasDetails() is true
     */
    QString url() const;

    /**
     * Define the package's url
     * \note this will make \sa hasDetails() return true
     */
    void setUrl(const QString &url);

    /**
     * Returns the package's size
     * \note this will only return a valid value if \sa hasDetails() is true
     */
    qulonglong size() const;

    /**
     * Define the package's size
     * \note this will make \sa hasDetails() return true
     */
    void setSize(qulonglong size);

    /**
     * Returns if the package has update details
     */
    bool hasUpdateDetails() const;

    /**
     * Returns the package list of packages that will be updated by updating this package
     * \note this will only return a valid value if \sa hasUpdateDetails() is true
     */
    QList<Package> updates() const;

    /**
     * Define the list of packages that will be updated by updating this package
     * \note this will make \sa hasUpdateDetails() return true
     */
    void setUpdates(const QList<Package> &updates);

    /**
     * Returns the package list of packages that will be obsoleted by this update
     * \note this will only return a valid value if \sa hasUpdateDetails() is true
     */
    QList<Package> obsoletes() const;

    /**
     * Define the list of packages that will be obsoleted by updating this package
     * \note this will make \sa hasUpdateDetails() return true
     */
    void setObsoletes(const QList<Package> &obsoletes);

    /**
     * Returns the verdor URL of this update
     * \note this will only return a valid value if \sa hasUpdateDetails() is true
     */
    QString vendorUrl() const;

    /**
     * Define the vendor URL
     * \note this will make \sa hasUpdateDetails() return true
     */
    void setVendorUrl(const QString &vendorUrl);

    /**
     * Returns the bugzilla URL of this update
     * \note this will only return a valid value if \sa hasUpdateDetails() is true
     */
    QString bugzillaUrl() const;

    /**
     * Define the bugzilla URL
     * \note this will make \sa hasUpdateDetails() return true
     */
    void setBugzillaUrl(const QString &bugzillaUrl);

    /**
     * Returns the CVE (Common Vulnerabilities and Exposures) URL of this update
     * \note this will only return a valid value if \sa hasUpdateDetails() is true
     */
    QString cveUrl() const;

    /**
     * Define the CVE (Common Vulnerabilities and Exposures) URL
     * \note this will make \sa hasUpdateDetails() return true
     */
    void setCveUrl(const QString &cveUrl);

    /**
     * Returns the what kind of restart will be required after this update
     * \note this will only return a valid value if \sa hasUpdateDetails() is true
     */
    Package::Restart restart() const;

    /**
     * Define the restart type
     * \note this will make \sa hasUpdateDetails() return true
     */
    void setRestart(Package::Restart restart);

    /**
     * Returns the update description's
     * \note this will only return a valid value if \sa hasUpdateDetails() is true
     */
    QString updateText() const;

    /**
     * Define the update description's
     * \note this will make \sa hasUpdateDetails() return true
     */
    void setUpdateText(const QString &updateText);

    /**
     * Returns the update changelog's
     * \note this will only return a valid value if \sa hasUpdateDetails() is true
     */
    QString changelog() const;

    /**
     * Define the update changelog's
     * \note this will make \sa hasUpdateDetails() return true
     */
    void setChangelog(const QString &changelog);

    /**
     * Returns the category of the update, eg. stable or testing
     * \note this will only return a valid value if \sa hasUpdateDetails() is true
     */
    UpdateState state() const;

    /**
     * Define the update changelog's
     * \note this will make \sa hasUpdateDetails() return true
     */
    void setState(UpdateState state);

    /**
     * Returns the date and time when this update was first issued
     * \note this will only return a valid value if \sa hasUpdateDetails() is true
     */
    QDateTime issued() const;

    /**
     * Define the date and time when this update was first issued
     * \note this will make \sa hasUpdateDetails() return true
     */
    void setIssued(const QDateTime &issued);

    /**
     * Returns the date and time when this updated was updated
     * \note this will only return a valid value if \sa hasUpdateDetails() is true
     */
    QDateTime updated() const;

    /**
     * Define the date and time when this updated was updated
     * \note this will make \sa hasUpdateDetails() return true
     */
    void setUpdated(const QDateTime &updated);

    /**
     * Returns the path to the package icon, if known
     * \return A QString holding the path to the package icon if known, an empty QString else
     */
    QString iconPath();

    /**
     * Compares two packages by it's ids
     */
    bool operator==(const Package &package) const;

    /**
     * Copy the other package data
     */
    Package& operator=(const Package &package);

private:
    PackagePrivate * const d_ptr;

private:
    Q_DECLARE_PRIVATE(Package);
};

} // End namespace PackageKit

#endif
