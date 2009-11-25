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
		UnknownState,
		StateInstalled,
		StateAvailable,
		StateLow,
		StateEnhancement,
		StateNormal,
		StateBugfix,
		StateImportant,
		StateSecurity,
		StateBlocked,
		StateDownloading,
		StateUpdating,
		StateInstalling,
		StateRemoving,
		StateCleanup,
		StateObsoleting,
		StateCollectionInstalled,
		StateCollectionAvailable,
		StateFinished,
		StateReinstalling,
		StateDowngrading,
		StatePreparing,
		StateDecompressing
	} State;
	/**
	 * Returns the package's state
	 */
	State state() const;

	/**
	 * Describes a package's license
	 */
	typedef enum {
		UnknownLicense,
		LicenseGlide,
		LicenseAfl,
		LicenseAmpasBsd,
		LicenseAmazonDsl,
		LicenseAdobe,
		LicenseAgplv1,
		LicenseAgplv3,
		LicenseAsl1Dot0,
		LicenseAsl1Dot1,
		LicenseAsl2Dot0,
		LicenseApsl2Dot0,
		LicenseArtisticClarified,
		LicenseArtistic2Dot0,
		LicenseArl,
		LicenseBittorrent,
		LicenseBoost,
		LicenseBsdWithAdvertising,
		LicenseBsd,
		LicenseCecill,
		LicenseCddl,
		LicenseCpl,
		LicenseCondor,
		LicenseCopyrightOnly,
		LicenseCryptix,
		LicenseCrystalStacker,
		LicenseDoc,
		LicenseWtfpl,
		LicenseEpl,
		LicenseEcos,
		LicenseEfl2Dot0,
		LicenseEu_datagrid,
		LicenseLgplv2WithExceptions,
		LicenseFtl,
		LicenseGiftware,
		LicenseGplv2,
		LicenseGplv2WithExceptions,
		LicenseGplv2PlusWithExceptions,
		LicenseGplv3,
		LicenseGplv3WithExceptions,
		LicenseGplv3PlusWithExceptions,
		LicenseLgplv2,
		LicenseLgplv3,
		LicenseGnuplot,
		LicenseIbm,
		LicenseImatix,
		LicenseImagemagick,
		LicenseImlib2,
		LicenseIjg,
		LicenseIntel_acpi,
		LicenseInterbase,
		LicenseIsc,
		LicenseJabber,
		LicenseJasper,
		LicenseLppl,
		LicenseLibtiff,
		LicenseLpl,
		LicenseMecabIpadic,
		LicenseMit,
		LicenseMitWithAdvertising,
		LicenseMplv1Dot0,
		LicenseMplv1Dot1,
		LicenseNcsa,
		LicenseNgpl,
		LicenseNosl,
		LicenseNetcdf,
		LicenseNetscape,
		LicenseNokia,
		LicenseOpenldap,
		LicenseOpenpbs,
		LicenseOsl1Dot0,
		LicenseOsl1Dot1,
		LicenseOsl2Dot0,
		LicenseOsl3Dot0,
		LicenseOpenssl,
		LicenseOreilly,
		LicensePhorum,
		LicensePhp,
		LicensePublicDomain,
		LicensePython,
		LicenseQpl,
		LicenseRpsl,
		LicenseRuby,
		LicenseSendmail,
		LicenseSleepycat,
		LicenseSlib,
		LicenseSissl,
		LicenseSpl,
		LicenseTcl,
		LicenseUcd,
		LicenseVim,
		LicenseVnlsl,
		LicenseVsl,
		LicenseW3c,
		LicenseWxwidgets,
		LicenseXinetd,
		LicenseZend,
		LicenseZplv1Dot0,
		LicenseZplv2Dot0,
		LicenseZplv2Dot1,
		LicenseZlib,
		LicenseZlibWithAck,
		LicenseCdl,
		LicenseFbsddl,
		LicenseGfdl,
		LicenseIeee,
		LicenseOfsfdl,
		LicenseOpenPublication,
		LicenseCcBy,
		LicenseCcBySa,
		LicenseCcByNd,
		LicenseDsl,
		LicenseFreeArt,
		LicenseOfl,
		LicenseUtopia,
		LicenseArphic,
		LicenseBaekmuk,
		LicenseBitstreamVera,
		LicenseLucida,
		LicenseMplus,
		LicenseStix,
		LicenseXano,
		LicenseVostrom,
		LicenseXerox,
		LicenseRicebsd,
		LicenseQhull
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
			QString license() const;

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

	/**
	 * \brief Returns the path to the package icon, if known
	 *
	 * \return A QString holding the path to the package icon if known, an empty QString else
	 */
	QString iconPath ();

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

