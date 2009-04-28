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

#include "util.h"
#include "package.h"

using namespace PackageKit;

QStringList Util::packageListToPids(const QList<Package*>& packages)
{
	QStringList pids;
	foreach(Package* p, packages)
		pids.append(p->id());

	return pids;
}

QString Util::filtersToString(const QFlags<PackageKit::Client::Filter>& flags)
{
	QStringList flagStrings;
	for (qint64 i = 1; i < Client::UnknownFilter; i *= 2) {
		if ((Client::Filter) i & flags) {
			flagStrings.append(Util::enumToString<Client>((Client::Filter) i, "Filter", "Filter"));
		}
	}

	return flagStrings.join(";");
}

