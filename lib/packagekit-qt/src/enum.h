/*
 * This file is part of the QPackageKit project
 * Copyright (C) 2008 Adrien Bustany <madcat@mymadcat.com>
 * Copyright (C) 2009-2010 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
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
#include "bitfield.h"

namespace PackageKit {

/**
* \class Enum enum.h Enum
* \author Adrien Bustany <madcat@mymadcat.com>
*
* \brief Represents a PackageKit's enums
*
* This class represents a PackageKit enums.
*
*/
class Enum : public QObject
{
    Q_OBJECT
    Q_ENUMS(Role)
    Q_ENUMS(Status)
    Q_ENUMS(Exit)
    Q_ENUMS(Network)
    Q_ENUMS(Filter)
    Q_ENUMS(Restart)
    Q_ENUMS(Message)
    Q_ENUMS(Error)
    Q_ENUMS(Group)
    Q_ENUMS(UpdateState)
    Q_ENUMS(Info)
    Q_ENUMS(DistroUpgrade)
    Q_ENUMS(SigType)
    Q_ENUMS(Provides)
    Q_ENUMS(License)
    Q_ENUMS(MediaType)
    Q_ENUMS(Authorize)
public:
    /**
    * Lists all the available actions
    * \sa getActions
    */
    typedef enum {
        UnknownRole,
        RoleCancel,
        RoleGetDepends,
        RoleGetDetails,
        RoleGetFiles,
        RoleGetPackages,
        RoleGetRepoList,
        RoleGetRequires,
        RoleGetUpdateDetail,
        RoleGetUpdates,
        RoleInstallFiles,
        RoleInstallPackages,
        RoleInstallSignature,
        RoleRefreshCache,
        RoleRemovePackages,
        RoleRepoEnable,
        RoleRepoSetData,
        RoleResolve,
        RoleRollback,
        RoleSearchDetails,
        RoleSearchFile,
        RoleSearchGroup,
        RoleSearchName,
        RoleUpdatePackages,
        RoleUpdateSystem,
        RoleWhatProvides,
        RoleAcceptEula,
        RoleDownloadPackages,
        RoleGetDistroUpgrades,
        RoleGetCategories,
        RoleGetOldTransactions,
        RoleSimulateInstallFiles,
        RoleSimulateInstallPackages,
        RoleSimulateRemovePackages,
        RoleSimulateUpdatePackages,
        RoleUpgradeSystem, // Since 0.6.11
        /* this always has to be at the end of the list */
        LastRole
    } Role;
    typedef Bitfield Roles;

    /**
    * Describes the current state of the transaction
    */
    typedef enum {
        UnknownStatus,
        StatusWait,
        StatusSetup,
        StatusRunning,
        StatusQuery,
        StatusInfo,
        StatusRemove,
        StatusRefreshCache,
        StatusDownload,
        StatusInstall,
        StatusUpdate,
        StatusCleanup,
        StatusObsolete,
        StatusDepResolve,
        StatusSigCheck,
        StatusRollback,
        StatusTestCommit,
        StatusCommit,
        StatusRequest,
        StatusFinished,
        StatusCancel,
        StatusDownloadRepository,
        StatusDownloadPackagelist,
        StatusDownloadFilelist,
        StatusDownloadChangelog,
        StatusDownloadGroup,
        StatusDownloadUpdateinfo,
        StatusRepackaging,
        StatusLoadingCache,
        StatusScanApplications,
        StatusGeneratePackageList,
        StatusWaitingForLock,
        StatusWaitingForAuth,
        StatusScanProcessList,
        StatusCheckExecutableFiles,
        StatusCheckLibraries,
        StatusCopyFiles,
        /* this always has to be at the end of the list */
        LastStatus
    } Status;

    /**
    * Describes how the transaction finished
    * \sa Transaction::finished()
    */
    typedef enum {
        UnknownExit,
        ExitSuccess,
        ExitFailed,
        ExitCancelled,
        ExitKeyRequired,
        ExitEulaRequired,
        ExitKilled, /* when we forced the cancel, but had to sigkill */
        ExitMediaChangeRequired,
        ExitNeedUntrusted,
        /* this always has to be at the end of the list */
        LastExit
    } Exit;

    /**
    * Describes the current network state
    */
    typedef enum {
        UnknownNetwork,
        NetworkOffline,
        NetworkOnline,
        NetworkWired,
        NetworkWifi,
        NetworkMobile,
        /* this always has to be at the end of the list */
        LastNetwork
    } Network;

        /**
    * Describes the different filters
    */
    typedef enum {
        UnknownFilter        = 0x0000001,
        NoFilter             = 0x0000002,
        FilterInstalled      = 0x0000004,
        FilterNotInstalled   = 0x0000008,
        FilterDevelopment    = 0x0000010,
        FilterNotDevelopment = 0x0000020,
        FilterGui            = 0x0000040,
        FilterNotGui         = 0x0000080,
        FilterFree           = 0x0000100,
        FilterNotFree        = 0x0000200,
        FilterVisible        = 0x0000400,
        FilterNotVisible     = 0x0000800,
        FilterSupported      = 0x0001000,
        FilterNotSupported   = 0x0002000,
        FilterBasename       = 0x0004000,
        FilterNotBasename    = 0x0008000,
        FilterNewest         = 0x0010000,
        FilterNotNewest      = 0x0020000,
        FilterArch           = 0x0040000,
        FilterNotArch        = 0x0080000,
        FilterSource         = 0x0100000,
        FilterNotSource      = 0x0200000,
        FilterCollections    = 0x0400000,
        FilterNotCollections = 0x0800000,
        FilterApplication    = 0x1000000,
        FilterNotApplication = 0x2000000,
        /* this always has to be at the end of the list */
        FilterLast           = 0x4000000
    } Filter;
    Q_DECLARE_FLAGS(Filters, Filter)

    /**
    * Describes a restart type
    */
    typedef enum {
        UnknownRestart,
        RestartNone,
        RestartApplication,
        RestartSession,
        RestartSystem,
        RestartSecuritySession,
        RestartSecuritySystem,
        /* this always has to be at the end of the list */
        LastRestart
    } Restart;

    /**
    * Describes a message's type
    */
    typedef enum {
        UnknownMessage,
        MessageBrokenMirror,
        MessageConnectionRefused,
        MessageParameterInvalid,
        MessagePriorityInvalid,
        MessageBackendError,
        MessageDaemonError,
        MessageCacheBeingRebuilt,
        MessageUntrustedPackage,
        MessageNewerPackageExists,
        MessageCouldNotFindPackage,
        MessageConfigFilesChanged,
        MessagePackageAlreadyInstalled,
        MessageAutoremoveIgnored,
        MessageRepoMetadataDownloadFailed,
        MessageRepoForDevelopersOnly,
        MessageOtherUpdatesHeldBack,
        /* this always has to be at the end of the list */
        LastMessage
    } Message;

    /**
    * Lists the different types of error
    */
    typedef enum {
        UnknownError,
        ErrorOom,
        ErrorNoNetwork,
        ErrorNotSupported,
        ErrorInternalError,
        ErrorGpgFailure,
        ErrorPackageIdInvalid,
        ErrorPackageNotInstalled,
        ErrorPackageNotFound,
        ErrorPackageAlreadyInstalled,
        ErrorPackageDownloadFailed,
        ErrorGroupNotFound,
        ErrorGroupListInvalid,
        ErrorDepResolutionFailed,
        ErrorFilterInvalid,
        ErrorCreateThreadFailed,
        ErrorTransactionError,
        ErrorTransactionCancelled,
        ErrorNoCache,
        ErrorRepoNotFound,
        ErrorCannotRemoveSystemPackage,
        ErrorProcessKill,
        ErrorFailedInitialization,
        ErrorFailedFinalise,
        ErrorFailedConfigParsing,
        ErrorCannotCancel,
        ErrorCannotGetLock,
        ErrorNoPackagesToUpdate,
        ErrorCannotWriteRepoConfig,
        ErrorLocalInstallFailed,
        ErrorBadGpgSignature,
        ErrorMissingGpgSignature,
        ErrorCannotInstallSourcePackage,
        ErrorRepoConfigurationError,
        ErrorNoLicenseAgreement,
        ErrorFileConflicts,
        ErrorPackageConflicts,
        ErrorRepoNotAvailable,
        ErrorInvalidPackageFile,
        ErrorPackageInstallBlocked,
        ErrorPackageCorrupt,
        ErrorAllPackagesAlreadyInstalled,
        ErrorFileNotFound,
        ErrorNoMoreMirrorsToTry,
        ErrorNoDistroUpgradeData,
        ErrorIncompatibleArchitecture,
        ErrorNoSpaceOnDevice,
        ErrorMediaChangeRequired,
        ErrorNotAuthorized,
        ErrorUpdateNotFound,
        ErrorCannotInstallRepoUnsigned,
        ErrorCannotUpdateRepoUnsigned,
        ErrorCannotGetFilelist,
        ErrorCannotGetRequires,
        ErrorCannotDisableRepository,
        ErrorRestrictedDownload,
        ErrorPackageFailedToConfigure,
        ErrorPackageFailedToBuild,
        ErrorPackageFailedToInstall,
        ErrorPackageFailedToRemove,
        ErrorUpdateFailedDueToRunningProcess,
        ErrorPackageDatabaseChanged,
        ErrorProvideTypeNotSupported,
        ErrorInstallRootInvalid,
        /* this always has to be at the end of the list */
        LastError
    } Error;

    /**
    * Describes the different groups
    */
    typedef enum {
        UnknownGroup,
        GroupAccessibility,
        GroupAccessories,
        GroupAdminTools,
        GroupCommunication,
        GroupDesktopGnome,
        GroupDesktopKde,
        GroupDesktopOther,
        GroupDesktopXfce,
        GroupEducation,
        GroupFonts,
        GroupGames,
        GroupGraphics,
        GroupInternet,
        GroupLegacy,
        GroupLocalization,
        GroupMaps,
        GroupMultimedia,
        GroupNetwork,
        GroupOffice,
        GroupOther,
        GroupPowerManagement,
        GroupProgramming,
        GroupPublishing,
        GroupRepos,
        GroupSecurity,
        GroupServers,
        GroupSystem,
        GroupVirtualization,
        GroupScience,
        GroupDocumentation,
        GroupElectronics,
        GroupCollections,
        GroupVendor,
        GroupNewest,
        /* this always has to be at the end of the list */
        LastGroup
    } Group;
    typedef QSet<Group> Groups;

    /**
    * Describes an update's state
    */
    typedef enum {
        UnknownUpdateState,
        UpdateStateStable,
        UpdateStateUnstable,
        UpdateStateTesting,
        /* this always has to be at the end of the list */
        LastUpdateState
    } UpdateState;

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
        InfoDecompressing,
        /* this always has to be at the end of the list */
        LastInfo
    } Info;

    /**
    * Describes an distro upgrade state
    */
    typedef enum {
        UnknownDistroUpgrade,
        DistroUpgradeStable,
        DistroUpgradeUnstable,
        /* this always has to be at the end of the list */
        LastDistroUpgrade
    } DistroUpgrade;

    /**
    * Describes a signature type
    */
    typedef enum {
        UnknownSigType,
        SigTypeGpg,
        /* this always has to be at the end of the list */
        LastSigType
    } SigType;


    /**
    * Enum used to describe a "provides" request
    * \sa whatProvides
    */
    typedef enum {
        UnknownProvides,
        ProvidesAny,
        ProvidesModalias,
        ProvidesCodec,
        ProvidesMimetype,
        ProvidesFont,
        ProvidesHardwareDriver,
        ProvidesPostscriptDriver,
        /* this always has to be at the end of the list */
        LastProvides
    } Provides;

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
        LicenseQhull,
        /* this always has to be at the end of the list */
        LastLicense
    } License;

    /**
    * Describes what kind of media is required
    */
    typedef enum {
        UnknownMediaType,
        MediaTypeCd,
        MediaTypeDvd,
        MediaTypeDisc,
        /* this always has to be at the end of the list */
        LastMediaType
    } MediaType;

    /**
    * Describes the authorization result
    */
    typedef enum {
        UnknownAuthorize,
        AuthorizeYes,
        AuthorizeNo,
        AuthorizeInteractive,
        /* this always has to be at the end of the list */
        LastAuthorize
    } Authorize;
};
Q_DECLARE_OPERATORS_FOR_FLAGS(Enum::Filters)

} // End namespace PackageKit

#endif

