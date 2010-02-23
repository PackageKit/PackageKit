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

QStringList Util::packageListToPids(const QList<QSharedPointer<Package> >& packages)
{
	QStringList pids;
	foreach(QSharedPointer<Package> p, packages)
		pids.append(p->id());

	return pids;
}

QString Util::filtersToString(const QFlags<PackageKit::Enum::Filter>& flags)
{
	QStringList flagStrings;
	for (int i = Enum::UnknownFilter; i < Enum::FilterLast; i *= 2) {
		if ((Enum::Filter) i & flags) {
			flagStrings.append(Util::enumToString<Enum>((Enum::Filter) i, "Filter", "Filter"));
		}
	}

	return flagStrings.join(";");
}

Client::DaemonError Util::errorFromString (QString errorName)
{
	if (errorName.startsWith ("org.freedesktop.packagekit.")) {
		return Client::ErrorFailedAuth;
	}

	errorName.replace ("org.freedesktop.PackageKit.Transaction.", "");

	if (errorName.startsWith ("PermissionDenied") || errorName.startsWith ("RefusedByPolicy")) {
		return Client::ErrorFailedAuth;
	}

	if (errorName.startsWith ("PackageIdInvalid") || errorName.startsWith ("SearchInvalid") || errorName.startsWith ("FilterInvalid") || errorName.startsWith ("InvalidProvide") || errorName.startsWith ("InputInvalid")) {
		return Client::ErrorInvalidInput;
	}

	if (errorName.startsWith ("PackInvalid") || errorName.startsWith ("NoSuchFile") || errorName.startsWith ("NoSuchDirectory")) {
		return Client::ErrorInvalidFile;
	}

	if (errorName.startsWith ("NotSupported")) {
		return Client::ErrorFunctionNotSupported;
	}

	return Client::ErrorFailed;
}


