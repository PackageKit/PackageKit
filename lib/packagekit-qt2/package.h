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

#include <QtCore/QSharedDataPointer>
#include <QtCore/QString>
#include <QtCore/QSet>

namespace PackageKit {

class PackagePrivate : public QSharedData
{
public:
    PackagePrivate() : info(1) { }
    PackagePrivate(const PackagePrivate &other) :
        QSharedData(other),
        info(other.info),
        summary(other.summary) { }
    ~PackagePrivate() { }
    uint info;
    QString summary;
};
/**
 * \class Package package.h Package
 * \author Adrien Bustany \e <madcat@mymadcat.com>
 * \author Daniel Nicoletti \e <dantti12@gmail.com>
 *
 * \brief Represents a software package
 *
 * This class represents a software package.
 *
 * \note All Package objects should be deleted by the user.
 */
class Package : protected QString
{
public:
    /**
     * Describes the state of a package
     */
    typedef enum {
        InfoUnknown,
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
        InfoUntrusted,
        InfoTrusted
    } Info;

    /**
     * Constructs package
     */
    Package(const QString &packageId, Info info = InfoUnknown, const QString &summary = QString());

    /**
     * Constructs a copy of other.
     */
    Package(const Package &other);

    /**
     * Constructs an invalid package.
     */
    Package();

    /**
     * Destructor
     */
    ~Package();

    /**
     * Return true is the package id is valid
     */
    bool isValid() const;

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
     * Returns the path to the package icon, if known
     * \return A QString holding the path to the package icon if known, an empty QString else
     */
    QString iconPath() const;

    /**
     * Compares two packages by it's ids
     */
    bool operator==(const Package &package) const;

    /**
     * Copy the other package data
     */
    Package& operator=(const Package &package);

private:
    QSharedDataPointer<PackagePrivate> d;
};
typedef QList<Package> PackageList;

} // End namespace PackageKit

#endif
