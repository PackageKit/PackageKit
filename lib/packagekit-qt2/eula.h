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

#ifndef PACKAGEKIT_EULA_H
#define PACKAGEKIT_EULA_H

#include <QtCore/QObject>

#include "package.h"

namespace PackageKit {

class Eula
{
public:
    /**
     * \c repoId is the id of the software repository containing the package
     */
    QString id;

    /**
     * \c package is the package for which an EULA is required
     */
    Package package;

    /**
     * Describes the name of the EULA's vendor
     */
    QString vendor;

    /**
     * Describes the EULA text
     */
    QString licenseAgreement;
};

} // End namespace PackageKit

#endif
