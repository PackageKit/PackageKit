/*
 * This file is part of the QPackageKit project
 * Copyright (C) 2008 Adrien Bustany <madcat@mymadcat.com>
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

#ifndef PACKAGEKIT_UTIL_H
#define PACKAGEKIT_UTIL_H

#include <QtCore/QLatin1String>
#include <QtCore/QMetaEnum>
#include <QtCore/QDebug>

#include "transaction.h"

namespace PackageKit {


class Util
{
public:
    /**
     * Converts \p str from the PK naming scheme to the QPackageKit enum naming
     * scheme, prepending \p prefix to the result, and returns the value for enum \p enum
     * For example, enumFromString("get_requires", "Action") == "ActionGetRequires"
     * \return the enum value
     */
    template<class T> static int enumFromString(const QString &str, const char *enumName, const QString &prefix = QString())
    {
        QString realName;
        bool lastWasDash = false;
        QChar buf;

        for(int i = 0 ; i < str.length() ; ++i) {
            buf = str[i].toLower();
            if(i == 0 || lastWasDash) {
                buf = buf.toUpper();
            }

            lastWasDash = false;
            if(buf.toAscii() == '-') {
                lastWasDash = true;
            } else if(buf.toAscii() == '~') {
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
            enumValue = e.keyToValue(QString("Unknown").append(enumName).toAscii().data());
            if (!QString(enumName).isEmpty()) {
                qDebug() << "enumFromString (" << enumName << ") : converted" << str << "to" << QString("Unknown").append(enumName) << ", enum id" << id;
            }
        }
        return enumValue;
    }

    static QStringList packageListToPids(const QList<Package> &packages);

    static Transaction::InternalError errorFromString(const QString &errorName);

};

} // End namespace PackageKit

#endif
