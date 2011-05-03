/*
 * This file is part of the QPackageKit project
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

#ifndef PACKAGEKIT_SIGNATURE_H
#define PACKAGEKIT_SIGNATURE_H

#include <QtCore/QObject>

#include "package.h"

namespace PackageKit {

class Signature
{
    Q_GADGET
    Q_ENUMS(Type)
public:
    /**
     * Describes a signature type
     */
    typedef enum {
        UnknownType,
        TypeGpg
    } Type;

    /**
     * \c package is the package that needs to be signed
     */
    Package package;

    /**
     * \c repoId is the id of the software repository containing the package
     */
    QString repoId;

    /**
     * Describes a signature url
     */
    QString keyUrl;

    /**
     * Describes the user id
     */
    QString keyUserid;

    /**
     * Describes the key id
     */
    QString keyId;

    /**
     * Describes the fingerprint
     */
    QString keyFingerprint;

    /**
     * Describes the signature time stamp
     */
    QString keyTimestamp;

    /**
     * The signature type
     */
    Signature::Type type;
};

} // End namespace PackageKit

#endif
