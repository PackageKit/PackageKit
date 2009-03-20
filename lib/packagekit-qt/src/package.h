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

#ifndef PACKAGE_H
#define PACKAGE_H

#include <QtCore>
#include "client.h"

namespace PackageKit {

/**
 * \class Package package.h Package
 * \author Adrien Bustany <madcat@mymadcat.com>
 *
 * \brief Represents a software package
 *
 * This class represents a software package.
 *
 * \note All Package objects should be deleted by the user.
 */
class Package : public QObject
{
	Q_OBJECT
	Q_ENUMS(State)
	Q_ENUMS(License)

public:
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
	 * Describes the state of a package
	 */
	typedef enum {
		Installed,
		Available,
		Low,
		Enhancement,
		Normal,
		Bugfix,
		Important,
		Security,
		Blocked,
		Downloading,
		Updating,
		Installing,
		Removing,
		Cleanup,
		Obsoleting,
		UnknownState
	} State;
	/**
	 * Returns the package's state
	 */
	State state() const;

	/**
	 * Describes a package's license
	 */
	typedef enum {
		Glide,
		Afl,
		AmpasBsd,
		AmazonDsl,
		Adobe,
		Agplv1,
		Agplv3,
		Asl1Dot0,
		Asl1Dot1,
		Asl2Dot0,
		Apsl2Dot0,
		ArtisticClarified,
		Artistic2Dot0,
		Arl,
		Bittorrent,
		Boost,
		BsdWithAdvertising,
		Bsd,
		Cecill,
		Cddl,
		Cpl,
		Condor,
		CopyrightOnly,
		Cryptix,
		CrystalStacker,
		Doc,
		Wtfpl,
		Epl,
		Ecos,
		Efl2Dot0,
		Eu_datagrid,
		Lgplv2WithExceptions,
		Ftl,
		Giftware,
		Gplv2,
		Gplv2WithExceptions,
		Gplv2PlusWithExceptions,
		Gplv3,
		Gplv3WithExceptions,
		Gplv3PlusWithExceptions,
		Lgplv2,
		Lgplv3,
		Gnuplot,
		Ibm,
		Imatix,
		Imagemagick,
		Imlib2,
		Ijg,
		Intel_acpi,
		Interbase,
		Isc,
		Jabber,
		Jasper,
		Lppl,
		Libtiff,
		Lpl,
		MecabIpadic,
		Mit,
		MitWithAdvertising,
		Mplv1Dot0,
		Mplv1Dot1,
		Ncsa,
		Ngpl,
		Nosl,
		Netcdf,
		Netscape,
		Nokia,
		Openldap,
		Openpbs,
		Osl1Dot0,
		Osl1Dot1,
		Osl2Dot0,
		Osl3Dot0,
		Openssl,
		Oreilly,
		Phorum,
		Php,
		PublicDomain,
		Python,
		Qpl,
		Rpsl,
		Ruby,
		Sendmail,
		Sleepycat,
		Slib,
		Sissl,
		Spl,
		Tcl,
		Ucd,
		Vim,
		Vnlsl,
		Vsl,
		W3c,
		Wxwidgets,
		Xinetd,
		Zend,
		Zplv1Dot0,
		Zplv2Dot0,
		Zplv2Dot1,
		Zlib,
		ZlibWithAck,
		Cdl,
		Fbsddl,
		Gfdl,
		Ieee,
		Ofsfdl,
		OpenPublication,
		CcBy,
		CcBySa,
		CcByNd,
		Dsl,
		FreeArt,
		Ofl,
		Utopia,
		Arphic,
		Baekmuk,
		BitstreamVera,
		Lucida,
		Mplus,
		Stix,
		Xano,
		Vostrom,
		Xerox,
		Ricebsd,
		Qhull,
		UnknownLicense
	} License;

	/**
	 * Holds additional details about a package
	 * \sa Client::getDetails
	 */
	class Details
	{
		public:
			/**
			 * Destructor
			 */
			~Details();

			/**
			 * Returns the package these details are linked to
			 * \return the Package object to which these details are related
			 */
			Package* package() const;

			/**
			 * Returns the package's license
			 */
			Package::License license() const;

			/**
			 * Returns the package's group (for example Multimedia, Editors...)
			 */
			Client::Group group() const;

			/**
			 * Returns the package's long description
			 */
			QString description() const;

			/**
			 * Returns the software's homepage url
			 */
			QString url() const;

			/**
			 * Returns the package's size
			 */
			qulonglong size() const;
		private:
			friend class Package;
			friend class TransactionPrivate;
			Details(Package* p, const QString& license, const QString& group, const QString& detail, const QString& url, qulonglong size);
			class Private;
			Private* d;
	};

	/**
	 * Checks weither the package has details or not
	 * \sa Client::getDetails
	 * \sa Package::details
	 */
	bool hasDetails() const;

	/**
	 * \brief Returns the package's extended details
	 *
	 * \note The returned value is only valid if hasDetails returned true
	 * \return a pointer to a Package::Details class containing the package's details
	 * \sa Client::getDetails
	 */
	Details* details() const;

	bool operator==(const Package *package) const;

private:
	friend class Transaction;
	friend class TransactionPrivate;
	friend class Details;
	friend class Client;
	Package(const QString& packageId, const QString& state = QString(), const QString& summary = QString());
	void setDetails(Details* det);
	class Private;
	Private* d;
};

} // End namespace PackageKit

#endif

