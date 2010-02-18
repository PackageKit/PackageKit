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

#include <QtSql>

#include "package.h"
#include "util.h"

using namespace PackageKit;

////// Details class
class Package::DetailsPrivate
{
public:
	QSharedPointer<Package> package;
	QString license;
	Enum::Group group;
	QString description;
	QString url;
	uint size;
};

Package::Details::Details(QSharedPointer<Package> p, const QString& license, const QString& group, const QString& detail, const QString& url, qulonglong size) : d_ptr(new DetailsPrivate)
{
	Q_D(Details);
	d->package = p;
	d->license = license;
	d->group = (Enum::Group)Util::enumFromString<Enum>(group, "Group", "Group");
	d->description = detail;
	d->url = url;
	d->size = size;
}

Package::Details::~Details()
{
	delete d_ptr;
}

QSharedPointer<Package> Package::Details::package() const
{
	Q_D(const Details);
	return d->package;
}

QString Package::Details::license() const
{
	Q_D(const Details);
	return d->license;
}

Enum::Group Package::Details::group() const
{
	Q_D(const Details);
	return d->group;
}

QString Package::Details::description() const
{
	Q_D(const Details);
	return d->description;
}

QString Package::Details::url() const
{
	Q_D(const Details);
	return d->url;
}

qulonglong Package::Details::size() const
{
	Q_D(const Details);
	return d->size;
}

////// Package class

class Package::Private
{
public:
	QString id;
	QString name;
	QString version;
	QString arch;
	QString data;
	QString summary;
	Enum::Info info;
	Package::Details* details;
	QString iconPath;
};

Package::Package(const QString& packageId, const QString& info, const QString& summary)
    : d(new Private)
{
	d->id = packageId;

	// Break down the packageId
	QStringList tokens = packageId.split(";");
	if(tokens.size() == 4) {
		d->name = tokens.at(0);
		d->version = tokens.at(1);
		d->arch = tokens.at(2);
		d->data = tokens.at(3);
	}

	d->info = (Enum::Info)Util::enumFromString<Enum>(info, "Info", "Info");
	d->summary = summary;
	d->details = NULL;
	d->iconPath = QString ();
}

Package::~Package()
{
	if (hasDetails())
		delete d->details;
	delete d;
}

QString Package::id() const
{
	return d->id;
}

QString Package::name() const
{
	return d->name;
}

QString Package::version() const
{
	return d->version;
}

QString Package::arch() const
{
	return d->arch;
}

QString Package::data() const
{
	return d->data;
}

QString Package::summary() const
{
	return d->summary;
}

Enum::Info Package::info() const
{
	return d->info;
}

bool Package::hasDetails() const
{
	return (d->details != NULL);
}

Package::Details* Package::details() const
{
	return d->details;
}

void Package::setDetails(Package::Details* det)
{
	d->details = det;
}

void Package::setInfoSummary(const QString& info, const QString& summary)
{
	d->info = (Enum::Info)Util::enumFromString<Enum>(info, "Info", "Info");
	d->summary = summary;
}

QString Package::iconPath ()
{
	if (d->iconPath.isNull ()) {
		d->iconPath = QString("");
		QSqlDatabase db = QSqlDatabase::database();
		if (!db.isOpen()) {
			qDebug() << "Desktop files database is not open";
			return d->iconPath;
		}

		QSqlQuery q(db);
		q.prepare("SELECT filename FROM cache WHERE package = :name");
		q.bindValue(":name", d->name);
		if(q.exec()) {
			if (q.next()) {
				QFile desktopFile (q.value(0).toString());
				if (desktopFile.open (QIODevice::ReadOnly | QIODevice::Text)) {
					while (!desktopFile.atEnd ()) {
						QByteArray line = desktopFile.readLine ().trimmed ();
						if (line.startsWith ("Icon=")) {
							d->iconPath = line.mid (5);
							break;
						}
					}
					desktopFile.close ();
				} else {
					qDebug() << "Cannot open desktop file " << q.value(0).toString ();
				}
			}
		} else {
			qDebug() << "Error while running query " << q.executedQuery();
		}
	}

	return d->iconPath;
}

bool Package::operator==(const Package *package) const
{
	return d->id == package->id();
}
