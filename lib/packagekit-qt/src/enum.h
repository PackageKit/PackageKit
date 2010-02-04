/*
 * This file is part of the QPackageKit project
 * Copyright (C) 2008 Adrien Bustany <madcat@mymadcat.com>
 * Copyright (C) 2009 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
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

#ifndef ENUM_H
#define ENUM_H

#include <QtCore>

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
class Enum : public QObject
{
	Q_OBJECT
	Q_ENUMS(PackageInfo)
	Q_ENUMS(License)

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
		InfoDecompressing
	} Info;

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

	Enum();
};

} // End namespace PackageKit

#endif

